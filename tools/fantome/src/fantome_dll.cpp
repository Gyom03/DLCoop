// fantome.dll - loaded by fantome.exe into a Dying Light 1.55 instance before its entry point.
// Lets you run several game instances on one PC to test LAN co-op.
//
// Settings read from the environment (set by fantome.exe):
//   DLF_INDEX     0 = host, 1..7 = ghosts
//   DLF_LOOPBACK  1 = each instance uses its own address 127.0.0.(INDEX+1) on the game ports
//   DLF_LOG       fichier journal
//   DLF_NOM       display name shown to the game instead of the Steam name (all instances; DLCoop launcher)
//   DLF_ADRESSE   network address 0..15 (127.0.0.(ADDRESS+1)) if it differs from DLF_INDEX: the DLCoop
//                 Internet relay, where the address is assigned by the relay and the identity (INDEX) stays local
//
// What the DLL does (only in the game process, never in Steam's memory):
//   1. ghosts: renames the "Global\ChromeEngine4DIMutex" lock (otherwise "Dying Light is already running");
//   2. ghosts: gives the game SteamUser / SteamFriends / SteamRemoteStorage proxies that
//      forward everything to Steam, except: modified SteamID, "Fantome N" name, ignored cloud writes;
//   3. all: the 1.55 LAN fix (packets sent to port 0 -> 20101, see Gyom03/DL-LAN-join-fix);
//   4. loopback: the game's sockets (20101..., 20111...) bound to 127.0.0.(INDEX+1), LAN broadcast replaced by a
//      direct send to each address 127.0.0.1..8.
// Everything goes through the import tables (IAT) of the game modules: not a byte of code is rewritten.
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {

constexpr int kMaxInstances = 33;  // 0..32: 32 players + 1 zombie (relay, or two PCs bridged by pont.exe)
constexpr uint16_t kGamePort = 20101;       // jeu en LAN (CLobbyConnectionsEos::GetLocalPort, type 1)
constexpr uint16_t kScanPort = 20111;       // LAN discovery (type 2)
constexpr uint16_t kPortRange = 20;         // the engine tries port, port + 1... if the port is taken
constexpr const char* kGameMutex = "Global\\ChromeEngine4DIMutex";

int g_index = 0;   // identity: 0 = real Steam account, N = additional instance (derived SteamID)
int g_slot = 0;    // network address 127.0.0.(g_slot+1) and lock Local\DLFantome_slot_N (= g_index by default)
bool g_loopback = false;
char g_name[64] = "Fantome";  // display name shown to the game (UTF-8)
bool g_nameOverride = false;   // DLF_NOM set: name replaced even for the main instance

// ---------------------------------------------------------------- journal
CRITICAL_SECTION g_logLock;
HANDLE g_log = INVALID_HANDLE_VALUE;

void Log(const char* fmt, ...)
{
    if (g_log == INVALID_HANDLE_VALUE)
        return;
    char buf[1024];
    SYSTEMTIME t;
    GetLocalTime(&t);
    int n = snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d [%d] ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds,
                     g_index);
    va_list ap;
    va_start(ap, fmt);
    n += vsnprintf(buf + n, sizeof(buf) - n - 2, fmt, ap);
    va_end(ap);
    if (n > (int)sizeof(buf) - 2)
        n = (int)sizeof(buf) - 2;
    buf[n++] = '\r';
    buf[n++] = '\n';
    EnterCriticalSection(&g_logLock);
    DWORD w;
    WriteFile(g_log, buf, n, &w, nullptr);
    LeaveCriticalSection(&g_logLock);
}

int EnvInt(const char* name, int def)
{
    char v[32];
    DWORD n = GetEnvironmentVariableA(name, v, sizeof(v));
    return (n && n < sizeof(v)) ? atoi(v) : def;
}

