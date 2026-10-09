"""Fake remote player to test a real game launched by DLCoop in relay mode, with a single PC.

    python tests/relais/faux_joueur.py <ip[:port]> <password> [duration_s=120]

Joins the session, takes an address, keeps alive, and counts the DATA packets received from the session's games
(src, destination port, size). The local game sees this address as a present instance (the DLCoop agent
holds its lock and ports) and so must send it its discovery announcements (port 20111) through the relay.
"""
import collections
import os
import socket
import struct
import sys
import time

MAGIC = 0x31524C44
JOIN, WELCOME, REFUSED, CLAIM, SLOT, RELEASE, KEEPALIVE, ROSTER, DATA, BYE = range(1, 11)
HDR = struct.Struct("<IBBBBHHQ")


def main():
    host, _, port = sys.argv[1].partition(":")
    server = (socket.gethostbyname(host), int(port or 20300))
    password = sys.argv[2].encode()
    duration = float(sys.argv[3]) if len(sys.argv) > 3 else 120
    s = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    s.settimeout(0.5)
    nonce = int.from_bytes(os.urandom(8), "little")
    token = slot = None
    while token is None:
        s.sendto(HDR.pack(MAGIC, JOIN, 0, 0, 1, 0, 0, nonce) + password, server)
        try:
            d, _ = s.recvfrom(65536)
        except socket.timeout:
            continue
        m, t, src, dst, aux, sp, dp, tok = HDR.unpack_from(d)
        if t == REFUSED:
            sys.exit("refused")
        if t == WELCOME:
            token = tok
    while slot is None:
        s.sendto(HDR.pack(MAGIC, CLAIM, 0xFF, 0, 7, 0, 0, token), server)
        try:
            d, _ = s.recvfrom(65536)
        except socket.timeout:
            continue
        m, t, src, dst, aux, sp, dp, tok = HDR.unpack_from(d)
        if t == SLOT and aux == 7:
            slot = src
    print(f"faux joueur : adresse {slot} (127.0.0.{slot + 1} chez les autres)", flush=True)
    counts = collections.Counter()
    sizes = collections.defaultdict(int)
    first = {}
    end = time.time() + duration
    last_keep = 0
    while time.time() < end:
        if time.time() - last_keep > 2:
            last_keep = time.time()
            s.sendto(HDR.pack(MAGIC, KEEPALIVE, 0, 0, 0, 0, 0, token), server)
        try:
            d, _ = s.recvfrom(65536)
        except socket.timeout:
            continue
        m, t, src, dst, aux, sp, dp, tok = HDR.unpack_from(d)
        if t == ROSTER:
            continue
        if t == DATA:
            key = (src, sp, dp)
            if key not in first:
                first[key] = time.strftime("%H:%M:%S")
                print(f"{first[key]} premier paquet du jeu {src} (127.0.0.{src + 1}:{sp}) vers mon port {dp}, "
                      f"{len(d) - HDR.size} octets", flush=True)
            counts[key] += 1
            sizes[key] += len(d) - HDR.size
    s.sendto(HDR.pack(MAGIC, BYE, 0, 0, 0, 0, 0, token), server)
    print("bilan :")
    for key, n in sorted(counts.items()):
        print(f"  jeu {key[0]} port {key[1]} -> port {key[2]} : {n} paquets, {sizes[key]} octets")
    if not counts:
        print("  no packet received")


if __name__ == "__main__":
    main()
