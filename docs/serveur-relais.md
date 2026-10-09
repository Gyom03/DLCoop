# Le serveur relais (coop par Internet sans VPN)

Sources : `tools/relais/`.

Permet de jouer en coop **sans VPN et sans le mode en ligne Steam** : un petit serveur public relaie les paquets du
jeu entre les joueurs. Le jeu reste en mode LAN et ne voit que des adresses locales `127.0.0.N` (même principe que le
pont entre deux PC d'un réseau local, `tools/fantome/pont.exe`, mais en étoile autour d'un serveur).

```
 jeu (127.0.0.1) ─┐                                           ┌─ jeu (127.0.0.2)
 PC du joueur A   │  DLCoop (agent) ──UDP──► relais ◄──UDP── DLCoop (agent)  PC du joueur B
                  └─ occupe 127.0.0.2 au nom de B   (VPS)      occupe 127.0.0.1 au nom de A ─┘
```

- **Joueurs** : rien à ouvrir sur leur box (connexions sortantes seulement, maintien toutes les 2 s). Dans DLCoop :
  *Server IP* et **mot de passe de session** (le même pour tous).
- **Serveur** : un seul port UDP ouvert (**20300**). Le jeu n'est jamais exposé à Internet.
- Chaque mot de passe = une session isolée de **33 adresses** (32 joueurs + 1 zombie).

## Installer sur un VPS

Un petit VPS Linux suffit largement (testé : 2 vCores, 4 Go, Debian 13 ; ~15 paquets/s pour 2 joueurs, moins de
1 Mo/min, 650 Ko de mémoire). Copier le dossier `tools/relais/` sur le serveur et lancer le script
d'installation ; il compile, installe le service systemd, le (re)démarre et crée un mot de passe de session au premier
passage.

```bash
# sur le VPS, dans le dossier contenant relais.cpp, dlcoop-relais.service, nftables.conf
bash deployer.sh                 # compile, installe, (re)démarre le service
bash deployer.sh --pare-feu      # en plus : pare-feu nftables (retour arrière automatique après 90 s si la
                                 # connexion SSH est perdue ; confirmer avec « systemctl stop nft-retour.timer »)
```

Depuis un poste Windows (remplacer `<clé>`, `<utilisateur>`, `<ip_du_vps>`) :

```powershell
scp -i <clé> tools\relais\* <utilisateur>@<ip_du_vps>:dlcoop-relais/
ssh -i <clé> <utilisateur>@<ip_du_vps> "sed -i 's/\r$//' ~/dlcoop-relais/*; bash ~/dlcoop-relais/deployer.sh"
ssh -i <clé> <utilisateur>@<ip_du_vps> "sudo journalctl -u dlcoop-relais -f"   # journal en direct
```

Mots de passe acceptés : variable `RELAIS_MOTS_DE_PASSE` (fichier `/etc/dlcoop-relais.env`, ou `--mots-de-passe a,b`).
Vide : tout mot de passe ouvre sa propre session. Le mot de passe n'apparaît jamais dans le journal.

Conseils de durcissement (déjà dans le service et le pare-feu fournis) : SSH par clé uniquement (mot de passe et root
refusés), mises à jour de sécurité automatiques, service en utilisateur temporaire et système en lecture seule, entrée
fermée sauf SSH (TCP 22) et relais (UDP 20300).

Variante en conteneur (`Dockerfile`) :

```bash
docker build -t dlcoop-relais tools/relais
docker run -d --restart unless-stopped -p 20300:20300/udp -e RELAIS_MOTS_DE_PASSE=... dlcoop-relais
```

## Fichiers

| Fichier | Rôle |
|---|---|
| `relais.cpp` | le serveur (C++17 portable Linux / Windows, sans dépendance) |
| `deployer.sh` | compilation + installation sur le VPS |
| `dlcoop-relais.service` | service systemd durci |
| `nftables.conf` | pare-feu (entrée fermée sauf SSH et UDP 20300) |
| `Dockerfile` | variante en conteneur |
| `../dlcoop/Relay.cs` | l'agent, côté joueur (dans le lanceur) |
| `../../tests/relais/test_relais.py` | test du protocole sans le jeu (agents simulés) |

## Protocole

En-tête de 20 octets, petit-boutiste : `magic "DLR1"`, `type`, `src`, `dst`, `aux`, puis des champs selon le type.
L'agent **JOIN** avec le mot de passe, reçoit un **WELCOME** (jeton de session), demande une adresse (**CLAIM** →
**SLOT**), envoie un **KEEPALIVE** toutes les 2 s (maintient aussi le passage dans sa box), et échange des **DATA**
relayés vers l'agent qui détient l'adresse destinataire. Le relais pousse un **ROSTER** (adresses actives de la session).
Protocole **v2** : 33 adresses (un ancien relais v1 à 16 adresses est accepté, l'agent se replie sur v1). Détail complet
dans `tools/relais/README.md` et en tête de `tools/relais/relais.cpp`.
