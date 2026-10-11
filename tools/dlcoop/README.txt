DLCoop - co-op for up to 32 players in Dying Light 1.55
======================================================

Files
  DLCoop.exe          the launcher
  winmm.dll           the mod loader: copied into the game folder, the game loads the mod by itself
  dlcoop_reseau.dll   network fixes (LAN join in 1.55), display name, additional instances
  coop6.dll           the mod: lobby up to 33 slots, fixes for more than 4 players
  logs\               created on first launch

Install / uninstall
  Extract the whole zip first (right-click -> Extract All): DLCoop.exe started from inside the zip cannot see
  the DLL files next to it.
  "Install mod" copies winmm.dll, dlcoop_reseau.dll and coop6.dll into the game folder ("Launch" also does it
  if needed). At every launch, DLCoop writes all the settings to dlmods.ini in the game folder.
  Once installed, the mod is also active when the game is started from Steam (with the last saved settings).
  "Uninstall mod" removes these files and dlmods.ini: the game is back to normal.

Before playing
  - Dying Light version 1.55 (Steam). With any other version the mod does not change anything.
  - Steam must be running.
  - DLML: if it is installed (dsound.dll in the game folder), disable it (rename dsound.dll).
  - Every player in the game must start Dying Light with DLCoop, the host included.
  - Play over the Internet through a relay server, a local network or a LAN-style VPN (see "Window").
    Never in public matchmaking: Dying Light is protected by VAC.

How to play
  1. Unzip this folder anywhere.
  2. Start DLCoop.exe. The game is found automatically (the location is saved and checked at every start).
  3. Enter the server IP and the session password (the same for every player, given by the group organizer),
     then click "Launch".
  4. The host creates a co-op game; the others join it with "Quick match".

Window
  Game location       found automatically; "Change..." to pick DyingLightGame.exe yourself.
  Server IP           relay server: no VPN, nothing to open on your router. Keep DLCoop open while playing:
                      it carries the game traffic ("Close after launch" only minimizes it, and it closes itself
                      when the game is closed).
                      Leave it EMPTY to play on a local network or a LAN-style VPN (Radmin VPN...).
  Session password    the same for every player of the group.

  Folded menus (click to open), usually not needed:
  Player name         "Hide my Steam name": the game shows the chosen (or random) name instead.
  Instance            main instance by default. "Additional instance": a 2nd (3rd...) game window on the same
                      PC, with its own Steam identity (cloud saves untouched).
                      On a local network, also tick "Several instances on this PC".
                      These choices go back to the defaults every time DLCoop starts.
  More options        "Max players when I host": players allowed in the game you host, 2 to 32 (the
                      Night Hunter of Be the Zombie is not counted; it only matters for the host).
                      "Disable the EOS connection" (recommended: avoids freezes at startup) and
                      "Close after launch".

  Co-op is always set up for up to 32 players + 1 Night Hunter (difficulty, colors, HUD: always on).

Command line (no window, uses the saved options)
  DLCoop.exe /launch [/instance N] [/multi] [/name Name] [/game path\DyingLightGame.exe]
             [/internet | /lan] [/server address] [/password X] [/maxplayers N] [/count N]

Nothing else is required: DLCoop.exe uses the .NET Framework built into Windows 10 / 11.
Options are saved in %APPDATA%\DLCoop\settings.txt.
