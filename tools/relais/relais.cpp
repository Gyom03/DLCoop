// relais — serveur relais Internet du lanceur DLCoop : coop Dying Light 1.55 sans VPN ni mode en ligne Steam.
//
// Même principe que tools/fantome/src/pont.cpp, en étoile : chaque joueur (le lanceur DLCoop, « agent ») se connecte
// au relais en UDP, depuis chez lui (connexion sortante : aucun port à ouvrir sur les box). Le relais attribue à
// chaque instance du jeu une ADRESSE 0..32 (le jeu parle sur 127.0.0.(adresse+1)) et transmet chaque paquet du jeu à
// l'agent qui détient l'adresse destinataire. Le jeu n'est jamais exposé à Internet : seul ce port l'est.
//
// Sessions : chaque mot de passe est une session de 33 adresses (32 joueurs + 1 zombie), isolée des autres. Liste des mots de passe
// acceptés : --mots-de-passe a,b ou RELAIS_MOTS_DE_PASSE (vide = tout mot de passe ouvre sa propre session).
//
// Protocole (en-tête de 20 octets, petit-boutiste, cf. tools/relais/README.md et tools/dlcoop/Relay.cs) :
//   JOIN      agent -> relais   token = nonce de l'agent, aux = version (1 ou 2), charge = mot de passe
//   WELCOME   relais -> agent   token = jeton de session de l'agent
//   REFUSED   relais -> agent   aux = raison (1 mot de passe, 2 relais plein, 3 version), token = nonce
//   CLAIM     agent -> relais   src = adresse souhaitée (0xFF : la première libre), aux = numéro de requête
//   SLOT      relais -> agent   src = adresse attribuée (0xFF : session pleine), aux = numéro de requête
//   RELEASE   agent -> relais   src = adresse libérée (instance du jeu fermée)
//   KEEPALIVE agent -> relais   toutes les 2 s (maintient aussi le passage dans la box de l'agent) ; v2 : charge
//                                vide de 16 octets (réponse ROSTER v2 de même taille)
//   ROSTER    relais -> agent   srcPort = adresses actives de la session, dstPort = celles de l'agent (masques
//                                de 16 bits) ; v2 : charge de 16 octets = les mêmes masques sur 64 bits
//   DATA      dans les deux sens : src, dst, srcPort, dstPort + paquet du jeu
//   BYE       agent -> relais   départ (toutes ses adresses sont libérées)
// Versions : v1 (lanceur DLCoop 1.1) = adresses 0..15 ; v2 (1.2+) = 0..32. Un agent v1 ne reçoit que des adresses
// et des paquets 0..15 : les anciens paquets jouent entre eux comme avant, même dans une session mixte.
// Un agent silencieux pendant 15 s est retiré. Le relais ne répond jamais par un paquet plus gros que la requête
// (pas d'amplification) et ne répond à rien sans jeton valide, sauf à JOIN.
//
// Compilation : Linux  g++ -O2 -std=c++17 -static -o relais relais.cpp   (Dockerfile fourni)
//               Windows cl /O2 /std:c++17 /EHsc relais.cpp ws2_32.lib    (essais en local)

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

constexpr uint32_t kMagic = 0x31524C44;  // « DLR1 »
constexpr uint8_t kVersionMax = 2;
constexpr int kSlots = 33;               // v2 : adresses 127.0.0.1..33 côté joueurs (32 joueurs + 1 zombie)
constexpr int kSlotsV1 = 16;             // v1 : 127.0.0.1..16
constexpr size_t kRosterV2 = 16;         // charge de ROSTER v2 : deux masques de 64 bits
constexpr uint8_t kNoSlot = 0xFF;
constexpr uint16_t kDefaultPort = 20300;
constexpr int kMaxClients = 64;
constexpr int kMaxRooms = 16;
constexpr int kTimeoutSeconds = 15;
constexpr int kMaxPacketsPerSecond = 8000;     // par agent (un PC peut faire tourner plusieurs instances)
constexpr int kMaxBytesPerSecond = 8 << 20;    // par agent
constexpr int kMaxJoinsPerSecond = 10;         // par adresse IP
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
static_assert(sizeof(Header) == 20, "en-tête du protocole");

