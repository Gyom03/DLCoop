#!/bin/bash
# Installe ou met à jour le relais DLCoop sur un VPS Debian (exécuté SUR le VPS, avec sudo).
# Les fichiers relais.cpp, dlcoop-relais.service et nftables.conf doivent être dans le dossier courant.
#   bash deployer.sh                 # compile, installe, (re)démarre
#   bash deployer.sh --pare-feu      # en plus : installe le pare-feu (retour arrière automatique après 90 s si
#                                    # la connexion SSH est perdue : `sudo systemctl stop nft-retour.timer` pour confirmer)
set -euo pipefail
cd "$(dirname "$0")"

g++ -O2 -std=c++17 -Wall -o dlcoop-relais relais.cpp
sudo install -m 755 dlcoop-relais /usr/local/bin/dlcoop-relais
sudo install -m 644 dlcoop-relais.service /etc/systemd/system/dlcoop-relais.service
if [ ! -f /etc/dlcoop-relais.env ]; then
    motdepasse=$(tr -dc 'a-km-zA-HJ-NP-Z2-9' </dev/urandom | head -c 12 || true)  # SIGPIPE de tr : normal
    echo "RELAIS_MOTS_DE_PASSE=$motdepasse" | sudo tee /etc/dlcoop-relais.env >/dev/null
    sudo chmod 600 /etc/dlcoop-relais.env
    echo "mot de passe de session créé : $motdepasse"
fi
sudo systemctl daemon-reload
sudo systemctl enable --quiet dlcoop-relais
sudo systemctl restart dlcoop-relais

if [ "${1:-}" = "--pare-feu" ]; then
    sudo systemd-run --quiet --on-active=90 --unit=nft-retour /usr/sbin/nft flush ruleset
    sudo install -m 644 nftables.conf /etc/nftables.conf
    sudo /usr/sbin/nft -f /etc/nftables.conf
    sudo systemctl enable --quiet nftables
    echo "pare-feu actif ; retour arrière dans 90 s sauf : sudo systemctl stop nft-retour.timer"
fi
sleep 1
systemctl is-active dlcoop-relais
sudo journalctl -u dlcoop-relais -n 3 --no-pager
