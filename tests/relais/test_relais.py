"""Test du relais DLCoop (tools/relais/relais.cpp) sans le jeu : deux agents simulés.

    python tests/relais/test_relais.py <ip[:port]> <mot_de_passe>

Vérifie : refus d'un mauvais mot de passe, JOIN/WELCOME, CLAIM/SLOT, ROSTER poussé, DATA relayé d'un agent à
l'autre (et pas en sens interdit : adresse source non détenue), RELEASE et BYE (protocole v1, lanceur 1.1) ; puis
protocole v2 (lanceur 1.2+) : 33 adresses (32 joueurs + 1 zombie), 34e refusée, ROSTER à masques de 64 bits, pas de
réponse plus grosse que la requête ; session mixte : un agent v1 n'obtient et ne reçoit que les adresses 0..15.
"""
import os
import socket
import struct
import sys
import time

MAGIC = 0x31524C44
JOIN, WELCOME, REFUSED, CLAIM, SLOT, RELEASE, KEEPALIVE, ROSTER, DATA, BYE = range(1, 11)
HDR = struct.Struct("<IBBBBHHQ")


class Agent:
    def __init__(self, server, password, version=1):
        self.server = server
        self.version = version
        self.password = password.encode()
        self.s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.s.settimeout(0.5)
        self.token = 0
        self.nonce = int.from_bytes(os.urandom(8), "little")

    def send(self, typ, src=0, dst=0, aux=0, sp=0, dp=0, payload=b"", token=None):
        tok = self.token if token is None else token
        self.s.sendto(HDR.pack(MAGIC, typ, src, dst, aux, sp, dp, tok) + payload, self.server)

    def recv(self, want, timeout=3.0):
        end = time.time() + timeout
        while time.time() < end:
            try:
                data, _ = self.s.recvfrom(65536)
            except socket.timeout:
                continue
            magic, typ, src, dst, aux, sp, dp, tok = HDR.unpack_from(data)
            if magic == MAGIC and typ in want:
                return typ, src, dst, aux, sp, dp, tok, data[HDR.size:]
        return None

    def join(self):
        for _ in range(6):
            self.send(JOIN, aux=self.version, payload=self.password, token=self.nonce)
            r = self.recv({WELCOME, REFUSED}, 1.0)
            if r:
                if r[0] == WELCOME:
                    self.token = r[6]
                return r
        return None

    def claim_raw(self, req):
        self.send(CLAIM, src=0xFF, aux=req)
        r = self.recv({SLOT})
        assert r and r[3] == req, f"CLAIM sans réponse : {r}"
        return r[1]

    def claim(self, req):
        slot = self.claim_raw(req)
        assert slot != 0xFF, "session pleine"
        return slot

    def drain(self):
        while self.recv({ROSTER, DATA, SLOT}, 0.3):
            pass


