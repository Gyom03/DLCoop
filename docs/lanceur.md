# Le lanceur DLCoop

`DLCoop.exe` (sources : `tools/dlcoop/`) est une petite fenêtre WPF sur le .NET Framework 4.7.2, intégré à Windows 10
(1803+) et 11 : rien à installer. Il **installe** le mod dans le dossier du jeu, **écrit les réglages** et **lance** le
jeu. Interface en anglais.

## La fenêtre

| Zone | Rôle |
|---|---|
| **Game location** | trouvé automatiquement (registre « Steam App 239140 », `libraryfolders.vdf`, `appmanifest_239140.acf`), vérifié à chaque ouverture ; « Change… » pour choisir `DyingLightGame.exe`. Le mod n'agit qu'en **1.55**. |
| **Install mod / Uninstall mod** | copie ou retire les fichiers du mod dans le dossier du jeu (voir plus bas). État : *Mod installed ✓*, *Update available*, *Mod not installed*. |
| **Server IP / Session password** | adresse du relais Internet et mot de passe de la session (le même pour tous). Vide : réseau local ou VPN de type LAN. |
| *Player name* | « Hide my Steam name » : le jeu affiche un pseudo choisi ou tiré au hasard. |
| *Instance* | instance principale (ton compte Steam) ou **instance supplémentaire** (2e, 3e… fenêtre sur le même PC). « Several instances on this PC » : adresses locales en réseau local. |
| *More options* | « Max players when I host » (2 à 32, le joueur zombie en plus), « Disable the EOS connection » (conseillé), « Close after launch ». |

Les choix sont mémorisés dans `%APPDATA%\DLCoop\settings.txt` (clé=valeur). Les choix d'instance reviennent aux valeurs
par défaut à chaque ouverture.

## Installation dans le dossier du jeu

**Install mod** copie trois fichiers, posés à côté de `DLCoop.exe`, dans le dossier du jeu :

| Fichier | Rôle |
|---|---|
| `winmm.dll` | le **chargeur** : le jeu le charge lui-même au démarrage, il charge à son tour les deux DLL ci-dessous (voir [chargement des mods](chargement-des-mods.md)) |
| `dlcoop_reseau.dll` | réseau (adresses `127.0.0.N`, correctif LAN de la 1.55), identité Steam par instance, pseudo |
| `coop6.dll` | le mod coop (limites du jeu levées) |

L'état affiché vient d'une **comparaison octet par octet** de ces trois fichiers avec ceux du dossier du jeu : tous
identiques → *Mod installed ✓* ; `winmm.dll` présent mais un fichier différent → *Update available* (Launch ou Install
met à jour) ; `winmm.dll` absent → *Mod not installed*. Les DLL sont compilées de façon **reproductible** : une nouvelle
version de DLCoop ne signale une mise à jour que si le mod a réellement changé.

Si un jeu est déjà ouvert, ses DLL sont verrouillées : le lanceur garde alors les fichiers installés (la nouvelle
instance charge les mêmes) et la mise à jour se fait au lancement suivant, jeux fermés.

**Uninstall mod** retire les trois DLL et `dlmods.ini`. DLML (`dsound.dll`) n'est jamais touché.

## Lancement

À chaque **Launch** :

1. installation ou mise à jour si nécessaire ;
2. choix du numéro d'instance (0 = principale) et, avec le relais, demande d'une adresse au serveur ;
3. réglages posés dans l'**environnement** du jeu (hérité par le processus lancé) :

   | Variable | Lue par | Sens |
   |---|---|---|
   | `DLF_INDEX` | dlcoop_reseau | numéro d'instance (0 = principale, compte Steam réel) |
   | `DLF_LOOPBACK`, `DLF_ADRESSE` | dlcoop_reseau | jeu sur `127.0.0.(N+1)` (plusieurs instances, relais) |
   | `DLF_NOM`, `DLF_LOG` | dlcoop_reseau | pseudo affiché, journal réseau (`logs\` à côté de DLCoop.exe) |
   | `COOP6_Places` | coop6 | places du lobby quand on héberge = max joueurs + 1 (zombie) |
   | `COOP6_SansEos`, `COOP6_Difficulte`, `COOP6_Couleurs`, `COOP6_Hud`, `COOP6_Diagnostic` | coop6 | options du mod |

4. les mêmes réglages sont **écrits dans `dlmods.ini`** (dossier du jeu), pour un lancement simple : instance
   principale, sans adresse de relais. Le chargeur les applique seulement s'ils ne sont pas déjà dans l'environnement :
   lancé par DLCoop, les valeurs par instance priment ; lancé par **Steam**, le mod marche quand même avec ces réglages ;
5. réparation du son si besoin : `Documents\DyingLight\out\settings\audio.scr` (partagé par toutes les instances) peut
   contenir `API("NoSound")`, écrit par une instance lancée avec `/nosound` et relu ensuite par tous les lancements ;
   le lanceur remet `API("FMOD")` (valeur du moteur) ;
6. lancement **normal** de `DyingLightGame.exe -nologos` (aucune écriture dans la mémoire du jeu par le lanceur),
   puis attente (10 s au plus) que les deux DLL soient chargées dans le jeu.

## Relais Internet et instances supplémentaires

Avec un *Server IP*, le lanceur devient l'**agent du relais** de ce PC (`Relay.cs`) : il se connecte au serveur, obtient
une adresse `0..32` par jeu lancé et, pour chaque joueur distant, occupe localement son adresse `127.0.0.N` : le jeu
croit jouer en réseau local. Il doit **rester ouvert** pendant la partie (« Close after launch » le réduit seulement ;
il se ferme seul quand le dernier jeu est fermé). Détails : [serveur relais](serveur-relais.md).

Un seul agent par PC (verrou `Local\DLCoop_relais`). Si l'on rouvre DLCoop pendant une partie (raccourci) pour lancer une
instance supplémentaire, la nouvelle fenêtre **transmet sa demande à l'agent déjà ouvert** par un tube nommé local
(`LaunchPipe.cs`) ; c'est lui qui lance le jeu et le suit.

## Ligne de commande

```
DLCoop.exe /launch [/instance N] [/multi] [/name Pseudo] [/game chemin\DyingLightGame.exe]
           [/internet | /lan] [/server adresse] [/password X] [/maxplayers N] [/count N]
```

Sans fenêtre, avec les réglages mémorisés pour le reste. `/count N` : N jeux à 10 s d'écart (les suivants en instances
supplémentaires). Code de sortie 0 : jeu(x) lancé(s) avec le mod chargé. Avec le relais, le processus reste actif tant
qu'un jeu lancé tourne.

## Construire le paquet

`tools\dlcoop\build.ps1 [-Relais <ip>]` compile les trois DLL et le lanceur et produit `out\dlcoop\paquet\` +
`DLCoop_<version>.zip` (`-Relais` : adresse préremplie dans `server.txt` pour un groupe ; le mot de passe n'est jamais mis
dans le paquet). Voir [compilation](compilation.md).
