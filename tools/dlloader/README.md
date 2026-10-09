# dlloader — chargeur de mods par DLL relais (winmm.dll)

Charge les mods coop de Dying Light 1.55 **sans injection par un processus extérieur**. But : un
« produit fini » qui ne déclenche pas les faux positifs des antivirus. Le motif signalé par les antivirus
n'est pas le mod lui-même, mais la façon de le charger aujourd'hui (`tools/fantome`, `tools/dlcoop`) :
jeu créé en pause, puis `VirtualAllocEx` / `WriteProcessMemory` / `QueueUserAPC(LoadLibraryW)` dans le
processus du jeu. Ici, **le jeu charge ses mods tout seul** : aucune écriture dans la mémoire d'un autre
processus.

## Principe

`engine_x64_rwdi.dll` et `gamedll_x64_rwdi.dll` **importent `winmm.dll`** (vérifié). En posant une
`winmm.dll` dans le dossier du jeu, le chargeur de Windows la prend avant celle du système (`winmm`
n'est pas une « KnownDLL » protégée — vérifié). C'est la technique classique des chargeurs de mods
(DLML utilise `dsound.dll`, Ultimate ASI Loader utilise `winmm`/`version`/…).

Notre `winmm.dll` relais :

1. **réexpédie** tous ses exports (181) vers la vraie `winmm` du système, par son chemin absolu
   `\\.\GLOBALROOT\SystemRoot\System32\winmm` (forwarders générés dans `winmm_exports.h`). Le jeu garde
   donc un `winmm` pleinement fonctionnel, et **rien n'est à copier** à côté (pas de `winmm_real.dll`) ;
2. au chargement, lit `dlmods.ini` (à côté d'elle) :
   - section `[env]` : chaque `CLE=VALEUR` devient une **variable d'environnement** du processus du jeu ;
   - section `[loader]`, clé `Mods=` : les **DLL de mods** à charger, dans l'ordre (défaut
     `dlcoop_reseau.dll;coop6.dll`) ;
3. charge ces DLL depuis son propre dossier.

Les mods ne changent pas : `coop6.dll` lit déjà `COOP6_<CLÉ>` en priorité puis `coop6.ini` ;
`dlcoop_reseau.dll` lit les `DLF_*`. Le relais pose ces variables à partir de `[env]` **seulement si elles
sont absentes** : les valeurs par instance posées par le lanceur (héritées par le jeu) priment ; le fichier
sert pour un lancement direct (Steam).

**Moment du chargement** : `winmm` est chargée pendant la résolution des imports statiques (verrou du chargeur
tenu, imports pas tous résolus) : trop tôt. Le relais détourne donc le **point d'entrée de l'exe** (12 octets
`mov rax, EntryHook ; jmp rax`, remis à l'origine dès l'appel) : les mods sont chargés sur le fil principal,
juste avant la 1re instruction du jeu — le même moment que l'ancienne injection par APC. Indispensable pour les
instances supplémentaires (le verrou `Global\ChromeEngine4DIMutex` doit être renommé avant que le jeu le
prenne). La 1re version (1.3.0) chargeait les mods dans un fil : le jeu gagnait la course (0/20 au test
`tests/dlloader/run.ps1`) ; avec le point d'entrée : 20/20. Si `winmm` est chargée tard (LoadLibrary), repli
sur un fil.

## Fichiers

| Fichier | Rôle | Versionné |
|---|---|---|
| `gen_def.py` | Lit la `winmm` du système, écrit `winmm_exports.h` (forwarders `#pragma … /export:`) | oui |
| `dllmain.cpp` | Point d'entrée : lit `dlmods.ini`, pose `[env]`, charge les mods | oui |
| `build.ps1` | Génère l'en-tête puis compile `winmm.dll` (MSVC 14.16, `/MT`) → `out\tools\dlloader\` | oui |
| `dlmods.ini.exemple` | Modèle de configuration | oui |
| `winmm_exports.h` | Généré par `gen_def.py` | non (régénérable) |

## Compiler

```powershell
.\tools\dlloader\build.ps1
```

Sort `out\tools\dlloader\winmm.dll` (~124 Ko, 181 forwarders x64) et `dlmods.ini` (copie de l'exemple si
absent).

## Installer (dossier du jeu)

Dans le dossier du jeu (`…\Dying Light\`), poser :

- `winmm.dll` (le relais) ;
- `dlmods.ini` (la config) ;
- les DLL de mods citées dans `dlmods.ini` (`dlcoop_reseau.dll`, `coop6.dll`).

> **Attention au `dsound.dll` de DLML** s'il est présent : il n'entre pas en conflit (nom différent), mais s'il
> est actif il peut agir sur le lobby. Pour la coop, désactiver DLML (renommer `dsound.dll`).

Lancer le jeu normalement (raccourci, Steam, ou le lanceur). Rien d'autre.

## Désinstaller

Supprimer `winmm.dll`, `dlmods.ini` et les DLL de mods du dossier du jeu. Le jeu retrouve la `winmm` du
système automatiquement.

## Lanceur DLCoop (fait, 1.3.x)

- **Install mod** : copie `winmm.dll`, `dlcoop_reseau.dll`, `coop6.dll` dans le dossier du jeu ; **Launch** le fait
  aussi si les fichiers manquent **ou diffèrent** de ceux du lanceur (mise à jour automatique).
- **Launch** : pose les variables par instance (`DLF_INDEX`, adresse du relais, journal…) dans l'environnement du
  jeu, réécrit `dlmods.ini` (réglages d'un lancement simple : instance principale, sans adresse de relais), lance le
  jeu normalement. Plusieurs instances sur un PC : chacune garde ses propres valeurs (elles priment sur le fichier).
- **Uninstall mod** : retire les trois DLL et `dlmods.ini` (DLML n'est pas touché).

## Validé (2026-10-09)

- Renvoi `\.\GLOBALROOT\SystemRoot\System32\winmm` résolu sans récursion (modules du jeu : notre `WINMM.dll` +
  la winmm du système).
- Les deux mods chargés et tous les correctifs posés : lancement par la fenêtre, par `DLCoop.exe /launch` et
  **par Steam** sans lanceur.
- Partie coop à 2 PC par le relais Internet (VPS) ; puis 3 joueurs (2 instances sur le PC 1 + PC 2) avec le
  chargeur 1.3.1 (verrou renommé pour l'instance supplémentaire, identité Steam dérivée).
- Microsoft Defender : aucune menace sur le paquet.
