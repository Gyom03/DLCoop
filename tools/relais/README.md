# Relais Internet DLCoop (coop sans VPN)

Permet de jouer en coop **sans VPN et sans le mode en ligne Steam** : un petit serveur public relaie les paquets du
jeu entre les joueurs. Le jeu reste en mode LAN et ne voit que des adresses locales `127.0.0.N` (même principe que
`tools/fantome/pont.exe`, mais en étoile autour d'un serveur au lieu de deux PC du réseau local).

```
 jeu (127.0.0.1) ─┐                                            ┌─ jeu (127.0.0.2)
 PC du joueur A   │  DLCoop.exe (agent) ──UDP──► relais ◄──UDP── DLCoop.exe (agent)  PC du joueur B
                  └─ occupe 127.0.0.2 au nom de B    (VPS)        occupe 127.0.0.1 au nom de A ─┘
```

- **Joueurs** : rien à ouvrir sur leur box (connexions sortantes seulement ; maintien toutes les 2 s). Dans DLCoop :
  *Server IP* et **mot de passe de session** (le même pour tous).
- **Serveur** : un seul port UDP ouvert (**20300**). Le jeu n'est jamais exposé à Internet.
- Chaque mot de passe = une session isolée de **33 adresses** (32 joueurs + 1 zombie ; 16 pour les lanceurs 1.1). Liste des mots de passe acceptés côté serveur :
  `RELAIS_MOTS_DE_PASSE` (`/etc/dlcoop-relais.env`).

## Fichiers

| Fichier | Rôle |
|---|---|
| `relais.cpp` | serveur (C++17 portable Linux / Windows, aucune dépendance) |
| `dlcoop-relais.service` | service systemd durci (utilisateur temporaire, système en lecture seule, 256 Mo max) |
| `nftables.conf` | pare-feu : entrée fermée sauf SSH (TCP 22) et relais (UDP 20300) |
| `deployer.sh` | compilation + installation sur le VPS (`--pare-feu` : installe aussi le pare-feu, avec retour arrière automatique après 90 s si la connexion SSH est perdue) |
| `Dockerfile` | variante en conteneur (non utilisée sur le VPS actuel) |
| `../dlcoop/Relay.cs` | agent côté joueur (dans le lanceur) |
| `../../tests/relais/test_relais.py` | test du protocole sans le jeu (agents simulés v1 et v2, session mixte) |
| `../../tests/relais/faux_joueur.py` | faux joueur distant : teste un vrai jeu avec un seul PC |

## Serveur

Un petit VPS Linux suffit (testé : 2 vCores, 4 Go, Debian 13 ; ~15 paquets/s pour 2 joueurs, < 1 Mo/min,
650 Ko de mémoire). Conseillé : SSH par clé uniquement (mot de passe et root refusés), mises à jour de sécurité
automatiques (`unattended-upgrades`). Remplacer ci-dessous `<ip_du_vps>`, `<utilisateur>` et `<clé>`.

```powershell
# mise à jour du relais après modification de relais.cpp
scp -i <clé> tools\relais\* <utilisateur>@<ip_du_vps>:dlcoop-relais/
ssh -i <clé> <utilisateur>@<ip_du_vps> "sed -i 's/\r$//' ~/dlcoop-relais/*; bash ~/dlcoop-relais/deployer.sh"
# journal en direct
ssh -i <clé> <utilisateur>@<ip_du_vps> "sudo journalctl -u dlcoop-relais -f"
# test du protocole
python tests\relais\test_relais.py <ip_du_vps> <mot_de_passe>
```

## Protocole

En-tête de 20 octets, petit-boutiste : `magic "DLR1"` (u32), `type` (u8), `src` (u8), `dst` (u8), `aux` (u8),
`srcPort` (u16), `dstPort` (u16), `token` (u64), puis la charge.

| Type | Sens | Contenu |
|---|---|---|
| 1 JOIN | agent → relais | `token` = nonce, `aux` = version (1 ou 2), charge = mot de passe |
| 2 WELCOME | relais → agent | `token` = jeton de session de l'agent, `aux` = version retenue |
| 3 REFUSED | relais → agent | `aux` = 1 mot de passe, 2 relais plein, 3 version ; `token` = nonce |
| 4 CLAIM | agent → relais | `src` = adresse souhaitée (0xFF : la première libre), `aux` = n° de requête |
| 5 SLOT | relais → agent | `src` = adresse (0xFF : session pleine), `aux` = n° de requête |
| 6 RELEASE | agent → relais | `src` = adresse libérée |
| 7 KEEPALIVE | agent → relais | toutes les 2 s ; v2 : charge vide de 16 octets |
| 8 ROSTER | relais → agent | `srcPort` = masque des adresses actives, `dstPort` = masque de celles de l'agent (16 bits ; poussé à chaque changement) ; v2 : charge de 16 octets = les deux masques sur 64 bits (seulement si la requête était aussi grande) |
| 9 DATA | les deux sens | `src`, `dst`, `srcPort`, `dstPort` + paquet du jeu ; le relais vérifie que l'émetteur détient `src` |
| 10 BYE | agent → relais | départ |

Versions : **v1** (lanceur 1.1) = adresses 0..15 ; **v2** (lanceur 1.2+) = 0..32. Un agent v1 n'obtient que des
adresses 0..15 et ne reçoit que leurs paquets : les anciens lanceurs jouent entre eux comme avant, même dans une session
où des v2 occupent 16..32. Le lanceur v2 refusé par un relais v1 (`REFUSED` raison 3) se replie sur la v1.

Sécurité : aucun paquet sans jeton valide n'obtient de réponse (sauf JOIN, limité à 10 par seconde et par IP) ; les
réponses ne sont jamais plus grosses que les requêtes (pas d'amplification) ; débit limité par agent (8 000 paquets
et 8 Mo par seconde : un PC peut faire tourner plusieurs instances) ; agent silencieux 15 s = retiré. Le mot de passe circule en clair : il sépare les groupes, ce
n'est pas un secret fort.

Reprises : relais redémarré → l'agent ne reçoit plus rien, se reconnecte au bout de 10 s et redemande les mêmes
adresses (le jeu garde la sienne) ; RELEASE perdu → répété d'après le ROSTER.
