// relais — the DLCoop launcher's Internet relay server: Dying Light 1.55 co-op without a VPN or Steam online mode.
//
// Same principle as tools/fantome/src/pont.cpp, in a star: each player (the DLCoop launcher, the "agent") connects
// to the relay over UDP, from home (outbound connection: no port to open on routers). The relay assigns
// each game instance an ADDRESS 0..32 (the game talks on 127.0.0.(address+1)) and forwards each game packet to
// the agent that holds the destination address. The game is never exposed to the Internet: only this port is.
//
// Sessions: each password is a session of 33 addresses (32 players + 1 zombie), isolated from the others. The list of accepted
// passwords: --mots-de-passe a,b or RELAIS_MOTS_DE_PASSE (empty = any password opens its own session).
//
// Protocol (20-byte header, little-endian, see tools/relais/README.md and tools/dlcoop/Relay.cs):
//   JOIN      agent -> relay    token = the agent's nonce, aux = version (1 or 2), payload = password
//   WELCOME   relay -> agent    token = the agent's session token
//   REFUSED   relay -> agent    aux = reason (1 password, 2 relay full, 3 version), token = nonce
//   CLAIM     agent -> relay    src = desired address (0xFF: the first free one), aux = request number
//   SLOT      relay -> agent    src = assigned address (0xFF: session full), aux = request number
//   RELEASE   agent -> relay    src = released address (game instance closed)
//   KEEPALIVE agent -> relay    every 2 s (also keeps the path through the agent's router open); v2: a
//                                16-byte empty payload (v2 ROSTER reply of the same size)
//   ROSTER    relay -> agent    srcPort = the session's active addresses, dstPort = the agent's (16-bit
//                                masks); v2: 16-byte payload = the same masks as 64 bits
//   DATA      both ways: src, dst, srcPort, dstPort + the game packet
//   BYE       agent -> relay    leaving (all its addresses are released)
// Versions: v1 (DLCoop launcher 1.1) = addresses 0..15; v2 (1.2+) = 0..32. A v1 agent only receives addresses
// and packets 0..15: the old launchers play among themselves as before, even in a mixed session.
// An agent silent for 15 s is removed. The relay never replies with a packet larger than the request
// (no amplification) and replies to nothing without a valid token, except JOIN.
//
// Build: Linux  g++ -O2 -std=c++17 -static -o relais relais.cpp   (Dockerfile provided)
//               Windows cl /O2 /std:c++17 /EHsc relais.cpp ws2_32.lib    (local tests)

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
using socklen_t = int;
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
using SOCKET = int;
constexpr SOCKET INVALID_SOCKET = -1;
#define closesocket close
#endif

#include <chrono>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr uint32_t kMagic = 0x31524C44;  // "DLR1"
constexpr uint8_t kVersionMax = 2;
constexpr int kSlots = 33;               // v2: addresses 127.0.0.1..33 on the player side (32 players + 1 zombie)
constexpr int kSlotsV1 = 16;             // v1 : 127.0.0.1..16
constexpr size_t kRosterV2 = 16;         // v2 ROSTER payload: two 64-bit masks
constexpr uint8_t kNoSlot = 0xFF;
constexpr uint16_t kDefaultPort = 20300;
constexpr int kMaxClients = 64;
constexpr int kMaxRooms = 16;
constexpr int kTimeoutSeconds = 15;
constexpr int kMaxPacketsPerSecond = 8000;     // per agent (one PC can run several instances)
constexpr int kMaxBytesPerSecond = 8 << 20;    // par agent
constexpr int kMaxJoinsPerSecond = 10;         // per IP address
constexpr size_t kMaxPassword = 64;

enum : uint8_t { kJoin = 1, kWelcome, kRefused, kClaim, kSlot, kRelease, kKeepalive, kRoster, kData, kBye };
enum : uint8_t { kBadPassword = 1, kServerFull = 2, kBadVersion = 3 };

