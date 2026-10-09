# Charger des mods dans Dying Light 1.55

Le jeu ne charge aucune DLL tierce. Pour qu'un mod sous forme de DLL agisse, il doit être présent **dans le processus
du jeu avant que le jeu ne commence vraiment** : `dlcoop_reseau.dll` doit par exemple renommer le verrou d'instance
unique du jeu (`Global\ChromeEngine4DIMutex`) avant que le jeu ne le prenne. Trois approches existent autour de ce jeu.

| | Injection (ancien DLCoop) | Chargeur `winmm.dll` (DLCoop actuel) | DLML |
|---|---|---|---|
| Où vit le code | dossier de DLCoop | dossier du jeu (`winmm.dll` + mods) | dossier du jeu (`dsound.dll`) |
| Qui charge le mod | le lanceur, de l'extérieur | le jeu lui-même | le jeu lui-même |
| Charge des DLL de mods | oui | oui | **non** (archives `.pak` seulement) |
| Lancement par Steam | sans mod | **avec** le mod | avec DLML |
| Antivirus | motif d'injection souvent signalé | motif courant des chargeurs de mods, bien moins signalé | idem |
| Branche | `dll-injection` | `main` | (projet externe) |

## Injection (branche `dll-injection`, et `tools/fantome`)

Le lanceur démarre le jeu suspendu et demande au système de charger les DLL du mod sur le fil principal du jeu, avant son
point d'entrée, par une APC `LoadLibraryW`. Code : `tools/dlcoop/Launcher.cs` (branche `dll-injection`),
`tools/fantome/src/fantome.cpp`.

- **Inconvénient principal** : un programme extérieur qui prépare le chargement d'une DLL dans un autre processus est un
  **motif que les antivirus signalent** (il est aussi employé par des logiciels malveillants), même pour un usage
  parfaitement légitime. C'est la raison du passage au chargeur `winmm.dll`.
- Lancé par Steam (sans le lanceur), le jeu n'a pas le mod.

## Chargeur `winmm.dll` (branche `main`, `tools/dlloader`)

Le jeu importe `winmm.dll` (via `engine_x64_rwdi.dll` et `gamedll_x64_rwdi.dll`), et `winmm` n'est pas une « KnownDLL »
protégée. En posant une `winmm.dll` à nous dans le dossier du jeu, Windows la charge à la place de celle du système :
c'est **le jeu qui charge le mod**, aucun programme extérieur n'intervient. Même technique que DLML (avec `dsound.dll`)
ou les chargeurs « ASI ».

Notre `winmm.dll` :

1. **renvoie** tous ses appels (181 fonctions) vers la vraie `winmm` de Windows (System32), par des *forwarders* générés
   (`gen_def.py` → `winmm_exports.h`). Le jeu garde donc un `winmm` pleinement fonctionnel ; rien d'autre à copier ;
2. lit `dlmods.ini` (à côté d'elle) : section `[loader] Mods=` (DLL à charger) et section `[env]` (réglages posés comme
   variables d'environnement, seulement si absentes — voir [lanceur](lanceur.md)) ;
3. charge les DLL de mods **au point d'entrée de l'exe** : elle y pose un court détournement (remis à l'origine dès
   l'appel) pour charger les mods sur le fil principal, juste avant la première instruction du jeu — le même moment que
   l'ancienne APC, sans programme extérieur. Un simple fil secondaire serait trop tardif (le jeu prendrait son verrou
   d'instance avant le mod ; mesuré : `tests/dlloader/run.ps1`).

Avantages : le mod se charge aussi quand le jeu est lancé par **Steam** (les fichiers sont dans son dossier), et le
motif « injection » disparaît. `winmm.dll` ne se fait **pas** passer pour le composant Microsoft : ses informations de
version la déclarent « DLCoop mod loader » (un fichier qui usurpe l'identité d'un composant système est justement ce que
les antivirus repèrent).

Fichiers : `tools/dlloader/` (`gen_def.py`, `dllmain.cpp`, `build.ps1`, `dlmods.ini.exemple`).

> **Modifier l'installation du jeu.** Cette méthode copie des fichiers dans le dossier du jeu. C'est réversible
> (*Uninstall mod*, ou supprimer `winmm.dll`, `dlmods.ini` et les DLL de mods) et ne touche pas aux fichiers d'origine.

## DLML

[DLML](https://github.com/12brendon34/DLML) (Dying Light Mod Loader) est un projet externe : un `dsound.dll` relais qui
charge des archives `.pak` et affiche le journal du jeu. Il **ne charge pas de DLL tierces**, il ne peut donc pas
charger `coop6.dll`. Comme il occupe `dsound.dll` et DLCoop `winmm.dll`, ils n'entrent pas en conflit de nom, mais si
DLML est actif il peut agir sur le lobby : **le désactiver pour les parties coop** (renommer son `dsound.dll`).
