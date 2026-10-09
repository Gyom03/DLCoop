# Ghost players: several Dying Light instances in LAN co-op on a single PC

Goal: test co-op (and the "6 players" mod) **without gathering other players**: run several instances of the game on
the same PC, each with its own identity and its own network address, and have them join in LAN.

Validated: 5 human players on a single PC with the `mods/coop6` mod (4 without the mod) — host + 3 ghosts, same quest,
each instance sees the other 3 in its player list.

This document is for reproducing, maintaining and continuing the work. For players, the DLCoop launcher
(`tools/dlcoop`) does all of this without scripts: see `docs/`.

---

## 1. Quick start

### Requirements
- Dying Light **1.55** (Steam), installed in `C:\Program Files (x86)\Steam\steamapps\common\Dying Light` (configurable
  in `fantome.ini`). **The install is never modified.**
- MSVC 14.16 (Build Tools 2022, toolset v141), like the rest of the project.
- **Steam open and in offline mode** (Steam menu → "Go offline") *before* launching. If Steam is closed, the exe's
  SteamStub protection runs `steam.exe steam://run/239140//` and exits: Steam then relaunches the game **without** our
  DLL (and maybe online).
- Memory: ~3.3 GB of committed memory per instance at the menu (see §6). Close memory-heavy programs; ideally reboot the
  PC before a 4-instance session.

### Build
```powershell
.\tools\fantome\build.ps1            # -> out\tools\fantome\: fantome.exe, fantome.dll, fantome.ini, mosaique.ps1
```
The build fails (LNK1104) if a game instance is running: it locks `fantome.dll`.

