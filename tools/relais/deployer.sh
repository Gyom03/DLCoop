#!/bin/bash
# Installs or updates the DLCoop relay on a Debian VPS (run ON the VPS, with sudo).
# The files relais.cpp, dlcoop-relais.service and nftables.conf must be in the current folder.
#   bash deployer.sh                 # build, install, (re)start
#   bash deployer.sh --pare-feu      # also: install the firewall (automatic rollback after 90 s if the SSH
#                                    # connection is lost: `sudo systemctl stop nft-retour.timer` to confirm)
set -euo pipefail
cd "$(dirname "$0")"

g++ -O2 -std=c++17 -Wall -o dlcoop-relais relais.cpp
sudo install -m 755 dlcoop-relais /usr/local/bin/dlcoop-relais
sudo install -m 644 dlcoop-relais.service /etc/systemd/system/dlcoop-relais.service
if [ ! -f /etc/dlcoop-relais.env ]; then
    motdepasse=$(tr -dc 'a-km-zA-HJ-NP-Z2-9' </dev/urandom | head -c 12 || true)  # SIGPIPE from tr: normal
    echo "RELAIS_MOTS_DE_PASSE=$motdepasse" | sudo tee /etc/dlcoop-relais.env >/dev/null
    sudo chmod 600 /etc/dlcoop-relais.env
    echo "session password created: $motdepasse"
fi
sudo systemctl daemon-reload
sudo systemctl enable --quiet dlcoop-relais
sudo systemctl restart dlcoop-relais

if [ "${1:-}" = "--pare-feu" ]; then
    sudo systemd-run --quiet --on-active=90 --unit=nft-retour /usr/sbin/nft flush ruleset
    sudo install -m 644 nftables.conf /etc/nftables.conf
    sudo /usr/sbin/nft -f /etc/nftables.conf
    sudo systemctl enable --quiet nftables
    echo "firewall active; rollback in 90 s unless: sudo systemctl stop nft-retour.timer"
fi
sleep 1
systemctl is-active dlcoop-relais
sudo journalctl -u dlcoop-relais -n 3 --no-pager
