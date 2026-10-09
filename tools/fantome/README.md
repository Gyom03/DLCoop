# Joueurs fantômes : plusieurs instances de Dying Light en coop LAN sur un seul PC

But : tester la coop (et plus tard le mod « 6 joueurs ») **sans réunir d'autres joueurs** : on lance plusieurs
instances du jeu sur le même PC, chacune avec sa propre identité et sa propre adresse réseau, et elles se
rejoignent en LAN.

**État au 2026-10-08 : coop à 5 joueurs humains validée sur un seul PC avec le mod `mods/coop6`** (4 sans mod,
validée le 2026-10-07) (hôte + 3 fantômes, même quête, chaque
instance voit les 3 autres dans sa liste de joueurs).

Ce document permet de reproduire, maintenir et reprendre le travail. Pour les joueurs, le lanceur DLCoop
(`tools/dlcoop`) fait tout cela sans script : voir `docs/`.

---

## 1. Démarrage rapide

### Prérequis
- Dying Light **1.55** (Steam), installé dans `C:\Program Files (x86)\Steam\steamapps\common\Dying Light` (modifiable dans
  `fantome.ini`). **L'installation n'est jamais modifiée.**
- MSVC 14.16 (Build Tools 2022, toolset v141), comme le reste du projet.
- **Steam ouvert et en mode hors ligne** (menu Steam → « Passer en mode hors ligne ») *avant* de lancer.
  Si Steam est fermé, la protection SteamStub de l'exe lance `steam.exe steam://run/239140//` et s'arrête :
  Steam relance alors le jeu **sans** notre DLL (et peut-être en ligne).
- Mémoire : ~3,3 Go de mémoire engagée par instance au menu (voir §6). Fermer les programmes gourmands ;
  idéalement redémarrer le PC avant une session à 4.

### Compiler
```powershell
.\tools\fantome\build.ps1            # -> out\tools\fantome\ : fantome.exe, fantome.dll, fantome.ini, mosaique.ps1
```
La compilation échoue (LNK1104) si une instance du jeu tourne : elle verrouille `fantome.dll`.

