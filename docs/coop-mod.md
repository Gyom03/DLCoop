# The co-op mod (`coop6.dll`)

Sources: `mods/coop6/`. The full list of changes, with their addresses in 1.55, is in
[`mods/coop6/README.md`](../mods/coop6/README.md). This document explains the principle.

## What the mod lifts

The game is designed for 4 players. Beyond that, several limits show up: a 4-slot lobby, fixed-size player lists
(overflows and crashes), HUD and colors for 4 players only, difficulty and zombie spawns that fall back to the
single-player values, "Be the Zombie" tuned for 1 to 4 hunters. The mod fixes each of these for **up to 32 players**
(+1 zombie player), by families:

- **A** — lobby: public slots raised to `Places`, widened join conditions, game search.
- **B** — player lists: rewrites of functions that used arrays of 5, HUD and colors for players 6+ (27 colors added),
  zombie spawn plan on a rolling window of 5 players.
- **C** — HUD: extra teammate blocks (without touching the game's archives).
- **D** — difficulty: health and damage curves extended beyond 4 players.
- **E** — EOS disabled (useless on LAN; a cause of startup freezes with several instances).
- **F** — mission gathering: stragglers are teleported to the gather point.
- **G / Z** — balancing beyond 4 (zombie groups, quests, loot) and **Be the Zombie for N hunters** (power law
  `HuntersScale`, `BtzScale.h`).

## How it acts

Everything is installed in the `DllMain` of `coop6.dll`, **after checking the bytes of 1.55** at each spot (if the
bytes do not match, that spot is left untouched). Depending on the case, the mod:

- replaces a specific byte (e.g. a cap of `5`, or a jump condition);
- places a **detour** to a replacement function (with a small *trampoline* that keeps the original bytes);
- replaces an entry in a **virtual method table** to wrap a method.

The replacement functions reproduce the game's original behavior (often the "4 players" case) and extend it. The game
structures they manipulate are described in `src/` (`HumanAI`, `LogicalLevelReplData`, `Sessions`…).

## Settings

`coop6.ini` next to the DLL, or `COOP6_<key>` environment variables set by the launcher (which take priority):
`Places` (slots when hosting, zombie player included), `Difficulte`, `Couleurs`, `Hud`, `Diagnostic` (log of the lobby
state), `SansEos`. Mod log: `coop6_instance_N.log` next to the DLL (crash reports included).

## Testing without launching the game

`tests/mod_install/run_coop6.ps1` compiles a harness that **loads** `gamedll_x64_rwdi.dll` and `engine_x64_rwdi.dll`
into memory without executing them, applies `coop6.dll`, then checks that the bytes, the detours, the trampolines and
the virtual tables correctly point to the mod. It reads the DLLs from the game install (the `-Jeu` parameter), without
modifying them. Expected result: "RÉSULTAT : CONFORME".
