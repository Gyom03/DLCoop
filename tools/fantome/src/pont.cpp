// pont.exe - bridges the ghost instances of two PCs (LAN co-op beyond ~9 players).
//
// On one PC, the instances talk on 127.0.0.(N+1), ports 20101 (game) and 20111 (discovery); fantome.dll
// replaces the broadcast with a send to each "live" instance (lock Local\DLFantome_slot_N). The bridge:
//   - holds the locks of the OTHER PC's instances: fantome.dll believes them alive and sends them its announcements;
//   - holds their addresses 127.0.0.(R+1):20101/20111: it receives what the local instances send them and
//     forwards it to the bridge on the other PC (UDP, port 20200), with the original source address;
//   - on receive, sends the packet back to the local instance from the address of the remote sending instance
//     (127.0.0.(S+1):port, socket opened on demand): the game sees exactly what it would see on a single PC.
// Nothing is modified in the game or in Steam.
//
// Usage : pont.exe <ip_de_l_autre_pc> <instances_distantes> [port_du_pont]
//   ex. PC 1 : pont.exe 192.168.1.20 7-15        PC 2 : pont.exe 192.168.1.10 0-6
// Run in the user's session (the Local\ locks are per-session), before the instances.

#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mstcpip.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <map>
#include <vector>

#ifndef SIO_UDP_CONNRESET
#define SIO_UDP_CONNRESET _WSAIOW(IOC_VENDOR, 12)
#endif

namespace {

constexpr uint16_t kGamePort = 20101;
constexpr uint16_t kScanPort = 20111;
constexpr uint16_t kDefaultBridgePort = 20200;
constexpr uint32_t kMagic = 0x31504C44;  // "DLP1"
constexpr int kMaxInstances = 33;  // adresses 0..32 (127.0.0.1..33)
constexpr uint8_t kHello = 0xFF;  // keep-alive packet (src = dst = 0xFF, no payload)

#pragma pack(push, 1)
struct Header {
    uint32_t magic;
    uint8_t src;       // sending instance (127.0.0.(src+1))
    uint8_t dst;       // instance destinataire
    uint16_t srcPort;  // ports in host order
    uint16_t dstPort;
};
#pragma pack(pop)

uint32_t LoopbackAddr(int index) { return htonl(0x7F000001u + static_cast<uint32_t>(index)); }

int InstanceOf(uint32_t netAddr)
{
    const uint32_t a = ntohl(netAddr);
    if ((a & 0xFFFFFF00u) != 0x7F000000u)
        return -1;
    const int i = static_cast<int>(a & 0xFF) - 1;
    return i >= 0 && i < kMaxInstances ? i : -1;
}

SOCKET OpenUdp(uint32_t netAddr, uint16_t hostPort)
{
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (s == INVALID_SOCKET)
        return s;
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = netAddr;
    a.sin_port = htons(hostPort);
    if (bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a)) == SOCKET_ERROR) {
        closesocket(s);
        return INVALID_SOCKET;
    }
    BOOL off = FALSE;  // no WSAECONNRESET error after an ICMP "port unreachable"
    DWORD ret = 0;
    WSAIoctl(s, SIO_UDP_CONNRESET, &off, sizeof(off), nullptr, 0, &ret, nullptr, nullptr);
    u_long nonBlocking = 1;
    ioctlsocket(s, FIONBIO, &nonBlocking);
    int size = 1 << 20;
    setsockopt(s, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&size), sizeof(size));
    return s;
}

struct Endpoint {  // address of a remote instance held by the bridge
    int instance;
    uint16_t port;
};

void Stamp(char* out, size_t n)
{
    SYSTEMTIME t;
    GetLocalTime(&t);
    snprintf(out, n, "%02d:%02d:%02d", t.wHour, t.wMinute, t.wSecond);
}

}  // namespace

