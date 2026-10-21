# DLCoop — massive co-op for Dying Light 1.55

### Play Dying Light 1.55 (Steam) in co-op with up to 32 players.

- **Up to 32 players** (+1 Night Hunter) instead of 4.
- **Auto-teleport** stragglers to the mission gather point.
- **Be the Zombie** working and rebalanced for any number of hunters.
- **Rebalanced** enemies, spawns, quests and loot with 4+ players.
- **Just one launcher** — install, set options, play.
- **Free.**

![Co-op session with the mod: the local player in the foreground, other players lined up against a wall, the teammate list on the left](assets/screenshot.png)



In this repo :

- **`coop6.dll`** — the mod.
- **`DLCoop.exe`** — the launcher: installs the mod and starts the game.
- **a relay server** — play over the Internet, no VPN, no port forwarding. Install on a small VPS and set it's IP address in the game launcher to make your own easy access lobbies. (not required to play)

Personal mod, **not affiliated with Techland**.

## Play

1. Dying Light **1.55** (Steam), Windows 10/11 64-bit. If DLML is installed, disable it (rename its `dsound.dll`).
2. Download `DLCoop_<version>.zip` from **Releases** and unzip it.
3. Run `DLCoop.exe` → **Install mod**, enter the **Server IP** and **session password** → **Launch**. *There is one free public server provided by me, just enter this IP : 51.79.69.69*
4. Set you game visibility to LAN. The host creates a co-op game; others join with **Quick match**. Every player needs the mod.

> **LAN only** (including through the relay, which the game sees as LAN) — Dying Light uses VAC, so never play this in
> public matchmaking. Steam offline is recommended.

## Layout

| Path | Contents |
|---|---|
| [`mods/coop6/`](mods/coop6/) | the mod — every change with its 1.55 address |
| [`tools/dlcoop/`](tools/dlcoop/) | the launcher ([docs](docs/launcher.md)) |
| [`tools/dlloader/`](tools/dlloader/) | the `winmm.dll` mod loader ([docs](docs/mod-loading.md)) |
| [`tools/relais/`](tools/relais/) | the relay server ([docs](docs/relay-server.md)) |
| [`tools/fantome/`](tools/fantome/) | network DLL + multi-instance tools ([docs](docs/multiple-instances.md)) |
| `src/` | game structures used by the mod |
| `tests/` | offline tests (loader race, relay protocol, mod install) |

**Docs:** [launcher](docs/launcher.md) · [mod loading](docs/mod-loading.md) · [building](docs/building.md) ·
[the co-op mod](docs/coop-mod.md) · [relay server](docs/relay-server.md) · [multiple instances](docs/multiple-instances.md)

## Build

MSVC v141 (14.16), the .NET SDK, Python 3 with `pefile`. Then:

```powershell
.\tools\dlcoop\build.ps1                       # -> out\dlcoop\DLCoop_<version>.zip
.\tools\dlcoop\build.ps1 -Relais 203.0.113.10  # with the relay address baked in
```

## Branches

- **`main`** — current version: `winmm.dll` loader, no injection.
- **`dll-injection`** — old 1.2.0 launcher that injects the DLLs; kept for reference, flagged by some antivirus.
