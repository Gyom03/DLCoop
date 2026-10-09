// Offline test of coop6.dll: loads gamedll_x64_rwdi.dll and engine_x64_rwdi.dll into memory WITHOUT running them
// (no DllMain, imports unresolved), then coop6.dll, which installs itself in its DllMain. Then checks:
//   - the A2 byte (lobby cap) and the jumps placed by the detours (to coop6.dll);
//   - the GetPlayerDrawColor trampoline (15 original bytes + jump to the rest);
//   - the vftables: InitializeLobbyInfo (2), GetMaxStamina (4), HudPlayersList::OnInit (1) point to the mod;
//   - the InitializeLobbyInfo wrapper: a LAN game (visibility 3) with a fake GameDI -> 7 public slots;
//     the prologue (part 1) -> 1 slot, unchanged.
// Usage : coop6_host.exe <gamedll> <engine> <coop6.dll>   (code de sortie 0 = tout est conforme)
#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>

#include "../../mods/coop6/BtzScale.h"

namespace {
int g_failures = 0;
void Check(bool ok, const wchar_t* what)
{
    wprintf(L"%s %s\n", ok ? L"[ok]  " : L"[FAIL]", what);
    if (!ok)
        ++g_failures;
}

std::uint8_t* g_game = nullptr;
std::uint8_t* g_mod = nullptr;
std::size_t g_modSize = 0;

bool InMod(const void* p) { return p >= g_mod && p < g_mod + g_modSize; }

const void* JumpTarget(const std::uint8_t* at)  // `jmp [rip+0] ; dq cible`
{
    if (at[0] != 0xFF || at[1] != 0x25 || at[2] || at[3] || at[4] || at[5])
        return nullptr;
    const void* to;
    std::memcpy(&to, at + 6, 8);
    return to;
}

int CountInRdata(const void* value)
{
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(g_game + reinterpret_cast<IMAGE_DOS_HEADER*>(g_game)->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (std::strncmp(reinterpret_cast<const char*>(sec->Name), ".rdata", 8) != 0)
            continue;
        int n = 0;
        for (std::size_t off = 0; off + 8 <= sec->Misc.VirtualSize; off += 8)
            if (*reinterpret_cast<const void* const*>(g_game + sec->VirtualAddress + off) == value)
                ++n;
        return n;
    }
    return -1;
}

void* VftableSlot(std::uintptr_t rva) { return *reinterpret_cast<void**>(g_game + rva); }

struct LobbyInfo {
    std::int32_t type;
    std::uint32_t publicSlots, privateSlots;
    std::int32_t flags, query;
};
}  // namespace

