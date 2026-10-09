# Loading mods into Dying Light 1.55

The game loads no third-party DLL. For a DLL-based mod to take effect, it must be present **in the game process before
the game really starts**: `dlcoop_reseau.dll`, for instance, must rename the game's single-instance lock
(`Global\ChromeEngine4DIMutex`) before the game takes it. Three approaches exist around this game.

| | Injection (old DLCoop) | `winmm.dll` loader (current DLCoop) | DLML |
|---|---|---|---|
| Where the code lives | DLCoop's folder | the game folder (`winmm.dll` + mods) | the game folder (`dsound.dll`) |
| Who loads the mod | the launcher, from outside | the game itself | the game itself |
| Loads mod DLLs | yes | yes | **no** (`.pak` archives only) |
| Launched via Steam | without the mod | **with** the mod | with DLML |
| Antivirus | an injection pattern, often flagged | a common mod-loader pattern, much less flagged | same |
| Branch | `dll-injection` | `main` | (external project) |

## Injection (branch `dll-injection`, and `tools/fantome`)

The launcher starts the game suspended and asks the system to load the mod DLLs on the game's main thread, before its
entry point, via a `LoadLibraryW` APC. Code: `tools/dlcoop/Launcher.cs` (branch `dll-injection`),
`tools/fantome/src/fantome.cpp`.

- **Main drawback**: an outside program that arranges to load a DLL into another process is a **pattern that antivirus
  software flags** (it is also used by malware), even for a perfectly legitimate use. This is why DLCoop moved to the
  `winmm.dll` loader.
- Launched via Steam (without the launcher), the game has no mod.

## The `winmm.dll` loader (branch `main`, `tools/dlloader`)

The game imports `winmm.dll` (via `engine_x64_rwdi.dll` and `gamedll_x64_rwdi.dll`), and `winmm` is not a protected
"KnownDLL". By placing our own `winmm.dll` in the game folder, Windows loads it instead of the system one: **the game
loads the mod**, no outside program is involved. Same technique as DLML (with `dsound.dll`) or the "ASI" loaders.

Our `winmm.dll`:

1. **forwards** all of its calls (181 functions) to the real Windows `winmm` (System32), through generated forwarders
   (`gen_def.py` → `winmm_exports.h`). The game keeps a fully functional `winmm`; nothing else to copy;
2. reads `dlmods.ini` (next to it): section `[loader] Mods=` (DLLs to load) and section `[env]` (settings placed as
   environment variables, only if absent — see [launcher](launcher.md));
3. loads the mod DLLs **at the exe's entry point**: it places a short hook there (restored as soon as it is called) to
   load the mods on the main thread, right before the game's first instruction — the same moment as the old APC, with no
   outside program. A plain background thread would be too late (the game would take its instance lock before the mod;
   measured in `tests/dlloader/run.ps1`).

Benefits: the mod also loads when the game is started via **Steam** (the files are in its folder), and the "injection"
pattern disappears. `winmm.dll` does **not** pretend to be the Microsoft component: its version information declares it
"DLCoop mod loader" (a file that impersonates a system component is exactly what antivirus software flags).

Files: `tools/dlloader/` (`gen_def.py`, `dllmain.cpp`, `build.ps1`, `dlmods.ini.exemple`).

> **Modifying the game install.** This method copies files into the game folder. It is reversible (*Uninstall mod*, or
> delete `winmm.dll`, `dlmods.ini` and the mod DLLs) and does not touch the original files.

## DLML

[DLML](https://github.com/12brendon34/DLML) (Dying Light Mod Loader) is an external project: a `dsound.dll` proxy that
loads `.pak` archives and shows the game log. It **does not load third-party DLLs**, so it cannot load `coop6.dll`.
Since it occupies `dsound.dll` and DLCoop uses `winmm.dll`, they do not clash by name, but if DLML is active it may
affect the lobby: **disable it for co-op games** (rename its `dsound.dll`).