using Clock = std::chrono::steady_clock;

struct Client {
    sockaddr_in addr{};
    uint64_t token = 0;
    uint64_t nonce = 0;
    int room = -1;
    uint8_t version = 1;
    uint64_t slots = 0;  // masque des adresses détenues
    Clock::time_point lastSeen;
    // limites de débit (fenêtre d'une seconde)
    int64_t windowStart = 0;
    int packets = 0;
    int bytes = 0;
    uint64_t dropped = 0;
};

struct Room {
    std::string password;
    uint64_t owner[kSlots] = {};  // jeton de l'agent qui détient chaque adresse (0 = libre)
    int number = 0;               // pour le journal (le mot de passe n'y apparaît jamais)
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

// `full` : agent v2 et requête assez grande (pas d'amplification) -> masques de 64 bits en charge.
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

// Les adresses actives ont changé : tous les agents de la session en sont avertis tout de suite (sinon au plus
// tard à leur prochain KEEPALIVE).
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
    Log("agent %s retiré (%s), session %d", AddrText(c.addr).c_str(), why, room >= 0 ? g_rooms[room].number : 0);
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
    // Session vide réutilisable ?
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
    // Limite par adresse IP (essais de mots de passe).
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
    // Répétition d'un JOIN déjà accepté (réponse perdue) : même jeton.
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
        Log("refus : mot de passe inconnu depuis %s", AddrText(from).c_str());
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
    Log("agent %s (v%u) entré dans la session %d (%zu agents)", AddrText(from).c_str(), c.version,
        g_rooms[room].number, g_clients.size());
    SendHeader(from, kWelcome, 0, c.version, token);
    SendRoster(g_clients[token]);
}

bool RateOk(Client& c, size_t n)
{
    const int64_t now = std::chrono::duration_cast<std::chrono::seconds>(Clock::now().time_since_epoch()).count();
    if (now != c.windowStart) {
        if (c.dropped)
            Log("agent %s : %llu paquets jetés (débit trop élevé)", AddrText(c.addr).c_str(),
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
    if (!SameEndpoint(c.addr, from)) {  // la box de l'agent a changé de port : on suit
        Log("agent %s : nouvelle adresse %s", AddrText(c.addr).c_str(), AddrText(from).c_str());
        c.addr = from;
    }
    Room& room = g_rooms[c.room];

    switch (h.type) {
    case kData: {
        if (h.src >= kSlots || h.dst >= kSlots || room.owner[h.src] != c.token)
            return;  // l'émetteur doit détenir l'adresse source
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
            slot = h.src;  // adresse souhaitée (reconnexion : l'instance du jeu garde son adresse)
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
            Log("session %d : adresse %d attribuée à %s", room.number, slot, AddrText(c.addr).c_str());
            PushRoster(c.room);
        }
        return;
    }
    case kRelease:
        if (h.src < kSlots && room.owner[h.src] == c.token) {
            room.owner[h.src] = 0;
            c.slots &= ~(1ull << h.src);
            Log("session %d : adresse %d libérée", room.number, h.src);
            PushRoster(c.room);
        }
        return;
    case kBye:
        RemoveClient(c.token, "départ");
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
            std::fprintf(stderr, "usage : relais [--port %u] [--mots-de-passe a,b]\n", kDefaultPort);
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
    BOOL off = FALSE;  // pas de WSAECONNRESET après un ICMP « port inaccessible »
    DWORD ret = 0;
    WSAIoctl(g_sock, _WSAIOW(IOC_VENDOR, 12), &off, sizeof(off), nullptr, 0, &ret, nullptr, nullptr);
#endif
    Log("relais DLCoop : UDP %u, %s", port,
        g_allowed.empty() ? "tout mot de passe ouvre une session"
                          : (std::to_string(g_allowed.size()) + " mot(s) de passe accepté(s)").c_str());

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
                Log("%zu agents ; %llu paquets relayés (%llu Mo)", g_clients.size(),
                    static_cast<unsigned long long>(g_forwarded),
                    static_cast<unsigned long long>(g_forwardedBytes >> 20));
            reportedPackets = g_forwarded;
        }
    }
    Log("arrêt du relais");
    closesocket(g_sock);
    return 0;
}
