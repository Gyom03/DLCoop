# Le mod coop (`coop6.dll`)

Sources : `mods/coop6/`. La liste complète des modifications, avec leurs adresses dans la 1.55, est dans
[`mods/coop6/README.md`](../mods/coop6/README.md). Ce document en donne le principe.

## Ce que le mod lève

Le jeu est prévu pour 4 joueurs. Au-delà, plusieurs limites se manifestent : lobby à 4 places, listes de joueurs de
taille fixe (débordements et plantages), HUD et couleurs pour 4 joueurs seulement, difficulté et apparitions de zombies
qui retombent aux valeurs d'un joueur seul, « Be the Zombie » réglé pour 1 à 4 chasseurs. Le mod corrige chacun de ces
points pour **jusqu'à 32 joueurs** (+ 1 joueur zombie), par familles :

- **A** — lobby : places publiques portées à `Places`, conditions d'entrée élargies, recherche de parties.
- **B** — listes de joueurs : réécriture des fonctions qui utilisaient des tableaux de 5, HUD et couleurs pour les
  joueurs 6+ (27 couleurs ajoutées), plan d'apparition des zombies sur une fenêtre tournante de 5 joueurs.
- **C** — HUD : blocs de coéquipiers supplémentaires (sans toucher aux archives du jeu).
- **D** — difficulté : courbes de santé et de dégâts prolongées au-delà de 4 joueurs.
- **E** — EOS désactivé (inutile en LAN ; cause de gels au démarrage à plusieurs instances).
- **F** — rassemblement de mission : les retardataires sont téléportés au point de rassemblement.
- **G / Z** — équilibrage au-delà de 4 (groupes de zombies, quêtes, butin) et **Be the Zombie à N chasseurs** (loi de
  puissance `HuntersScale`, `BtzScale.h`).

## Comment il agit

Tout est installé dans le `DllMain` de `coop6.dll`, **après contrôle des octets de la 1.55** à chaque endroit (si les
octets ne correspondent pas, cet endroit n'est pas touché). Selon le cas, le mod :

- remplace un octet précis (par ex. un plafond `5` ou une condition de saut) ;
- pose un **détour** vers une fonction de remplacement (avec un petit *trampoline* qui garde les octets d'origine) ;
- remplace une entrée dans une **table de méthodes virtuelles** pour envelopper une méthode.

Les fonctions de remplacement reprennent le comportement d'origine du jeu (souvent le cas « 4 joueurs ») et l'étendent.
Les structures du jeu qu'elles manipulent sont décrites dans `src/` (`HumanAI`, `LogicalLevelReplData`, `Sessions`…).

## Réglages

`coop6.ini` à côté de la DLL, ou variables d'environnement `COOP6_<clé>` posées par le lanceur (prioritaires) :
`Places` (places quand on héberge, joueur zombie compris), `Difficulte`, `Couleurs`, `Hud`, `Diagnostic` (journal de
l'état du lobby), `SansEos`. Journal du mod : `coop6_instance_N.log` à côté de la DLL (relevés de plantage compris).

## Tester sans lancer le jeu

`tests/mod_install/run_coop6.ps1` compile un banc qui **charge** `gamedll_x64_rwdi.dll` et `engine_x64_rwdi.dll` en
mémoire sans les exécuter, applique `coop6.dll`, puis vérifie que les octets, les détours, les trampolines et les tables
virtuelles pointent bien vers le mod. Il lit les DLL depuis l'installation du jeu (paramètre `-Jeu`), sans les modifier.
Résultat attendu : « RÉSULTAT : CONFORME ».
