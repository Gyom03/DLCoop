# Compilation

## Prérequis

- **Visual Studio Build Tools 2022** avec l'ensemble d'outils **MSVC v141 (14.16)** — c'est l'éditeur de liens 14.16,
  celui qui a servi à compiler le jeu (voir plus bas). Les DLL du mod sont compilées avec, en CRT statique (`/MT`) :
  aucun redistribuable Visual C++ à installer chez les joueurs.
- Le **SDK .NET** (`dotnet`) pour compiler le lanceur WPF (cible .NET Framework 4.7.2).
- **Python 3** avec `pefile` (le chargeur `winmm.dll` génère ses renvois depuis la `winmm` du système).
- Pour le serveur relais : **g++** (C++17) sur Linux.

## Tout compiler (mod + chargeur + lanceur)

Dans PowerShell, à la racine du dépôt :

```powershell
.\tools\dlcoop\build.ps1                          # -> out\dlcoop\paquet\ + out\dlcoop\DLCoop_<version>.zip
.\tools\dlcoop\build.ps1 -Relais 203.0.113.10     # idem, adresse du relais préremplie (server.txt) pour un groupe
```

Le paquet contient `DLCoop.exe`, `winmm.dll`, `dlcoop_reseau.dll`, `coop6.dll` et `README.txt`. La version vient de
`<Version>` dans `tools/dlcoop/DLCoop.csproj` : c'est aussi celle des trois DLL (informations de version, `version.rc`).

Compiler séparément : `tools\dlloader\build.ps1` (chargeur `winmm.dll`), `mods\coop6\build.ps1` (mod).

## Compilation reproductible

Les DLL natives sont compilées et liées avec `/Brepro` : à source identique, elles sortent **identiques à l'octet
près** (sans la date de compilation que MSVC écrit sinon dans l'en-tête PE). C'est ce qui permet au lanceur de ne
signaler « Update available » que pour un vrai changement du mod (comparaison octet par octet avec le dossier du jeu).

## Le serveur relais

Compilé sur le VPS Linux par `tools/relais/deployer.sh` (`g++ -O2 -std=c++17`). `relais.cpp` est du C++17 portable, sans
dépendance ; il compile aussi sous Windows pour les tests. Voir [serveur relais](serveur-relais.md).

## Comment le jeu lui-même a été compilé (et pourquoi ça compte)

Dying Light 1.55 a été compilé avec **MSVC 2017 (éditeur de liens 14.16)**, options `/O2 /Gy /GS- /fp:fast`, avec
**LTCG** (`/GL` + `/LTCG`). Le mod ne recompile pas le jeu : il **modifie en mémoire** des fonctions précises, après
avoir vérifié que les octets à l'adresse visée sont bien ceux de la 1.55 (sinon il ne touche à rien). Toutes les
adresses du mod (`mods/coop6/README.md`) sont donc propres à cette version ; le jeu doit rester en **1.55**.

Deux conséquences visibles dans ce dépôt :

- les en-têtes de `src/` décrivent les structures du jeu (positions des champs, places dans les tables de méthodes
  virtuelles) telles qu'elles sont dans les binaires 1.55 ;
- `mods/coop6/engine_imports.def` liste les quelques fonctions exportées par `engine_x64_rwdi.dll` que le mod appelle ;
  `build.ps1` en fait une bibliothèque d'import (`engine_imports.lib`) pour que `coop6.dll` se lie à elles.

Les binaires du jeu ne sont **pas** fournis dans ce dépôt (copie légitime requise). La compilation du mod n'en a pas
besoin ; le test hors jeu `tests/mod_install/run_coop6.ps1`, lui, lit `gamedll_x64_rwdi.dll` et `engine_x64_rwdi.dll`
depuis l'installation du jeu (paramètre `-Jeu`), sans les modifier.