int main(int argc, char** argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage : pont.exe <ip_de_l_autre_pc> <instances_distantes ex. 7-15> [port]\n");
        return 2;
    }
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    sockaddr_in peer{};
    peer.sin_family = AF_INET;
    if (inet_pton(AF_INET, argv[1], &peer.sin_addr) != 1) {
        fprintf(stderr, "adresse invalide : %s\n", argv[1]);
        return 2;
    }
    int first = 0, last = -1;
    if (sscanf(argv[2], "%d-%d", &first, &last) < 2)
        last = first;
    if (first < 0 || last >= kMaxInstances || first > last) {
        fprintf(stderr, "invalid instances: %s (0 to %d)\n", argv[2], kMaxInstances - 1);
        return 2;
    }
    const uint16_t bridgePort = argc >= 4 ? static_cast<uint16_t>(atoi(argv[3])) : kDefaultBridgePort;
    peer.sin_port = htons(bridgePort);

    SOCKET tunnel = OpenUdp(htonl(INADDR_ANY), bridgePort);
    if (tunnel == INVALID_SOCKET) {
        fprintf(stderr, "port du pont %u indisponible (%d)\n", bridgePort, WSAGetLastError());
        return 1;
    }

    // Verrous et adresses des instances distantes.
    std::vector<HANDLE> slots;
    std::map<SOCKET, Endpoint> local;            // socket -> the remote instance it represents
    std::map<uint32_t, SOCKET> byEndpoint;       // (instance << 16 | port) -> socket
    auto key = [](int instance, uint16_t port) { return (static_cast<uint32_t>(instance) << 16) | port; };
    for (int r = first; r <= last; ++r) {
        wchar_t name[64];
        swprintf(name, 64, L"Local\\DLFantome_slot_%d", r);
        HANDLE h = CreateMutexW(nullptr, FALSE, name);
        if (GetLastError() == ERROR_ALREADY_EXISTS)
            fprintf(stderr, "warning: instance %d seems to run on THIS PC (lock already taken)\n", r);
        slots.push_back(h);
        for (uint16_t port : {kGamePort, kScanPort}) {
            SOCKET s = OpenUdp(LoopbackAddr(r), port);
            if (s == INVALID_SOCKET) {
                fprintf(stderr, "127.0.0.%d:%u already in use (%d) - is instance %d running here?\n", r + 1, port,
                        WSAGetLastError(), r);
                return 1;
            }
            local[s] = {r, port};
            byEndpoint[key(r, port)] = s;
        }
    }
    printf("bridge: remote instances %d-%d, other PC %s:%u ; %zu addresses held\n", first, last, argv[1],
           bridgePort, local.size());
    fflush(stdout);

    std::vector<char> buf(65536);
    uint64_t sentCount = 0, recvCount = 0, sentBytes = 0, recvBytes = 0;
    ULONGLONG lastReport = GetTickCount64();
    ULONGLONG lastHello = 0;
    bool linked = false;
    std::map<int, uint64_t> seenFrom;  // remote instance -> packets received (for the log)

    for (;;) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(tunnel, &readable);
        for (auto& [s, ep] : local)
            FD_SET(s, &readable);
        timeval tv{1, 0};
        if (select(0, &readable, nullptr, nullptr, &tv) == SOCKET_ERROR) {
            fprintf(stderr, "select : %d\n", WSAGetLastError());
            Sleep(100);
            continue;
        }
        // Packets from the local instances to a remote instance -> tunnel.
        for (auto& [s, ep] : local) {
            if (!FD_ISSET(s, &readable))
                continue;
            for (;;) {
                sockaddr_in from{};
                int fromLen = sizeof(from);
                const int n = recvfrom(s, buf.data() + sizeof(Header), static_cast<int>(buf.size() - sizeof(Header)),
                                       0, reinterpret_cast<sockaddr*>(&from), &fromLen);
                if (n == SOCKET_ERROR)
                    break;
                const int src = InstanceOf(from.sin_addr.s_addr);
                if (src < 0)
                    continue;  // neither an instance nor loopback: ignored
                Header h{kMagic, static_cast<uint8_t>(src), static_cast<uint8_t>(ep.instance),
                         ntohs(from.sin_port), ep.port};
                std::memcpy(buf.data(), &h, sizeof(h));
                sendto(tunnel, buf.data(), n + static_cast<int>(sizeof(h)), 0, reinterpret_cast<sockaddr*>(&peer),
                       sizeof(peer));
                ++sentCount;
                sentBytes += n;
            }
        }
        // Packets from the other PC -> local instance, from the remote sender's address.
        if (FD_ISSET(tunnel, &readable)) {
            for (;;) {
                sockaddr_in from{};
                int fromLen = sizeof(from);
                const int n = recvfrom(tunnel, buf.data(), static_cast<int>(buf.size()), 0,
                                       reinterpret_cast<sockaddr*>(&from), &fromLen);
                if (n == SOCKET_ERROR)
                    break;
                if (n < static_cast<int>(sizeof(Header)) || from.sin_addr.s_addr != peer.sin_addr.s_addr)
                    continue;
                Header h;
                std::memcpy(&h, buf.data(), sizeof(h));
                if (h.magic == kMagic && h.src == kHello) {
                    if (!linked) {
                        linked = true;
                        char t[16];
                        Stamp(t, sizeof(t));
                        printf("%s bridge: link established with %s\n", t, argv[1]);
                    }
                    continue;
                }
                if (h.magic != kMagic || h.src >= kMaxInstances || h.dst >= kMaxInstances)
                    continue;
                if (h.src < first || h.src > last)
                    continue;  // the sender must be an instance of the other PC
                SOCKET out;
                auto it = byEndpoint.find(key(h.src, h.srcPort));
                if (it != byEndpoint.end()) {
                    out = it->second;
                } else {  // unusual source port: address opened on demand
                    out = OpenUdp(LoopbackAddr(h.src), h.srcPort);
                    if (out == INVALID_SOCKET)
                        continue;
                    local[out] = {h.src, h.srcPort};
                    byEndpoint[key(h.src, h.srcPort)] = out;
                    printf("bridge: address 127.0.0.%d:%u opened on demand\n", h.src + 1, h.srcPort);
                }
                sockaddr_in to{};
                to.sin_family = AF_INET;
                to.sin_addr.s_addr = LoopbackAddr(h.dst);
                to.sin_port = htons(h.dstPort);
                sendto(out, buf.data() + sizeof(Header), n - static_cast<int>(sizeof(Header)), 0,
                       reinterpret_cast<sockaddr*>(&to), sizeof(to));
                ++recvCount;
                recvBytes += n - sizeof(Header);
                if (seenFrom[h.src]++ == 0) {
                    char t[16];
                    Stamp(t, sizeof(t));
                    printf("%s pont : premier paquet de l'instance distante %d (vers %d:%u)\n", t, h.src, h.dst,
                           h.dstPort);
                }
            }
        }
        const ULONGLONG now = GetTickCount64();
        if (now - lastHello >= 2000) {
            // Keep-alive: both bridges send from and to the same port, so the Windows firewall lets
            // the other PC's packets through as replies (no inbound rule to create).
            lastHello = now;
            Header hello{kMagic, kHello, kHello, 0, 0};
            sendto(tunnel, reinterpret_cast<char*>(&hello), sizeof(hello), 0, reinterpret_cast<sockaddr*>(&peer),
                   sizeof(peer));
        }
        if (now - lastReport >= 10000) {
            lastReport = now;
            char t[16];
            Stamp(t, sizeof(t));
            printf("%s bridge: sent %llu packets (%llu B), received %llu packets (%llu B)\n", t, sentCount, sentBytes,
                   recvCount, recvBytes);
            fflush(stdout);
        }
        fflush(stdout);
    }
}
