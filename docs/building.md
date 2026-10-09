# Building

## Requirements

- **Visual Studio Build Tools 2022** with the **MSVC v141 (14.16)** toolset — that is linker 14.16, the one used to
  compile the game (see below). The mod DLLs are built with it, statically linking the CRT (`/MT`): no Visual C++
  redistributable to install on the players' machines.
- The **.NET SDK** (`dotnet`) to build the WPF launcher (targets .NET Framework 4.7.2).
- **Python 3** with `pefile` (the `winmm.dll` loader generates its forwarders from the system `winmm`).
- For the relay server: **g++** (C++17) on Linux.

## Build everything (mod + loader + launcher)

In PowerShell, at the repository root:

```powershell
.\tools\dlcoop\build.ps1                          # -> out\dlcoop\paquet\ + out\dlcoop\DLCoop_<version>.zip
.\tools\dlcoop\build.ps1 -Relais 203.0.113.10     # same, relay address pre-filled (server.txt) for a group
```

The package contains `DLCoop.exe`, `winmm.dll`, `dlcoop_reseau.dll`, `coop6.dll` and `README.txt`. The version comes
from `<Version>` in `tools/dlcoop/DLCoop.csproj`: it is also the version of the three DLLs (version information,
`version.rc`).

Build separately: `tools\dlloader\build.ps1` (the `winmm.dll` loader), `mods\coop6\build.ps1` (the mod).

## Reproducible builds

The native DLLs are compiled and linked with `/Brepro`: given identical source, they come out **byte-for-byte
identical** (without the build date that MSVC otherwise writes into the PE header). This is what lets the launcher
report "Update available" only for a real change to the mod (byte-by-byte comparison with the game folder).

## The relay server

Built on the Linux VPS by `tools/relais/deployer.sh` (`g++ -O2 -std=c++17`). `relais.cpp` is portable C++17 with no
dependency; it also compiles on Windows for the tests. See [relay server](relay-server.md).

## How the game itself was compiled (and why it matters)

Dying Light 1.55 was compiled with **MSVC 2017 (linker 14.16)**, options `/O2 /Gy /GS- /fp:fast`, with **LTCG**
(`/GL` + `/LTCG`). The mod does not recompile the game: it **patches precise functions in memory**, after checking that
the bytes at the target address are indeed those of 1.55 (otherwise it touches nothing). All of the mod's addresses
(`mods/coop6/README.md`) are therefore specific to this version; the game must stay on **1.55**.

Two consequences visible in this repository:

- the headers in `src/` describe the game's structures (field offsets, slots in the virtual method tables) exactly as
  they are in the 1.55 binaries;
- `mods/coop6/engine_imports.def` lists the few functions exported by `engine_x64_rwdi.dll` that the mod calls;
  `build.ps1` turns it into an import library (`engine_imports.lib`) so that `coop6.dll` links against them.

The game binaries are **not** included in this repository (a legitimate copy is required). Building the mod does not
need them; the offline test `tests/mod_install/run_coop6.ps1` does read `gamedll_x64_rwdi.dll` and
`engine_x64_rwdi.dll` from the game install (the `-Jeu` parameter), without modifying them.
