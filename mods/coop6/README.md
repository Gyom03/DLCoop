# coop6 — coop à 6 joueurs humains (et plus) pour Dying Light 1.55

**État au 2026-10-08 : 12 humains dans la même partie LAN validés** (hôte + 11 fantômes sur deux PC reliés par
`tools/fantome/pont.exe` ; 6, 7 et 8 humains validés avant). Depuis : jusqu'à 32 joueurs + 1 zombie (`Places` 3..33), 3 joueurs validés par le relais Internet avec le
lanceur DLCoop 1.3.x. Vue d'ensemble : `docs/`.

## Utilisation
```powershell
.\mods\coop6\build.ps1                 # -> out\mods\coop6\coop6.dll (+ coop6.ini)
.\tests\mod_install\run_coop6.ps1      # test hors jeu : « RÉSULTAT : CONFORME »
```
Dans `out\tools\fantome\fantome.ini` : `Mods=..\..\mods\coop6\coop6.dll`. Chaque instance lancée par
`fantome.exe` charge alors la DLL avant le point d'entrée du jeu. **Toutes les instances doivent avoir le mod**
(hôte et invités : le correctif est le même partout, peu importe qui héberge). Lancement :
`out\tools\fantome\lancer.ps1 -Fantomes 5 -Ecart 10 -SansEspace`.

