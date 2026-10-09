# DLCoop — the co-op mod launcher (distributable package)

> Historical version (branch `dll-injection`). On `main`, the launcher installs a `winmm.dll` loader and the game loads
> the mod itself, with no injection.

A WPF window (`DLCoop.exe`, ~30 KB), **entirely in English**, that launches Dying Light 1.55 with the two mod DLLs
loaded before the game's entry point (same method as `tools/fantome`: the game is created suspended, one `LoadLibraryW`
APC per DLL). **No dependency**: .NET Framework 4.7.2, a component of Windows 10 (1803+) and 11; DLLs statically link the
CRT (`/MT`). The game folder is never modified.

## Build the package
```powershell
.\tools\dlcoop\build.ps1     # -> out\dlcoop\paquet\ and out\dlcoop\DLCoop_<version>.zip
```
The DLLs are built in `out\dlcoop\obj` (no clash with running instances that lock `out\mods\coop6` and
`out\tools\fantome`).

| File | Source |
|---|---|
| `DLCoop.exe` | `tools/dlcoop/*.cs`, `*.xaml` (`DLCoop.csproj`, x64 required: the `LoadLibraryW` address is read in this process) |
| `dlcoop_reseau.dll` | `tools/fantome/src/fantome_dll.cpp` (1.55 "port 0" fix, display name, additional instances, `127.0.0.N` addresses) |
| `coop6.dll` | `mods/coop6/Coop6Mod.cpp` |
| `README.txt` | the player-facing notice (in English) |

## How it works
- **Game**: checked on each start (`Launcher.CheckInstallation`): the saved path is kept if it exists and is 1.55;
  otherwise a search ("Steam App 239140" uninstall key, `libraryfolders.vdf`, `appmanifest_239140.acf`), preferring a
  1.55 install, with the new path saved and flagged if it moved. "Change…" saves the chosen path immediately.
- **Simplified window**: only the game location, the server address and the session password are visible, plus
  "Launch"; the rest is in three collapsed expanders (dark `Expander` style in `App.xaml`): "Player name" (hidden
  display name, `DLF_NOM`), "Instance" (main / additional `DLF_INDEX`, several instances on this PC `DLF_LOOPBACK`) and
  "More options" ("Max players when I host" 2..32, saved: `COOP6_Places` = value + 1 for the zombie; "Disable the EOS
  connection" `COOP6_SansEos`; "Close after launch"). Instance choices return to their defaults on each start. Co-op up
  to 32 players (`Launcher.MaxInstances` = 33 slots); difficulty, colors, HUD and diagnostics always on.
- **Network**: no explicit choice — a filled-in server = Internet relay (`Relay.cs`, server `tools/relais/`); an empty
  field = local network or VPN (original behavior). With the relay, the launcher becomes the relay agent (it connects,
  requests an address 0..32 for each launched game and passes it via `DLF_ADRESSE`, and holds the remote players'
  addresses on `127.0.0.N`). It must stay open during the game: "Close after launch" minimizes it, and it closes itself
  when the last game closes. The relay address is pre-filled from `server.txt` (`build.ps1 -Relais <ip>`); the session
  password is never in the package. Protocol v2 (33 addresses, 64-bit masks), automatic fallback to v1 (16) against an
  older relay. Only one DLCoop window connected to a relay per PC (`Local\DLCoop_relais`).
- Choices saved in `%APPDATA%\DLCoop\settings.txt`; logs in `logs\` next to the exe.
- Command line: `DLCoop.exe /launch [/instance N] [/multi] [/name Name] [/game path] [/internet | /lan]
  [/server address] [/password X] [/count N]` (`/internet`, `/lan` force the mode; the later instances of `/count` are
  additional instances). No window; exit code 0 if both DLLs are loaded in the game; with the relay, the process stays
  alive as long as a launched game runs; `/count N`: N instances 10 s apart.
- Compatible with `tools/fantome` and `pont.exe` (same `Local\DLFantome_slot_N` locks).
