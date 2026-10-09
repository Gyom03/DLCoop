# coop6 — co-op for 6 human players (and more) in Dying Light 1.55

Up to 32 players + 1 zombie (`Places` 3..33). Validated: 12 humans in the same LAN game (host + 11 instances across two
PCs bridged by `tools/fantome/pont.exe`); 3 players over the Internet relay with the DLCoop 1.3.x launcher. Overview:
`docs/`.

## Usage
```powershell
.\mods\coop6\build.ps1                 # -> out\mods\coop6\coop6.dll (+ coop6.ini)
.\tests\mod_install\run_coop6.ps1      # offline test: "RÉSULTAT : CONFORME"
```
With the DLCoop launcher this is automatic. For the raw tools, in `out\tools\fantome\fantome.ini`:
`Mods=..\..\mods\coop6\coop6.dll`. Each instance launched by `fantome.exe` then loads the DLL before the game's entry
point. **Every instance must have the mod** (host and guests: the patch is the same everywhere, regardless of who
hosts).

Log: `coop6_instance_N.log` next to the DLL (appended on each launch, crash reports included).
Settings: `coop6.ini` — `Places` 3..33 (slots when hosting, zombie player included; default 7 = 6 humans; DLCoop:
"Max players when I host" + 1), `Difficulte`, `Couleurs`, `Hud`, `Diagnostic` (log of the lobby state and searches),
`SansEos` (default 1), `ForcerLan` (default 1).

## What the DLL does
Everything is installed in `DllMain`, after checking the bytes of 1.55 at each spot (otherwise nothing is touched).