#pragma pack(push, 1)
struct Header {
    uint32_t magic;
    uint8_t type;
    uint8_t src;
    uint8_t dst;
    uint8_t aux;
    uint16_t srcPort;
    uint16_t dstPort;
    uint64_t token;
};
#pragma pack(pop)
static_assert(sizeof(Header) == 20, "protocol header");

using Clock = std::chrono::steady_clock;

struct Client {
    sockaddr_in addr{};
    uint64_t token = 0;
    uint64_t nonce = 0;
    int room = -1;
    uint8_t version = 1;
    uint64_t slots = 0;  // mask of held addresses
    Clock::time_point lastSeen;
    // rate limits (one-second window)
    int64_t windowStart = 0;
    int packets = 0;
    int bytes = 0;
    uint64_t dropped = 0;
};

struct Room {
    std::string password;
    uint64_t owner[kSlots] = {};  // token of the agent that holds each address (0 = free)
    int number = 0;               // for the log (the password never appears in it)
};

SOCKET g_sock = INVALID_SOCKET;
std::map<uint64_t, Client> g_clients;  // jeton -> agent
std::vector<Room> g_rooms;
std::vector<std::string> g_allowed;
int g_nextRoomNumber = 1;
std::mt19937_64 g_rng;
uint64_t g_forwarded = 0, g_forwardedBytes = 0;
volatile std::sig_atomic_t g_stop = 0;

void Log(const char* fmt, ...)
{
    std::time_t t = std::time(nullptr);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y-%m-%d %H:%M:%S", std::localtime(&t));
    std::printf("%s ", stamp);
    va_list ap;
    va_start(ap, fmt);
    std::vprintf(fmt, ap);
    va_end(ap);
    std::printf("\n");
    std::fflush(stdout);
}

std::string AddrText(const sockaddr_in& a)
{
    char ip[INET_ADDRSTRLEN] = "?";
    inet_ntop(AF_INET, &a.sin_addr, ip, sizeof(ip));
    return std::string(ip) + ":" + std::to_string(ntohs(a.sin_port));
}

bool SameEndpoint(const sockaddr_in& a, const sockaddr_in& b)
{
    return a.sin_addr.s_addr == b.sin_addr.s_addr && a.sin_port == b.sin_port;
}

void Send(const sockaddr_in& to, const void* data, size_t n)
{
    sendto(g_sock, static_cast<const char*>(data), static_cast<int>(n), 0, reinterpret_cast<const sockaddr*>(&to),
           sizeof(to));
}

void SendHeader(const sockaddr_in& to, uint8_t type, uint8_t src, uint8_t aux, uint64_t token, uint16_t srcPort = 0,
                uint16_t dstPort = 0)
{
    Header h{kMagic, type, src, 0, aux, srcPort, dstPort, token};
    Send(to, &h, sizeof(h));
}

uint64_t ActiveMask(const Room& r)
{
    uint64_t m = 0;
    for (int i = 0; i < kSlots; ++i)
        if (r.owner[i])
            m |= 1ull << i;
    return m;
}

int SlotsOf(const Client& c) { return c.version >= 2 ? kSlots : kSlotsV1; }

// `full`: v2 agent and a large enough request (no amplification) -> 64-bit masks in the payload.
void SendRoster(const Client& c, bool full = true)
{
    if (c.room < 0)
        return;
    const uint64_t active = ActiveMask(g_rooms[c.room]);
    Header h{kMagic, kRoster, 0, 0, 0, static_cast<uint16_t>(active), static_cast<uint16_t>(c.slots), c.token};
    if (c.version < 2 || !full) {
        Send(c.addr, &h, sizeof(h));
        return;
    }
    char b[sizeof(Header) + kRosterV2];
    std::memcpy(b, &h, sizeof(h));
    std::memcpy(b + sizeof(h), &active, 8);
    std::memcpy(b + sizeof(h) + 8, &c.slots, 8);
    Send(c.addr, b, sizeof(b));
}

// The active addresses have changed: every agent in the session is told right away (otherwise at the latest
// at their next KEEPALIVE).
void PushRoster(int room)
{
    for (auto& [token, c] : g_clients)
        if (c.room == room)
            SendRoster(c);
}