Journal : `out\mods\coop6\coop6_instance_N.log` (complété à chaque lancement, relevés de plantage compris).
Réglages : `coop6.ini` — `Places` 3..33 (places quand on héberge, joueur zombie compris ; défaut 7 = 6 humains ; DLCoop : « Max players
when I host » + 1), `Difficulte`, `Couleurs`,
`Hud`, `Diagnostic` (journal de l'état du lobby et des recherches), `SansEos` (défaut 1).

## Ce que fait la DLL
Tout est installé dans `DllMain`, après contrôle des octets de la 1.55 à chaque endroit (sinon rien n'est touché).

| Repère | Où (gamedll 1.55, sauf mention) | Modification |
|---|---|---|
| A1 | `SessionCooperative::InitializeLobbyInfo` 0x1804a62c0 (2 vftables) | enveloppe : 5 places publiques → `Places` |
| A2 | `SessionDI::Create` 0x1804b307a (`ValidateLobbyInfo` inliné) | plafond `mov edx,5` → `Places` (1 octet) |
| A3 | `Sessions::StatusDL::CheckCanConnect` 0x180f31a90 | humain sans place d'équipe (4 places pleines) autorisé au lieu de l'erreur 0x2e |
| A3 | `SessionDI::Lobby_UpdateZombieCanJoin` 0x1804c9b30 et `SessionCooperativeDI::Lobby_UpdateZombieCanJoin` 0x1804a8ec0 | détours, réécrites : partie annoncée « humain possible » tant que < `Places−1` humains |
| A3 | `SessionCooperativeDI::IsJoinable` 0x1804a9449 | `humains < 4` → `< Places−1` (1 octet) |
| A4 | `SessionCooperativeDI::IsJoinable` 0x1804a9363 | `joueurs rejoints + 1 < 5` → `< Places` (1 octet) |
| A5 | `SGameScannerResult` : constructeur 0x180a1a397, `ExtractResults` 0x180a1a7e2 | maximum de joueurs d'une partie trouvée 5 → 33 (toujours le maximum, quel que soit `Places`) (côté client : « Pas de correspondances » en partie rapide dès 5 joueurs) |
| B2 | `LogicalLevel::UpdatePlayerIndexList` 0x18048ce10 | détour, réécrite sans les tableaux de pile de 5 (débordement au 9e joueur) |
| B3 | `HUDDIManager::GetPlayerDrawColor(LogicalPlayer*)` 0x180557790 | détour + trampoline : joueurs sans index (6e à 32e) : 27 couleurs ajoutées |
| B5 | `AIManager::LimitAtackingWalkers` 0x1801b9460 (appel de `GameDI::GetAllPlayers` 0x1801b94a1) | liste des joueurs bornée à 5 (tableaux de pile de 5 : corruption du tas au 6e joueur) |
| B6 | `AISpawnSystem::Manager::CalculateSpawnPlaceRatings` 0x180318a10 (appel 0x180318a42) | liste des joueurs bornée à 5 (`SpawnPlaceRating` : 3 tableaux de 5 flottants ; écrasements dès le 6e, plantage de l'hôte au 11e) |
| B7 | `AISpawnSystem::Manager::RegenerateSpawnPlan` 0x18031d050 (appel `GameDI::GetAllPlayers` 0x18031d0e2) + B6 | notes des lieux et plan par joueur sur la même **fenêtre tournante de 5 joueurs** (une par régénération, ~0,5 s) : sans ça, le rang ≥ 5 des joueurs 6+ était lu hors des tableaux de 5 de `SpawnPlaceRating` (zombies placés selon de mauvaises données) |
| C2 | `HudPlayersList::OnInit` 0x18058b510 (vftable) | copies du bloc `G_Player3` ajoutées à la liste des coéquipiers (sans toucher `Data0.pak`) ; pivots NaN laissés par `IUIElement::Clone` corrigés (barre de vie vide sinon) |
| F1 | `GatherPlace::Update` 0x180aa9f00 (détour + trampoline) | rassemblement de mission : ≥ 70 % des joueurs dans la zone depuis 5 s (valeurs fixes) → le joueur local resté dehors est téléporté au point de rassemblement (`PlayerDI::SetPosAndRot` 0x180bad0b0) |
| D1 | `HumanAI::GetMaxHealthCoopMul` 0x1805eb2a0, `GetMeleeDamageCoopMul` 0x1805eb330 (détours), `GetMaxStamina` 0x1805eb080 (4 vftables) | difficulté prolongée à 5-6 joueurs (sinon retour aux valeurs solo) |
| E1 | engine : `CEosAuthenticator::Login` 0x1806a28e0 | `ret` : pas de connexion EOS (inutile en LAN ; gelait les instances via le registre de rappels de steam_api64) |
| G1 | `AISpawnBoxCompound::GetNoInitiallySpawned` 0x1801cce30 (Ghidra `vfunc_151`), octet 0x1801ccede | groupes de zombies placés, zombies au départ : cas « 4 joueurs » pour 4 et plus (`jne` → `jl`) ; avant : valeur solo à 5+ |
| G2 | `AISpawnBoxCompound::CalcExpectedNumber` 0x1801ccf40 (`vfunc_115`), octet 0x1801ccfee | idem, total maximum (couvre `AIRandomSpawnBoxCompound`, qui remplit les mêmes champs) |
| G3 | `Quests::FillAreaPhase::GetReqCountToPass` 0x180e51730, octet 0x180e517d1 | nombre requis des étapes « remplir une zone » |
| G4 | `Quests::WaitGuardPhase::OnEnterChangeWorld` 0x180ea7060, octet 0x180ea71ea | délai des étapes « attendre / tenir » |
| G5 | `PlayerDI::GetRepairPartsAmountMul` 0x180bcf8b0, octet 0x180bcf921 | pièces de réparation du butin : ×0,65 (4 joueurs) au lieu de ×1 à 5+ |
| Z1 | engine : `ITools::GetVarFloat` (place 0x390), remplacée par `CEngineScriptManager::Initialize` 0x181104480 (détour + trampoline 14 octets) | réglages Be the Zombie `<nom>_<N>_human` (sinon variable absente → 0 : lampe UV, dégâts, réapparitions, objets lancés) : N ≥ 5 → `HuntersScale(v1, v4, N)` (`BtzScale.h`) |
| Z2 | `PlayerDI::GetMaxHealth` 0x180ba7170 (Ghidra `Monkey::vfunc_41`), octet 0x180ba71db | santé du joueur zombie : `cmove` → `cmovge` |
| Z3 | régénération du joueur zombie (inlinée dans 0x180ce0ce0), octet 0x180ce11f2 | idem |
| Z4 | `ZombieAbilitiesController::GetSpitsRegenTime` 0x180dd7ca0 (détour + trampoline 16 octets ; appel 0x180dd7d8c redirigé) | recharge des crachats (sinon 0) : valeurs calculées à 1 et 4 chasseurs (nombre imposé), puis `HuntersScale` |
| Z5 | `BtzDamageCoopSwitch` 0x180a61c30, octet 0x180a61e7e | objets activés aussi au-delà de 4 chasseurs |
| Z6 | `ActionPlayerZombieMission::CalculateHiveClusterRelativeDistance` 0x1800e4070 (détour + trampoline 18 octets), 18 octets en 0x1800e40f6 | index des chasseurs borné à 3 (sinon -1,0) ; case « 4 chasseurs » de la table remplacée pendant l'appel par `HuntersScale(table[0], table[3], N)` |

Be the Zombie au-delà de 4 chasseurs : `HuntersScale(v1, v4, N) = v4 · (N/4)^b`, `b = ln(v4/v1)/ln 4` (valeurs du jeu
à 1 et 4 chasseurs ; constante si égales ; valeur 4 si l'une n'est pas > 0). Ex. à 16 : dégâts au zombie ×0,25,
crachats 11 s, réapparition des chasseurs 48 s, du zombie 8 s.

Diagnostic (`Diagnostic=1`) : raisons de refus et bits publiés par l'hôte, verdict de `GameScanner::ValidateResult`
chez le client. Relevé de plantage toujours actif : code, adresse, registres, pile (gestionnaire d'exceptions
vectorisé, écriture sans allocation).

Limites gardées telles quelles (choix) : 5 index de joueur répliqués (`LogicalLevelReplData`) et 4 places d'équipe
humaines (`StatusDL`) — champs répliqués enregistrés dans le RTTI pendant l'initialisation statique de gamedll,
avant toute DLL injectée. Les joueurs au-delà n'ont ni index ni place d'équipe ; le mod les laisse entrer et leur
donne une couleur.

## Reste à faire
- 16 humains : mémoire (~3,5 Go par instance ; fichier d'échange du PC 2 à agrandir pour 8 instances).
- B5 / B6 : réécritures complètes pour N joueurs (aujourd'hui les 6e et suivants ne comptent pas dans la limite des
  attaquants ni dans la note des points d'apparition).
- Menu des joueurs (`MenuPlayersOverlay` : 5 boutons `B_Player0..4`).
- Couleur « sans index » donnée à tort pendant la connexion d'un joueur.
- Vérifier en jeu : HUD à 5-6 coéquipiers (position des copies), difficulté.
- Vérifier en jeu G1-G5 (groupes de zombies placés à 5+ joueurs) ; Z1-Z6 dès qu'un essai Be the Zombie sera fait.
- Étape 2 de l'équilibrage (courbes au-delà de 4) : à décider.