// ---------------------------------------------------------------- IAT
// In `mod`'s import table, replaces any entry equal to `real` with `hook`. Comparing addresses
// (not names) also covers imports by ordinal (common for WS2_32).
int PatchIat(HMODULE mod, void* real, void* hook)
{
    if (!mod || !real)
        return 0;
    auto* base = reinterpret_cast<uint8_t*>(mod);
    auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    const auto& dir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!dir.VirtualAddress)
        return 0;
    int count = 0;
    for (auto* imp = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base + dir.VirtualAddress); imp->Name; ++imp) {
        for (auto* th = reinterpret_cast<IMAGE_THUNK_DATA64*>(base + imp->FirstThunk); th->u1.Function; ++th) {
            if (reinterpret_cast<void*>(th->u1.Function) != real)
                continue;
            DWORD old;
            VirtualProtect(&th->u1.Function, sizeof(void*), PAGE_READWRITE, &old);
            th->u1.Function = reinterpret_cast<ULONGLONG>(hook);
            VirtualProtect(&th->u1.Function, sizeof(void*), old, &old);
            ++count;
        }
    }
    return count;
}

const wchar_t* const kGameModules[] = {nullptr, L"engine_x64_rwdi.dll", L"gamedll_x64_rwdi.dll",
                                       L"filesystem_x64_rwdi.dll"};

int PatchGame(const char* what, void* real, void* hook)
{
    int total = 0;
    for (const wchar_t* m : kGameModules) {
        int n = PatchIat(GetModuleHandleW(m), real, hook);
        total += n;
    }
    Log("IAT %-40s : %d entry(ies) replaced", what, total);
    return total;
}

// ---------------------------------------------------------------- 1. single-instance lock
using CreateMutexA_t = HANDLE(WINAPI*)(LPSECURITY_ATTRIBUTES, BOOL, LPCSTR);
CreateMutexA_t g_CreateMutexA;

HANDLE WINAPI HookCreateMutexA(LPSECURITY_ATTRIBUTES sa, BOOL owner, LPCSTR name)
{
    if (name && g_index > 0 && _stricmp(name, kGameMutex) == 0) {
        char renamed[96];
        snprintf(renamed, sizeof(renamed), "%s_fantome%d", kGameMutex, g_index);
        Log("game lock renamed: %s", renamed);
        return g_CreateMutexA(sa, owner, renamed);
    }
    return g_CreateMutexA(sa, owner, name);
}

// ---------------------------------------------------------------- 2. Steam proxies
// A proxy has the shape of a Steam interface: its vftable points to "thunks" that replace `this` with the
// real Steam object then jump to the same slot of the real vftable. Only a few slots are redirected to our
// functions. steamclient64.dll's memory is never modified.
constexpr int kSlots = 256;

struct SteamProxy {
    void** vtbl;
    void* real;
};

uint8_t* g_relays;  // kSlots relais de 16 octets