void RemoveClient(uint64_t token, const char* why)
{
    auto it = g_clients.find(token);
    if (it == g_clients.end())
        return;
    Client& c = it->second;
    const int room = c.room;
    if (room >= 0)
        for (int i = 0; i < kSlots; ++i)
            if (g_rooms[room].owner[i] == token)
                g_rooms[room].owner[i] = 0;
    Log("agent %s removed (%s), session %d", AddrText(c.addr).c_str(), why, room >= 0 ? g_rooms[room].number : 0);
    g_clients.erase(it);
    if (room >= 0)
        PushRoster(room);
}

int FindOrCreateRoom(const std::string& password)
{
    for (size_t i = 0; i < g_rooms.size(); ++i)
        if (g_rooms[i].password == password)
            return static_cast<int>(i);
    if (!g_allowed.empty()) {
        bool ok = false;
        for (auto& p : g_allowed)
            ok |= p == password;
        if (!ok)
            return -1;
    }
    // A reusable empty session?
    for (size_t i = 0; i < g_rooms.size(); ++i) {
        bool used = false;
        for (auto& [t, c] : g_clients)
            used |= c.room == static_cast<int>(i);
        if (!used) {
            g_rooms[i].password = password;
            for (auto& o : g_rooms[i].owner)
                o = 0;
            g_rooms[i].number = g_nextRoomNumber++;
            return static_cast<int>(i);
        }
    }
    if (static_cast<int>(g_rooms.size()) >= kMaxRooms)
        return -2;
    Room r;
    r.password = password;
    r.number = g_nextRoomNumber++;
    g_rooms.push_back(r);
    return static_cast<int>(g_rooms.size() - 1);
}

void HandleJoin(const sockaddr_in& from, const Header& h, const char* payload, size_t n)
{
    // Per-IP-address limit (password attempts).
    static std::map<uint32_t, int> joins;
    static int64_t joinsWindow = 0;
    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(Clock::now().time_since_epoch()).count();
    if (now != joinsWindow) {
        joins.clear();
        joinsWindow = now;
    }
    if (++joins[from.sin_addr.s_addr] > kMaxJoinsPerSecond)
        return;

    if (h.aux < 1 || h.aux > kVersionMax) {
        SendHeader(from, kRefused, 0, kBadVersion, h.token);
        return;
    }
    // Repeat of an already-accepted JOIN (lost reply): same token.
    for (auto& [token, c] : g_clients)
        if (c.nonce == h.token && SameEndpoint(c.addr, from)) {
            c.lastSeen = Clock::now();
            SendHeader(from, kWelcome, 0, c.version, token);
            return;
        }
    if (n == 0 || n > kMaxPassword) {
        SendHeader(from, kRefused, 0, kBadPassword, h.token);
        return;
    }
    const std::string password(payload, n);
    const int room = FindOrCreateRoom(password);
    if (room == -1) {
        SendHeader(from, kRefused, 0, kBadPassword, h.token);
        Log("refused: unknown password from %s", AddrText(from).c_str());
        return;
    }
    if (room < 0 || static_cast<int>(g_clients.size()) >= kMaxClients) {
        SendHeader(from, kRefused, 0, kServerFull, h.token);
        return;
    }
    uint64_t token;
    do
        token = g_rng();
    while (token == 0 || g_clients.count(token));
    Client c;
    c.addr = from;
    c.token = token;
    c.nonce = h.token;
    c.room = room;
    c.version = h.aux;
    c.lastSeen = Clock::now();
    g_clients[token] = c;
    Log("agent %s (v%u) joined session %d (%zu agents)", AddrText(from).c_str(), c.version,
        g_rooms[room].number, g_clients.size());
    SendHeader(from, kWelcome, 0, c.version, token);
    SendRoster(g_clients[token]);
}