### Lancer
Le plus simple : `.\lancer.ps1 -Fantomes 5 -Ecart 10 -SansEspace` (hôte puis fantômes 1..N toutes les 10 s, puis
mosaïque ; sans `-SansEspace`, Espace est envoyé dans chaque fenêtre 20 s après son ouverture par `espace.ps1`).
À la main, depuis `out\tools\fantome\`, un par un :
```powershell
.\fantome.exe hote     # instance 0 : l'hôte (vraie identité Steam, son actif) -> 127.0.0.1
.\fantome.exe          # fantôme 1 (premier numéro libre)                     -> 127.0.0.2
.\fantome.exe          # fantôme 2                                             -> 127.0.0.3
.\fantome.exe          # fantôme 3                                             -> 127.0.0.4
```
Le jeu est toujours lancé avec `-nologos` (passe les vidéos d'introduction) ; les
fantômes reçoivent en plus `/nosound -texStreamMemMB 512`.

Puis ranger les fenêtres : `.\mosaique.ps1` (2 × 2 pour 4 instances, `-Colonnes 3` pour 5-6). Les fenêtres sont
rangées **par numéro d'instance** (0 = hôte en haut à gauche ; pid lu dans `journaux\instance_N.log`) et leur titre
devient « Dying Light - instance 0 (hôte) » / « Dying Light - fantôme N ». Voir §5.6 : le jeu ne se laisse pas
redimensionner, il faut une résolution adaptée dans ses options vidéo (fenêtré).

Deux PC (au-delà de ~9 instances par PC) : sur chaque PC, `pont.exe` tient les verrous `Local\DLFantome_slot_N`
des instances de l'autre PC (fantome.dll leur envoie donc ses annonces), occupe leurs adresses
127.0.0.(N+1):20101/20111 et relaie les paquets UDP vers le pont de l'autre PC, qui les réémet depuis l'adresse
de l'émetteur : le jeu voit ce qu'il verrait sur un seul PC. Maintien toutes les 2 s sur le même port des deux
côtés : pas de règle de pare-feu à créer (réseau en profil Privé). Ex. PC 1 : `pont.exe 192.168.1.20 7-15` ;
PC 2 : `pont.exe 192.168.1.10 0-6`, puis `fantome.exe 7`, `8`… (33 instances au plus, numéros 0..32 : 32 joueurs + 1 zombie).

Mods chargés au démarrage : clé `Mods=` de `fantome.ini` (DLL séparées par « ; », chemins relatifs à
`fantome.exe`), une APC `LoadLibraryW` chacune après `fantome.dll`. Ex. coop à 6 : `Mods=..\..\mods\coop6\coop6.dll`
(`mods/coop6/README.md`). Gels et plantages au démarrage à 5+ instances (« seul un redémarrage du PC répare ») :
connexion EOS du jeu, qui demande un ticket Steam ; deux fils s'inscrivent / se désinscrivent en même temps dans le
registre de rappels de steam_api64, non protégé → boucle infinie. Le mod coop6 désactive EOS (`SansEos=1`, E1) ;
depuis, 10 s d'écart suffisent sans redémarrage.

### Jouer
1. Dans la fenêtre de l'hôte : lancer/charger une partie coop (Steam hors ligne → partie LAN, §4.2).
2. Dans chaque fenêtre de fantôme : liste des parties → rejoindre la partie de l'hôte.
3. En cas de problème : lire `out\tools\fantome\journaux\instance_N.log` (une ligne par événement réseau
   nouveau ; voir §7).

### Tester sans le jeu
```powershell
.\tools\fantome\tests\run.ps1        # aucune instance du jeu ouverte (les verrous d'emplacement seraient pris)
```
Compile un faux jeu (`tests/faux_jeu.cpp`), lance un « hôte » et un « fantôme » via `fantome.exe`, et vérifie :
DLL chargée avant `main`, verrou du jeu renommé, binds 127.0.0.1:20101 et 127.0.0.2:20101 simultanés, port 0
corrigé, diffusion reçue, réponse vue comme `127.0.0.1:0` puis, après un envoi au vrai port, comme
`127.0.0.1:20101`. Sortie attendue dans `out\tests\fantome\fantome1.txt` :
```
reponse "reponse" de 127.0.0.1:0
reponse "reponse" de 127.0.0.1:20101
```

---

## 2. Fichiers

| Fichier | Rôle |
|---|---|
| `src/fantome.cpp` | Lanceur `fantome.exe` : lit `fantome.ini`, choisit le numéro, prépare l'environnement, crée le jeu en pause, injecte la DLL (APC), relâche. |
| `src/fantome_dll.cpp` | `fantome.dll` : tous les crochets (verrou, Steam, réseau). Uniquement par tables d'imports (IAT). |
| `build.ps1` | Compilation MSVC 14.16 (`/MT`, CRT statique) → `out\tools\fantome\` (paramètre `-Out`). |
| `fantome.ini.exemple` | Réglages par défaut (copié en `fantome.ini` à la 1re compilation). |
| `mosaique.ps1` | Range les fenêtres du jeu en grille (ordre de lancement ; `-Colonnes`, `-SansBordure`). |
| `lancer.ps1` | Lance hôte + N fantômes (`-Fantomes`, `-Ecart` s, `-SansHote`, `-SansEspace`), puis la mosaïque. |
| `espace.ps1` | Envoie Espace dans les fenêtres du jeu (`-Processus pid,…`) : premier plan puis `SendInput`. |
| `src/pont.cpp` → `pont.exe` | Relie les instances de deux PC : `pont.exe <ip_autre_pc> <instances_distantes> [port 20200]` (voir ci-dessous). |
| `tests/faux_jeu.cpp`, `tests/run.ps1` | Test hors jeu de toute la chaîne. |

Sorties (non versionnées) : `out\tools\fantome\` (binaires, `fantome.ini`, `journaux\instance_N.log`).

### fantome.ini
| Clé | Défaut | Effet |
|---|---|---|
| `Jeu` | `C:\Program Files (x86)\Steam\…\DyingLightGame.exe` | Exe lancé (jamais modifié). |
| `Bouclage` | `1` | Une adresse 127.0.0.(N+1) par instance sur les ports du jeu. Lancer l'hôte avec `fantome.exe hote`. |
| `SonFantomes` | `0` | `0` = `/nosound` pour les fantômes. |
| `Arguments` | vide | Ajoutés à toutes les instances (`-nologos` est toujours ajouté). |
| `ArgumentsFantomes` | `-texStreamMemMB 512` | Ajoutés aux fantômes seulement. |
| `Mods` | vide | DLL chargées au démarrage après fantome.dll (« ; », relatives à fantome.exe). |

### Environnement transmis au jeu (par fantome.exe)
`SteamAppId=239140`, `SteamGameId=239140` (lancement hors client Steam), `DLF_INDEX` (0 = hôte, 1..7),
`DLF_LOOPBACK` (0/1), `DLF_LOG` (chemin du journal).

---

## 3. Fonctionnement

### 3.1 Injection avant le point d'entrée (« early bird »)
`fantome.exe` : `CreateProcessW(…, CREATE_SUSPENDED)` → `VirtualAllocEx` + `WriteProcessMemory` (chemin de la
DLL) → `QueueUserAPC(LoadLibraryW, fil principal)` → `ResumeThread`. L'APC s'exécute à la fin de
l'initialisation du chargeur de Windows : **toutes les DLL importées statiquement sont chargées et leurs
imports résolus** (l'exe importe `gamedll`, `engine`, `filesystem` ; `engine` importe `steam_api64` et
`WS2_32`), mais le point d'entrée de l'exe (stub SteamStub) n'a pas encore tourné et `SteamAPI_Init` n'a pas
encore été appelé. On peut donc tout faire par simple remplacement d'entrées d'IAT, sans réécrire de code.
Le lanceur attend jusqu'à 10 s que `fantome.dll` apparaisse dans les modules du processus.

### 3.2 Crochets de fantome.dll
Tous posés en remplaçant, dans les IAT de l'exe, `engine_x64_rwdi.dll`, `gamedll_x64_rwdi.dll` et
`filesystem_x64_rwdi.dll`, **toute entrée égale à l'adresse réelle** de la fonction (et non par nom) : couvre
les imports par numéro d'ordre (engine importe WS2_32 presque entièrement par ordinal : `recvfrom`=17,
`sendto`=20, `bind`=2…).

| Crochet | Instances | Effet |
|---|---|---|
| `kernel32!CreateMutexA` (2 entrées) | fantômes | `Global\ChromeEngine4DIMutex` → `…_fantomeN` : plus de « Dying Light is already running ». |
| `steam_api64!SteamInternal_FindOrCreateUserInterface` (engine) | fantômes | Renvoie un **intermédiaire** pour `SteamUser0xx`, `SteamFriends0xx`, `STEAMREMOTESTORAGE_INTERFACE_VERSION0xx`. |
| `ws2_32!bind` | toutes | Ports 20101…20120 et 20111…20130 liés à 127.0.0.(N+1) (si `Bouclage=1`) ; `SIO_UDP_CONNRESET` désactivé. |
| `ws2_32!sendto` / `WSASendTo` | toutes | Port de destination 0 → 20101 ; diffusion vers les ports du jeu → envoi direct à chaque instance vivante ; suivi de la forme d'adresse par pair. |
| `ws2_32!recvfrom` | toutes | Paquet reçu (r > 0) d'un pair que le jeu adresse au port 0 → source présentée avec le port 0. |

À la fin de l'installation, la DLL crée `Local\DLFantome_slot_N` (tenu toute la vie du processus) : le lanceur
s'en sert pour choisir le numéro libre, la DLL pour savoir quelles instances sont vivantes.

### 3.3 Intermédiaires Steam (aucune écriture dans la mémoire de Steam)
Un intermédiaire est un petit objet `{vftable, vrai objet}` à nous. Sa vftable (256 places) pointe vers des
**relais** générés (16 octets chacun : `mov rcx,[rcx+8] ; mov rax,[rcx] ; jmp [rax+i*8]`) qui transmettent
l'appel au vrai objet Steam, sauf quelques places redirigées :

| Interface | Place | Méthode | Remplacement |
|---|---|---|---|
| `SteamUser023` | 2 | `GetSteamID` | Numéro de compte (32 bits bas) `^ (0x5A000000 \| N)` ; univers/type/instance intacts. |
| `SteamFriends017` | 0 | `GetPersonaName` | `"Fantome N"`. |
| `STEAMREMOTESTORAGE_INTERFACE_VERSION016` | 0 | `FileWrite` | Ignoré (renvoie vrai, journalisé). |
| idem | 5 | `FileForget` | Ignoré. |
| idem | 6 | `FileDelete` | Ignoré. |

Places et convention vérifiées dans les fonctions « plates » de `steam_api64.dll` (dossier du jeu) :
`SteamAPI_ISteamUser_GetSteamID` = `lea rdx,[rsp+30h] ; call [rax+10h] ; mov rax,[rax]` → place 2, **CSteamID
renvoyé par pointeur caché** (rdx) ; `SteamAPI_ISteamFriends_GetPersonaName` = `jmp [rax]` ;
`…RemoteStorage_FileWrite` = `jmp [rax]`, `FileWriteAsync` = `[rax+10h]`, `FileForget` = `[rax+28h]`,
`FileDelete` = `[rax+30h]`. Place 0 de RemoteStorage confirmée côté moteur par
`CLobbyStorageSteam::CloseFile` (Linux 0xd0ae48 : `call *(%rax)`).

Résultat mesuré : `SteamID présenté au jeu : <SteamID dérivé> (vrai : <SteamID réel>)` (fantôme 1 ; puis les suivants
pour 2 et 3) ; pseudos « Fantome N » affichés dans les HUD des autres joueurs.

---

## 4. Découvertes (code machine)

Adresses : `gamedll`/`engine` = DLL Windows 1.55 ; « Linux » = `libgamelib.so` / `libengine.so`
(version Linux native, noms C++ complets, très proche de la 1.55).

### 4.1 Steam et EOS
- `engine` importe `steam_api64.dll` mais n'utilise que `SteamUser023`, `SteamFriends017`, `SteamUtils010`,
  `STEAMAPPS…008`, `STEAMUSERSTATS…012`, `STEAMREMOTESTORAGE…016`, `SteamController008` : compte, amis,
  présence/invitations (`GameRichPresenceJoinRequested`), succès, stats, cloud. **Ni SteamMatchmaking ni
  SteamNetworking.**
- Lobby en ligne = **EOS** (`EOS_Lobby_*`), transport en ligne = `EOS_P2P_*` (`gamedll` importe
  `EOSSDK-Win64-Shipping.dll`). Fabrique : `CEosFactory<CLobbyFactorySteam>`.
- Identité du joueur (`CLobbyProfileSteam::GetPlayerId`, Linux 0xcfaee0) → `SPlayerIdEos` : PUID EOS
  (0x21 octets, **vide si pas de connexion EOS**), SteamID 64 bits à +0x21 (copié de profil+0x98), type de
  compte 1 (Steam) à +0x44. Steam hors ligne → pas d'EOS → seule la SteamID distingue les instances.
- Sauvegardes : cloud Steam, `Steam\userdata\<compte>\239140\remote\out\save\save_coop_N.sav` et
  `remote\out\profiles\default.pro`, écrits par `ISteamRemoteStorage::FileWrite`.

### 4.2 Mode LAN
- `CLobby::CreateSessionObject` (Linux 0xcb4b30) : mode 0 = solo, 1 = en ligne (EOS), 2 = **LAN**
  (`CLobbySessionLan`) ; `CLobby::IsModeAvailable` renvoie toujours vrai. Classes LAN présentes dans engine
  Windows (`CLobbySessionLan`, `CLobbyScannerLan`).
- gamedll `0x1804b3042` (dans `SessionDI`, `ValidateLobbyInfo` inliné) : mode 1 et utilisateur **non
  connecté** → mode 2. C'est pour ça que « Steam hors ligne » donne des parties LAN.
- `CInetAddrMultiPlatform::IsLANAddress` (Linux 0xd764f0) : 10.x, **127.x**, 172.16–31.x, 192.168.x
  acceptés → les adresses de bouclage 127.0.0.N fonctionnent **sans configuration système**.

### 4.3 Ports
| Rôle | Port | Si occupé |
|---|---|---|
| Jeu en LAN | UDP **20101** | +1 et nouvel essai, jusqu'à 100 fois (`Net::CLocalClient::InitializeSocket`, Linux 0xd35490) |
| Découverte LAN | UDP **20111** | +1, **4 essais** (`CLobbyScannerLan::InitializeSocket`, Linux 0xd076e0) ; annonces en diffusion vers 20111 |
| En ligne | 0 (choisi par Windows) | — |

Table des ports : `CLobbyConnectionsEos::GetLocalPort` (Linux table 0xf3e8d0 = `{0, 20101, 20111, -1, 2}` ;
Windows : `mov eax,20101/20111 ; ret` vers `engine 0x1806c8daf`). **Le port de jeu n'est jamais échangé** :
le jeu suppose que tous les pairs sont sur 20101. D'où le choix « une IP par instance, même port » au lieu de
« un port par instance ».

### 4.4 Défaut « port 0 » de la 1.55 (connu de l'utilisateur : dépôt `Gyom03/DL-LAN-join-fix`)
- La réponse de découverte ne contient pas le port de jeu ; le client envoie sa demande de connexion vers
  **IP:0** → ICMP « port inaccessible ». Le correctif Frida de l'utilisateur réécrivait le port à l'envoi.
- Cause côté code : `CInetAddr::CInetAddr(SGameAddressEos const&)` (Linux 0xd727c0) construit **toujours** un
  `CInetAddrEos`. `SGameAddressEos` : IPv4 à +0x48, port à +0x4c, PUID texte à +0x90, indicateur à +0x44.
  Point exact où le port devient 0 : **pas encore établi** (piste : `CLobbyScannerLan::ProcessGamesScanResponse`,
  Linux 0xd062e0, adresse construite depuis l'expéditeur via `CInetAddr` virtuel +0x80).
- Second effet, découvert ici : corriger l'envoi ne suffit pas. Le moteur **mémorise** le pair sous `IP:0` et
  ne reconnaît pas ses réponses venant de `IP:20101` (`Net::CLocalClient::FindSender`) → abandon après ~17 s
  (« connexion impossible »). D'où le crochet `recvfrom` qui remet le port 0 dans la source.
- Le même pair peut être connu sous `IP:0` dans une session et `IP:20101` dans la suivante (adresse transmise
  par un autre joueur) : la DLL suit la **dernière forme utilisée à l'envoi**, pair par pair
  (`NotePeerForm`), jamais une décision définitive.
- Réception moteur : `recvfrom` + `select` uniquement (pas d'IOCP) ; le crochet ne touche que les appels qui
  ont réellement reçu un paquet (le moteur interroge le socket en boucle).

### 4.5 Lancement, verrous, options
- `DyingLightGame.exe` est protégé par **SteamStub** (section `.bind`, `.text` chiffré sur disque, point
  d'entrée dans `.bind`) : pas de correctif possible sur fichier ; tout se fait à l'exécution.
- Verrou d'instance unique : l'exe appelle `CreateMutexA("Global\ChromeEngine4DIMutex")` (message
  « Dying Light is already running »). L'`OpenMutexA` d'engine concerne `ce6-baker-mutex` (outil).
- Options lues par `CGame::Initialize` (engine) : `/norender` (`StrStrIA` sur la ligne de commande, Linux
  0x81e20c, passé à `r_Initialize`), `/nosound` (vérifié : `API("NoSound")` dans le journal du jeu),
  `-texStreamMemMB <Mo>` (`strncmp(arg,"-texStreamMemMB",15)`, valeur = argument suivant, Linux 0x81ea02),
  `-nologos`, `-video_mode_performance/balanced/quality`, `-port`, `-multimon`. Le « 16 joueurs » de
  `Data0/data/defaultserversettings.scr` (`MaxPlayers(16)`, « CoJ server ») est un reliquat de Call of Juarez,
  non lu par la coop.

---

## 5. Problèmes rencontrés et solutions (dans l'ordre)

1. **Steam fermé** → SteamStub lance Steam et s'arrête ; Steam relance le jeu sans DLL. → Steam ouvert hors ligne.
2. **Le fantôme trouve la partie mais ne la rejoint pas** (paquets dans les deux sens, abandon 17 s après) →
   §4.4, crochet `recvfrom`.
3. **Diffusion remplacée par des envois vers des 127.0.0.N sans instance** → « port inaccessible » →
   `WSAECONNRESET` au `recvfrom` suivant (une vraie diffusion ne fait jamais ça ; trouvé par le test hors jeu).
   → envois limités aux instances vivantes + `SIO_UDP_CONNRESET` désactivé.
4. **Après une 2e session, l'hôte ne peut plus rejoindre** (il appliquait le port 0 à un pair désormais connu
   sous son vrai port) → forme d'adresse suivie pair par pair.
5. **La 4e instance plante au démarrage** (2 fois, `VCRUNTIME140.dll+0x1c177`, c0000005, ~10 s après le
   lancement) → mémoire engagée du système presque épuisée (§6) → `-texStreamMemMB 512` pour les fantômes,
   lancements espacés, PC redémarré : 4 instances stables ensuite. Cause **non prouvée** (une fois avec 10 Go
   encore libres) : surveiller.
6. **Fenêtres non redimensionnables de l'extérieur** : style `0x14C80000` (titre, pas de bord épais), client
   forcé à la résolution du jeu (2560 × 1415 malgré `SetWindowPos`). `mosaique.ps1` ne fait que **placer** les
   fenêtres ; régler la résolution dans les options vidéo (fichier commun à toutes les instances :
   `Documents\DyingLight\out\settings\video.scr`, `Resolution(…)`, `WindowOffset(i,i)` existe aussi).

---

## 6. Mesures

| Mesure | Valeur |
|---|---|
| Mémoire engagée par instance (au menu) | 3,2 – 3,8 Go (`PrivateMemorySize64`) ; jeu chargé ~3,0 – 3,5 Go de RAM |
| Limite de mémoire engagée du PC | 39,9 Go (32 Go RAM + 2 × 4 Go de fichier d'échange) |
| Déjà engagée jeux fermés | 18 Go avant redémarrage ; 10,8 Go après (29,1 Go libres) |
| Après 4 instances (après redémarrage) | 12,8 Go encore libres |
| Fichiers d'échange lors des plantages | pics 2,2 + 2,6 Go |
| Chargement de la DLL | avant le point d'entrée (faux jeu : chargée avant `main`) ; dans le jeu, verrou renommé ~0,3 – 0,7 s après le chargement de la DLL, SteamID demandée ~1 s après |
| Délai entre lancements | 25 – 30 s (fiable) ; 15 s → plantages de la 4e instance |
| Temps avant abandon d'une connexion ratée | ~17 s |

---

## 7. Lire les journaux
`out\tools\fantome\journaux\instance_N.log` (N = 0 hôte, 1..7 fantômes) :
- `IAT … : n entrée(s) remplacée(s)` — attendu : CreateMutexA 2 (fantômes), FindOrCreateUserInterface 1,
  bind 2, sendto 2, WSASendTo 0, recvfrom 2.
- `verrou du jeu renommé`, `SteamID présenté au jeu`, `intermédiaire Steam créé pour …`.
- `bind 127.0.0.N:20111 → ok` (menu), `bind 127.0.0.N:20101 → ok` (création/entrée en session ; se répète à
  chaque changement de session).
- `sendto → IP:port (note)` : **une ligne par destination nouvelle seulement** (ne pas conclure à l'absence de
  trafic sur une destination déjà vue).
- `pair X adressé avec le port 0 / avec son vrai port 20101` : chaque changement de forme ;
  `recvfrom ← X:20101 présenté comme port 0`.
- `cloud : écriture ignorée …` (fantômes).
Journal du jeu lui-même : `Documents\DyingLight\out\logs\crash_<date>.log` (créé à chaque lancement, nom
trompeur). Plantages Windows : journal Application, événements 1000/1001.

---

## 8. Sécurité du compte et du jeu
- Dying Light a **VAC activé** (API du magasin Steam, catégorie 8). Pas d'EasyAntiCheat/BattlEye.
- Précautions retenues : **Steam hors ligne** pendant les tests ; **aucune écriture dans la mémoire de
  Steam** (intermédiaires au lieu de modifier les vftables de `steamclient64`) ; aucune modification de
  l'installation Steam ni de l'exe ; **LAN seulement**, jamais en matchmaking public ; les fantômes n'écrivent
  pas dans le cloud.
- **DLML est actif** dans le dossier du jeu (`dsound.dll`, installé hors de ce projet) avec un greffon qui
  modifie le lobby (journal du jeu : `ReadLobbySettings public_slots=5 -> forcing 9`, `GetGameProperty …`). Il
  se charge aussi dans les fantômes. Il peut fausser les tests sur le nombre de joueurs et ajouter de
  l'instabilité : à désactiver par l'utilisateur pour les mesures (le projet ne touche pas à l'installation).

---

## 9. Suite : passage à 6 joueurs (analyse faite, rien de modifié)
Limites trouvées (toutes à **5 places = 4 humains + 1 joueur zombie « Be the Zombie »**) :
| Où | Limite | Correctif envisagé |
|---|---|---|
| `SessionDI::ValidateLobbyInfo` inliné, gamedll `0x1804b307a` (`mov edx,5 ; mov ecx,0Ch ; cmovne`) | places = `IsCooperative() ? 5 : 12` | 5 → 7 |
| `LogicalLevelReplData` (objet répliqué, 0x2a8 octets) : `m_ReplPlayerIndex[5]` à +0x290 (dernier champ) ; `RegisterRTTI` gamedll `0x18048d280` ; `GetPlayerIndex`, `UpdatePlayerIndexList` | tableau et boucles à 5 ; **tableaux de 5 octets sur la pile** dans `UpdatePlayerIndexList` (débordement au-delà de 5, jeu compilé `/GS-`) | agrandir l'objet (0x2b0), réécrire les 3 fonctions |
| `HUDDIManager::COLORS[5]` (`DL_Player0..4`, `menu_project.xml`) ; `GetPlayerDrawColor(LogicalPlayer*)` gamedll `0x180557790` sans borne | lecture hors tableau à l'index ≥ 5 | tableau agrandi + couleurs |
| `HumanAI::GetMaxHealthCoopMul` | 2/3/4 joueurs seulement, sinon 1,0 | gérer 5-6 |
| HUD `hud.xui` / `hud_btz.xui` : `G_Player0..3` (`HudPlayerListElement`) | 4 blocs | données |
| `ActionPlayerDeadBodies::GetMaxNetIDOccupiedByActionPlayer` | `min(2n, 10)` avec seuil 5 | à étudier |
Moteur réseau (`Net::CLocalClient`, `Repl::CReplicator`, `CObject`) : listes dynamiques, identifiants 16 bits,
pas de limite trouvée. Recensement exhaustif encore à faire (~640 comparaisons à 3/4/5 dans des fonctions
« joueur/session » côté Linux, surtout des faux positifs).

Ordre suivi : (1) réécrire **à l'identique** les fonctions multijoueur dans `src/`
(2) versions modifiées pour 6 joueurs dans
`mods/coop6/`, (3) tester avec l'hôte + 5 fantômes. Pour 6 instances, prévoir ~20 Go de mémoire engagée libre
(agrandir le fichier d'échange : réglage système, à faire par l'utilisateur) ou alléger les fantômes
(réglages graphiques minimum ; `/norender` plante, voir §10). Fichier d'échange : mettre la taille initiale égale à la
maximale, sinon la limite reste celle de la taille initiale jusqu'à l'agrandissement.

## 10. Points ouverts
- Origine exacte du port 0 (corriger à la source serait plus propre que réécrire les paquets).
- Cause certaine des plantages de la 4e instance (ajouter à la DLL un relevé de pile au plantage si ça revient).
- `/norender` **inutilisable** (essai du 2026-10-08) : plantage ~1 s après le lancement, engine 0x18022cf90, la
  procédure de fenêtre appelle sur WM_SIZE l'objet global du rendu (`[0x180a40218]`), nul dans ce mode.
- Mémoire au menu mesurée le 2026-10-08 (fantôme seul, fenêtre 2048×1152) : 4,4 → 4,7 Go engagés, 3,3 → 3,7 Go de RAM.
- Taille des fenêtres : résolution commune à toutes les instances (fichier `video.scr` partagé) ; une
  redirection par instance serait possible dans la DLL.
- Test de référence d'une 5e instance (refus attendu avec le jeu d'origine) : à faire **sans** DLML.