def main():
    host, _, port = sys.argv[1].partition(":")
    server = (socket.gethostbyname(host), int(port or 20300))
    password = sys.argv[2]
    ok = True

    def check(cond, what):
        nonlocal ok
        print(("OK    " if cond else "ÉCHEC ") + what)
        ok &= bool(cond)

    bad = Agent(server, password + "x")
    r = bad.join()
    check(r and r[0] == REFUSED and r[3] == 1, "mauvais mot de passe refusé")

    a, b = Agent(server, password), Agent(server, password)
    t0 = time.time()
    check(a.join() and a.token, f"agent A accepté ({(time.time() - t0) * 1000:.0f} ms aller-retour compris)")
    check(b.join() and b.token, "agent B accepté")
    sa, sb = a.claim(1), b.claim(2)
    check(sa != sb, f"adresses attribuées : A={sa}, B={sb}")
    def latest_roster(agent):  # des ROSTER poussés plus tôt peuvent attendre dans la file : le plus récent compte
        r, last = None, agent.recv({ROSTER})
        while last:
            r, last = last, agent.recv({ROSTER}, 1.0)
        return r

    a.send(KEEPALIVE)
    r = latest_roster(a)
    check(r and (r[4] >> sb) & 1 and (r[5] >> sa) & 1 and not (r[5] >> sb) & 1,
          f"ROSTER de A : actives={r and bin(r[4])}, à A={r and bin(r[5])}")

    payload = b"paquet du jeu " + os.urandom(8)
    t0 = time.time()
    a.send(DATA, src=sa, dst=sb, sp=20101, dp=20111, payload=payload)
    r = b.recv({DATA})
    check(r and r[1] == sa and r[2] == sb and r[4] == 20101 and r[5] == 20111 and r[7] == payload,
          f"DATA A -> B relayé intact ({(time.time() - t0) * 1000:.0f} ms)")
    a.send(DATA, src=sb, dst=sa, sp=20101, dp=20101, payload=b"usurpation")
    check(a.recv({DATA}, 1.0) is None and b.recv({DATA}, 0.5) is None, "DATA avec une adresse source non détenue jeté")

    a.send(RELEASE, src=sa)
    r = latest_roster(b)
    check(r and not (r[4] >> sa) & 1, "RELEASE : l'adresse de A disparaît du ROSTER de B")
    a.send(BYE)
    b.send(BYE)
    time.sleep(0.3)

    # --- v2 : 33 adresses ; session mixte avec un agent v1 ---------------------------------------------------
    d = Agent(server, password, version=1)
    c = Agent(server, password, version=2)
    check(d.join() and d.token, "agent v1 accepté")
    r = c.join()
    check(r and r[0] == WELCOME and r[3] == 2 and c.token, "agent v2 accepté (WELCOME version 2)")
    sd = d.claim(1)
    mine = []
    for req in range(1, 40):
        slot = c.claim_raw(req)
        if slot == 0xFF:
            break
        mine.append(slot)
    check(len(mine) == 32 and max(mine) == 32 and sd not in mine and sd < 16,
          f"v2 : {len(mine)} adresses obtenues (max {max(mine) if mine else None}), la 34e refusée ; v1 : {sd}")
    c.drain()
    c.send(KEEPALIVE, payload=bytes(16))
    r = c.recv({ROSTER})
    full = r and len(r[7]) == 16
    active, own = struct.unpack("<QQ", r[7]) if full else (0, 0)
    check(full and active == (1 << 33) - 1 and own == sum(1 << i for i in mine),
          f"ROSTER v2 : masques de 64 bits en charge (actives {bin(active).count('1')}, à l'agent {bin(own).count('1')})")
    c.drain()
    c.send(KEEPALIVE)
    r = c.recv({ROSTER})
    check(r and len(r[7]) == 0, "KEEPALIVE v2 sans charge : réponse de 20 octets seulement (pas d'amplification)")
    d.drain()
    d.send(KEEPALIVE)
    r = d.recv({ROSTER})
    check(r and len(r[7]) == 0 and r[4] == 0xFFFF and r[5] == 1 << sd,
          f"ROSTER v1 : masques de 16 bits (actives {r and hex(r[4])})")
    check(d.claim_raw(50) == 0xFF, "v1 : plus d'adresse 0..15 libre -> session pleine pour lui")
    low, high = min(mine), max(mine)
    c.send(DATA, src=high, dst=sd, sp=20101, dp=20101, payload=b"haute")
    check(d.recv({DATA}, 1.0) is None, f"DATA depuis l'adresse {high} (>= 16) non transmis à l'agent v1")
    c.send(DATA, src=low, dst=sd, sp=20101, dp=20101, payload=b"basse")
    r = d.recv({DATA})
    check(r and r[1] == low and r[7] == b"basse", f"DATA depuis l'adresse {low} transmis à l'agent v1")
    d.send(DATA, src=sd, dst=high, sp=20101, dp=20101, payload=b"vers haute")
    r = c.recv({DATA})
    check(r and r[2] == high and r[7] == b"vers haute", f"DATA v1 -> adresse {high} de l'agent v2 transmis")
    c.send(BYE)
    d.send(BYE)
    print("CONFORME" if ok else "NON CONFORME")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