bool RateOk(Client& c, size_t n)
{
    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(Clock::now().time_since_epoch()).count();
    if (now != c.windowStart) {
        if (c.dropped)
            Log("agent %s: %llu packets dropped (rate too high)", AddrText(c.addr).c_str(),
                static_cast<unsigned long long>(c.dropped));
        c.windowStart = now;
        c.packets = 0;
        c.bytes = 0;
        c.dropped = 0;
    }
    if (++c.packets > kMaxPacketsPerSecond || (c.bytes += static_cast<int>(n)) > kMaxBytesPerSecond) {
        ++c.dropped;
        return false;
    }
    return true;
}

void HandlePacket(const sockaddr_in& from, char* buf, size_t n)
{
    if (n < sizeof(Header))
        return;
    Header h;
    std::memcpy(&h, buf, sizeof(h));
    if (h.magic != kMagic)
        return;
    if (h.type == kJoin) {
        HandleJoin(from, h, buf + sizeof(h), n - sizeof(h));
        return;
    }
    auto it = g_clients.find(h.token);
    if (it == g_clients.end())
        return;  // jeton inconnu : silence
    Client& c = it->second;
    if (!RateOk(c, n))
        return;
    c.lastSeen = Clock::now();
    if (!SameEndpoint(c.addr, from)) {  // the agent's router changed port: we follow
        Log("agent %s: new address %s", AddrText(c.addr).c_str(), AddrText(from).c_str());
        c.addr = from;
    }
    Room& room = g_rooms[c.room];

    switch (h.type) {
    case kData: {
        if (h.src >= kSlots || h.dst >= kSlots || room.owner[h.src] != c.token)
            return;  // the sender must hold the source address
        const uint64_t dest = room.owner[h.dst];
        if (!dest || dest == c.token)
            return;
        auto d = g_clients.find(dest);
        if (d == g_clients.end() || h.src >= SlotsOf(d->second))
            return;  // agent v1 : adresses 0..15 seulement
        h.token = dest;
        std::memcpy(buf, &h, sizeof(h));
        Send(d->second.addr, buf, n);
        ++g_forwarded;
        g_forwardedBytes += n - sizeof(Header);
        return;
    }
    case kKeepalive:
        SendRoster(c, n >= sizeof(Header) + kRosterV2);
        return;
    case kClaim: {
        int slot = -1;
        const int limit = SlotsOf(c);
        if (h.src < limit && (room.owner[h.src] == 0 || room.owner[h.src] == c.token))
            slot = h.src;  // desired address (reconnect: the game instance keeps its address)
        for (int i = 0; slot < 0 && i < limit; ++i)
            if (room.owner[i] == 0)
                slot = i;
        if (slot < 0) {
            SendHeader(from, kSlot, kNoSlot, h.aux, c.token);
            return;
        }
        const bool fresh = room.owner[slot] != c.token;
        room.owner[slot] = c.token;
        c.slots |= 1ull << slot;
        SendHeader(from, kSlot, static_cast<uint8_t>(slot), h.aux, c.token);
        if (fresh) {
            Log("session %d: address %d assigned to %s", room.number, slot, AddrText(c.addr).c_str());
            PushRoster(c.room);
        }
        return;
    }
    case kRelease:
        if (h.src < kSlots && room.owner[h.src] == c.token) {
            room.owner[h.src] = 0;
            c.slots &= ~(1ull << h.src);
            Log("session %d: address %d released", room.number, h.src);
            PushRoster(c.room);
        }
        return;
    case kBye:
        RemoveClient(c.token, "leaving");
        return;
    default:
        return;
    }
}

void OnSignal(int) { g_stop = 1; }

}  // namespace

