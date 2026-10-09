# DLCoop Internet relay (co-op without a VPN)

Lets you play co-op **without a VPN and without Steam online mode**: a small public server relays the game's packets
between players. The game stays in LAN mode and only sees local `127.0.0.N` addresses (same principle as
`tools/fantome/pont.exe`, but in a star around a server instead of two PCs on a local network).

```
 game (127.0.0.1) ─┐                                            ┌─ game (127.0.0.2)
 Player A's PC     │  DLCoop.exe (agent) ──UDP──► relay ◄──UDP── DLCoop.exe (agent)  Player B's PC
                   └─ holds 127.0.0.2 for B    (VPS)        holds 127.0.0.1 for A ─┘
```

- **Players**: nothing to open on their router (outbound connections only; keep-alive every 2 s). In DLCoop:
  *Server IP* and the **session password** (the same for everyone).
- **Server**: a single open UDP port (**20300**). The game is never exposed to the Internet.
- Each password = an isolated session of **33 addresses** (32 players + 1 zombie; 16 for the 1.1 launchers). Accepted
  passwords on the server side: `RELAIS_MOTS_DE_PASSE` (`/etc/dlcoop-relais.env`).

## Files

| File | Role |
|---|---|
| `relais.cpp` | the server (portable C++17, Linux / Windows, no dependency) |
| `dlcoop-relais.service` | hardened systemd service (throwaway user, read-only system, 256 MB max) |
| `nftables.conf` | firewall: inbound closed except SSH (TCP 22) and the relay (UDP 20300) |
| `deployer.sh` | build + install on the VPS (`--pare-feu`: also installs the firewall, with automatic rollback after 90 s if the SSH connection is lost) |
| `Dockerfile` | container variant |
| `../dlcoop/Relay.cs` | the agent on the player's side (inside the launcher) |
| `../../tests/relais/test_relais.py` | protocol test without the game (simulated v1 and v2 agents, mixed session) |
| `../../tests/relais/faux_joueur.py` | fake remote player: tests a real game with a single PC |

## Server

A small Linux VPS is enough (tested: 2 vCores, 4 GB, Debian 13; ~15 packets/s for 2 players, under 1 MB/min, 650 KB of
memory). Recommended: SSH by key only (password and root refused), automatic security updates
(`unattended-upgrades`). Replace `<vps_ip>`, `<user>` and `<key>` below.

```powershell
# update the relay after changing relais.cpp
scp -i <key> tools\relais\* <user>@<vps_ip>:dlcoop-relais/
ssh -i <key> <user>@<vps_ip> "sed -i 's/\r$//' ~/dlcoop-relais/*; bash ~/dlcoop-relais/deployer.sh"
# live log
ssh -i <key> <user>@<vps_ip> "sudo journalctl -u dlcoop-relais -f"
# protocol test
python tests\relais\test_relais.py <vps_ip> <password>
```

## Protocol

A 20-byte header, little-endian: `magic "DLR1"` (u32), `type` (u8), `src` (u8), `dst` (u8), `aux` (u8), `srcPort`
(u16), `dstPort` (u16), `token` (u64), then the payload.

| Type | Direction | Contents |
|---|---|---|
| 1 JOIN | agent → relay | `token` = nonce, `aux` = version (1 or 2), payload = password |
| 2 WELCOME | relay → agent | `token` = the agent's session token, `aux` = the chosen version |
| 3 REFUSED | relay → agent | `aux` = 1 password, 2 relay full, 3 version; `token` = nonce |
| 4 CLAIM | agent → relay | `src` = desired address (0xFF: the first free one), `aux` = request number |
| 5 SLOT | relay → agent | `src` = address (0xFF: session full), `aux` = request number |
| 6 RELEASE | agent → relay | `src` = released address |
| 7 KEEPALIVE | agent → relay | every 2 s; v2: empty 16-byte payload |
| 8 ROSTER | relay → agent | `srcPort` = mask of active addresses, `dstPort` = mask of the agent's (16 bits; pushed on every change); v2: 16-byte payload = both masks as 64 bits (only if the request was that large) |
| 9 DATA | both ways | `src`, `dst`, `srcPort`, `dstPort` + the game packet; the relay checks that the sender holds `src` |
| 10 BYE | agent → relay | leaving |

Versions: **v1** (launcher 1.1) = addresses 0..15; **v2** (launcher 1.2+) = 0..32. A v1 agent only gets addresses 0..15
and only receives their packets: the old launchers play among themselves as before, even in a session where v2 agents
hold 16..32. A v2 launcher refused by a v1 relay (`REFUSED` reason 3) falls back to v1.

Security: no packet without a valid token gets a reply (except JOIN, limited to 10 per second per IP); replies are never
larger than requests (no amplification); per-agent rate limit (8,000 packets and 8 MB per second: one PC can run several
instances); an agent silent for 15 s is removed. The password travels in clear: it separates groups, it is not a strong
secret.

Recovery: relay restarted → the agent receives nothing, reconnects after 10 s and requests the same addresses (the game
keeps its own); a lost RELEASE → repeated from the ROSTER.
