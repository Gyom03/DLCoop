# DLCoop — massive co-op for Dying Light 1.55

> ⚠️ **Historical branch `dll-injection`.** This is the old version: the launcher (1.2.0) starts the game
> suspended and **injects** the mod DLLs. That pattern is flagged by some antivirus software. The current,
> recommended version — no injection (`winmm.dll` loader) — is on the **`main`** branch.
> Differences: [docs/mod-loading.md](docs/mod-loading.md).


![Co-op session with the mod: the player in the foreground, the other players lined up against a wall, the teammate list on the left](assets/screenshot.png)

**DLCoop** lets you play **Dying Light 1.55** (Steam) in co-op with **up to 32 players** (+1 Night Hunter in
"Be the Zombie" mode), instead of 4. It includes:

- a **mod** (`coop6.dll`) that lifts the game's limits (lobby, player list, HUD, colors, difficulty, zombie
  spawns, mission gathering, Be the Zombie for any number of hunters…);
- a Windows **launcher** (`DLCoop.exe`) that configures the mod, launches the game and injects the DLLs;
- a **relay server** to run on a small VPS, to play **over the Internet without a VPN** and without opening any port;
- a tool to run **several game instances on one PC** (for testing, or several players on one machine).

> **FR** — DLCoop est un mod coop pour Dying Light 1.55 (Steam, Windows) qui porte la limite de 4 à 32 joueurs
> (+1 chasseur de nuit). Il fournit une DLL de mod, un lanceur (installer / régler / lancer), un serveur relais UDP
> pour jouer par Internet sans VPN, et un outil multi-instances. L'interface du lanceur est en anglais ; la
> documentation est en anglais, les commentaires du code sont en français. Téléchargez la dernière version dans
> **Releases**.

Personal mod, **not affiliated with Techland**. No game file is shipped or modified on disk: the mod acts in memory,
after checking the bytes of version 1.55.

## Play (players)

1. **Requirements**: Dying Light **1.55** on Steam, Windows 10 (1803+) or 11, 64-bit. Nothing else to install (the
   launcher uses the .NET Framework built into Windows). If **DLML** is installed (`dsound.dll` in the game folder),
   disable it for co-op (rename `dsound.dll`).
2. Download `DLCoop_<version>.zip` from the **Releases** page and unzip it anywhere.
3. Run `DLCoop.exe`: the game is found automatically. **Install mod**, then enter **Server IP** (your group's relay)
   and the **session password** (the same for everyone), then **Launch**. Leave *Server IP* empty to play on a local
   network or a LAN-style VPN (Radmin VPN…).
4. The host creates a co-op game; the others join it with **Quick match**. **Every** player, host included, must have
   the mod.

Several game windows on one PC: *Instance > Additional instance*, then **Launch** (one Steam identity and one address
per window). **Uninstall mod** restores the game folder to its original state.

> **Never in public matchmaking**: Dying Light is protected by VAC. DLCoop is for **LAN** games (including through the
> relay, which the game sees as a local network); Steam in offline mode is recommended.

## Repository layout

| Folder | Contents | Documentation |
|---|---|---|
| `mods/coop6/` | the co-op mod (`coop6.dll`): list of every change, with its address in 1.55 | [`mods/coop6/README.md`](mods/coop6/README.md) |
| `src/` | game structures used by the mod (headers, layouts verified against the machine code) | — |
| `tools/dlcoop/` | the `DLCoop.exe` launcher (WPF, .NET Framework 4.7.2) and the package build | [`docs/launcher.md`](docs/launcher.md) |
| `tools/relais/` | the relay server (C++17, Linux), systemd service, firewall, container | [`docs/relay-server.md`](docs/relay-server.md) |
| `tools/fantome/` | `dlcoop_reseau.dll` (network, identities, instances), `fantome.exe` and `pont.exe` (test tools) | [`docs/multiple-instances.md`](docs/multiple-instances.md) |
| `tests/` | tests without the game: loader race, relay protocol, mod install | — |
| `docs/` | how it works, building, loading methods | below |

## Documentation

- [How the launcher works](docs/launcher.md): install, settings, launch, relay, multiple instances.
- [Loading mods into the game](docs/mod-loading.md): DLL injection, the `winmm.dll` loader (DLML-style), DLML itself;
  why DLCoop moved from one to the other (antivirus).
- [Building](docs/building.md): build the mod, the loader, the launcher and the server; how the game itself was
  compiled (and why it matters for the mod).
- [The co-op mod](docs/coop-mod.md): the game's limits and how they are lifted.
- [The relay server](docs/relay-server.md): principle, VPS install, protocol.
- [Several instances on one PC](docs/multiple-instances.md): Steam identity, addresses, the game's instance lock.

## Build

Requirements: Visual Studio Build Tools 2022 with the **MSVC v141 (14.16)** toolset, the .NET SDK (to build the
launcher), Python 3 with `pefile`. Then, in PowerShell at the repository root:

```powershell
.\tools\dlcoop\build.ps1                          # -> out\dlcoop\paquet\ and out\dlcoop\DLCoop_<version>.zip
.\tools\dlcoop\build.ps1 -Relais 203.0.113.10     # same, with the relay address pre-filled for a group
```

Details, reproducible builds and the server: [`docs/building.md`](docs/building.md).

## Branches

| Branch | Contents |
|---|---|
| `main` | current version: `winmm.dll` loader, launcher 1.3.x (no DLL injection) |
| `dll-injection` | old version (launcher 1.2.0): the launcher starts the game suspended and injects the mod DLLs. Kept for reference; flagged by some antivirus. |