int main(int argc, char** argv)
{
    std::setvbuf(stdout, nullptr, _IOLBF, 4096);
    uint16_t port = kDefaultPort;
    std::string passwords;
    if (const char* p = std::getenv("RELAIS_PORT"))
        port = static_cast<uint16_t>(std::atoi(p));
    if (const char* p = std::getenv("RELAIS_MOTS_DE_PASSE"))
        passwords = p;
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--port") && i + 1 < argc)
            port = static_cast<uint16_t>(std::atoi(argv[++i]));
        else if (!std::strcmp(argv[i], "--mots-de-passe") && i + 1 < argc)
            passwords = argv[++i];
        else {
            std::fprintf(stderr, "usage: relais [--port %u] [--mots-de-passe a,b]
", kDefaultPort);
            return 2;
        }
    }
    for (size_t start = 0; start <= passwords.size();) {
        size_t end = passwords.find(',', start);
        if (end == std::string::npos)
            end = passwords.size();
        std::string p = passwords.substr(start, end - start);
        if (!p.empty())
            g_allowed.push_back(p.substr(0, kMaxPassword));
        start = end + 1;
    }

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    std::random_device rd;
    g_rng.seed((static_cast<uint64_t>(rd()) << 32) ^ rd() ^
               static_cast<uint64_t>(Clock::now().time_since_epoch().count()));
    std::signal(SIGINT, OnSignal);
    std::signal(SIGTERM, OnSignal);

    g_sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    sockaddr_in local{};
    local.sin_family = AF_INET;
    local.sin_addr.s_addr = htonl(INADDR_ANY);
    local.sin_port = htons(port);
    if (g_sock == INVALID_SOCKET || bind(g_sock, reinterpret_cast<sockaddr*>(&local), sizeof(local)) != 0) {
        std::fprintf(stderr, "port UDP %u indisponible\n", port);
        return 1;
    }
    int size = 4 << 20;
    setsockopt(g_sock, SOL_SOCKET, SO_RCVBUF, reinterpret_cast<char*>(&size), sizeof(size));
    setsockopt(g_sock, SOL_SOCKET, SO_SNDBUF, reinterpret_cast<char*>(&size), sizeof(size));
#ifdef _WIN32
    BOOL off = FALSE;  // no WSAECONNRESET after an ICMP "port unreachable"
    DWORD ret = 0;
    WSAIoctl(g_sock, _WSAIOW(IOC_VENDOR, 12), &off, sizeof(off), nullptr, 0, &ret, nullptr, nullptr);
#endif
    Log("relais DLCoop : UDP %u, %s", port,
        g_allowed.empty() ? "any password opens a session"
                          : (std::to_string(g_allowed.size()) + " password(s) accepted").c_str());

    std::vector<char> buf(65536);
    auto lastSweep = Clock::now();
    auto lastReport = Clock::now();
    uint64_t reportedPackets = 0;
    while (!g_stop) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(g_sock, &readable);
        timeval tv{1, 0};
        const int ready = select(static_cast<int>(g_sock) + 1, &readable, nullptr, nullptr, &tv);
        if (ready > 0 && FD_ISSET(g_sock, &readable)) {
            for (int burst = 0; burst < 256; ++burst) {
                sockaddr_in from{};
                socklen_t fromLen = sizeof(from);
                const int n = recvfrom(g_sock, buf.data(), static_cast<int>(buf.size()),
#ifdef _WIN32
                                       0,
#else
                                       MSG_DONTWAIT,
#endif
                                       reinterpret_cast<sockaddr*>(&from), &fromLen);
                if (n <= 0)
                    break;
                HandlePacket(from, buf.data(), static_cast<size_t>(n));
#ifdef _WIN32
                u_long pending = 0;
                if (ioctlsocket(g_sock, FIONREAD, &pending) != 0 || pending == 0)
                    break;
#endif
            }
        }
        const auto now = Clock::now();
        if (now - lastSweep >= std::chrono::seconds(1)) {
            lastSweep = now;
            std::vector<uint64_t> stale;
            for (auto& [token, c] : g_clients)
                if (now - c.lastSeen > std::chrono::seconds(kTimeoutSeconds))
                    stale.push_back(token);
            for (uint64_t t : stale)
                RemoveClient(t, "silencieux");
        }
        if (now - lastReport >= std::chrono::seconds(60)) {
            lastReport = now;
            if (g_forwarded != reportedPackets)
                Log("%zu agents; %llu packets relayed (%llu MB)", g_clients.size(),
                    static_cast<unsigned long long>(g_forwarded),
                    static_cast<unsigned long long>(g_forwardedBytes >> 20));
            reportedPackets = g_forwarded;
        }
    }
    Log("relay stopped");
    closesocket(g_sock);
    return 0;
}
