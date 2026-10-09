# Several game instances on one PC

Sources: `tools/fantome/` (a test and development tool) and `tools/dlcoop/` (the same mechanism, built into the
launcher).

Dying Light allows only one instance at a time and one Steam identity. To run **several game windows on one PC** (solo
co-op testing, or several players on one machine), `dlcoop_reseau.dll` (the same DLL as the launcher's network side)
lifts these two locks, per instance:

1. **Single-instance lock.** The game creates `Global\ChromeEngine4DIMutex` and refuses to start if it already exists.
   For an additional instance (number > 0), the DLL intercepts the call and **renames** the lock (`…_fantomeN`): each
   instance has its own. This is why the mod must be loaded **before** the game takes its lock (see
   [mod loading](mod-loading.md)).
2. **Steam identity.** Additional instances receive an identity **derived** from the real SteamID (the real account and
   its cloud saves are untouched; only the display goes through a proxy). The main instance (number 0) keeps the real
   Steam identity.

**Network addresses.** Each instance talks on `127.0.0.(N+1)`, and the game sees the others on their local addresses.
On a LAN between two PCs, `pont.exe` relays the `127.0.0.N` packets from one PC to the other; over the Internet, that is
the role of the [relay server](relay-server.md). The fix for the 1.55 "port 0" bug (packets sent to port 0, corrected
to 20101) is applied in both directions.

**Memory.** Each instance uses about 3.3 GB of committed memory at the menu. For many instances on one PC, provide a
large page file (initial size = maximum size).

## With the DLCoop launcher

All of this is built in: *Instance > Additional instance*, and the launcher sets the right per-instance environment
variables (`DLF_INDEX`, `DLF_LOOPBACK`, `DLF_ADRESSE`, `DLF_NOM`). On a LAN, also tick "Several instances on this PC".
See [launcher](launcher.md).

## With the raw tools (`tools/fantome`)

`fantome.exe` launches one instance (number 0 = host with the real identity, 1..32 = additional instances), settings in
`fantome.ini` (including `Jeu=` and `Mods=` to load the mod). `pont.exe` relays the addresses between two PCs on a LAN.
Details in `tools/fantome/README.md`.
