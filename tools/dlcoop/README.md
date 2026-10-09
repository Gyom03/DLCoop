# DLCoop — lanceur du mod coop (paquet à distribuer)

Fenêtre WPF (`DLCoop.exe`, ~30 Ko), **entièrement en anglais**, qui lance Dying Light 1.55
avec les deux DLL du mod chargées avant le point d'entrée du jeu (même méthode que `tools/fantome` : jeu créé en
pause, une APC `LoadLibraryW` par DLL). **Aucune dépendance** : .NET Framework 4.7.2, composant de Windows 10
(1803+) et 11 ; DLL en CRT statique (`/MT`). Le dossier du jeu n'est jamais modifié.

## Compiler le paquet
```powershell
.\tools\dlcoop\build.ps1     # -> out\dlcoop\paquet\ et out\dlcoop\DLCoop_<version>.zip (~190 Ko)
```
Les DLL sont compilées dans `out\dlcoop\obj` (pas de conflit avec les instances en cours qui verrouillent
`out\mods\coop6` et `out\tools\fantome`).

| Fichier | Source |
|---|---|
| `DLCoop.exe` | `tools/dlcoop/*.cs`, `*.xaml` (`DLCoop.csproj`, x64 obligatoire : adresse de `LoadLibraryW` lue dans ce processus) |
| `dlcoop_reseau.dll` | `tools/fantome/src/fantome_dll.cpp` (correctif « port 0 » de la 1.55, pseudo, instance supplémentaire, adresses 127.0.0.N) |
| `coop6.dll` | `mods/coop6/Coop6Mod.cpp` |
| `README.txt` | notice pour les joueurs (en anglais) |

## Fonctionnement
- **Jeu** : vérifié à chaque démarrage (`Launcher.CheckInstallation`) : chemin mémorisé gardé s'il existe et qu'il
  est en 1.55 ; sinon recherche (clé de désinstallation « Steam App 239140 », `libraryfolders.vdf`,
  `appmanifest_239140.acf`), préférence à une installation 1.55, nouveau chemin enregistré et signalé s'il a bougé.
  « Change… » enregistre aussitôt le chemin choisi.
- **Fenêtre simplifiée** : visibles seulement l'emplacement du jeu, l'adresse du serveur et
  le mot de passe de session, puis « Launch » ; le reste dans trois menus dépliants fermés (style `Expander` sombre
  dans `App.xaml`) : « Player name » (pseudo masqué, `DLF_NOM`), « Instance » (principale / supplémentaire
  `DLF_INDEX`, sans son `/nosound`, textures réduites `-texStreamMemMB 512`, plusieurs instances sur ce PC
  `DLF_LOOPBACK`) et « More options » (« Max players when I host » 2..32, mémorisé : `COOP6_Places` = valeur + 1 pour le zombie ;
  « Disable the EOS connection » `COOP6_SansEos` ; « Close after launch »).
  Les choix d'instance reviennent aux valeurs par défaut à chaque démarrage (instance principale, son, textures
  complètes). Message de réussite réduit à « Game launched. ». Coop jusqu'à 32 joueurs (`Launcher.MaxInstances` = 33 places),
  difficulté, couleurs, HUD et diagnostic toujours actifs.
- **Réseau** : plus de choix explicite : serveur renseigné = relais Internet (`Relay.cs`, serveur `tools/relais/`) ; champ vide = réseau local
  ou VPN (comportement d'origine). Avec le relais, le lanceur devient l'agent du relais (connexion, adresse 0..32 demandée
  au relais pour chaque jeu lancé et passée par `DLF_ADRESSE`, adresses des joueurs distants occupées en
  `127.0.0.N`). Il doit rester ouvert pendant la partie : « Close after launch » le réduit, et il se ferme seul
  quand le dernier jeu se ferme. Adresse du relais préremplie par `server.txt` (`build.ps1 -Relais <ip>`) ; le mot
  de passe de session n'est jamais dans le paquet. Protocole v2 (33 adresses, masques de 64 bits), repli automatique sur la v1 (16) face à un ancien relais.
  Une seule fenêtre DLCoop connectée à un relais par PC
  (`Local\DLCoop_relais`) ; avec le relais, l'identité d'une instance est vérifiée par le verrou du jeu
  (`Global\ChromeEngine4DIMutex[_fantomeN]`), les verrous `Local\DLFantome_slot_N` désignant des adresses.
- Choix mémorisés dans `%APPDATA%\DLCoop\settings.txt` ; journaux dans `logs\` à côté de l'exe.
- Ligne de commande : `DLCoop.exe /launch [/instance N] [/multi] [/name Nom] [/game chemin] [/internet | /lan]
  [/server adresse] [/password X] [/count N]` (`/internet`, `/lan` forcent le mode ; instances suivantes de `/count`
  sans son et à textures réduites) (sans fenêtre, code de sortie 0 si les deux DLL sont chargées dans le jeu ;
  avec le relais, le processus reste actif tant qu'un jeu lancé tourne ; `/count N` : N instances à 10 s d'écart).
- Compatible avec `tools/fantome` et `pont.exe` (mêmes verrous `Local\DLFantome_slot_N`).
