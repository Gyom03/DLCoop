# The relay server (co-op over the Internet, no VPN)

Sources: `tools/relais/`.

Lets you play co-op **without a VPN and without Steam online mode**: a small public server relays the game's packets
between players. The game stays in LAN mode and only sees local `127.0.0.N` addresses (same principle as the bridge
between two PCs on a local network, `tools/fantome/pont.exe`, but in a star around a server).

```
 game (127.0.0.1) ─┐                                           ┌─ game (127.0.0.2)
 Player A's PC     │  DLCoop (agent) ──UDP──► relay ◄──UDP── DLCoop (agent)  Player B's PC
                   └─ holds 127.0.0.2 for B    (VPS)          holds 127.0.0.1 for A ─┘
```

- **Players**: nothing to open on their router (outbound connections only, keep-alive every 2 s). In DLCoop:
  *Server IP* and the **session password** (the same for everyone).
- **Server**: a single open UDP port (**20300**). The game is never exposed to the Internet.
- Each password = an isolated session of **33 addresses** (32 players + 1 zombie).

## Installing on a VPS

A small Linux VPS is plenty (tested: 2 vCores, 4 GB, Debian 13; ~15 packets/s for 2 players, under 1 MB/min, 650 KB of
memory). Copy the `tools/relais/` folder to the server and run the install script; it compiles, installs the systemd
service, (re)starts it and creates a session password on the first run.

```bash
# on the VPS, in the folder containing relais.cpp, dlcoop-relais.service, nftables.conf
bash deployer.sh                 # compile, install, (re)start the service
bash deployer.sh --pare-feu      # also: nftables firewall (automatic rollback after 90 s if the SSH
                                 # connection is lost; confirm with "systemctl stop nft-retour.timer")
```

From a Windows machine (replace `<key>`, `<user>`, `<vps_ip>`):

```powershell
scp -i <key> tools\relais\* <user>@<vps_ip>:dlcoop-relais/
ssh -i <key> <user>@<vps_ip> "sed -i 's/\r$//' ~/dlcoop-relais/*; bash ~/dlcoop-relais/deployer.sh"
ssh -i <key> <user>@<vps_ip> "sudo journalctl -u dlcoop-relais -f"   # live log
```

Accepted passwords: the `RELAIS_MOTS_DE_PASSE` variable (file `/etc/dlcoop-relais.env`, or `--mots-de-passe a,b`).
Empty: any password opens its own session. The password never appears in the log.

Hardening tips (already in the provided service and firewall): SSH by key only (password and root refused), automatic
security updates, the service as a throwaway user with a read-only system, inbound closed except SSH (TCP 22) and the
relay (UDP 20300).

Container variant (`Dockerfile`):

```bash
docker build -t dlcoop-relais tools/relais
docker run -d --restart unless-stopped -p 20300:20300/udp -e RELAIS_MOTS_DE_PASSE=... dlcoop-relais
```

## Files

| File | Role |
|---|---|
| `relais.cpp` | the server (portable C++17, Linux / Windows, no dependency) |
| `deployer.sh` | build + install on the VPS |
| `dlcoop-relais.service` | hardened systemd service |
| `nftables.conf` | firewall (inbound closed except SSH and UDP 20300) |
| `Dockerfile` | container variant |
| `../dlcoop/Relay.cs` | the agent, on the player's side (inside the launcher) |
| `../../tests/relais/test_relais.py` | protocol test without the game (simulated agents) |

## Protocol

A 20-byte header, little-endian: `magic "DLR1"`, `type`, `src`, `dst`, `aux`, then fields depending on the type. The
agent sends **JOIN** with the password, receives a **WELCOME** (session token), requests an address (**CLAIM** →
**SLOT**), sends a **KEEPALIVE** every 2 s (which also keeps the path through its router open), and exchanges **DATA**
relayed to the agent that holds the destination address. The relay pushes a **ROSTER** (the session's active
addresses). Protocol **v2**: 33 addresses (an older v1 relay with 16 addresses is accepted, the agent falls back to
v1). Full details in `tools/relais/README.md` and at the top of `tools/relais/relais.cpp`.
