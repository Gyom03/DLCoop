# The DLCoop launcher

`DLCoop.exe` (sources: `tools/dlcoop/`) is a small WPF window on the .NET Framework 4.7.2, built into Windows 10
(1803+) and 11: nothing to install. It **installs** the mod into the game folder, **writes the settings** and
**launches** the game. The UI is in English.

## The window

| Area | Role |
|---|---|
| **Game location** | found automatically (registry "Steam App 239140", `libraryfolders.vdf`, `appmanifest_239140.acf`), checked on each start; "Change…" to pick `DyingLightGame.exe`. The mod only acts on **1.55**. |
| **Install mod / Uninstall mod** | copies or removes the mod files in the game folder (see below). State: *Mod installed ✓*, *Update available*, *Mod not installed*. |
| **Server IP / Session password** | Internet relay address and session password (the same for everyone). Empty: local network or LAN-style VPN. |
| *Player name* | "Hide my Steam name": the game shows a chosen or random name. |
| *Instance* | main instance (your Steam account) or **additional instance** (2nd, 3rd… window on the same PC). "Several instances on this PC": local addresses on a LAN. |
| *More options* | "Max players when I host" (2 to 32, the Night Hunter on top), "Disable the EOS connection" (recommended), "Close after launch". |

Choices are saved in `%APPDATA%\DLCoop\settings.txt` (key=value). The instance choices return to their defaults on each
start.

## Installing into the game folder

**Install mod** copies three files, sitting next to `DLCoop.exe`, into the game folder:

| File | Role |
|---|---|
| `winmm.dll` | the **loader**: the game loads it itself at startup, and it in turn loads the two DLLs below (see [mod loading](mod-loading.md)) |
| `dlcoop_reseau.dll` | network (`127.0.0.N` addresses, the 1.55 LAN fix), per-instance Steam identity, display name |
| `coop6.dll` | the co-op mod (game limits lifted) |

The displayed state comes from a **byte-by-byte comparison** of these three files with those in the game folder: all
identical → *Mod installed ✓*; `winmm.dll` present but one file differs → *Update available* (Launch or Install
updates); `winmm.dll` missing → *Mod not installed*. The DLLs are built **reproducibly**: a new DLCoop version only
reports an update if the mod actually changed.

If a game is already running, its DLLs are locked: the launcher then keeps the installed files (the new instance loads
the same ones) and the update happens on the next launch, once the games are closed.

**Uninstall mod** removes the three DLLs and `dlmods.ini`. DLML (`dsound.dll`) is never touched.

## Launch

On each **Launch**:

1. install or update if needed;
2. choice of the instance number (0 = main) and, with the relay, a request for an address from the server;
3. settings placed in the game's **environment** (inherited by the launched process):

   | Variable | Read by | Meaning |
   |---|---|---|
   | `DLF_INDEX` | dlcoop_reseau | instance number (0 = main, real Steam account) |
   | `DLF_LOOPBACK`, `DLF_ADRESSE` | dlcoop_reseau | game on `127.0.0.(N+1)` (several instances, relay) |
   | `DLF_NOM`, `DLF_LOG` | dlcoop_reseau | display name, network log (`logs\` next to DLCoop.exe) |
   | `COOP6_Places` | coop6 | lobby slots when hosting = max players + 1 (zombie) |
   | `COOP6_SansEos`, `COOP6_Difficulte`, `COOP6_Couleurs`, `COOP6_Hud`, `COOP6_Diagnostic` | coop6 | mod options |

4. the same settings are **written to `dlmods.ini`** (game folder), for a plain launch: main instance, no relay
   address. The loader applies them only if they are not already in the environment: launched via DLCoop, the
   per-instance values win; launched via **Steam**, the mod still works with these settings;
5. sound repair if needed: `Documents\DyingLight\out\settings\audio.scr` (shared by all instances) may contain
   `API("NoSound")`, written by an instance started with `/nosound` and then re-read by every launch; the launcher
   restores `API("FMOD")` (the engine's value);
6. **normal** launch of `DyingLightGame.exe -nologos` (the launcher writes nothing into the game's memory), then waits
   (up to 10 s) for both DLLs to be loaded in the game.

## Internet relay and additional instances

With a *Server IP*, the launcher becomes this PC's **relay agent** (`Relay.cs`): it connects to the server, gets an
address `0..32` per launched game and, for each remote player, locally holds their `127.0.0.N` address so the game
believes it is on a LAN. It must **stay open** during the game ("Close after launch" only minimizes it; it closes
itself when the last game is closed). Details: [relay server](relay-server.md).

Only one agent per PC (lock `Local\DLCoop_relais`). If you reopen DLCoop during a game (shortcut) to start an
additional instance, the new window **forwards its request to the already-open agent** through a local named pipe
(`LaunchPipe.cs`); that agent launches the game and tracks it.

## Command line

```
DLCoop.exe /launch [/instance N] [/multi] [/name Name] [/game path\DyingLightGame.exe]
           [/internet | /lan] [/server address] [/password X] [/maxplayers N] [/count N]
```

No window, using the saved settings for the rest. `/count N`: N games 10 s apart (the later ones as additional
instances). Exit code 0: game(s) launched with the mod loaded. With the relay, the process stays alive as long as a
launched game runs.

## Building the package

`tools\dlcoop\build.ps1 [-Relais <ip>]` builds the three DLLs and the launcher and produces `out\dlcoop\paquet\` +
`DLCoop_<version>.zip` (`-Relais`: address pre-filled in `server.txt` for a group; the password is never put in the
package). See [building](building.md).