| Tag | Where (gamedll 1.55, unless noted) | Change |
|---|---|---|
| A1 | `SessionCooperative::InitializeLobbyInfo` 0x1804a62c0 (2 vftables) | wrapper: 5 public slots → `Places` |
| A2 | `SessionDI::Create` 0x1804b307a (`ValidateLobbyInfo` inlined) | cap `mov edx,5` → `Places` (1 byte) |
| A3 | `Sessions::StatusDL::CheckCanConnect` 0x180f31a90 | a human with no team slot (4 slots full) allowed instead of error 0x2e |
| A3 | `SessionDI::Lobby_UpdateZombieCanJoin` 0x1804c9b30 and `SessionCooperativeDI::Lobby_UpdateZombieCanJoin` 0x1804a8ec0 | detours, rewritten: game advertised "human can join" while < `Places−1` humans |
| A3 | `SessionCooperativeDI::IsJoinable` 0x1804a9449 | `humans < 4` → `< Places−1` (1 byte) |
| A4 | `SessionCooperativeDI::IsJoinable` 0x1804a9363 | `joined players + 1 < 5` → `< Places` (1 byte) |
| A5 | `SGameScannerResult`: constructor 0x180a1a397, `ExtractResults` 0x180a1a7e2 | max players of a found game 5 → 33 (always the maximum, whatever `Places`) (client side: "No matches" in quick match from 5 players) |
| B2 | `LogicalLevel::UpdatePlayerIndexList` 0x18048ce10 | detour, rewritten without the stack arrays of 5 (overflow at the 9th player) |
| B3 | `HUDDIManager::GetPlayerDrawColor(LogicalPlayer*)` 0x180557790 | detour + trampoline: players with no index (6th to 32nd): 27 colors added |
| B5 | `AIManager::LimitAtackingWalkers` 0x1801b9460 (call to `GameDI::GetAllPlayers` 0x1801b94a1) | player list capped at 5 (stack arrays of 5: heap corruption at the 6th player) |
| B6 | `AISpawnSystem::Manager::CalculateSpawnPlaceRatings` 0x180318a10 (call 0x180318a42) | player list capped at 5 (`SpawnPlaceRating`: 3 arrays of 5 floats; overwrites from the 6th, host crash at the 11th) |
| B7 | `AISpawnSystem::Manager::RegenerateSpawnPlan` 0x18031d050 (call `GameDI::GetAllPlayers` 0x18031d0e2) + B6 | place notes and per-player plan on the same **rolling window of 5 players** (one per regeneration, ~0.5 s): otherwise the rank ≥ 5 of players 6+ was read past the arrays of 5 in `SpawnPlaceRating` (zombies placed from bad data) |
| C2 | `HudPlayersList::OnInit` 0x18058b510 (vftable) | copies of the `G_Player3` block added to the teammate list (without touching `Data0.pak`); NaN pivots left by `IUIElement::Clone` fixed (otherwise an empty health bar) |
| F1 | `GatherPlace::Update` 0x180aa9f00 (detour + trampoline) | mission gathering: ≥ 70% of players in the zone for 5 s (fixed values) → the local player left outside is teleported to the gather point (`PlayerDI::SetPosAndRot` 0x180bad0b0) |
| D1 | `HumanAI::GetMaxHealthCoopMul` 0x1805eb2a0, `GetMeleeDamageCoopMul` 0x1805eb330 (detours), `GetMaxStamina` 0x1805eb080 (4 vftables) | difficulty extended to 5-6 players (otherwise it drops back to solo values) |
| E1 | engine: `CEosAuthenticator::Login` 0x1806a28e0 | `ret`: no EOS connection (useless on LAN; froze instances via the steam_api64 callback registry) |
| E2 | `LobbySettings::LoadOnlyGameVisibility` 0x1804e4200 and `LoadSettings` 0x1804e4250 (vftable wrappers) | `m_GameVisibility` (`LobbySettings+0x10`) forced to 3 (**LAN**) after loading → the menu shows "LAN" by default at launch, whatever was last saved. Not locked (the menu writes the field through another path). Setting `ForcerLan` (default 1) |
| G1 | `AISpawnBoxCompound::GetNoInitiallySpawned` 0x1801cce30 (Ghidra `vfunc_151`), byte 0x1801ccede | placed zombie groups, zombies at the start: "4 players" case for 4 and more (`jne` → `jl`); before: solo value at 5+ |
| G2 | `AISpawnBoxCompound::CalcExpectedNumber` 0x1801ccf40 (`vfunc_115`), byte 0x1801ccfee | same, maximum total (covers `AIRandomSpawnBoxCompound`, which fills the same fields) |
| G3 | `Quests::FillAreaPhase::GetReqCountToPass` 0x180e51730, byte 0x180e517d1 | required count of "fill an area" steps |
| G4 | `Quests::WaitGuardPhase::OnEnterChangeWorld` 0x180ea7060, byte 0x180ea71ea | timer of "wait / hold" steps |
| G5 | `PlayerDI::GetRepairPartsAmountMul` 0x180bcf8b0, byte 0x180bcf921 | loot repair parts: ×0.65 (4 players) instead of ×1 at 5+ |
| Z1 | engine: `ITools::GetVarFloat` (slot 0x390), replaced by `CEngineScriptManager::Initialize` 0x181104480 (detour + 14-byte trampoline) | Be the Zombie settings `<name>_<N>_human` (otherwise the variable is missing → 0: UV lamp, damage, respawns, thrown items): N ≥ 5 → `HuntersScale(v1, v4, N)` (`BtzScale.h`) |
| Z2 | `PlayerDI::GetMaxHealth` 0x180ba7170 (Ghidra `Monkey::vfunc_41`), byte 0x180ba71db | zombie player health: `cmove` → `cmovge` |
| Z3 | zombie player regeneration (inlined in 0x180ce0ce0), byte 0x180ce11f2 | same |
| Z4 | `ZombieAbilitiesController::GetSpitsRegenTime` 0x180dd7ca0 (detour + 16-byte trampoline; call 0x180dd7d8c redirected) | spit recharge (otherwise 0): values computed at 1 and 4 hunters (forced count), then `HuntersScale` |
| Z5 | `BtzDamageCoopSwitch` 0x180a61c30, byte 0x180a61e7e | items also enabled beyond 4 hunters |
| Z6 | `ActionPlayerZombieMission::CalculateHiveClusterRelativeDistance` 0x1800e4070 (detour + 18-byte trampoline), 18 bytes at 0x1800e40f6 | hunter index capped at 3 (otherwise -1.0); the table's "4 hunters" entry replaced during the call by `HuntersScale(table[0], table[3], N)` |

Be the Zombie beyond 4 hunters: `HuntersScale(v1, v4, N) = v4 · (N/4)^b`, `b = ln(v4/v1)/ln 4` (game values at 1 and 4
hunters; constant if equal; value 4 if one is not > 0). E.g. at 16: damage to the zombie ×0.25, spits 11 s, hunter
respawn 48 s, zombie respawn 8 s.

Diagnostics (`Diagnostic=1`): refusal reasons and bits published by the host, the `GameScanner::ValidateResult` verdict
on the client. A crash report is always active: code, address, registers, stack (vectored exception handler, written
without allocation).

Limits kept as-is (by choice): 5 replicated player indices (`LogicalLevelReplData`) and 4 human team slots
(`StatusDL`) — replicated fields registered in the RTTI during gamedll's static initialization, before any injected
DLL. Players beyond those have neither an index nor a team slot; the mod lets them in and gives them a color.

## To do
- 16 humans: memory (~3.5 GB per instance; the second PC's page file needs enlarging for 8 instances).
- B5 / B6: full rewrites for N players (today the 6th and beyond do not count toward the attacker limit or the spawn
  place ratings).
- Players menu (`MenuPlayersOverlay`: 5 buttons `B_Player0..4`).
- "No index" color wrongly given while a player is connecting.
- Verify in game: HUD with 5-6 teammates (position of the copies), difficulty.
- Verify in game G1-G5 (placed zombie groups at 5+ players); Z1-Z6 once a Be the Zombie run is tried.
- Balancing step 2 (curves beyond 4): to be decided.