### Launch
Simplest: `.\lancer.ps1 -Fantomes 5 -Ecart 10 -SansEspace` (host then ghosts 1..N every 10 s, then a tile layout;
without `-SansEspace`, Space is sent into each window 20 s after it opens, via `espace.ps1`). By hand, from
`out\tools\fantome\`, one at a time:
```powershell
.\fantome.exe hote     # instance 0: the host (real Steam identity, sound on) -> 127.0.0.1
.\fantome.exe          # ghost 1 (first free number)                          -> 127.0.0.2
.\fantome.exe          # ghost 2                                               -> 127.0.0.3
.\fantome.exe          # ghost 3                                               -> 127.0.0.4
```
The game is always launched with `-nologos` (skips the intro videos); ghosts also get `/nosound -texStreamMemMB 512`.

Then arrange the windows: `.\mosaique.ps1` (2 × 2 for 4 instances, `-Colonnes 3` for 5-6). The windows are arranged
**by instance number** (0 = host top-left; pid read from `journaux\instance_N.log`) and their title becomes
"Dying Light - instance 0 (host)" / "Dying Light - ghost N". See §5.6: the game resists resizing, so set a suitable
resolution in its video options (windowed).

Two PCs (beyond ~9 instances per PC): on each PC, `pont.exe` holds the `Local\DLFantome_slot_N` locks of the other PC's
instances (so fantome.dll sends its announcements to them), holds their addresses 127.0.0.(N+1):20101/20111 and relays
the UDP packets to the bridge on the other PC, which re-emits them from the sender's address: the game sees what it
would see on a single PC. Keep-alive every 2 s on the same port on both sides: no firewall rule to create (network on
the Private profile). E.g. PC 1: `pont.exe 192.168.1.20 7-15`; PC 2: `pont.exe 192.168.1.10 0-6`, then `fantome.exe 7`,
`8`… (33 instances max, numbers 0..32: 32 players + 1 zombie).

Mods loaded at startup: the `Mods=` key of `fantome.ini` (DLLs separated by ";", paths relative to `fantome.exe`), one
`LoadLibraryW` APC each after `fantome.dll`. E.g. 6-player co-op: `Mods=..\..\mods\coop6\coop6.dll`
(`mods/coop6/README.md`). Freezes and crashes at startup with 5+ instances ("only a reboot fixes it"): the game's EOS
connection, which requests a Steam ticket; two threads register / unregister at the same time in the steam_api64
callback registry, which is unprotected → infinite loop. The coop6 mod disables EOS (`SansEos=1`, E1); since then, 10 s
apart is enough without a reboot.

### Play
1. In the host window: start/load a co-op game (Steam offline → LAN game, §4.2).
2. In each ghost window: game list → join the host's game.
3. If there is a problem: read `out\tools\fantome\journaux\instance_N.log` (one line per new network event; see §7).

### Test without the game
```powershell
.\tools\fantome\tests\run.ps1        # no game instance open (the slot locks would be taken)
```
Compiles a fake game (`tests/faux_jeu.cpp`), launches a "host" and a "ghost" via `fantome.exe`, and checks: DLL loaded
before `main`, the game lock renamed, binds 127.0.0.1:20101 and 127.0.0.2:20101 simultaneously, port 0 corrected,
broadcast received, reply seen as `127.0.0.1:0` then, after a send to the real port, as `127.0.0.1:20101`. Expected
output in `out\tests\fantome\fantome1.txt`:
```
reponse "reponse" de 127.0.0.1:0
reponse "reponse" de 127.0.0.1:20101
```

---

## 2. Files

| File | Role |
|---|---|
| `src/fantome.cpp` | The `fantome.exe` launcher: reads `fantome.ini`, picks the number, prepares the environment, creates the game suspended, injects the DLL (APC), resumes. |
| `src/fantome_dll.cpp` | `fantome.dll`: all the hooks (lock, Steam, network). Through the import tables (IAT) only. |
| `build.ps1` | MSVC 14.16 build (`/MT`, static CRT) → `out\tools\fantome\` (`-Out` parameter). |
| `fantome.ini.exemple` | Default settings (copied to `fantome.ini` on the first build). |
| `mosaique.ps1` | Arranges the game windows in a grid (launch order; `-Colonnes`, `-SansBordure`). |
| `lancer.ps1` | Launches host + N ghosts (`-Fantomes`, `-Ecart` s, `-SansHote`, `-SansEspace`), then the tiling. |
| `espace.ps1` | Sends Space into the game windows (`-Processus pid,…`): bring to front then `SendInput`. |
| `src/pont.cpp` → `pont.exe` | Links the instances of two PCs: `pont.exe <other_pc_ip> <remote_instances> [port 20200]` (see below). |
| `tests/faux_jeu.cpp`, `tests/run.ps1` | Offline test of the whole chain. |

Outputs (not versioned): `out\tools\fantome\` (binaries, `fantome.ini`, `journaux\instance_N.log`).

### fantome.ini
| Key | Default | Effect |
|---|---|---|
| `Jeu` | `C:\Program Files (x86)\Steam\…\DyingLightGame.exe` | The launched exe (never modified). |
| `Bouclage` | `1` | One 127.0.0.(N+1) address per instance on the game ports. Launch the host with `fantome.exe hote`. |
| `SonFantomes` | `0` | `0` = `/nosound` for the ghosts. |
| `Arguments` | empty | Added to all instances (`-nologos` is always added). |
| `ArgumentsFantomes` | `-texStreamMemMB 512` | Added to the ghosts only. |
| `Mods` | empty | DLLs loaded at startup after fantome.dll (";", relative to fantome.exe). |

### Environment passed to the game (by fantome.exe)
`SteamAppId=239140`, `SteamGameId=239140` (launch outside the Steam client), `DLF_INDEX` (0 = host, 1..7),
`DLF_LOOPBACK` (0/1), `DLF_LOG` (log path).

---

## 3. How it works

### 3.1 Injection before the entry point ("early bird")
`fantome.exe`: `CreateProcessW(…, CREATE_SUSPENDED)` → `VirtualAllocEx` + `WriteProcessMemory` (the DLL path) →
`QueueUserAPC(LoadLibraryW, main thread)` → `ResumeThread`. The APC runs at the end of the Windows loader's
initialization: **all statically imported DLLs are loaded and their imports resolved** (the exe imports `gamedll`,
`engine`, `filesystem`; `engine` imports `steam_api64` and `WS2_32`), but the exe's entry point (the SteamStub stub)
has not run yet and `SteamAPI_Init` has not been called yet. So everything can be done by simply replacing IAT entries,
without rewriting code. The launcher waits up to 10 s for `fantome.dll` to appear among the process's modules.

### 3.2 fantome.dll hooks
All placed by replacing, in the IATs of the exe, `engine_x64_rwdi.dll`, `gamedll_x64_rwdi.dll` and
`filesystem_x64_rwdi.dll`, **any entry equal to the real address** of the function (not by name): this covers imports
by ordinal (engine imports WS2_32 almost entirely by ordinal: `recvfrom`=17, `sendto`=20, `bind`=2…).

| Hook | Instances | Effect |
|---|---|---|
| `kernel32!CreateMutexA` (2 entries) | ghosts | `Global\ChromeEngine4DIMutex` → `…_fantomeN`: no more "Dying Light is already running". |
| `steam_api64!SteamInternal_FindOrCreateUserInterface` (engine) | ghosts | Returns a **proxy** for `SteamUser0xx`, `SteamFriends0xx`, `STEAMREMOTESTORAGE_INTERFACE_VERSION0xx`. |
| `ws2_32!bind` | all | Ports 20101…20120 and 20111…20130 bound to 127.0.0.(N+1) (if `Bouclage=1`); `SIO_UDP_CONNRESET` disabled. |
| `ws2_32!sendto` / `WSASendTo` | all | Destination port 0 → 20101; broadcast to the game ports → direct send to each live instance; per-peer address-form tracking. |
| `ws2_32!recvfrom` | all | A received packet (r > 0) from a peer the game addresses on port 0 → source presented with port 0. |

At the end of install, the DLL creates `Local\DLFantome_slot_N` (held for the process's lifetime): the launcher uses it
to pick the free number, the DLL to know which instances are alive.

### 3.3 Steam proxies (no writing into Steam's memory)
A proxy is a small `{vftable, real object}` object of ours. Its vftable (256 slots) points to generated **thunks**
(16 bytes each: `mov rcx,[rcx+8] ; mov rax,[rcx] ; jmp [rax+i*8]`) that forward the call to the real Steam object,
except for a few redirected slots:

| Interface | Slot | Method | Replacement |
|---|---|---|---|
| `SteamUser023` | 2 | `GetSteamID` | Account number (low 32 bits) `^ (0x5A000000 \| N)`; universe/type/instance unchanged. |
| `SteamFriends017` | 0 | `GetPersonaName` | `"Fantome N"`. |
| `STEAMREMOTESTORAGE_INTERFACE_VERSION016` | 0 | `FileWrite` | Ignored (returns true, logged). |
| same | 5 | `FileForget` | Ignored. |
| same | 6 | `FileDelete` | Ignored. |

Slots and calling convention verified in the "flat" functions of `steam_api64.dll` (the game folder):
`SteamAPI_ISteamUser_GetSteamID` = `lea rdx,[rsp+30h] ; call [rax+10h] ; mov rax,[rax]` → slot 2, **CSteamID returned
through a hidden pointer** (rdx); `SteamAPI_ISteamFriends_GetPersonaName` = `jmp [rax]`; `…RemoteStorage_FileWrite` =
`jmp [rax]`, `FileWriteAsync` = `[rax+10h]`, `FileForget` = `[rax+28h]`, `FileDelete` = `[rax+30h]`. RemoteStorage slot
0 confirmed on the engine side by `CLobbyStorageSteam::CloseFile` (Linux 0xd0ae48: `call *(%rax)`).

Measured result: `SteamID présenté au jeu : <derived SteamID> (vrai : <real SteamID>)` (ghost 1; then the next ones for
2 and 3); "Fantome N" names shown in the other players' HUDs.

---

## 4. Findings (machine code)

Addresses: `gamedll`/`engine` = Windows 1.55 DLLs; "Linux" = `libgamelib.so` / `libengine.so` (the native Linux build,
full C++ names, very close to 1.55).

### 4.1 Steam and EOS
- `engine` imports `steam_api64.dll` but only uses `SteamUser023`, `SteamFriends017`, `SteamUtils010`,
  `STEAMAPPS…008`, `STEAMUSERSTATS…012`, `STEAMREMOTESTORAGE…016`, `SteamController008`: account, friends,
  presence/invites (`GameRichPresenceJoinRequested`), achievements, stats, cloud. **Neither SteamMatchmaking nor
  SteamNetworking.**
- Online lobby = **EOS** (`EOS_Lobby_*`), online transport = `EOS_P2P_*` (`gamedll` imports
  `EOSSDK-Win64-Shipping.dll`). Factory: `CEosFactory<CLobbyFactorySteam>`.
- Player identity (`CLobbyProfileSteam::GetPlayerId`, Linux 0xcfaee0) → `SPlayerIdEos`: EOS PUID (0x21 bytes, **empty if
  there is no EOS connection**), 64-bit SteamID at +0x21 (copied from profile+0x98), account type 1 (Steam) at +0x44.
  Steam offline → no EOS → only the SteamID tells the instances apart.
- Saves: Steam cloud, `Steam\userdata\<account>\239140\remote\out\save\save_coop_N.sav` and
  `remote\out\profiles\default.pro`, written by `ISteamRemoteStorage::FileWrite`.

### 4.2 LAN mode
- `CLobby::CreateSessionObject` (Linux 0xcb4b30): mode 0 = solo, 1 = online (EOS), 2 = **LAN**
  (`CLobbySessionLan`); `CLobby::IsModeAvailable` always returns true. LAN classes present in the Windows engine
  (`CLobbySessionLan`, `CLobbyScannerLan`).
- gamedll `0x1804b3042` (in `SessionDI`, `ValidateLobbyInfo` inlined): mode 1 and the user **not connected** → mode 2.
  This is why "Steam offline" yields LAN games.
- `CInetAddrMultiPlatform::IsLANAddress` (Linux 0xd764f0): 10.x, **127.x**, 172.16–31.x, 192.168.x accepted → the
  127.0.0.N loopback addresses work **without any system configuration**.

### 4.3 Ports
| Role | Port | If taken |
|---|---|---|
| Game on LAN | UDP **20101** | +1 and retry, up to 100 times (`Net::CLocalClient::InitializeSocket`, Linux 0xd35490) |
| LAN discovery | UDP **20111** | +1, **4 tries** (`CLobbyScannerLan::InitializeSocket`, Linux 0xd076e0); announcements broadcast to 20111 |
| Online | 0 (chosen by Windows) | — |

Port table: `CLobbyConnectionsEos::GetLocalPort` (Linux table 0xf3e8d0 = `{0, 20101, 20111, -1, 2}`; Windows:
`mov eax,20101/20111 ; ret` at `engine 0x1806c8daf`). **The game port is never exchanged**: the game assumes every peer
is on 20101. Hence the choice "one IP per instance, same port" rather than "one port per instance".

### 4.4 The 1.55 "port 0" bug (see the `Gyom03/DL-LAN-join-fix` repo)
- The discovery reply does not contain the game port; the client sends its connection request to **IP:0** → ICMP "port
  unreachable". The earlier Frida fix rewrote the port on send.
- Cause in the code: `CInetAddr::CInetAddr(SGameAddressEos const&)` (Linux 0xd727c0) **always** builds a
  `CInetAddrEos`. `SGameAddressEos`: IPv4 at +0x48, port at +0x4c, PUID text at +0x90, flag at +0x44. The exact spot
  where the port becomes 0 is **not yet established** (lead: `CLobbyScannerLan::ProcessGamesScanResponse`, Linux
  0xd062e0, address built from the sender via a virtual `CInetAddr` +0x80).
- A second effect found here: fixing the send is not enough. The engine **remembers** the peer under `IP:0` and does not
  recognize its replies coming from `IP:20101` (`Net::CLocalClient::FindSender`) → it gives up after ~17 s ("connection
  failed"). Hence the `recvfrom` hook that puts port 0 back into the source.
- The same peer can be known under `IP:0` in one session and `IP:20101` in the next (address passed by another player):
  the DLL tracks the **last form used on send**, per peer (`NotePeerForm`), never a final decision.
- Engine reception: `recvfrom` + `select` only (no IOCP); the hook only touches calls that actually received a packet
  (the engine polls the socket in a loop).

### 4.5 Launch, locks, options
- `DyingLightGame.exe` is protected by **SteamStub** (`.bind` section, `.text` encrypted on disk, entry point in
  `.bind`): no on-disk patch possible; everything is done at runtime.
- Single-instance lock: the exe calls `CreateMutexA("Global\ChromeEngine4DIMutex")` (the "Dying Light is already
  running" message). The engine's `OpenMutexA` concerns `ce6-baker-mutex` (a tool).
- Options read by `CGame::Initialize` (engine): `/norender` (`StrStrIA` on the command line, Linux 0x81e20c, passed to
  `r_Initialize`), `/nosound` (verified: `API("NoSound")` in the game's log), `-texStreamMemMB <MB>`
  (`strncmp(arg,"-texStreamMemMB",15)`, value = the next argument, Linux 0x81ea02), `-nologos`,
  `-video_mode_performance/balanced/quality`, `-port`, `-multimon`. The "16 players" in
  `Data0/data/defaultserversettings.scr` (`MaxPlayers(16)`, "CoJ server") is a Call of Juarez leftover, not read by
  co-op.

---

## 5. Problems met and solutions (in order)

1. **Steam closed** → SteamStub launches Steam and exits; Steam relaunches the game without the DLL. → Steam open,
   offline.
2. **The ghost finds the game but does not join it** (packets both ways, gives up 17 s later) → §4.4, the `recvfrom`
   hook.
3. **Broadcast replaced by sends to 127.0.0.N with no instance** → "port unreachable" → `WSAECONNRESET` on the next
   `recvfrom` (a real broadcast never does that; found by the offline test). → sends limited to live instances +
   `SIO_UDP_CONNRESET` disabled.
4. **After a 2nd session, the host can no longer join** (it applied port 0 to a peer now known under its real port) →
   address form tracked per peer.
5. **The 4th instance crashes at startup** (twice, `VCRUNTIME140.dll+0x1c177`, c0000005, ~10 s after launch) → the
   system's committed memory nearly exhausted (§6) → `-texStreamMemMB 512` for the ghosts, spaced-out launches, PC
   rebooted: 4 instances stable afterward. Cause **not proven** (once with 10 GB still free): watch it.
6. **Windows that can't be resized from outside**: style `0x14C80000` (title bar, no thick border), the client forced to
   the game's resolution (2560 × 1415 despite `SetWindowPos`). `mosaique.ps1` only **places** the windows; set the
   resolution in the video options (a file shared by all instances:
   `Documents\DyingLight\out\settings\video.scr`, `Resolution(…)`; `WindowOffset(i,i)` also exists).

---

## 6. Measurements

| Measurement | Value |
|---|---|
| Committed memory per instance (at the menu) | 3.2 – 3.8 GB (`PrivateMemorySize64`); game loaded ~3.0 – 3.5 GB of RAM |
| The PC's committed-memory limit | 39.9 GB (32 GB RAM + 2 × 4 GB page file) |
| Already committed, games closed | 18 GB before reboot; 10.8 GB after (29.1 GB free) |
| After 4 instances (after a reboot) | 12.8 GB still free |
| Page files during the crashes | peaks 2.2 + 2.6 GB |
| DLL loading | before the entry point (fake game: loaded before `main`); in the game, lock renamed ~0.3 – 0.7 s after the DLL loads, SteamID requested ~1 s later |
| Delay between launches | 25 – 30 s (reliable); 15 s → 4th-instance crashes |
| Time before a failed connection is abandoned | ~17 s |

---

## 7. Reading the logs
`out\tools\fantome\journaux\instance_N.log` (N = 0 host, 1..7 ghosts):
- `IAT … : n entrée(s) remplacée(s)` — expected: CreateMutexA 2 (ghosts), FindOrCreateUserInterface 1, bind 2, sendto 2,
  WSASendTo 0, recvfrom 2.
- `verrou du jeu renommé`, `SteamID présenté au jeu`, `intermédiaire Steam créé pour …`.
- `bind 127.0.0.N:20111 → ok` (menu), `bind 127.0.0.N:20101 → ok` (session create/join; repeats on every session
  change).
- `sendto → IP:port (note)`: **one line per new destination only** (do not conclude there is no traffic to an
  already-seen destination).
- `pair X adressé avec le port 0 / avec son vrai port 20101`: each form change; `recvfrom ← X:20101 présenté comme port
  0`.
- `cloud : écriture ignorée …` (ghosts).
The game's own log: `Documents\DyingLight\out\logs\crash_<date>.log` (created on every launch, misleading name). Windows
crashes: the Application log, events 1000/1001.

---

## 8. Account and game safety
- Dying Light has **VAC enabled** (Steam store API, category 8). No EasyAntiCheat/BattlEye.
- Precautions taken: **Steam offline** during tests; **no writing into Steam's memory** (proxies instead of modifying
  the `steamclient64` vftables); no change to the Steam install or the exe; **LAN only**, never public matchmaking; the
  ghosts do not write to the cloud.
- If **DLML** is active in the game folder (`dsound.dll`, installed outside this project) with a plugin that changes the
  lobby (game log: `ReadLobbySettings public_slots=5 -> forcing 9`, `GetGameProperty …`), it also loads in the ghosts.
  It can skew player-count tests and add instability: disable it for measurements (this project does not touch the
  install).

---

## 9. Background: moving to 6 players (analysis done)
Limits found (all at **5 slots = 4 humans + 1 "Be the Zombie" zombie player**):
| Where | Limit | Planned fix |
|---|---|---|
| `SessionDI::ValidateLobbyInfo` inlined, gamedll `0x1804b307a` (`mov edx,5 ; mov ecx,0Ch ; cmovne`) | slots = `IsCooperative() ? 5 : 12` | 5 → 7 |
| `LogicalLevelReplData` (replicated object, 0x2a8 bytes): `m_ReplPlayerIndex[5]` at +0x290 (last field); `RegisterRTTI` gamedll `0x18048d280`; `GetPlayerIndex`, `UpdatePlayerIndexList` | array and loops of 5; **5-byte stack arrays** in `UpdatePlayerIndexList` (overflow beyond 5, game compiled `/GS-`) | grow the object (0x2b0), rewrite the 3 functions |
| `HUDDIManager::COLORS[5]` (`DL_Player0..4`, `menu_project.xml`); `GetPlayerDrawColor(LogicalPlayer*)` gamedll `0x180557790` with no bound | out-of-bounds read at index ≥ 5 | grow the array + colors |
| `HumanAI::GetMaxHealthCoopMul` | 2/3/4 players only, otherwise 1.0 | handle 5-6 |
| HUD `hud.xui` / `hud_btz.xui`: `G_Player0..3` (`HudPlayerListElement`) | 4 blocks | data |
| `ActionPlayerDeadBodies::GetMaxNetIDOccupiedByActionPlayer` | `min(2n, 10)` with a threshold of 5 | to study |
The network engine (`Net::CLocalClient`, `Repl::CReplicator`, `CObject`): dynamic lists, 16-bit ids, no limit found. A
full survey is still to do (~640 comparisons to 3/4/5 in "player/session" functions on the Linux side, mostly false
positives).

Order followed: (1) rewrite the multiplayer functions **byte-identically** in `src/`, (2) modified versions for 6
players in `mods/coop6/`, (3) test with host + 5 ghosts. For 6 instances, provide ~20 GB of free committed memory
(enlarge the page file: a system setting) or lighten the ghosts (minimum graphics; `/norender` crashes, see §10). Page
file: set the initial size equal to the maximum, otherwise the limit stays at the initial size until it grows.

## 10. Open points
- The exact origin of port 0 (fixing it at the source would be cleaner than rewriting the packets).
- A sure cause for the 4th-instance crashes (add a stack dump on crash to the DLL if it comes back).
- `/norender` **unusable** (tried 2026-10-08): crash ~1 s after launch, engine 0x18022cf90, the window procedure calls
  the global render object (`[0x180a40218]`) on WM_SIZE, null in this mode.
- Menu memory measured 2026-10-08 (lone ghost, 2048×1152 window): 4.4 → 4.7 GB committed, 3.3 → 3.7 GB of RAM.
- Window size: resolution shared by all instances (shared `video.scr` file); a per-instance redirect would be possible
  in the DLL.
- Reference test of a 5th instance (refusal expected with the original game): to do **without** DLML.