void BuildRelays()
{
    g_relays = static_cast<uint8_t*>(VirtualAlloc(nullptr, kSlots * 16, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
    for (int i = 0; i < kSlots; ++i) {
        uint8_t* p = g_relays + i * 16;
        const uint8_t code[] = {
            0x48, 0x8B, 0x49, 0x08,              // mov rcx, [rcx+8]   ; vrai objet
            0x48, 0x8B, 0x01,                    // mov rax, [rcx]     ; vraie vftable
            0xFF, 0xA0, 0, 0, 0, 0,              // jmp [rax+i*8]
            0xCC, 0xCC, 0xCC};
        memcpy(p, code, sizeof(code));
        int32_t disp = i * 8;
        memcpy(p + 9, &disp, 4);
    }
    DWORD old;
    VirtualProtect(g_relays, kSlots * 16, PAGE_EXECUTE_READ, &old);
    FlushInstructionCache(GetCurrentProcess(), g_relays, kSlots * 16);
}

template <class F>
F RealSlot(void* self, int slot)
{
    void* real = static_cast<SteamProxy*>(self)->real;
    return reinterpret_cast<F>((*static_cast<void***>(real))[slot]);
}
void* RealObj(void* self) { return static_cast<SteamProxy*>(self)->real; }

// ISteamUser::GetSteamID (slot 2): CSteamID returned through a hidden pointer (verified in
// SteamAPI_ISteamUser_GetSteamID de steam_api64 : lea rdx,[rsp+..] ; call [rax+0x10]).
uint64_t* __fastcall User_GetSteamID(void* self, uint64_t* out)
{
    RealSlot<uint64_t* (*)(void*, uint64_t*)>(self, 2)(RealObj(self), out);
    const uint64_t before = *out;
    // The account number (low 32 bits) is shifted; universe, type and instance stay valid.
    const uint32_t account = static_cast<uint32_t>(before) ^ (0x5A000000u | static_cast<uint32_t>(g_index));
    *out = (before & 0xFFFFFFFF00000000ull) | account;
    static bool once;
    if (!once) {
        once = true;
        Log("SteamID shown to the game: %llu (real: %llu)", *out, before);
    }
    return out;
}

// ISteamFriends::GetPersonaName (place 0).
const char* __fastcall Friends_GetPersonaName(void*) { return g_name; }

// ISteamRemoteStorage: FileWrite (0), FileForget (5), FileDelete (6) ignored for a ghost.
bool __fastcall Storage_FileWrite(void*, const char* file, const void*, int32_t size)
{
    Log("cloud: write ignored %s (%d bytes)", file ? file : "?", size);
    return true;
}
bool __fastcall Storage_FileForget(void*, const char* file)
{
    Log("cloud: FileForget ignored %s", file ? file : "?");
    return true;
}
bool __fastcall Storage_FileDelete(void*, const char* file)
{
    Log("cloud: delete ignored %s", file ? file : "?");
    return true;
}

struct ProxyEntry {
    void* real;
    SteamProxy* proxy;
};
std::vector<ProxyEntry> g_proxies;
CRITICAL_SECTION g_proxyLock;

SteamProxy* MakeProxy(void* real, const char* version)
{
    for (auto& e : g_proxies)
        if (e.real == real)
            return e.proxy;
    auto** vt = new void*[kSlots];
    for (int i = 0; i < kSlots; ++i)
        vt[i] = g_relays + i * 16;
    if (strncmp(version, "SteamUser0", 10) == 0) {
        vt[2] = reinterpret_cast<void*>(&User_GetSteamID);
    } else if (strncmp(version, "SteamFriends0", 13) == 0) {
        vt[0] = reinterpret_cast<void*>(&Friends_GetPersonaName);
    } else if (strncmp(version, "STEAMREMOTESTORAGE_INTERFACE_VERSION", 36) == 0) {
        vt[0] = reinterpret_cast<void*>(&Storage_FileWrite);
        vt[5] = reinterpret_cast<void*>(&Storage_FileForget);
        vt[6] = reinterpret_cast<void*>(&Storage_FileDelete);
    }
    auto* p = new SteamProxy{vt, real};
    g_proxies.push_back({real, p});
    Log("Steam proxy created for %s", version);
    return p;
}

using FindOrCreate_t = void*(__cdecl*)(int32_t, const char*);
FindOrCreate_t g_FindOrCreate;

void* __cdecl HookFindOrCreateUserInterface(int32_t user, const char* version)
{
    void* real = g_FindOrCreate(user, version);
    if (!real || !version)
        return real;
    // Main instance with a replaced name: only SteamFriends goes through a proxy (real SteamID, real cloud
    // saves). Additional instance: all three.
    const bool friends = strncmp(version, "SteamFriends0", 13) == 0;
    const bool other = strncmp(version, "SteamUser0", 10) == 0 ||
                       strncmp(version, "STEAMREMOTESTORAGE_INTERFACE_VERSION", 36) == 0;
    if (!(friends && (g_index > 0 || g_nameOverride)) && !(other && g_index > 0))
        return real;
    EnterCriticalSection(&g_proxyLock);
    SteamProxy* p = MakeProxy(real, version);
    LeaveCriticalSection(&g_proxyLock);
    return p;
}

// ---------------------------------------------------------------- 3 and 4. network
using bind_t = int(WSAAPI*)(SOCKET, const sockaddr*, int);
using sendto_t = int(WSAAPI*)(SOCKET, const char*, int, int, const sockaddr*, int);
using WSASendTo_t = int(WSAAPI*)(SOCKET, LPWSABUF, DWORD, LPDWORD, DWORD, const sockaddr*, int, LPWSAOVERLAPPED,
                                 LPWSAOVERLAPPED_COMPLETION_ROUTINE);
bind_t g_bind;
sendto_t g_sendto;
WSASendTo_t g_WSASendTo;

uint32_t LoopbackAddr(int index) { return htonl(0x7F000001u + static_cast<uint32_t>(index)); }  // 127.0.0.(i+1)

bool IsGamePort(uint16_t hostPort)
{
    return (hostPort >= kGamePort && hostPort < kGamePort + kPortRange) ||
           (hostPort >= kScanPort && hostPort < kScanPort + kPortRange);
}

bool IsBroadcast(uint32_t netAddr)
{
    const uint32_t a = ntohl(netAddr);
    return a == 0xFFFFFFFFu || (a & 0xFFu) == 0xFFu;
}

void FmtAddr(const sockaddr_in& a, char* out, size_t n)
{
    const uint8_t* b = reinterpret_cast<const uint8_t*>(&a.sin_addr);
    snprintf(out, n, "%u.%u.%u.%u:%u", b[0], b[1], b[2], b[3], ntohs(a.sin_port));
}

// Logs each distinct destination only once (diagnostics for the first attempts).
void LogDest(const char* fn, const sockaddr_in& a, const char* note)
{
    static uint64_t seen[64];
    static int nseen;
    const uint64_t key = (static_cast<uint64_t>(a.sin_addr.s_addr) << 16) | a.sin_port;
    for (int i = 0; i < nseen; ++i)
        if (seen[i] == key)
            return;
    if (nseen < 64)
        seen[nseen++] = key;
    char s[32];
    FmtAddr(a, s, sizeof(s));
    Log("%s -> %s %s", fn, s, note);
}

using WSAIoctl_t = int(WSAAPI*)(SOCKET, DWORD, LPVOID, DWORD, LPVOID, DWORD, LPDWORD, LPWSAOVERLAPPED,
                                LPWSAOVERLAPPED_COMPLETION_ROUTINE);
WSAIoctl_t g_WSAIoctl;

// An instance is alive if its DLL holds the "Local\DLFantome_slot_N" lock.
bool InstanceAlive(int index)
{
    wchar_t name[64];
    swprintf(name, 64, L"Local\\DLFantome_slot_%d", index);
    HANDLE h = OpenMutexW(SYNCHRONIZE, FALSE, name);
    if (h)
        CloseHandle(h);
    return h != nullptr;
}

int WSAAPI HookBind(SOCKET s, const sockaddr* name, int len)
{
    if (name && name->sa_family == AF_INET && len >= (int)sizeof(sockaddr_in)) {
        sockaddr_in a = *reinterpret_cast<const sockaddr_in*>(name);
        const uint16_t port = ntohs(a.sin_port);
        if (g_loopback && IsGamePort(port)) {
            a.sin_addr.s_addr = LoopbackAddr(g_slot);
            // A send to a 127.0.0.N address with no instance returns "port unreachable"; Windows
            // signale alors au recvfrom suivant (WSAECONNRESET), ce qu'une vraie diffusion LAN ne fait jamais.
            BOOL off = FALSE;
            DWORD ret = 0;
            if (g_WSAIoctl)
                g_WSAIoctl(s, _WSAIOW(IOC_VENDOR, 12) /* SIO_UDP_CONNRESET */, &off, sizeof(off), nullptr, 0, &ret,
                           nullptr, nullptr);
        }
        int r = g_bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
        if (IsGamePort(port)) {
            char t[32];
            FmtAddr(a, t, sizeof(t));
            Log("bind %s -> %s", t, r == 0 ? "ok" : "FAILED");
        }
        return r;
    }
    return g_bind(s, name, len);
}

// The address form under which the engine knows each peer. 1.55 bug: `CInetAddr(SGameAddressEos
// const&)` builds an address with no port, and the engine then remembers some peers under "IP:0". Their
// replies, which arrive from "IP:20101", would not be recognized (Net::CLocalClient::FindSender) and would be
// dropped: so we put port 0 back on receive. But from one session to the next, the same peer may be known
// by its real address (passed by another player): so we track the **last** form used by the game for sending,
// per peer, instead of a final decision.
constexpr int kMaxPeers = 64;
struct PeerForm {
    uint32_t addr;  // IPv4, network order
    bool zero;      // the game addresses this peer on port 0
};
PeerForm g_peers[kMaxPeers];
int g_peerCount;
SRWLOCK g_peerLock = SRWLOCK_INIT;

void NotePeerForm(uint32_t addr, bool zero)
{
    AcquireSRWLockExclusive(&g_peerLock);
    int i = 0;
    while (i < g_peerCount && g_peers[i].addr != addr)
        ++i;
    const bool known = i < g_peerCount;
    const bool changed = !known || g_peers[i].zero != zero;
    if (!known && g_peerCount < kMaxPeers)
        g_peers[g_peerCount++] = {addr, zero};
    else if (known)
        g_peers[i].zero = zero;
    ReleaseSRWLockExclusive(&g_peerLock);
    if (changed) {
        const uint8_t* b = reinterpret_cast<const uint8_t*>(&addr);
        Log("peer %u.%u.%u.%u addressed %s", b[0], b[1], b[2], b[3],
            zero ? "on port 0 (replies presented on port 0)" : "on its real port 20101");
    }
}

bool IsZeroPortPeer(uint32_t addr)
{
    AcquireSRWLockShared(&g_peerLock);
    bool zero = false;
    for (int i = 0; i < g_peerCount; ++i)
        if (g_peers[i].addr == addr) {
            zero = g_peers[i].zero;
            break;
        }
    ReleaseSRWLockShared(&g_peerLock);
    return zero;
}

// Prepares the destination address; returns the number of destinations written to `out`.
int Destinations(const sockaddr* to, int tolen, sockaddr_in* out, const char* fn)
{
    if (!to || to->sa_family != AF_INET || tolen < (int)sizeof(sockaddr_in))
        return 0;
    sockaddr_in a = *reinterpret_cast<const sockaddr_in*>(to);
    const char* note = "";
    if (a.sin_port == 0) {  // 1.55 bug: the game port is never passed by discovery
        a.sin_port = htons(kGamePort);
        note = "(port 0 corrected to 20101)";
        if (!IsBroadcast(a.sin_addr.s_addr))
            NotePeerForm(a.sin_addr.s_addr, true);
    } else if (a.sin_port == htons(kGamePort) && !IsBroadcast(a.sin_addr.s_addr)) {
        NotePeerForm(a.sin_addr.s_addr, false);
    }
    if (g_loopback && IsBroadcast(a.sin_addr.s_addr) && IsGamePort(ntohs(a.sin_port))) {
        int n = 0;
        for (int i = 0; i < kMaxInstances; ++i) {
            if (i == g_slot || !InstanceAlive(i))
                continue;
            out[n] = a;
            out[n].sin_addr.s_addr = LoopbackAddr(i);
            ++n;
        }
        LogDest(fn, a, "(broadcast replaced by a send to each launched instance)");
        return n > 0 ? n : -1;  // -1: no other instance, nothing to send
    }
    out[0] = a;
    LogDest(fn, a, note);
    return 1;
}

int WSAAPI HookSendTo(SOCKET s, const char* buf, int len, int flags, const sockaddr* to, int tolen)
{
    sockaddr_in dst[kMaxInstances];
    int n = Destinations(to, tolen, dst, "sendto");
    if (n == 0)
        return g_sendto(s, buf, len, flags, to, tolen);
    if (n < 0)
        return len;  // broadcast with no recipient: succeeded, as on an empty network
    int r = SOCKET_ERROR;
    for (int i = 0; i < n; ++i) {
        int ri = g_sendto(s, buf, len, flags, reinterpret_cast<sockaddr*>(&dst[i]), sizeof(sockaddr_in));
        if (ri != SOCKET_ERROR)
            r = ri;
    }
    return n > 1 ? len : r;  // broadcast: succeeds as soon as one send goes through, like a real broadcast
}

int WSAAPI HookWSASendTo(SOCKET s, LPWSABUF bufs, DWORD nbufs, LPDWORD sent, DWORD flags, const sockaddr* to,
                         int tolen, LPWSAOVERLAPPED ov, LPWSAOVERLAPPED_COMPLETION_ROUTINE cb)
{
    sockaddr_in dst[kMaxInstances];
    int n = Destinations(to, tolen, dst, "WSASendTo");
    if (n <= 0)  // nothing to rewrite, or broadcast with no recipient: original send (also completes the OVERLAPPED)
        return g_WSASendTo(s, bufs, nbufs, sent, flags, to, tolen, ov, cb);
    if (n > 1) {
        // Extra destinations as plain sends (an OVERLAPPED can serve only once).
        std::vector<char> flat;
        for (DWORD i = 0; i < nbufs; ++i)
            flat.insert(flat.end(), bufs[i].buf, bufs[i].buf + bufs[i].len);
        for (int i = 1; i < n; ++i)
            g_sendto(s, flat.data(), static_cast<int>(flat.size()), 0, reinterpret_cast<sockaddr*>(&dst[i]),
                     sizeof(sockaddr_in));
    }
    return g_WSASendTo(s, bufs, nbufs, sent, flags, reinterpret_cast<sockaddr*>(&dst[0]), sizeof(sockaddr_in), ov,
                       cb);
}

using recvfrom_t = int(WSAAPI*)(SOCKET, char*, int, int, sockaddr*, int*);
recvfrom_t g_recvfrom;

int WSAAPI HookRecvFrom(SOCKET s, char* buf, int len, int flags, sockaddr* from, int* fromlen)
{
    const int r = g_recvfrom(s, buf, len, flags, from, fromlen);
    // Only if a packet was actually received (r > 0): the engine polls the socket in a loop.
    if (r > 0 && from && fromlen && *fromlen >= (int)sizeof(sockaddr_in) && from->sa_family == AF_INET) {
        auto* a = reinterpret_cast<sockaddr_in*>(from);
        if (a->sin_port == htons(kGamePort) && IsZeroPortPeer(a->sin_addr.s_addr)) {
            a->sin_port = 0;
            static uint32_t logged[8];
            static int nlogged;
            bool seen = false;
            for (int i = 0; i < nlogged; ++i)
                seen |= logged[i] == a->sin_addr.s_addr;
            if (!seen && nlogged < 8) {
                logged[nlogged++] = a->sin_addr.s_addr;
                const uint8_t* b = reinterpret_cast<const uint8_t*>(&a->sin_addr);
                Log("recvfrom <- %u.%u.%u.%u:20101 presented as port 0", b[0], b[1], b[2], b[3]);
            }
        }
    }
    return r;
}

// ---------------------------------------------------------------- installation
void Install()
{
    InitializeCriticalSection(&g_logLock);
    InitializeCriticalSection(&g_proxyLock);
    g_index = EnvInt("DLF_INDEX", 0);
    g_loopback = EnvInt("DLF_LOOPBACK", 0) != 0;
    if (g_index < 0 || g_index >= kMaxInstances)
        g_index = 0;
    g_slot = EnvInt("DLF_ADRESSE", g_index);
    if (g_slot < 0 || g_slot >= kMaxInstances)
        g_slot = g_index;
    snprintf(g_name, sizeof(g_name), "Fantome %d", g_index);
    wchar_t nom[32];
    if (GetEnvironmentVariableW(L"DLF_NOM", nom, 32) > 0 && nom[0]) {  // pseudo choisi (lanceur DLCoop)
        WideCharToMultiByte(CP_UTF8, 0, nom, -1, g_name, sizeof(g_name), nullptr, nullptr);
        g_nameOverride = true;
    }

    char path[MAX_PATH];
    if (GetEnvironmentVariableA("DLF_LOG", path, sizeof(path)))
        g_log = CreateFileA(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL,
                            nullptr);
    Log("---- fantome.dll loaded (pid %lu, instance %d, %s, loopback %s, address 127.0.0.%d)", GetCurrentProcessId(),
        g_index, g_index ? "ghost" : "host", g_loopback ? "yes" : "no", g_slot + 1);

    // The exe's single lock (also KernelBase's address in case the import is already resolved by then).
    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    g_CreateMutexA = reinterpret_cast<CreateMutexA_t>(GetProcAddress(k32, "CreateMutexA"));
    if (g_index > 0) {
        int n = PatchGame("kernel32!CreateMutexA", reinterpret_cast<void*>(g_CreateMutexA),
                          reinterpret_cast<void*>(&HookCreateMutexA));
        HMODULE kb = GetModuleHandleW(L"kernelbase.dll");
        void* kbFn = kb ? reinterpret_cast<void*>(GetProcAddress(kb, "CreateMutexA")) : nullptr;
        if (kbFn && kbFn != reinterpret_cast<void*>(g_CreateMutexA))
            n += PatchGame("kernelbase!CreateMutexA", kbFn, reinterpret_cast<void*>(&HookCreateMutexA));
        if (!n)
            Log("WARNING: CreateMutexA import not found, the 2nd launch may be refused");
    }

    // Steam (ghosts: derived identity; main instance: only if the name is replaced).
    if (g_index > 0 || g_nameOverride) {
        HMODULE sa = GetModuleHandleW(L"steam_api64.dll");
        g_FindOrCreate = sa ? reinterpret_cast<FindOrCreate_t>(
                                  GetProcAddress(sa, "SteamInternal_FindOrCreateUserInterface"))
                            : nullptr;
        BuildRelays();
        if (!g_FindOrCreate ||
            !PatchGame("steam_api64!SteamInternal_FindOrCreateUserInterface",
                       reinterpret_cast<void*>(g_FindOrCreate),
                       reinterpret_cast<void*>(&HookFindOrCreateUserInterface)))
            Log("WARNING: Steam interfaces not intercepted (duplicate identity)");
    }

    // Network (all instances).
    HMODULE ws = GetModuleHandleW(L"ws2_32.dll");
    if (!ws)
        ws = LoadLibraryW(L"ws2_32.dll");
    g_bind = reinterpret_cast<bind_t>(GetProcAddress(ws, "bind"));
    g_sendto = reinterpret_cast<sendto_t>(GetProcAddress(ws, "sendto"));
    g_WSASendTo = reinterpret_cast<WSASendTo_t>(GetProcAddress(ws, "WSASendTo"));
    g_WSAIoctl = reinterpret_cast<WSAIoctl_t>(GetProcAddress(ws, "WSAIoctl"));
    PatchGame("ws2_32!bind", reinterpret_cast<void*>(g_bind), reinterpret_cast<void*>(&HookBind));
    PatchGame("ws2_32!sendto", reinterpret_cast<void*>(g_sendto), reinterpret_cast<void*>(&HookSendTo));
    PatchGame("ws2_32!WSASendTo", reinterpret_cast<void*>(g_WSASendTo), reinterpret_cast<void*>(&HookWSASendTo));
    g_recvfrom = reinterpret_cast<recvfrom_t>(GetProcAddress(ws, "recvfrom"));
    PatchGame("ws2_32!recvfrom", reinterpret_cast<void*>(g_recvfrom), reinterpret_cast<void*>(&HookRecvFrom));

    // Marks the slot as taken so fantome.exe picks the next number.
    wchar_t slot[64];
    swprintf(slot, 64, L"Local\\DLFantome_slot_%d", g_slot);
    CreateMutexW(nullptr, FALSE, slot);
    Log("install finished");
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE mod, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(mod);
        Install();
    }
    return TRUE;
}