int wmain(int argc, wchar_t** argv)
{
    if (argc != 4)
        return 2;
    g_game = reinterpret_cast<std::uint8_t*>(LoadLibraryExW(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES));
    HMODULE engine = LoadLibraryExW(argv[2], nullptr, DONT_RESOLVE_DLL_REFERENCES);
    if (!g_game || !engine) {
        wprintf(L"chargement de gamedll/engine impossible (%lu)\n", GetLastError());
        return 1;
    }
    // Original bytes of GetPlayerDrawColor, before install (to verify the trampoline).
    std::uint8_t drawColorOrig[15];
    std::memcpy(drawColorOrig, g_game + 0x557790, 15);
    std::uint8_t gatherOrig[15];  // F1: GatherPlace::Update
    std::memcpy(gatherOrig, g_game + 0xaa9f00, 15);
    std::uint8_t scriptInitOrig[14];  // Z1 : CEngineScriptManager::Initialize
    std::memcpy(scriptInitOrig, g_game + 0x1104480, 14);
    std::uint8_t spitsOrig[16], hiveOrig[18];  // Z4, Z6
    std::memcpy(spitsOrig, g_game + 0xdd7ca0, 16);
    std::memcpy(hiveOrig, g_game + 0xe4070, 18);

    HMODULE mod = LoadLibraryW(argv[3]);
    if (!mod) {
        wprintf(L"chargement de coop6.dll impossible (%lu)\n", GetLastError());
        return 1;
    }
    g_mod = reinterpret_cast<std::uint8_t*>(mod);
    g_modSize = reinterpret_cast<IMAGE_NT_HEADERS*>(g_mod + reinterpret_cast<IMAGE_DOS_HEADER*>(g_mod)->e_lfanew)
                    ->OptionalHeader.SizeOfImage;
    wprintf(L"gamedll %p, engine %p, coop6 %p\n", g_game, engine, g_mod);

    Check(g_game[0x4b307a] == 0xBA && g_game[0x4b307b] == 7 && g_game[0x4b307f] == 0xB9,
          L"A2 : `mov edx,7` dans SessionDI::Create");
    const std::uint8_t permit[7] = {0xBF, 0x01, 0x00, 0x00, 0x00, 0xEB, 0x2F};
    Check(std::memcmp(g_game + 0xf31a90, permit, 7) == 0 && g_game[0xf31a97] == 0x90 && g_game[0xf31aa1] == 0x90 &&
              g_game[0xf31aa2] == 0x48,
          L"A3: CheckCanConnect, `mov edi,1 ; jmp short` + nop in place of error 0x2e");
    Check(InMod(JumpTarget(g_game + 0x4c9b30)), L"A3: Lobby_UpdateZombieCanJoin detoured to the mod");
    Check(InMod(JumpTarget(g_game + 0x4a8ec0)), L"A3: SessionCooperativeDI::Lobby_UpdateZombieCanJoin detoured to the mod");
    Check(g_game[0x4a9449] == 0x83 && g_game[0x4a944a] == 0xF8 && g_game[0x4a944b] == 6, L"A3 : IsJoinable, humains < 6");
    Check(g_game[0x4a9363] == 0x83 && g_game[0x4a9364] == 0xF8 && g_game[0x4a9365] == 7, L"A4 : IsJoinable, joueurs < 7");
    Check(g_game[0xa1a397 + 6] == 33 && g_game[0xa1a7e2 + 6] == 33,
          L"A5 : SGameScannerResult, maximum de joueurs 33 (toujours le maximum, quel que soit Places)");
    Check(reinterpret_cast<std::uint8_t*>(engine)[0x6a28e0] == 0xC3 &&
              reinterpret_cast<std::uint8_t*>(engine)[0x6a28e1] == 0x53,
          L"E1: CEosAuthenticator::Login -> ret (EOS connection disabled)");
    {
        // B5: LimitAtackingWalkers' call leads (nearby relay) to the mod function, which caps the list at 5.
        std::uint8_t* site = g_game + 0x1b94a1;
        std::uint8_t* relay = site + 5 + *reinterpret_cast<std::int32_t*>(site + 1);
        Check(site[0] == 0xE8 && InMod(JumpTarget(relay)), L"B5: LimitAtackingWalkers, GetAllPlayers redirected to the mod");
        std::uint8_t* site6 = g_game + 0x318a42;
        std::uint8_t* relay6 = site6 + 5 + *reinterpret_cast<std::int32_t*>(site6 + 1);
        Check(site6[0] == 0xE8 && InMod(JumpTarget(relay6)),
              L"B6: CalculateSpawnPlaceRatings, GetAllPlayers redirected to the mod");
    }
    Check(InMod(JumpTarget(g_game + 0x48ce10)), L"B2: UpdatePlayerIndexList detoured to the mod");
    Check(InMod(JumpTarget(g_game + 0x5eb2a0)), L"D1: GetMaxHealthCoopMul detoured to the mod");
    Check(InMod(JumpTarget(g_game + 0x5eb330)), L"D1: GetMeleeDamageCoopMul detoured to the mod");
    Check(InMod(JumpTarget(g_game + 0x557790)), L"B3: GetPlayerDrawColor detoured to the mod");
    Check(InMod(JumpTarget(g_game + 0xaa9f00)), L"F1: GatherPlace::Update detoured to the mod");

    // G / Z: the "4 players" case extended to 4 and more.
    Check(g_game[0x1ccede] == 0x7C && g_game[0x1ccfee] == 0x7C, L"G1 G2: placed groups, jne -> jl");
    Check(g_game[0xe517d1] == 0x7C && g_game[0xea71ea] == 0x7C, L"G3 G4: quest steps, jne -> jl");
    Check(g_game[0xbcf921] == 0x7C, L"G5: repair parts, jne -> jl");
    Check(g_game[0xa61e7e] == 0x7C, L"Z5 : BtzDamageCoopSwitch, jne -> jl");
    Check(g_game[0xba71da] == 0x0F && g_game[0xba71db] == 0x4D && g_game[0xce11f1] == 0x0F && g_game[0xce11f2] == 0x4D,
          L"Z2 Z3: zombie health and regeneration, cmove -> cmovge");
    {
        std::uint8_t* site = g_game + 0xdd7d8c;
        std::uint8_t* relay = site + 5 + *reinterpret_cast<std::int32_t*>(site + 1);
        Check(site[0] == 0xE8 && InMod(JumpTarget(relay)), L"Z4: GetSpitsRegenTime, player count redirected to the mod");
    }
    const std::uint8_t hive[18] = {0xBA, 0x03, 0x00, 0x00, 0x00, 0xFF, 0xC8, 0x39, 0xD0,
                                   0x0F, 0x4C, 0xD0, 0x85, 0xD2, 0x0F, 0x48, 0xD3, 0x90};
    Check(std::memcmp(g_game + 0xe40f6, hive, 18) == 0 && g_game[0xe40f0] == 0x8B && g_game[0xe4108] == 0x48,
          L"Z6 : ruches, index = min(max(chasseurs - 1, 0), 3)");
    Check(InMod(JumpTarget(g_game + 0x1104480)), L"Z1: CEngineScriptManager::Initialize detoured to the mod");
    {
        std::size_t prefix = 0;
        int n = 0;
        char out[128];
        const bool p16 = ParseHumansKey("f_btz_human_respawn_delay_16_human", prefix, n) && n == 16 &&
                         MakeHumansKey("f_btz_human_respawn_delay_16_human", prefix, 4, out, sizeof(out)) &&
                         std::strcmp(out, "f_btz_human_respawn_delay_4_human") == 0 &&
                         MakeHumansKey("f_btz_human_respawn_delay_16_human", prefix, 1, out, sizeof(out)) &&
                         std::strcmp(out, "f_btz_human_respawn_delay_1_human") == 0;
        const bool p2 = ParseHumansKey("f_btz_zombie_player_damage_scale_2_human", prefix, n) && n == 2;
        const bool none = !ParseHumansKey("f_btz_human_glow_far_dist", prefix, n) && !ParseHumansKey("_human", prefix, n) &&
                          !ParseHumansKey("x5_human", prefix, n) && !ParseHumansKey("f_x_human", prefix, n);
        Check(p16 && p2 && none, L"Z1: _N_human keys recognized and rebuilt for 1 and 4; others ignored");
        auto close = [](float a, float b) { return a > b - 0.005f && a < b + 0.005f; };
        const bool same = close(HuntersScale(0.75f, 0.52f, 4), 0.52f) && close(HuntersScale(0.75f, 0.52f, 2), 0.52f) &&
                          close(HuntersScale(1.0f, 1.0f, 50), 1.0f);
        const bool law = close(HuntersScale(1.0f, 0.5f, 16), 0.25f) && close(HuntersScale(80.0f, 30.0f, 16), 11.25f) &&
                         close(HuntersScale(6.0f, 17.0f, 8), 28.62f);
        const bool safe = close(HuntersScale(0.0f, 0.0f, 16), 0.0f) && close(HuntersScale(0.0f, 2.0f, 16), 2.0f) &&
                          close(HuntersScale(-1.0f, 3.0f, 9), 3.0f);
        Check(same && law && safe, L"Z: HuntersScale (= value at 4 up to 4, power law beyond, 0 or negative -> value at 4)");
    }
    Check(InMod(JumpTarget(g_game + 0xdd7ca0)), L"Z4: GetSpitsRegenTime detoured to the mod");
    Check(InMod(JumpTarget(g_game + 0xe4070)), L"Z6: CalculateHiveClusterRelativeDistance detoured to the mod");
    {
        std::uint8_t* site = g_game + 0x31d0e2;
        std::uint8_t* relay = site + 5 + *reinterpret_cast<std::int32_t*>(site + 1);
        Check(site[0] == 0xE8 && InMod(JumpTarget(relay)), L"B7: RegenerateSpawnPlan, GetAllPlayers redirected to the mod");
    }

    // Vftables: no reference to the originals left, and the known slots point to the mod.
    Check(CountInRdata(g_game + 0x4a62c0) == 0 && InMod(VftableSlot(0x14b5348)) && InMod(VftableSlot(0x14b6220)),
          L"A1: InitializeLobbyInfo replaced in the 2 vftables");
    Check(CountInRdata(g_game + 0x5eb080) == 0 && InMod(VftableSlot(0x151dba8)) && InMod(VftableSlot(0x17a8b90)) &&
              InMod(VftableSlot(0x17abf90)) && InMod(VftableSlot(0x17afd50)),
          L"D1: GetMaxStamina replaced in the 4 vftables (slot 317)");
    Check(CountInRdata(g_game + 0x58b510) == 0, L"C2: HudPlayersList::OnInit replaced");

    // GetPlayerDrawColor trampoline: found by following the jump? No: we look for it by its content
    // (15 original bytes followed by a jump to 0x18055779f) in the executable memory allocated by the mod.
    bool trampolineOk = false, gatherTrampolineOk = false, scriptInitTrampolineOk = false, spitsTrampolineOk = false,
         hiveTrampolineOk = false;
    MEMORY_BASIC_INFORMATION mbi;
    for (std::uint8_t* p = nullptr; VirtualQuery(p, &mbi, sizeof(mbi)); p = static_cast<std::uint8_t*>(mbi.BaseAddress) + mbi.RegionSize) {
        if (mbi.State != MEM_COMMIT || mbi.Type != MEM_PRIVATE || mbi.Protect != PAGE_EXECUTE_READWRITE)
            continue;
        auto* t = static_cast<std::uint8_t*>(mbi.BaseAddress);
        if (std::memcmp(t, drawColorOrig, 15) == 0 && JumpTarget(t + 15) == g_game + 0x557790 + 15)
            trampolineOk = true;
        if (std::memcmp(t, gatherOrig, 15) == 0 && JumpTarget(t + 15) == g_game + 0xaa9f00 + 15)
            gatherTrampolineOk = true;
        if (std::memcmp(t, scriptInitOrig, 14) == 0 && JumpTarget(t + 14) == g_game + 0x1104480 + 14)
            scriptInitTrampolineOk = true;
        if (std::memcmp(t, spitsOrig, 16) == 0 && JumpTarget(t + 16) == g_game + 0xdd7ca0 + 16)
            spitsTrampolineOk = true;
        if (std::memcmp(t, hiveOrig, 18) == 0 && JumpTarget(t + 18) == g_game + 0xe4070 + 18)
            hiveTrampolineOk = true;
    }
    Check(spitsTrampolineOk, L"Z4 : trampoline = 16 octets d'origine + saut vers GetSpitsRegenTime+16");
    Check(hiveTrampolineOk, L"Z6 : trampoline = 18 octets d'origine + saut vers CalculateHiveClusterRelativeDistance+18");
    Check(scriptInitTrampolineOk, L"Z1 : trampoline = 14 octets d'origine + saut vers Initialize+14");
    Check(gatherTrampolineOk, L"F1 : trampoline = 15 octets d'origine + saut vers GatherPlace::Update+15");
    Check(trampolineOk, L"B3 : trampoline = 15 octets d'origine + saut vers GetPlayerDrawColor+15");

    // InitializeLobbyInfo wrapper, with a fake GameDI (gamedll .data singleton @ 0x181c16348).
    using InitFn = void (*)(void* self, LobbyInfo& info, int part);
    auto init = reinterpret_cast<InitFn>(VftableSlot(0x14b5348));
    alignas(16) static std::uint8_t settings[0x200] = {}, gameDI[0x600] = {}, session[0x3000] = {};
    *reinterpret_cast<std::int32_t*>(settings + 0xe0 + 0x10) = 3;          // LAN visibility
    *reinterpret_cast<void**>(gameDI + 0x50) = settings;                  // m_Settings, pas de session
    DWORD old;
    void** singleton = reinterpret_cast<void**>(g_game + 0x1c16348);
    VirtualProtect(singleton, 8, PAGE_READWRITE, &old);
    *singleton = gameDI;
    LobbyInfo info{};
    init(session, info, 2);
    wprintf(L"         LAN game: type %d, public slots %u, private %u, flag %d\n", info.type,
            info.publicSlots, info.privateSlots, info.flags);
    Check(info.type == 2 && info.publicSlots == 7 && info.privateSlots == 0 && (settings[0xc] & 2),
          L"A1: LAN game -> LAN session, 7 public slots");
    LobbyInfo prologue{};
    init(session, prologue, 1);
    Check(prologue.type == 0 && prologue.publicSlots == 1, L"A1: prologue -> solo, 1 slot (unchanged)");
    *singleton = nullptr;

    wprintf(L"%s: %d failure(s)\n", g_failures ? L"RESULT: FAIL" : L"RESULT: PASS", g_failures);
    return g_failures ? 1 : 0;
}
