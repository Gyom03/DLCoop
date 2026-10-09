// Coop6Mod — coop jusqu'à 32 joueurs humains (+ 1 joueur zombie) pour Dying Light 1.55 (gamedll_x64_rwdi.dll).
// Structures du jeu utilisées : src/coop/*.h (dispositions vérifiées sur le code machine de la 1.55).
//
// Chargée au démarrage du jeu avant son point d'entrée (fantome.exe, clé Mods=), comme fantome.dll. Tout est
// installé dans DllMain, après contrôle des octets de la 1.55 à CHAQUE endroit touché : si un seul diffère,
// rien n'est modifié et le journal le dit.
//
// Modifications :
//   A1  SessionCooperative::InitializeLobbyInfo (2 vftables) : enveloppe, 5 places publiques -> Places (7).
//   A2  SessionDI::Create @ 0x1804b307a, ValidateLobbyInfo inliné : plafond `mov edx,5` -> Places (1 octet).
//   A3  Places d'équipe humaines (Sessions::StatusDL, 4 places répliquées à +0x58, figées) :
//       - StatusDL::CheckCanConnect @ 0x180f31a90 (CheckTeamSlotsPermission inliné) : humain sans place d'équipe
//         alors que les 4 sont prises -> autorisé (au lieu de l'erreur 0x2e) ; la limite reste le lobby (A1/A2) ;
//       - SessionDI::Lobby_UpdateZombieCanJoin (détour, réécrite) et sa redéfinition coop
//         SessionCooperativeDI::Lobby_UpdateZombieCanJoin (même code inliné + UpdateGameNotJoinableReason) :
//         « humain bienvenu » annoncé tant qu'il reste une place d'équipe OU moins de Places-1 humains ;
//       - SessionCooperativeDI::IsJoinable @ 0x1804a9449 : `cmp eax,4` (humains < 4) -> Places-1 (1 octet).
//   A4  SessionCooperativeDI::IsJoinable @ 0x1804a9363 : `joueurs rejoints + 1 < 5` -> < Places (1 octet) ; sans ça
//       la partie devient « non rejoignable » (et disparaît des recherches) dès 5 joueurs.
//   A5  Côté client : SGameScannerResult (constructeur @ 0x180a1a397, ExtractResults @ 0x180a1a7e2) fixe le maximum
//       de joueurs d'une partie trouvée à 5 en dur (+0x270) ; SMenuLobbyResult::Update (partie rapide, liste) et
//       le compteur de parties rejoignables n'acceptent qu'avec `joueurs < [+0x270]` -> « Pas de correspondances »
//       dès que l'hôte a 5 joueurs. 5 -> 33, le maximum, quel que soit Places (1 octet chacun).
//   B2  LogicalLevel::UpdatePlayerIndexList @ 0x18048ce10 (détour, réécrite) : même logique, mais sans les tableaux
//       de pile de 5 octets de l'original ; le 2e, indexé par le rang du joueur, atteint le rbx sauvegardé
//       (rbp+0x28) au 9e joueur (8 humains + zombie).
//   B3  HUDDIManager::GetPlayerDrawColor(LogicalPlayer*) : détour + trampoline ; joueurs sans index (6e, 7e)
//       -> 2 couleurs ajoutées au lieu du blanc par défaut. Les 5 index répliqués restent tels quels.
//   C2  HudPlayersList::OnInit (vftable) : enveloppe ; copies du bloc G_Player3 ajoutées à la liste du HUD (jusqu'à
//       Places-1 coéquipiers), pivots NaN laissés par IUIElement::Clone corrigés (sinon barre de vie vide).
//   F1  GatherPlace::Update (détour + trampoline) : zone de rassemblement d'une mission, au moins 70 % des joueurs
//       dedans depuis 5 s -> le joueur local, s'il est dehors, est téléporté au point de rassemblement
//       (PlayerDI::SetPosAndRot, comme le bouton du jeu). Valeurs fixes.
//   D1  HumanAI::GetMaxHealthCoopMul / GetMeleeDamageCoopMul (détours, remplacement complet) et
//       HumanAI::GetMaxStamina (4 vftables) : multiplicateurs coop prolongés à 5 et 6 joueurs.
//   G   Au-delà de 4 joueurs, valeurs « 4 joueurs » au lieu du solo (1 octet chacun) : groupes de zombies placés
//       (G1, G2), étapes de quête (G3, G4), pièces de réparation du butin (G5).
//   Z   Be the Zombie pour tout nombre de chasseurs : loi de puissance passant par les valeurs à 1 et 4 chasseurs
//       (HuntersScale, BtzScale.h) pour les réglages "_N_human" (Z1, ITools::GetVarFloat remplacée à l'initialisation
//       des scripts), les crachats (Z4) et les ruches (Z6) ; santé et régénération du zombie (Z2, Z3 : valeurs égales
//       de 1 à 4 dans les données) et objets activés (Z5) : cas « 4 » pour 4 et plus.
//   B7  AISpawnSystem::Manager::RegenerateSpawnPlan : notes des lieux (B6) et plan par joueur sur la même fenêtre
//       tournante de 5 joueurs (sinon rangs >= 5 lus hors des tableaux de 5 : zombies des joueurs 6+ mal placés).
//
// Réglages : coop6.ini à côté de la DLL. Journal : coop6_instance_N.log (N = DLF_INDEX de fantome) ou coop6.log.
#define NOMINMAX
#include <windows.h>

#include <io.h>

#include <algorithm>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <iterator>

#include "BtzScale.h"

#include "../../src/coop/HumanAI.h"
#include "../../src/coop/LogicalLevelReplData.h"
#include "../../src/coop/Sessions.h"

// --- Interface du moteur (exports d'engine_x64_rwdi.dll, bibliothèque d'import engine_imports.lib) ------
class vec3 {
public:
    float x, y, z;
};

class IUIElement {
public:
    __declspec(dllimport) IUIElement* Clone(IUIElement* parent);
    __declspec(dllimport) IUIElement* GetParent();
    __declspec(dllimport) vec3 GetRelativePos() const;
    __declspec(dllimport) void SetRelativePos(const vec3& pos);
    __declspec(dllimport) int GetChildrenCount();
    __declspec(dllimport) IUIElement* GetChildren(int index);
    __declspec(dllimport) vec3 GetUnscaledPivot() const;
    __declspec(dllimport) void SetUnscaledPivot(const vec3& pivot);
};

namespace {

// --- Repères dans gamedll 1.55 (adresses relatives au début du module) --------------------------------
constexpr std::uintptr_t kRvaInitializeLobbyInfo = 0x4a62c0;
constexpr std::uintptr_t kRvaValidateLobbySlots = 0x4b307a;  // mov edx,5 ; mov ecx,0Ch
constexpr std::uintptr_t kRvaGetMaxStamina = 0x5eb080;
constexpr std::uintptr_t kRvaGetMaxHealthCoopMul = 0x5eb2a0;
constexpr std::uintptr_t kRvaGetMeleeDamageCoopMul = 0x5eb330;
constexpr std::uintptr_t kRvaGetPlayerDrawColor = 0x557790;
constexpr std::uintptr_t kRvaHudPlayersListOnInit = 0x58b510;
constexpr std::uintptr_t kRvaFindParam = 0x233c60;            // AIDataSetWrapper::FindParam
constexpr std::uintptr_t kRvaRTDynamicCast = 0x134ec60;       // relais vers __RTDynamicCast
constexpr std::uintptr_t kRvaTypeIUIElement = 0x1bcdc40;      // descripteurs de type (RTTI MSVC)
constexpr std::uintptr_t kRvaTypeHudPlayerListElement = 0x1bd11b0;
constexpr std::uintptr_t kRvaDefaultColor = 0x1bac190;        // vec3(1,1,1) renvoyé sans index
constexpr std::uintptr_t kRvaCheckCanConnectNoSlot = 0xf31a90;  // mov eax,2Eh ; mov [rbx+40h],eax ; mov eax,-1 ; jmp
constexpr std::uintptr_t kRvaUpdateZombieCanJoin = 0x4c9b30;  // SessionDI::Lobby_UpdateZombieCanJoin(bool)
constexpr std::uintptr_t kRvaCoopUpdateZombieCanJoin = 0x4a8ec0;  // SessionCooperativeDI::Lobby_UpdateZombieCanJoin
constexpr std::uintptr_t kRvaUpdateGameNotJoinableReason = 0x4a9580;  // SessionCooperativeDI::UpdateGameNotJoinableReason
constexpr std::uintptr_t kRvaIsJoinableHumans = 0x4a9449;     // SessionCooperativeDI::IsJoinable : cmp eax,4 ; setb al
constexpr std::uintptr_t kRvaIsJoinablePlayers = 0x4a935b;    // SessionCooperativeDI::IsJoinable : [this+0xd0]+1 < 5
constexpr std::uintptr_t kRvaScanResultCtorMax = 0xa1a397;   // SGameScannerResult::SGameScannerResult : [+0x270] = 5
constexpr std::uintptr_t kRvaExtractResultsMax = 0xa1a7e2;    // SGameScannerResult::ExtractResults : [+0x270] = 5
constexpr std::uintptr_t kRvaLimitWalkersGetPlayers = 0x1b94a1;  // AIManager::LimitAtackingWalkers : call GetAllPlayers
constexpr std::uintptr_t kRvaSpawnRatingsGetPlayers = 0x318a42;  // AISpawnSystem::Manager::CalculateSpawnPlaceRatings : idem
constexpr std::uintptr_t kRvaGameGetAllPlayers = 0x443560;       // GameDI::GetAllPlayers(EPlayerConflictSide) const
constexpr std::uintptr_t kRvaUpdatePlayerIndexList = 0x48ce10;  // LogicalLevel::UpdatePlayerIndexList()
constexpr std::uintptr_t kRvaSetGameBitProperties = 0xa21620;  // LobbyTools::Properties::SetGameBitProperties(uint)
constexpr std::uintptr_t kRvaValidateResult = 0xa16a80;      // GameScanner::ValidateResult(SGameSearchResult const&)
constexpr std::uintptr_t kRvaGetRemoteLogicalPlayers = 0x48bf70;  // LogicalLevel::GetRemoteLogicalPlayers(vec&, side)
constexpr std::uintptr_t kRvaImpFree = 0x137a3d0;             // IAT : api-ms-win-crt-heap free (CRT du jeu)
constexpr std::uintptr_t kRvaGetZombieCanJoin = 0x4c9c50;     // SessionDI::GetZombieCanJoin() const
constexpr std::uintptr_t kRvaGetTeamCount = 0xf2ea90;         // Sessions::Session::GetTeamCount(ETeam) const
constexpr std::uintptr_t kRvaSetZombieCanJoin = 0xa21d10;     // LobbyTools::Properties::SetZombieCanJoin(ETeamSlots)
constexpr std::uintptr_t kRvaGameDISingleton = 0x1c16348;     // GameDI* (Linux : Game::sm_cSingleton)
constexpr std::uintptr_t kRvaTypeStatus = 0x1bcb9a0;          // descripteurs RTTI Sessions::Status / StatusDL
constexpr std::uintptr_t kRvaTypeStatusDL = 0x1bcb9c8;
constexpr std::uintptr_t kRvaHudPlayerListElementUpdate = 0x5873d0;  // HudPlayerListElement::Update (vftable)
constexpr std::uintptr_t kRvaFindPlayerDI = 0x492f00;  // LogicalPlayer::FindPlayerDI() const (par pair propriétaire)
constexpr std::uintptr_t kRvaGatherPlaceUpdate = 0xaa9f00;  // GatherPlace::Update(IGatherPlaceOwner*)
constexpr std::uintptr_t kRvaSetPosAndRot = 0xbad0b0;       // PlayerDI::SetPosAndRot(vec3 const&, vec3 const&)
// G / Z : valeurs « 4 joueurs » au-delà de 4 (voir FourPlus plus bas).
constexpr std::uintptr_t kRvaGetNumberOfLogicalPlayers = 0x443770;  // GameDI::GetNumberOfLogicalPlayers(side) const
constexpr std::uintptr_t kRvaSpitsCountCall = 0xdd7d8c;     // ZombieAbilitiesController::GetSpitsRegenTime : call ci-dessus
constexpr std::uintptr_t kRvaHiveIndex = 0xe40f0;           // CalculateHiveClusterRelativeDistance : chasseurs - 1 -> index
constexpr std::uintptr_t kRvaSpitsRegenTime = 0xdd7ca0;     // ZombieAbilitiesController::GetSpitsRegenTime(type) const
constexpr std::uintptr_t kRvaHiveDistance = 0xe4070;        // ActionPlayerZombieMission::CalculateHiveClusterRelativeDistance
constexpr std::uintptr_t kRvaSpawnLoopGetPlayers = 0x31d0e2;  // AISpawnSystem::Manager::RegenerateSpawnPlan : call GetAllPlayers
constexpr std::uintptr_t kRvaScriptInitialize = 0x1104480;  // CEngineScriptManager::Initialize(…, ITools*, …)
constexpr std::size_t kGetVarFloatSlot = 0x390 / sizeof(void*);  // ITools::GetVarFloat(string const&, CVarSet const*)

struct Expected {
    const char* name;
    std::uintptr_t rva;
    unsigned char bytes[16];
    std::size_t size;
};
const Expected kExpected[] = {
    {"SessionCooperative::InitializeLobbyInfo", kRvaInitializeLobbyInfo,
     {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57}, 16},
    {"SessionDI::Create (ValidateLobbyInfo inliné)", kRvaValidateLobbySlots,
     {0xBA, 0x05, 0x00, 0x00, 0x00, 0xB9, 0x0C, 0x00, 0x00, 0x00}, 10},
    {"HumanAI::GetMaxStamina", kRvaGetMaxStamina,
     {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x50, 0x0F}, 16},
    {"HumanAI::GetMaxHealthCoopMul", kRvaGetMaxHealthCoopMul,
     {0x48, 0x83, 0xEC, 0x28, 0x80, 0xB9, 0x58, 0x10, 0x00, 0x00, 0x00, 0x74, 0x09, 0x83, 0xB9, 0x24}, 16},
    {"HumanAI::GetMeleeDamageCoopMul", kRvaGetMeleeDamageCoopMul,
     {0x48, 0x83, 0xEC, 0x28, 0x48, 0x83, 0xC1, 0x28, 0xFF, 0x15, 0xE2, 0xFE, 0xD8, 0x00, 0x8B, 0x90}, 16},
    {"HUDDIManager::GetPlayerDrawColor", kRvaGetPlayerDrawColor,
     {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x30, 0x48}, 16},
    {"HudPlayersList::OnInit", kRvaHudPlayersListOnInit,
     {0x40, 0x55, 0x41, 0x56, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x58, 0x48, 0x89, 0xB4, 0x24, 0x80}, 16},
    {"AIDataSetWrapper::FindParam", kRvaFindParam,
     {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57}, 16},
    {"vec3(1,1,1) par défaut", kRvaDefaultColor,
     {0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x80, 0x3F}, 12},
    {"StatusDL::CheckCanConnect (erreur 0x2e)", kRvaCheckCanConnectNoSlot,
     {0xB8, 0x2E, 0x00, 0x00, 0x00, 0x89, 0x43, 0x40, 0xB8, 0xFF, 0xFF, 0xFF, 0xFF, 0xE9, 0xC7, 0x00}, 16},
    {"SessionDI::Lobby_UpdateZombieCanJoin", kRvaUpdateZombieCanJoin,
     {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57}, 16},
    {"SessionDI::GetZombieCanJoin", kRvaGetZombieCanJoin,
     {0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x8B, 0xD9, 0xC7, 0x44, 0x24, 0x20, 0x00, 0x00, 0x00}, 16},
    {"Sessions::Session::GetTeamCount", kRvaGetTeamCount,
     {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57}, 16},
    {"SessionCooperativeDI::Lobby_UpdateZombieCanJoin", kRvaCoopUpdateZombieCanJoin,
     {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x57}, 16},
    {"SessionCooperativeDI::UpdateGameNotJoinableReason", kRvaUpdateGameNotJoinableReason,
     {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48}, 16},
    {"SessionCooperativeDI::IsJoinable (humains < 4)", kRvaIsJoinableHumans,
     {0x83, 0xF8, 0x04, 0x0F, 0x92, 0xC0, 0x48, 0x83, 0xC4, 0x20, 0x5B, 0xC3}, 12},
    {"SessionCooperativeDI::IsJoinable (joueurs + 1 < 5)", kRvaIsJoinablePlayers,
     {0x8B, 0x83, 0xD0, 0x00, 0x00, 0x00, 0xFF, 0xC0, 0x83, 0xF8, 0x05, 0x0F, 0x92, 0xC0}, 14},
    {"SGameScannerResult::SGameScannerResult (max joueurs = 5)", kRvaScanResultCtorMax,
     {0xC7, 0x83, 0x70, 0x02, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00}, 10},
    {"SGameScannerResult::ExtractResults (max joueurs = 5)", kRvaExtractResultsMax,
     {0xC7, 0x86, 0x70, 0x02, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00}, 10},
    {"AIManager::LimitAtackingWalkers (call GameDI::GetAllPlayers)", kRvaLimitWalkersGetPlayers,
     {0xE8, 0xBA, 0xA0, 0x28, 0x00, 0x45, 0x33, 0xFF, 0xC7, 0x44, 0x24, 0x40, 0x01}, 13},
    {"AISpawnSystem::Manager::CalculateSpawnPlaceRatings (call GameDI::GetAllPlayers)", kRvaSpawnRatingsGetPlayers,
     {0xE8, 0x19, 0xAB, 0x12, 0x00, 0xF3, 0x44, 0x0F, 0x10, 0x2D, 0x00, 0xE6, 0x60}, 13},
    {"GameDI::GetAllPlayers", kRvaGameGetAllPlayers,
     {0x40, 0x53, 0x48, 0x83, 0xEC, 0x20, 0x48, 0x8B, 0x89, 0x40, 0x05, 0x00, 0x00, 0x8B, 0xDA}, 15},
    {"LogicalLevel::UpdatePlayerIndexList", kRvaUpdatePlayerIndexList,
     {0x40, 0x55, 0x41, 0x55, 0x48, 0x8B, 0xEC, 0x48, 0x83, 0xEC, 0x68, 0x48, 0x8B, 0x81, 0x40, 0x0A}, 16},
    {"LogicalLevel::GetRemoteLogicalPlayers", kRvaGetRemoteLogicalPlayers,
     {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x74, 0x24, 0x18, 0x48, 0x89, 0x7C, 0x24, 0x20, 0x41}, 16},
    {"LobbyTools::Properties::SetGameBitProperties", kRvaSetGameBitProperties,
     {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x60, 0x33}, 16},
    {"GameScanner::ValidateResult", kRvaValidateResult,
     {0x48, 0x89, 0x5C, 0x24, 0x10, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x4C, 0x8D, 0x44, 0x24, 0x30, 0xC7}, 16},
    {"LobbyTools::Properties::SetZombieCanJoin", kRvaSetZombieCanJoin,
     {0x40, 0x53, 0x48, 0x83, 0xEC, 0x40, 0x33, 0xC0, 0x8B, 0xD9, 0x48, 0x8B, 0x0D, 0xC7, 0x01, 0x1F}, 16},
    {"HudPlayerListElement::Update", kRvaHudPlayerListElementUpdate,
     {0x48, 0x8B, 0xC4, 0x48, 0x89, 0x58, 0x10, 0x48, 0x89, 0x78, 0x18, 0x4C, 0x89, 0x70, 0x20, 0x55}, 16},
    {"LogicalPlayer::FindPlayerDI", kRvaFindPlayerDI,
     {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x6C, 0x24, 0x18, 0x48, 0x89, 0x74, 0x24, 0x20, 0x57}, 16},
    {"GatherPlace::Update", kRvaGatherPlaceUpdate,
     {0x48, 0x89, 0x54, 0x24, 0x10, 0x56, 0x57, 0x41, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x33, 0xF6, 0x48}, 16},
    {"PlayerDI::SetPosAndRot", kRvaSetPosAndRot,
     {0x48, 0x89, 0x5C, 0x24, 0x08, 0x48, 0x89, 0x74, 0x24, 0x10, 0x48, 0x89, 0x7C, 0x24, 0x18, 0x55}, 16},
    // G : `cmp r,1 ; jne solo` (r = joueurs - 3) et ce qui suit.
    {"G1 AISpawnBoxCompound::GetNoInitiallySpawned", 0x1ccedb,
     {0x83, 0xFB, 0x01, 0x75, 0x3A, 0x8B, 0x87, 0x0C, 0x03, 0x00, 0x00}, 11},
    {"G2 AISpawnBoxCompound::CalcExpectedNumber", 0x1ccfeb,
     {0x83, 0xFB, 0x01, 0x75, 0x3A, 0x8B, 0x87, 0x1C, 0x03, 0x00, 0x00}, 11},
    {"G3 Quests::FillAreaPhase::GetReqCountToPass", 0xe517ce,
     {0x83, 0xFB, 0x01, 0x75, 0x3E, 0x8B, 0x87, 0x0C, 0x01, 0x00, 0x00}, 11},
    {"G4 Quests::WaitGuardPhase::OnEnterChangeWorld", 0xea71e7,
     {0x83, 0xFB, 0x01, 0x75, 0x27, 0xF3, 0x0F, 0x10, 0x96, 0x9C, 0x00, 0x00, 0x00}, 13},
    {"G5 PlayerDI::GetRepairPartsAmountMul", 0xbcf91e, {0x83, 0xFA, 0x01, 0x75, 0x1C, 0xF3, 0x0F, 0x10, 0x35}, 9},
    // Z : Be the Zombie.
    {"Z2 PlayerDI::GetMaxHealth (cmp eax,4 ; mov ecx,2C1h ; cmove)", 0xba71d2,
     {0x83, 0xF8, 0x04, 0xB9, 0xC1, 0x02, 0x00, 0x00, 0x0F, 0x44, 0xD9}, 11},
    {"Z3 régénération du joueur zombie (cmp ebp,4 ; mov eax,1F6h ; cmove)", 0xce11e9,
     {0x83, 0xFD, 0x04, 0xB8, 0xF6, 0x01, 0x00, 0x00, 0x0F, 0x44, 0xD8}, 11},
    {"Z4 GetSpitsRegenTime (call GetNumberOfLogicalPlayers)", kRvaSpitsCountCall,
     {0xE8, 0xDF, 0xB9, 0x66, 0xFF, 0xEB, 0x03, 0x41, 0x8B, 0xC5, 0xFF, 0xC8}, 12},
    {"GameDI::GetNumberOfLogicalPlayers", kRvaGetNumberOfLogicalPlayers,
     {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x33, 0xFF, 0xC7, 0x44, 0x24, 0x30}, 16},
    {"Z5 BtzDamageCoopSwitch (cmp esi,1 ; jne)", 0xa61e7b,
     {0x83, 0xFE, 0x01, 0x75, 0x0C, 0x48, 0x8D, 0x93, 0x88, 0x04, 0x00, 0x00}, 12},
    {"Z6 CalculateHiveClusterRelativeDistance (index)", kRvaHiveIndex,
     {0x8B, 0x80, 0x20, 0x0E, 0x00, 0x00, 0x8B, 0xD3, 0xFF, 0xC8, 0x85, 0xC0, 0x0F, 0x4F, 0xD0, 0x83}, 16},
    {"Z6 CalculateHiveClusterRelativeDistance (suite)", kRvaHiveIndex + 0x10,
     {0xFA, 0x04, 0x0F, 0x8D, 0xA5, 0x00, 0x00, 0x00, 0x48, 0x8B, 0x4F, 0x70}, 12},
    {"Z4 ZombieAbilitiesController::GetSpitsRegenTime (début)", kRvaSpitsRegenTime,
     {0x40, 0x55, 0x53, 0x56, 0x48, 0x8D, 0x6C, 0x24, 0xB9, 0x48, 0x81, 0xEC, 0x90, 0x00, 0x00, 0x00}, 16},
    {"Z6 CalculateHiveClusterRelativeDistance (début)", kRvaHiveDistance,
     {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x40, 0x48, 0x8B, 0xC2, 0x0F, 0x29, 0x74}, 16},
    {"B7 RegenerateSpawnPlan (call GameDI::GetAllPlayers)", kRvaSpawnLoopGetPlayers,
     {0xE8, 0x79, 0x64, 0x12, 0x00, 0x33, 0xF6, 0x4C, 0x8B, 0xF0, 0x39, 0x70, 0x08}, 13},
    {"Z1 CEngineScriptManager::Initialize", kRvaScriptInitialize,
     {0x40, 0x56, 0x48, 0x83, 0xEC, 0x20, 0x80, 0x7C, 0x24, 0x50, 0x00, 0x48, 0x8B, 0xF1, 0x48, 0x89}, 16},
};

// --- Réglages, journal ----------------------------------------------------------------------------------
// Places au plus : 32 joueurs + 1 zombie. La recherche de parties (A5) et le HUD (C2) prévoient toujours ce maximum :
// un joueur qui rejoint doit pouvoir trouver et afficher une partie hébergée avec plus de places que son propre réglage.
constexpr int kMaxPlaces = 33;

struct Settings {
    int places = 7;          // places du lobby quand on héberge (humains + zombie) ; jeu d'origine : 5 ; 3..33
    bool difficulty = true;  // D1
    bool colors = true;      // B3
    bool hud = true;         // C2
    bool diagnostic = true;  // journal de l'état du lobby (raisons de refus, bits publiés, compteurs)
    bool noEos = true;       // E1 : pas de connexion EOS (multiplateforme), inutile en LAN
} g_settings;

wchar_t g_dir[MAX_PATH];
FILE* g_log = nullptr;
std::uint8_t* g_base = nullptr;

void Log(const char* fmt, ...)
{
    if (!g_log)
        return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    fprintf(g_log, "%02d:%02d:%02d.%03d ", t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    va_list args;
    va_start(args, fmt);
    vfprintf(g_log, fmt, args);
    va_end(args);
    fputc('\n', g_log);
    fflush(g_log);
}

// Réglage : variable d'environnement COOP6_<CLÉ> (posée par le lanceur DLCoop), sinon coop6.ini, sinon défaut.
int Setting(const wchar_t* ini, const wchar_t* key, int def)
{
    wchar_t name[64], value[16];
    swprintf_s(name, L"COOP6_%s", key);
    if (GetEnvironmentVariableW(name, value, 16) > 0)
        return _wtoi(value);
    return static_cast<int>(GetPrivateProfileIntW(L"coop6", key, def, ini));
}

void LoadSettings()
{
    wchar_t ini[MAX_PATH];
    swprintf_s(ini, L"%s\\coop6.ini", g_dir);
    g_settings.places = std::clamp(Setting(ini, L"Places", 7), 3, kMaxPlaces);
    g_settings.difficulty = Setting(ini, L"Difficulte", 1) != 0;
    g_settings.colors = Setting(ini, L"Couleurs", 1) != 0;
    g_settings.hud = Setting(ini, L"Hud", 1) != 0;
    g_settings.diagnostic = Setting(ini, L"Diagnostic", 1) != 0;
    g_settings.noEos = Setting(ini, L"SansEos", 1) != 0;
}

// E1 : CEosAuthenticator::Login (engine 0x1806a28e0, appelants sans valeur de retour) -> `ret`. Chaque instance
// demande sinon un ticket Steam pour EOS ; avec plusieurs instances sur un même client Steam (hors ligne), la
// tâche dépasse son délai et est détruite par le fil principal (SteamAPI_UnregisterCallback) pendant que le fil
// du lobby en relance une (SteamAPI_RegisterCallback) : le registre de rappels de steam_api64 n'est pas protégé,
// sa liste se corrompt, les deux fils bouclent à 100 % et le jeu gèle (et plantages engine+0x68641a).
constexpr std::uintptr_t kRvaEosLogin = 0x6a28e0;  // engine : CEosAuthenticator::Login()
const std::uint8_t kEosLoginBytes[] = {0x40, 0x53, 0x48, 0x83, 0xEC, 0x30, 0x48, 0x83, 0x79, 0x40, 0x00};

void WriteMemory(void* at, const void* data, std::size_t size);

void InstallNoEos()
{
    auto* engine = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(L"engine_x64_rwdi.dll"));
    if (!engine || std::memcmp(engine + kRvaEosLogin, kEosLoginBytes, sizeof(kEosLoginBytes)) != 0) {
        Log("E1 ERREUR : CEosAuthenticator::Login introuvable ou octets inattendus — EOS laissé tel quel");
        return;
    }
    const std::uint8_t ret = 0xC3;
    WriteMemory(engine + kRvaEosLogin, &ret, 1);
    Log("E1 connexion EOS désactivée (CEosAuthenticator::Login -> ret)");
}

template <typename T>
T At(std::uintptr_t rva) { return reinterpret_cast<T>(g_base + rva); }

// --- Écriture dans le code et les vftables -------------------------------------------------------------
void WriteMemory(void* at, const void* data, std::size_t size)
{
    DWORD old;
    VirtualProtect(at, size, PAGE_EXECUTE_READWRITE, &old);
    std::memcpy(at, data, size);
    VirtualProtect(at, size, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, size);
}

// `jmp [rip+0] ; dq cible` : saut absolu de 14 octets (notre DLL est trop loin pour un saut relatif).
void WriteJump(void* at, const void* to)
{
    unsigned char code[14] = {0xFF, 0x25, 0, 0, 0, 0};
    std::memcpy(code + 6, &to, 8);
    WriteMemory(at, code, sizeof(code));
}

// Trampoline : les `size` premiers octets de la fonction (indépendants de leur position, vérifiés) puis un
// saut vers la suite. Permet d'appeler l'original d'une fonction détournée.
void* MakeTrampoline(std::uint8_t* fn, std::size_t size)
{
    auto* t = static_cast<std::uint8_t*>(VirtualAlloc(nullptr, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!t)
        return nullptr;
    std::memcpy(t, fn, size);
    WriteJump(t + size, fn + size);
    return t;
}

// Mémoire exécutable à moins de 2 Go de `target` (pour un `call rel32` redirigé vers un relais).
std::uint8_t* AllocNear(std::uint8_t* target, std::size_t size)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    const std::uintptr_t granularity = si.dwAllocationGranularity;
    std::uintptr_t at = (reinterpret_cast<std::uintptr_t>(target) - 0x70000000u) & ~(granularity - 1);
    const std::uintptr_t end = reinterpret_cast<std::uintptr_t>(target) + 0x70000000u;
    while (at < end) {
        MEMORY_BASIC_INFORMATION mbi;
        if (!VirtualQuery(reinterpret_cast<void*>(at), &mbi, sizeof(mbi)))
            break;
        if (mbi.State == MEM_FREE) {
            if (void* p = VirtualAlloc(reinterpret_cast<void*>(at), size, MEM_COMMIT | MEM_RESERVE,
                                       PAGE_EXECUTE_READWRITE))
                return static_cast<std::uint8_t*>(p);
        }
        at = (reinterpret_cast<std::uintptr_t>(mbi.BaseAddress) + mbi.RegionSize + granularity - 1) & ~(granularity - 1);
    }
    return nullptr;
}

// Redirige le `call rel32` situé en `site` vers `to` (relais proche : saut absolu de 14 octets).
bool RedirectCall(std::uint8_t* site, const void* to)
{
    std::uint8_t* relay = AllocNear(site, 64);
    if (!relay)
        return false;
    WriteJump(relay, to);
    const std::int32_t rel = static_cast<std::int32_t>(relay - (site + 5));
    WriteMemory(site + 1, &rel, 4);
    return true;
}

bool SectionOf(const char* name, std::uint8_t*& start, std::size_t& size)
{
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS*>(g_base + reinterpret_cast<IMAGE_DOS_HEADER*>(g_base)->e_lfanew);
    auto* sec = IMAGE_FIRST_SECTION(nt);
    for (int i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++sec) {
        if (std::strncmp(reinterpret_cast<const char*>(sec->Name), name, 8) == 0) {
            start = g_base + sec->VirtualAddress;
            size = sec->Misc.VirtualSize;
            return true;
        }
    }
    return false;
}

// Remplace chaque pointeur `from` par `to` dans .rdata (vftables). Renvoie le nombre de places changées.
int PatchVftables(void* from, void* to)
{
    std::uint8_t* rdata;
    std::size_t size;
    if (!SectionOf(".rdata", rdata, size))
        return 0;
    int count = 0;
    for (std::size_t off = 0; off + sizeof(void*) <= size; off += sizeof(void*)) {
        void** slot = reinterpret_cast<void**>(rdata + off);
        if (*slot != from)
            continue;
        WriteMemory(slot, &to, sizeof(to));
        ++count;
    }
    return count;
}

// --- A1 : places publiques du lobby ---------------------------------------------------------------------
using InitializeLobbyInfoFn = void (*)(SessionCooperative*, Sessions::LobbyInfo&, EGamePart::TYPE);
InitializeLobbyInfoFn g_origInitializeLobbyInfo = nullptr;

void ModInitializeLobbyInfo(SessionCooperative* self, Sessions::LobbyInfo& info, EGamePart::TYPE part)
{
    g_origInitializeLobbyInfo(self, info, part);
    if (info.m_PublicSlots == 5) {
        info.m_PublicSlots = static_cast<unsigned>(g_settings.places);
        Log("lobby : type de session %d, places publiques 5 -> %u", info.m_SessionType, info.m_PublicSlots);
    }
}

// --- A3 : places d'équipe humaines au-delà de 4 -----------------------------------------------------------
using GetZombieCanJoinFn = int (*)(void* sessionDI);
using GetTeamCountFn = unsigned (*)(void* session, int team);
using SetZombieCanJoinFn = void (*)(int teamSlots);
using RTDynamicCastFn = void* (*)(void*, long, void*, void*, int);
int g_lastHumansLogged = -1;

// Réécriture de SessionDI::Lobby_UpdateZombieCanJoin (gamedll 0x1804c9b30, Linux 0x2ec1350, GetHumanCanJoin
// inliné). Seule différence : un humain peut rejoindre si la session compte moins de Places-1 humains (au lieu de :
// seulement s'il reste une des 4 places d'équipe ; identique pour Places >= 5, et respecte un maximum plus bas).
// Valeur publiée (ETeamSlots, propriété MP_ZombieCanJoin filtrée par la requête « Find Cooperative Matches ») :
// zombie et humain possibles 1, zombie seul 2, humain seul 0, aucun -1.
void ModLobbyUpdateZombieCanJoin(std::uint8_t* self, bool onlyIfChanged)
{
    const bool zombieOk = At<GetZombieCanJoinFn>(kRvaGetZombieCanJoin)(self) == 0;
    bool humanOk = false;
    auto* status = static_cast<std::uint8_t*>(At<RTDynamicCastFn>(kRvaRTDynamicCast)(
        *reinterpret_cast<void**>(self + 0x90), 0, At<void*>(kRvaTypeStatus), At<void*>(kRvaTypeStatusDL), 0));
    if (status != nullptr) {
        const auto* teamSlots = reinterpret_cast<const std::uint16_t*>(status + 0x58);
        const bool freeTeamSlot = std::find(teamSlots, teamSlots + 4, std::uint16_t{0xffff}) != teamSlots + 4;
        const unsigned humans = At<GetTeamCountFn>(kRvaGetTeamCount)(self, 1);
        if (humans < static_cast<unsigned>(g_settings.places - 1)) {
            // Réglage « humains autorisés » du lobby (SettingsDI::GetLobbySettings(true), bit 0 de +0x20).
            std::uint8_t* game = *At<std::uint8_t**>(kRvaGameDISingleton);
            std::uint8_t* settings = *reinterpret_cast<std::uint8_t**>(game + 0x50);
            std::uint8_t* session = *reinterpret_cast<std::uint8_t**>(game + 0x540);
            const std::uint8_t* allowed;
            if (session != nullptr) {
                allowed = session + 0x7d78;
            } else {
                settings[0xc] |= 2;
                allowed = settings + 0x100;
            }
            humanOk = (*allowed & 1) != 0;
        }
        if (!freeTeamSlot && g_lastHumansLogged != static_cast<int>(humans)) {
            g_lastHumansLogged = static_cast<int>(humans);
            Log("lobby : 4 places d'équipe prises, %u humain(s) -> humains %s", humans,
                humanOk ? "encore acceptés" : "refusés");
        }
    }
    int teamSlots;
    if (zombieOk)
        teamSlots = humanOk ? 1 : 2;
    else
        teamSlots = humanOk ? 0 : -1;
    int& cached = *reinterpret_cast<int*>(self + 0x2ee8);
    if (onlyIfChanged && cached == teamSlots)
        return;
    cached = teamSlots;
    At<SetZombieCanJoinFn>(kRvaSetZombieCanJoin)(teamSlots);
    using VoidMethod = void (*)(void*);
    (*reinterpret_cast<VoidMethod**>(self))[0xa68 / 8](self);  // place 333 (après mise à jour du lobby)
}

// --- Diagnostic : état du lobby de l'hôte, journalisé à chaque changement ---------------------------------
// Raisons « non rejoignable » (GameDI+0x900, FGameNotJoinableReason, posées par UpdateGameNotJoinableReason à
// partir des cas de SessionCooperativeDI::IsJoinable), ETeamSlots publié, compteurs de session.
void LogLobbyState(std::uint8_t* session, const char* where)
{
    if (!g_settings.diagnostic)
        return;
    static int last[8] = {-1, -1, -1, -1, -1, -1, -1, -1};
    std::uint8_t* game = *At<std::uint8_t**>(kRvaGameDISingleton);
    const int now[8] = {*reinterpret_cast<int*>(game + 0x900), *reinterpret_cast<int*>(game + 0x904),
                        *reinterpret_cast<int*>(session + 0x2ee8), *reinterpret_cast<int*>(session + 0xd0),
                        *reinterpret_cast<int*>(session + 0x50), *reinterpret_cast<int*>(session + 0x54),
                        *reinterpret_cast<int*>(session + 0x58), *reinterpret_cast<int*>(session + 0x5c)};
    if (std::equal(now, now + 8, last))
        return;
    std::copy(now, now + 8, last);
    Log("diag %s : raisons 0x%x (+0x904 %d) ; ETeamSlots %d ; session +0xd0=%d +0x50=%d +0x54=%d +0x58=%d +0x5c=%d",
        where, now[0], now[1], now[2], now[3], now[4], now[5], now[6], now[7]);
}

using SetGameBitPropertiesFn = void (*)(unsigned bits);
SetGameBitPropertiesFn g_origSetGameBitProperties = nullptr;  // trampoline

void ModSetGameBitProperties(unsigned bits)
{
    static unsigned last = 0xffffffffu;
    if (bits != last) {
        last = bits;
        Log("diag bits publiés (ELobbyGameBitProperty) : 0x%x", bits);
    }
    g_origSetGameBitProperties(bits);
}

// Côté client : verdict de GameScanner::ValidateResult pour chaque partie reçue (2 = valide ; sinon drapeaux
// FGameScannerValidateResult de refus).
using ValidateResultFn = unsigned (*)(void* scanner, const void* result);
ValidateResultFn g_origValidateResult = nullptr;  // trampoline

// B5 : AIManager::LimitAtackingWalkers (gamedll 0x1801b9460, Linux 0x22567e0) garde sur sa pile 5 vecteurs
// (rsp+0x30 + 0x18·i, détruits par une boucle fixe de 5) et 5 positions (rbp-0x38 + 0xc·i), mais parcourt tous
// les joueurs de GameDI::GetAllPlayers : au 6e, son « vecteur » est de la pile non initialisée -> realloc d'un
// pointeur quelconque -> corruption du tas (c0000374), au même instant chez tous les pairs. Elle ne lit la liste
// que par [liste] (données) et [liste+8] (nombre) : son appel est redirigé ici vers une copie bornée à 5 joueurs
// (les 6e et 7e ne comptent pas dans la limite des attaquants : rôdeurs non bridés près d'eux).
struct PlayerList {
    void** data;
    std::uint32_t count;
    std::uint32_t capacity;
};
using GetAllPlayersFn = const PlayerList* (*)(void* game, int side);
PlayerList g_limitWalkersPlayers = {};

const PlayerList* ModLimitWalkersGetPlayers(void* game, int side)
{
    const PlayerList* all = reinterpret_cast<GetAllPlayersFn>(g_base + kRvaGameGetAllPlayers)(game, side);
    g_limitWalkersPlayers = *all;
    if (g_limitWalkersPlayers.count > 5)
        g_limitWalkersPlayers.count = 5;
    return &g_limitWalkersPlayers;
}

// B6 : AISpawnSystem::Manager::CalculateSpawnPlaceRatings (gamedll 0x180318a10, Linux 0x230a600) écrit, pour le
// joueur de rang i, dans 3 tableaux de 5 flottants de chaque SpawnPlaceRating (+0x00, +0x14, +0x28 ; puis total
// +0x3c et position +0x40) : joueurs 6 à 9 -> total et position écrasés en silence, 11e joueur -> pointeurs de
// l'arbre (ttl::map) écrasés -> plantage de l'hôte (relevé : rbx = 10, nœud 0x3ca393dd = bits d'un flottant).
// Même remède que B5 : liste bornée à 5.
//
// B7 : la boucle qui suit dans RegenerateSpawnPlan (0x18031d050, appel GetAllPlayers 0x18031d0e2) passe le RANG de
// chaque joueur à IncreaseGameIntensity -> FindBestSpawnPlace, qui lit les notes [rang] de chaque lieu : rang >= 5
// -> lecture hors des tableaux de 5 (notes d'autres joueurs, total, position) : les zombies des joueurs 6 et suivants
// étaient placés selon de mauvaises données. Désormais, notes et boucle reçoivent la MÊME fenêtre de 5 joueurs, qui
// tourne à chaque régénération (~0,5 s) : rangs toujours < 5, chaque joueur servi toutes les ~2 s à 16. Les lieux
// restent rassemblés autour de tous les joueurs (GatherSpawnPlacesAroundPlayers, non modifiée). Liste reconstruite à
// chaque appel depuis la liste du jeu (aucun pointeur gardé d'une image à l'autre).
constexpr unsigned kSpawnWindow = 5;
void* g_spawnWindowData[kSpawnWindow];
PlayerList g_spawnWindow = {};
unsigned g_spawnWindowStart = 0;  // début de la fenêtre de la régénération en cours
unsigned g_spawnWindowNext = 0;   // début de la suivante

const PlayerList* SpawnWindow(void* game, int side)
{
    const PlayerList* all = reinterpret_cast<GetAllPlayersFn>(g_base + kRvaGameGetAllPlayers)(game, side);
    if (all->count <= kSpawnWindow)
        return all;  // jusqu'à 5 joueurs : liste du jeu, comportement d'origine
    const unsigned start = g_spawnWindowStart % all->count;
    for (unsigned k = 0; k < kSpawnWindow; ++k)
        g_spawnWindowData[k] = all->data[(start + k) % all->count];
    g_spawnWindow = {g_spawnWindowData, kSpawnWindow, kSpawnWindow};
    return &g_spawnWindow;
}

const PlayerList* ModSpawnRatingsGetPlayers(void* game, int side)  // 1er appel de chaque régénération
{
    g_spawnWindowStart = g_spawnWindowNext;
    g_spawnWindowNext = g_spawnWindowStart + kSpawnWindow;
    return SpawnWindow(game, side);
}

const PlayerList* ModSpawnLoopGetPlayers(void* game, int side) { return SpawnWindow(game, side); }

unsigned ModValidateResult(void* scanner, const void* result)
{
    const unsigned verdict = g_origValidateResult(scanner, result);
    static unsigned last = 0xffffffffu;
    static DWORD lastTime = 0;
    const DWORD now = GetTickCount();
    if (verdict != last || now - lastTime > 5000) {
        last = verdict;
        lastTime = now;
        Log("diag client : GameScanner::ValidateResult -> 0x%x (2 = valide)", verdict);
    }
    return verdict;
}

// Redéfinition coop (gamedll 0x1804a8ec0 ; Linux 0x2f51ea0 : SessionDI::Lobby_UpdateZombieCanJoin puis
// SessionCooperativeDI::UpdateGameNotJoinableReason).
void ModCoopLobbyUpdateZombieCanJoin(std::uint8_t* self, bool onlyIfChanged)
{
    ModLobbyUpdateZombieCanJoin(self, onlyIfChanged);
    using VoidFn = void (*)(void*);
    At<VoidFn>(kRvaUpdateGameNotJoinableReason)(self);
    LogLobbyState(self, "UpdateZombieCanJoin");
}

// --- B2 : liste des index de joueur sans débordement de pile ----------------------------------------------
// Vecteur du jeu (ttl::vector) tel qu'initialisé par l'original : données, taille, capacité, étiquette mémoire.
struct GameVector {
    void** data;
    std::uint32_t size;
    std::uint32_t capacity;
    std::uint32_t tag;
};
using GetRemoteLogicalPlayersFn = void (*)(void* level, GameVector* out, int side);
using CrtFreeFn = void (*)(void*);

// Réécriture de LogicalLevel::UpdatePlayerIndexList (gamedll 0x18048ce10 ; Linux 0x2f55d50 +
// LogicalLevelReplData::UpdatePlayerIndexList 0x2f4b150 inlinée). Comportement identique (5 index répliqués à
// LogicalLevelReplData+0x290, 0xffff = libre ; un joueur non placé prend le 1er index libre, sans le marquer pris,
// comme l'original), avec des tableaux locaux de 64 joueurs au lieu de 2 × 5 octets dans la zone d'accueil.
void ModUpdatePlayerIndexList(std::uint8_t* level)
{
    std::uint8_t* replData = *reinterpret_cast<std::uint8_t**>(level + 0xa40);
    if (replData[0x28] == 0)
        return;
    GameVector v{nullptr, 0, 0, 0x23};
    At<GetRemoteLogicalPlayersFn>(kRvaGetRemoteLogicalPlayers)(level, &v, 2);
    constexpr unsigned kMax = 64;
    const LogicalPlayer* players[kMax];
    unsigned count = 0;
    for (unsigned j = 0; j < v.size && count < kMax - 1; ++j)
        players[count++] = static_cast<const LogicalPlayer*>(v.data[j]);
    auto* local = *reinterpret_cast<const LogicalPlayer**>(level + 0x9d8);
    if (v.size != 0 && local != nullptr)
        players[count++] = local;  // l'original l'ajoute au vecteur seulement s'il n'est pas vide

    int* index = reinterpret_cast<int*>(replData + 0x290);
    bool taken[5] = {};
    bool placed[kMax] = {};
    for (unsigned j = 0; j < count; ++j) {
        if (players[j] == nullptr)
            continue;
        for (unsigned i = 0; i < 5; ++i) {
            if (index[i] == players[j]->GetNetOwnerID()) {
                taken[i] = true;
                placed[j] = true;
                break;
            }
        }
    }
    for (unsigned i = 0; i < 5; ++i)
        if (!taken[i])
            index[i] = 0xffff;
    for (unsigned j = 0; j < count; ++j) {
        if (placed[j] || players[j] == nullptr)
            continue;
        for (unsigned i = 0; i < 5; ++i) {
            if (!taken[i]) {
                index[i] = players[j]->GetNetOwnerID();
                break;
            }
        }
    }
    v.size = 0;
    (*At<CrtFreeFn*>(kRvaImpFree))(v.data);
}

// --- D1 : difficulté des ennemis humains selon le nombre de joueurs -------------------------------------
using FindParamFn = AIDataParam* (*)(const AIDataSetWrapper*, int);
FindParamFn g_findParam = nullptr;

// Multiplicateur coop au-delà de 4 joueurs : prolongement linéaire de l'écart entre 3 et 4 joueurs,
// plafonné à 6 joueurs (au-delà, le joueur zombie est compté).
float Extrapolate(float at3, float at4, int players)
{
    return at4 + static_cast<float>(std::min(players, 6) - 4) * (at4 - at3);
}

// Réécriture de HumanAI::GetMaxHealthCoopMul (identique au jeu jusqu'à 4 joueurs), prolongée à 5-6.
float ModGetMaxHealthCoopMul(const HumanAI* self, int players)
{
    if (self->m_Flag1058 && !EHellraidAIType::IsHellEnemy(self->m_HellraidType))
        return 1.0f;
    if (!self->m_ScaleHealthInCoop)
        return 1.0f;
    const AIDataSetWrapper* data = self->m_AIData;
    switch (players) {
    case 2:
        return data->GetParam<float>(EAIParam::COOP_DAMAGE_MUL_2);
    case 3:
        return data->GetParam<float>(EAIParam::COOP_DAMAGE_MUL_3);
    case 4:
        return data->GetParam<float>(EAIParam::COOP_DAMAGE_MUL_4);
    }
    if (players < 2)
        return 1.0f;
    return Extrapolate(data->GetParam<float>(EAIParam::COOP_DAMAGE_MUL_3),
                       data->GetParam<float>(EAIParam::COOP_DAMAGE_MUL_4), players);
}

float ModGetMeleeDamageCoopMul(const HumanAI* self)
{
    const int players = static_cast<int>(self->GetLevelDI()->GetAllPlayersCount());
    switch (players) {
    case 2:
        return 1.1f;
    case 3:
        return 1.2f;
    case 4:
        return 1.3f;
    }
    return players < 2 ? 1.0f : Extrapolate(1.2f, 1.3f, players);
}

float ModGetMaxStamina(const HumanAI* self, int difficulty)
{
    const float staminaMul = self->m_AIData->GetParam<float>(EAIParam::STAMINA_MUL);
    const float staminaToHealth = self->m_AIData->GetParam<float>(EAIParam::STAMINA_TO_HEALTH_RATIO);
    const int players = static_cast<int>(self->GetLevelDI()->GetAllPlayersCount());
    float coopMul = self->GetMaxStaminaCoopMul();  // 2..4 : valeurs d'origine (1,5 / 2,1 / 2,75)
    if (players > 4)
        coopMul = Extrapolate(2.1f, 2.75f, players);
    return self->GetMaxHealth(difficulty) * staminaMul * staminaToHealth * coopMul;
}

// --- B3 : couleur des joueurs sans index (6e, 7e) -------------------------------------------------------
using GetPlayerDrawColorFn = const vec3* (*)(const LogicalPlayer*, bool);
GetPlayerDrawColorFn g_origGetPlayerDrawColor = nullptr;  // trampoline
// Joueurs 6 à 32 : 27 couleurs distinctes de celles du jeu (rose, jaune-vert, orange, violet, turquoise, brun,
// bleu clair, vert pâle, rouge brique, gris clair, bleu-violet, puis 16 teintes intermédiaires plus ou moins claires).
const vec3 kExtraColors[] = {{0.85f, 0.40f, 0.70f}, {0.80f, 0.85f, 0.30f}, {0.95f, 0.55f, 0.15f},
                             {0.60f, 0.35f, 0.85f}, {0.25f, 0.80f, 0.75f}, {0.60f, 0.45f, 0.30f},
                             {0.55f, 0.75f, 0.95f}, {0.65f, 0.90f, 0.60f}, {0.75f, 0.30f, 0.25f},
                             {0.80f, 0.80f, 0.80f}, {0.45f, 0.45f, 0.90f},
                             {1.00f, 0.70f, 0.75f}, {0.95f, 0.85f, 0.55f}, {0.70f, 0.95f, 0.85f},
                             {0.85f, 0.70f, 1.00f}, {0.40f, 0.60f, 0.35f}, {0.70f, 0.25f, 0.50f},
                             {0.30f, 0.50f, 0.70f}, {0.90f, 0.45f, 0.45f}, {0.55f, 0.55f, 0.20f},
                             {0.35f, 0.70f, 0.95f}, {1.00f, 0.80f, 0.35f}, {0.50f, 0.30f, 0.60f},
                             {0.60f, 0.85f, 0.35f}, {0.95f, 0.60f, 0.85f}, {0.45f, 0.80f, 0.55f},
                             {0.65f, 0.65f, 0.55f}};
constexpr int kExtraColorCount = sizeof(kExtraColors) / sizeof(kExtraColors[0]);
int g_unindexedIds[64];
int g_unindexedCount = 0;

const vec3* ModGetPlayerDrawColor(const LogicalPlayer* player, bool flag)
{
    const vec3* color = g_origGetPlayerDrawColor(player, flag);
    if (player == nullptr || color != At<const vec3*>(kRvaDefaultColor))
        return color;
    // Couleur par défaut = joueur sans index de joueur (au-delà de 5 places). Couleur choisie par le rang de
    // son identifiant réseau parmi les joueurs sans index déjà vus : même résultat sur toutes les machines.
    const int id = player->GetNetOwnerID();
    int* end = g_unindexedIds + g_unindexedCount;
    if (std::find(g_unindexedIds, end, id) == end) {
        if (g_unindexedCount == 64)
            g_unindexedCount = 0;
        g_unindexedIds[g_unindexedCount++] = id;
        Log("couleur : joueur %d sans index de joueur (%d connu(s))", id, g_unindexedCount);
    }
    const int rank = static_cast<int>(std::count_if(g_unindexedIds, g_unindexedIds + g_unindexedCount,
                                                    [id](int other) { return other < id; }));
    return &kExtraColors[rank % kExtraColorCount];
}

// --- C2 : blocs du HUD pour les coéquipiers 5 et 6 ------------------------------------------------------
using OnInitFn = void (*)(void* self);
OnInitFn g_origHudPlayersListOnInit = nullptr;

bool HasNaN(const vec3& v) { return std::isnan(v.x) || std::isnan(v.y) || std::isnan(v.z); }

// IUIElement::Clone laisse un pivot NaN sur le conteneur de la barre de vie (I_HpBar) et deux de ses enfants : leur
// position absolue devient NaN et le remplissage n'est jamais dessiné (barre vide ; relevé en jeu le 2026-10-08,
// corrigé en direct par cette même méthode). On parcourt l'original et la copie en parallèle (même arborescence)
// et la copie reprend le pivot de l'original partout où le sien est NaN. Renvoie le nombre de pivots corrigés.
int FixClonedPivots(IUIElement* original, IUIElement* copy)
{
    int fixed = 0;
    if (HasNaN(copy->GetUnscaledPivot())) {
        copy->SetUnscaledPivot(original->GetUnscaledPivot());
        ++fixed;
    }
    const int n = original->GetChildrenCount();
    if (n != copy->GetChildrenCount())
        return fixed;
    for (int i = 0; i < n; ++i) {
        IUIElement* a = original->GetChildren(i);
        IUIElement* b = copy->GetChildren(i);
        if (a != nullptr && b != nullptr)
            fixed += FixClonedPivots(a, b);
    }
    return fixed;
}

void ModHudPlayersListOnInit(void* self)
{
    g_origHudPlayersListOnInit(self);
    // HudPlayersList : vecteur des blocs HudPlayerListElement à +0x78 (données, taille, capacité).
    auto* list = reinterpret_cast<TtlVector<IUIElement*>*>(static_cast<std::uint8_t*>(self) + 0x78);
    unsigned wanted = static_cast<unsigned>(kMaxPlaces - 1);  // coéquipiers affichés (sans soi), partie de toute taille
    if (list->m_Size < 2 || list->m_Size >= wanted) {
        Log("HUD : %u bloc(s) G_Player trouvé(s), rien à ajouter", list->m_Size);
        return;
    }
    if (list->m_Capacity < wanted) {  // tableau alloué par le jeu : on n'ajoute que dans sa capacité
        Log("HUD : capacité de la liste %u < %u coéquipiers : limité à %u", list->m_Capacity, wanted,
            list->m_Capacity);
        wanted = list->m_Capacity;
    }
    IUIElement* last = list->m_Data[list->m_Size - 1];
    const vec3 posLast = last->GetRelativePos();
    const vec3 posPrev = list->m_Data[list->m_Size - 2]->GetRelativePos();
    const vec3 step{posLast.x - posPrev.x, posLast.y - posPrev.y, posLast.z - posPrev.z};
    auto dynCast = At<RTDynamicCastFn>(kRvaRTDynamicCast);
    const unsigned before = list->m_Size;
    int pivots = 0;
    for (int k = 1; list->m_Size < wanted; ++k) {
        IUIElement* copy = last->Clone(last->GetParent());
        if (copy == nullptr ||
            dynCast(copy, 0, At<void*>(kRvaTypeIUIElement), At<void*>(kRvaTypeHudPlayerListElement), 0) != copy) {
            Log("HUD : copie de G_Player%u impossible ou de type inattendu (%p)", before - 1, copy);
            break;
        }
        const vec3 pos{posLast.x + k * step.x, posLast.y + k * step.y, posLast.z + k * step.z};
        copy->SetRelativePos(pos);
        pivots += FixClonedPivots(last, copy);
        list->m_Data[list->m_Size++] = copy;
    }
    Log("HUD : %u -> %u blocs de coéquipiers (pas %.1f, %.1f) ; %d pivot(s) NaN corrigé(s) dans les copies", before,
        list->m_Size, step.x, step.y, pivots);
}

// --- Diagnostic du HUD : barre de vie des coéquipiers ---------------------------------------------------
// HudPlayerListElement::Update remplit la barre avec PlayerDI::[+0x125c] / vfunc +0x148 (vie / vie max) du
// personnage trouvé par LogicalPlayer::FindPlayerDI (pair propriétaire) ; sans personnage, le bloc reste vide.
// On journalise, à chaque changement, ce que le bloc voit : joueur, personnage trouvé ou non, vie, vie max.
using ElementUpdateFn = void (*)(void* self);
ElementUpdateFn g_origElementUpdate = nullptr;
using FindPlayerDIFn = void* (*)(const LogicalPlayer*);
using MaxHealthFn = float (*)(void* player, int);

struct ElementSeen {
    void* element;
    int id;
    bool found;
    int health, maxHealth;
};
ElementSeen g_elementsSeen[32];
int g_elementsSeenCount = 0;

void ModElementUpdate(void* self)
{
    g_origElementUpdate(self);
    const auto* player = *reinterpret_cast<const LogicalPlayer* const*>(static_cast<std::uint8_t*>(self) + 0x140);
    ElementSeen now{self, -1, false, -1, -1};
    if (player != nullptr) {
        now.id = player->GetNetOwnerID();
        void* di = At<FindPlayerDIFn>(kRvaFindPlayerDI)(player);
        now.found = di != nullptr;
        if (di != nullptr) {
            now.health = static_cast<int>(*reinterpret_cast<const float*>(static_cast<std::uint8_t*>(di) + 0x125c));
            auto maxHealth = (*reinterpret_cast<MaxHealthFn* const*>(di))[0x148 / 8];
            now.maxHealth = static_cast<int>(maxHealth(di, -1));
        }
    }
    ElementSeen* slot = nullptr;
    for (int i = 0; i < g_elementsSeenCount; ++i)
        if (g_elementsSeen[i].element == self)
            slot = &g_elementsSeen[i];
    if (slot == nullptr) {
        if (g_elementsSeenCount == 32)
            return;
        slot = &g_elementsSeen[g_elementsSeenCount++];
        *slot = {self, -2, false, -2, -2};
    }
    // Vie : changement significatif seulement (pas une ligne par point de vie).
    const bool changed = slot->id != now.id || slot->found != now.found ||
                         (now.health == 0) != (slot->health == 0) || slot->maxHealth != now.maxHealth;
    if (!changed)
        return;
    int* end = g_unindexedIds + g_unindexedCount;
    const bool unindexed = now.id >= 0 && std::find(g_unindexedIds, end, now.id) != end;
    Log("HUD bloc %d : joueur %d%s, personnage %s, vie %d / %d", static_cast<int>(slot - g_elementsSeen), now.id,
        unindexed ? " (sans index)" : "", now.found ? "trouvé" : "INTROUVABLE", now.health, now.maxHealth);
    *slot = now;
}

// --- F1 : rassemblement à 70 % (téléportation des retardataires) ---------------------------------------
// Les missions qui exigent tous les joueurs au même endroit (entrée de bâtiment MapPortal, début de quête
// QuestStartTrigger, phases UseTrigger / BringItem / GatherPlayersGuard…) passent par GatherPlace::Update, appelée sur
// CHAQUE machine pour chaque zone active. Elle range les joueurs dans la zone (liste +0x20), calcule « tous dedans »
// (+1, condition de la mission) et, si le joueur local est dehors alors qu'un autre est dedans, pose un point de
// rassemblement (+3 actif, +4 position et +0x10 direction du 1er joueur arrivé) : c'est la cible du bouton de
// téléportation du jeu (QuickActionTravelToPlayer -> PlayerDI::SetPosAndRot du joueur local).
// Ajout : après l'original, si le point est actif, le joueur local dehors et au moins 70 % des joueurs dedans depuis
// 5 s, le joueur local est téléporté au point, comme par le bouton. Chaque machine ne
// déplace que son propre joueur (position répliquée par le jeu) ; la mission continue ensuite normalement (« tous
// dedans »). Le joueur zombie n'est jamais concerné (GatherPlace::Update ne lui pose aucun point).
struct GatherPlace {
    bool localInside;  // +0x00
    bool allInside;    // +0x01
    bool anyInside;    // +0x02
    bool active;       // +0x03 point de rassemblement posé (LevelDI::AddActiveGatherPlace)
    vec3 pos;          // +0x04
    vec3 dir;          // +0x10
    std::uint32_t _pad1c;
    void** inside;           // +0x20 joueurs dans la zone (PlayerDI*)
    std::uint32_t insideCount;  // +0x28
    std::uint32_t insideCapacity;
};
static_assert(offsetof(GatherPlace, pos) == 0x04 && offsetof(GatherPlace, dir) == 0x10, "GatherPlace");
static_assert(offsetof(GatherPlace, inside) == 0x20 && offsetof(GatherPlace, insideCount) == 0x28, "GatherPlace");

constexpr unsigned kGatherPercent = 70;       // part des joueurs dans la zone (fixe)
constexpr ULONGLONG kGatherDelayMs = 5000;    // attente, seuil atteint, avant la téléportation

using GatherPlaceUpdateFn = void (*)(GatherPlace* self, void* owner);
GatherPlaceUpdateFn g_origGatherPlaceUpdate = nullptr;  // trampoline
using SetPosAndRotFn = void (*)(void* player, const vec3& pos, const vec3& dir);

struct GatherWait {
    const GatherPlace* place = nullptr;  // zone en attente (une seule à la fois)
    ULONGLONG since = 0;                 // seuil atteint depuis
};
GatherWait g_gatherWait;

// Joueur local, par le même chemin que GatherPlace::Update : GameDI +0x540 -> +0xb8 -> (+0x50 non nul) +0x58.
void* LocalPlayerDI()
{
    auto* game = *At<std::uint8_t**>(kRvaGameDISingleton);
    auto* level = game ? *reinterpret_cast<std::uint8_t**>(game + 0x540) : nullptr;
    auto* holder = level ? *reinterpret_cast<std::uint8_t**>(level + 0xb8) : nullptr;
    if (!holder || !*reinterpret_cast<void**>(holder + 0x50))
        return nullptr;
    return *reinterpret_cast<void**>(holder + 0x58);
}

void ModGatherPlaceUpdate(GatherPlace* self, void* owner)
{
    g_origGatherPlaceUpdate(self, owner);
    auto* game = *At<std::uint8_t**>(kRvaGameDISingleton);
    const PlayerList* all = game ? reinterpret_cast<GetAllPlayersFn>(g_base + kRvaGameGetAllPlayers)(game, 0) : nullptr;
    const unsigned total = all ? all->count : 0;
    const unsigned inside = self->insideCount;
    const bool ready = self->active && !self->localInside && total >= 2 && inside < total &&
                       inside * 100 >= total * kGatherPercent;
    if (!ready) {
        if (g_gatherWait.place == self) {
            Log("rassemblement : attente annulée (%u/%u dans la zone, local %s, point %s)", inside, total,
                self->localInside ? "dedans" : "dehors", self->active ? "actif" : "inactif");
            g_gatherWait = {};
        }
        return;
    }
    const ULONGLONG now = GetTickCount64();
    if (g_gatherWait.place != self) {
        g_gatherWait = {self, now};
        Log("rassemblement : %u/%u joueurs dans la zone (seuil %u %%), joueur local dehors : téléportation dans %llu s",
            inside, total, kGatherPercent, kGatherDelayMs / 1000);
        return;
    }
    if (now - g_gatherWait.since < kGatherDelayMs)
        return;
    void* local = LocalPlayerDI();
    if (local == nullptr)
        return;
    const vec3 pos = self->pos, dir = self->dir;  // copies : SetPosAndRot peut relancer une mise à jour
    At<SetPosAndRotFn>(kRvaSetPosAndRot)(local, pos, dir);
    Log("rassemblement : joueur local téléporté au point de rassemblement (%.1f, %.1f, %.1f), %u/%u dans la zone",
        pos.x, pos.y, pos.z, inside, total);
    g_gatherWait.since = now;  // encore dehors au prochain délai : nouvel essai
}

// --- G / Z : au-delà de 4 joueurs, valeurs prévues pour 4 (au lieu du solo) -----------------------------
// Plusieurs systèmes ont des valeurs pour 2, 3 et 4 joueurs (switch sur le nombre) et retombent sur la valeur SOLO
// pour tout autre nombre : à 5 joueurs et plus, groupes de zombies placés, étapes de quête, butin et Be the Zombie
// redevenaient ceux d'un joueur seul (BtZ : réglages absents lus à 0). Ici, le cas « 4 joueurs » vaut pour 4 et plus.
// Liste et adresses : mods/coop6/README.md.
struct FourPlusPatch {
    const char* name;
    std::uintptr_t rva;  // octet modifié (contrôlé avec son contexte dans kExpected)
    std::uint8_t from, to;
};
const FourPlusPatch kFourPlus[] = {
    // `sub r,2 ; je ; sub r,1 ; je ; cmp r,1 ; jne solo` (r = joueurs - 3) : jne (75) -> jl (7C).
    {"G1 groupes placés : zombies au départ", 0x1ccede, 0x75, 0x7C},
    {"G2 groupes placés : total maximum", 0x1ccfee, 0x75, 0x7C},
    {"G3 étape « remplir une zone » : nombre requis", 0xe517d1, 0x75, 0x7C},
    {"G4 étape « attendre / tenir » : délai", 0xea71ea, 0x75, 0x7C},
    {"G5 pièces de réparation dans le butin", 0xbcf921, 0x75, 0x7C},
    {"Z5 BtZ : objets activés selon les chasseurs", 0xa61e7e, 0x75, 0x7C},
    // `cmp n,4 ; mov r,<paramètre 4 joueurs> ; cmove` : cmove (0F 44) -> cmovge (0F 4D).
    {"Z2 BtZ : santé du joueur zombie", 0xba71db, 0x44, 0x4D},
    {"Z3 BtZ : régénération du joueur zombie", 0xce11f2, 0x44, 0x4D},
};

// --- Z : Be the Zombie pour tout nombre de chasseurs (HuntersScale, BtzScale.h) --------------------------
// Chasseurs = joueurs humains (camp 0) : GameDI::GetNumberOfLogicalPlayers(0), ou LevelDI+0xe20 là où le jeu l'inline.
using CountPlayersFn = int (*)(void* game, int side);

int Hunters()
{
    void* game = *At<void**>(kRvaGameDISingleton);
    return game ? At<CountPlayersFn>(kRvaGetNumberOfLogicalPlayers)(game, 0) : 0;
}

// Z4 : ZombieAbilitiesController::GetSpitsRegenTime(type) prend (chasseurs - 1) comme index d'un tableau de 4 temps
// de recharge (zombie_levels.xml, "…AmmoRegenTime1v1".."4v1" : 80 / 50 / 40 / 30 s) et des réglages "…_N_human" ;
// au-delà de 4, elle renvoyait 0 (recharge instantanée). Son appel à GetNumberOfLogicalPlayers (index et clés) passe
// par ModSpitsPlayerCount ; l'enveloppe calcule la valeur à 1 et à 4 chasseurs (nombre imposé) puis HuntersScale.
using SpitsRegenTimeFn = float (*)(void* self, int type);
SpitsRegenTimeFn g_origSpitsRegenTime = nullptr;  // trampoline
int g_spitsForcedHunters = 0;                     // 0 : nombre réel (borné à 4)

int ModSpitsPlayerCount(void* game, int side)
{
    if (g_spitsForcedHunters > 0)
        return g_spitsForcedHunters;
    return std::min(At<CountPlayersFn>(kRvaGetNumberOfLogicalPlayers)(game, side), 4);
}

float ModSpitsRegenTime(void* self, int type)
{
    const int hunters = Hunters();
    if (hunters <= 4)
        return g_origSpitsRegenTime(self, type);
    g_spitsForcedHunters = 1;
    const float v1 = g_origSpitsRegenTime(self, type);
    g_spitsForcedHunters = 4;
    const float v4 = g_origSpitsRegenTime(self, type);
    g_spitsForcedHunters = 0;
    return HuntersScale(v1, v4, hunters);
}

// Z6 : ActionPlayerZombieMission::CalculateHiveClusterRelativeDistance (0x1800e4070) lit une table de 4 distances
// relatives par nombre de chasseurs ([[this+0x70]+0x88 + 4·(chasseurs-1)]) et renvoyait -1.0 au-delà de 4 (choix des
// ruches faussé). Index borné à 3 sur place (18 octets, ebx = 0 à cet endroit) : min(max(chasseurs - 1, 0), 3) ;
// l'enveloppe place pendant l'appel HuntersScale(table[0], table[3], chasseurs) dans la case 3, puis la remet.
const std::uint8_t kHiveIndexCode[18] = {
    0xBA, 0x03, 0x00, 0x00, 0x00,  // mov edx, 3
    0xFF, 0xC8,                    // dec eax
    0x39, 0xD0,                    // cmp eax, edx
    0x0F, 0x4C, 0xD0,              // cmovl edx, eax
    0x85, 0xD2,                    // test edx, edx
    0x0F, 0x48, 0xD3,              // cmovs edx, ebx
    0x90,                          // nop (fin de l'ancien `jge`)
};
using HiveDistanceFn = float (*)(void* self, void* cluster, const vec3* pos);
HiveDistanceFn g_origHiveDistance = nullptr;  // trampoline

// Chasseurs comme la fonction : GameDI +0x540, vfunc +0x7a0 (LevelDI), +0xe20.
int HuntersFromLevel()
{
    auto* game = *At<std::uint8_t**>(kRvaGameDISingleton);
    void* holder = game ? *reinterpret_cast<void**>(game + 0x540) : nullptr;
    if (!holder)
        return 0;
    using GetLevelFn = std::uint8_t* (*)(void*);
    auto* level = (*reinterpret_cast<GetLevelFn**>(holder))[0x7a0 / sizeof(void*)](holder);
    return level ? *reinterpret_cast<int*>(level + 0xe20) : 0;
}

float ModHiveDistance(void* self, void* cluster, const vec3* pos)
{
    const int hunters = HuntersFromLevel();
    auto* params = *reinterpret_cast<std::uint8_t**>(static_cast<std::uint8_t*>(self) + 0x70);
    if (hunters <= 4 || !params)
        return g_origHiveDistance(self, cluster, pos);
    float* table = reinterpret_cast<float*>(params + 0x88);
    MEMORY_BASIC_INFORMATION mbi;  // paramètres lus d'un script (tas) ; par prudence, jamais d'écriture ailleurs
    if (!VirtualQuery(table, &mbi, sizeof(mbi)) || mbi.State != MEM_COMMIT ||
        !(mbi.Protect & (PAGE_READWRITE | PAGE_EXECUTE_READWRITE)))
        return g_origHiveDistance(self, cluster, pos);
    const float saved = table[3];
    table[3] = HuntersScale(table[0], saved, hunters);
    const float result = g_origHiveDistance(self, cluster, pos);
    table[3] = saved;
    return result;
}

// Z1 : réglages BtZ "<nom>_<N>_human" (27 réglages, N = 1..4 dans les données) lus par ITools::GetVarFloat (place
// 0x390 de l'objet ITools du moteur) : clés de LevelDI::GetBtzTunableKeyByCoopPLayerCount (lampe UV), délais de
// réapparition, dégâts au zombie, objets lancés, crachats. Au-delà de 4 chasseurs, ces clés n'existent pas (-> 0) :
// valeur = HuntersScale(valeur "_1_human", valeur "_4_human", N). L'objet ITools n'existe pas encore dans DllMain : la
// place est remplacée quand le jeu le reçoit (CEngineScriptManager::Initialize, détour + trampoline de 14 octets).
struct TtlString {
    const char* data;
    std::uint32_t size;
    std::uint32_t capacity;
};
using GetVarFloatFn = float (*)(void* tools, const TtlString& name, const void* varSet);
GetVarFloatFn g_origGetVarFloat = nullptr;
using ScriptInitializeFn = std::uintptr_t (*)(void* self, void* tools, void* a3, void* a4, std::uintptr_t a5,
                                              std::uintptr_t a6);
ScriptInitializeFn g_origScriptInitialize = nullptr;  // trampoline

float ReadVarFloat(void* tools, const char* key, const void* varSet)
{
    const std::uint32_t len = static_cast<std::uint32_t>(std::strlen(key));
    const TtlString name = {key, len, len};
    return g_origGetVarFloat(tools, name, varSet);
}

float ModGetVarFloat(void* tools, const TtlString& name, const void* varSet)
{
    std::size_t prefix = 0;
    int hunters = 0;
    char key1[256], key4[256];
    if (!name.data || !ParseHumansKey(name.data, prefix, hunters) || hunters <= 4 ||
        !MakeHumansKey(name.data, prefix, 1, key1, sizeof(key1)) || !MakeHumansKey(name.data, prefix, 4, key4, sizeof(key4)))
        return g_origGetVarFloat(tools, name, varSet);
    const float v1 = ReadVarFloat(tools, key1, varSet);
    const float v4 = ReadVarFloat(tools, key4, varSet);
    const float value = HuntersScale(v1, v4, hunters);
    static volatile LONG logged = 0;
    if (InterlockedIncrement(&logged) <= 64)
        Log("Z1 réglage BtZ : %s = %g (1 chasseur %g, 4 chasseurs %g)", name.data, value, v1, v4);
    return value;
}

std::uintptr_t ModScriptInitialize(void* self, void* tools, void* a3, void* a4, std::uintptr_t a5, std::uintptr_t a6)
{
    const std::uintptr_t result = g_origScriptInitialize(self, tools, a3, a4, a5, a6);
    if (!tools)
        return result;
    void** slot = *reinterpret_cast<void***>(tools) + kGetVarFloatSlot;
    if (*slot == reinterpret_cast<void*>(&ModGetVarFloat))
        return result;
    HMODULE owner = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                       static_cast<LPCWSTR>(*slot), &owner);
    HMODULE engine = GetModuleHandleW(L"engine_x64_rwdi.dll");
    if (!engine || owner != engine) {
        Log("Z1 ERREUR : ITools place 0x390 hors d'engine (%p) — réglages BtZ inchangés", *slot);
        return result;
    }
    g_origGetVarFloat = reinterpret_cast<GetVarFloatFn>(*slot);
    void* to = reinterpret_cast<void*>(&ModGetVarFloat);
    WriteMemory(slot, &to, sizeof(to));
    Log("Z1 ITools::GetVarFloat (engine+0x%llx) remplacée : réglages BtZ « _N_human » au-delà de 4 par HuntersScale",
        static_cast<unsigned long long>(reinterpret_cast<std::uint8_t*>(g_origGetVarFloat) -
                                        reinterpret_cast<std::uint8_t*>(engine)));
    return result;
}

void InstallFourPlus()
{
    for (const FourPlusPatch& p : kFourPlus) {
        if (g_base[p.rva] != p.from) {  // déjà contrôlé par kExpected : simple garde
            Log("%s : octet inattendu, laissé tel quel", p.name);
            continue;
        }
        WriteMemory(g_base + p.rva, &p.to, 1);
    }
    Log("G1-G5, Z2, Z3, Z5 : cas « 4 joueurs » étendu à 4 et plus (%zu sites)", std::size(kFourPlus));

    // Z4 : relais de l'appel (nombre de chasseurs) + enveloppe (trampoline sur 16 octets : push rbp ; push rbx ;
    // push rsi ; lea rbp,[rsp-47h] ; sub rsp,90h — sans adresse relative).
    g_origSpitsRegenTime = reinterpret_cast<SpitsRegenTimeFn>(MakeTrampoline(g_base + kRvaSpitsRegenTime, 16));
    if (g_origSpitsRegenTime && RedirectCall(g_base + kRvaSpitsCountCall, reinterpret_cast<void*>(&ModSpitsPlayerCount))) {
        WriteJump(g_base + kRvaSpitsRegenTime, reinterpret_cast<void*>(&ModSpitsRegenTime));
        Log("Z4 GetSpitsRegenTime : au-delà de 4 chasseurs, HuntersScale des temps à 1 et 4 chasseurs");
    } else {
        Log("Z4 ERREUR : trampoline ou relais impossible — crachats inchangés");
    }

    // Z6 : index borné sur place + enveloppe (trampoline sur 18 octets : mov [rsp+8],rbx ; push rdi ; sub rsp,40h ;
    // mov rax,rdx ; movaps [rsp+30h],xmm6 — sans adresse relative).
    WriteMemory(g_base + kRvaHiveIndex + 6, kHiveIndexCode, sizeof(kHiveIndexCode));
    g_origHiveDistance = reinterpret_cast<HiveDistanceFn>(MakeTrampoline(g_base + kRvaHiveDistance, 18));
    if (g_origHiveDistance) {
        WriteJump(g_base + kRvaHiveDistance, reinterpret_cast<void*>(&ModHiveDistance));
        Log("Z6 CalculateHiveClusterRelativeDistance : index borné à 3, distance à N chasseurs par HuntersScale");
    } else {
        Log("Z6 : trampoline impossible, index seulement borné à 3");
    }

    g_origScriptInitialize = reinterpret_cast<ScriptInitializeFn>(MakeTrampoline(g_base + kRvaScriptInitialize, 14));
    if (g_origScriptInitialize) {
        WriteJump(g_base + kRvaScriptInitialize, reinterpret_cast<void*>(&ModScriptInitialize));
        Log("Z1 CEngineScriptManager::Initialize détournée (trampoline %p) : GetVarFloat remplacée à l'initialisation",
            g_origScriptInitialize);
    } else {
        Log("Z1 : trampoline impossible, réglages BtZ inchangés");
    }
}

// --- Installation ---------------------------------------------------------------------------------------
void Install()
{
    g_base = reinterpret_cast<std::uint8_t*>(GetModuleHandleW(L"gamedll_x64_rwdi.dll"));
    if (!g_base) {
        Log("ERREUR : gamedll_x64_rwdi.dll non chargée — mod inactif");
        return;
    }
    Log("gamedll à %p ; Places=%d Difficulte=%d Couleurs=%d Hud=%d SansEos=%d", g_base, g_settings.places,
        g_settings.difficulty, g_settings.colors, g_settings.hud, g_settings.noEos);
    if (g_settings.noEos)
        InstallNoEos();  // indépendant des contrôles de gamedll
    bool ok = true;
    for (const Expected& e : kExpected) {
        if (std::memcmp(g_base + e.rva, e.bytes, e.size) != 0) {
            Log("ERREUR : octets inattendus pour %s (rva 0x%llx) — pas la 1.55 ?", e.name,
                static_cast<unsigned long long>(e.rva));
            ok = false;
        }
    }
    if (!ok) {
        Log("mod inactif : rien n'a été modifié");
        return;
    }
    g_findParam = At<FindParamFn>(kRvaFindParam);

    // A1 + A2 : lobby.
    g_origInitializeLobbyInfo = At<InitializeLobbyInfoFn>(kRvaInitializeLobbyInfo);
    const int nLobby = PatchVftables(g_base + kRvaInitializeLobbyInfo, reinterpret_cast<void*>(&ModInitializeLobbyInfo));
    const unsigned char places = static_cast<unsigned char>(g_settings.places);
    WriteMemory(g_base + kRvaValidateLobbySlots + 1, &places, 1);
    Log("A1 InitializeLobbyInfo enveloppée dans %d vftable(s) (attendu 2) ; A2 plafond 5 -> %d", nLobby, places);

    // A3 : places d'équipe. CheckCanConnect : `mov edi,1 ; jmp short +2Fh` (vers la suite des contrôles, comme
    // quand l'identifiant est trouvé dans une place) + nop, à la place de l'erreur 0x2e.
    const unsigned char permit[18] = {0xBF, 0x01, 0x00, 0x00, 0x00, 0xEB, 0x2F, 0x90, 0x90,
                                      0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
    WriteMemory(g_base + kRvaCheckCanConnectNoSlot, permit, sizeof(permit));
    WriteJump(g_base + kRvaUpdateZombieCanJoin, reinterpret_cast<void*>(&ModLobbyUpdateZombieCanJoin));
    WriteJump(g_base + kRvaCoopUpdateZombieCanJoin, reinterpret_cast<void*>(&ModCoopLobbyUpdateZombieCanJoin));
    const unsigned char humansMax = static_cast<unsigned char>(g_settings.places - 1);
    WriteMemory(g_base + kRvaIsJoinableHumans + 2, &humansMax, 1);
    Log("A3 CheckCanConnect : humain sans place d'équipe autorisé ; Lobby_UpdateZombieCanJoin (SessionDI et "
        "SessionCooperativeDI) réécrites ; IsJoinable : humains < %d", humansMax);

    // B2 : liste des index de joueur.
    WriteJump(g_base + kRvaUpdatePlayerIndexList, reinterpret_cast<void*>(&ModUpdatePlayerIndexList));
    Log("B2 UpdatePlayerIndexList réécrite (sans débordement de pile au-delà de 8 joueurs)");

    // Diagnostic : bits publiés (trampoline sur 15 octets sans adresse relative).
    if (g_settings.diagnostic) {
        g_origSetGameBitProperties =
            reinterpret_cast<SetGameBitPropertiesFn>(MakeTrampoline(g_base + kRvaSetGameBitProperties, 15));
        if (g_origSetGameBitProperties)
            WriteJump(g_base + kRvaSetGameBitProperties, reinterpret_cast<void*>(&ModSetGameBitProperties));
        g_origValidateResult = reinterpret_cast<ValidateResultFn>(MakeTrampoline(g_base + kRvaValidateResult, 15));
        if (g_origValidateResult)
            WriteJump(g_base + kRvaValidateResult, reinterpret_cast<void*>(&ModValidateResult));
        Log("diagnostic actif (bits publiés, raisons de refus, compteurs de session, verdicts de recherche)");
    }

    // A4 : IsJoinable, cas « joueurs rejoints + 1 < 5 » -> < Places.
    WriteMemory(g_base + kRvaIsJoinablePlayers + 10, &places, 1);
    Log("A4 IsJoinable : joueurs < %d", places);

    // A5 : côté client, maximum de joueurs d'une partie trouvée 5 -> kMaxPlaces (toujours le maximum : la partie
    // trouvée a les places de SON hôte, qui décide seul s'il accepte encore quelqu'un).
    const unsigned char searchMax = static_cast<unsigned char>(kMaxPlaces);
    WriteMemory(g_base + kRvaScanResultCtorMax + 6, &searchMax, 1);
    WriteMemory(g_base + kRvaExtractResultsMax + 6, &searchMax, 1);
    Log("A5 recherche : maximum de joueurs d'une partie trouvée 5 -> %d", searchMax);

    // B5 : AIManager::LimitAtackingWalkers, liste des joueurs bornée à 5 (tableaux de pile de 5).
    if (RedirectCall(g_base + kRvaLimitWalkersGetPlayers, reinterpret_cast<void*>(&ModLimitWalkersGetPlayers)))
        Log("B5 LimitAtackingWalkers : liste des joueurs bornée à 5 (plus de corruption du tas au 6e joueur)");
    else
        Log("B5 ERREUR : pas de mémoire proche pour le relais — 6e joueur : corruption du tas");

    // B6 : AISpawnSystem::Manager::CalculateSpawnPlaceRatings, liste des joueurs bornée à 5 (tableaux de 5).
    if (RedirectCall(g_base + kRvaSpawnRatingsGetPlayers, reinterpret_cast<void*>(&ModSpawnRatingsGetPlayers)))
        Log("B6 CalculateSpawnPlaceRatings : liste des joueurs bornée à 5 (plus de plantage de l'hôte au 11e joueur)");
    else
        Log("B6 ERREUR : pas de mémoire proche pour le relais — 11e joueur : plantage de l'hôte");

    // B7 : boucle de RegenerateSpawnPlan, même fenêtre tournante de 5 joueurs que les notes (B6).
    if (RedirectCall(g_base + kRvaSpawnLoopGetPlayers, reinterpret_cast<void*>(&ModSpawnLoopGetPlayers)))
        Log("B7 RegenerateSpawnPlan : fenêtre tournante de %u joueurs (notes et plan), tous servis à tour de rôle",
            kSpawnWindow);
    else
        Log("B7 ERREUR : pas de mémoire proche pour le relais — joueurs 6+ : lieux d'apparition mal choisis");

    // D1 : difficulté.
    if (g_settings.difficulty) {
        WriteJump(g_base + kRvaGetMaxHealthCoopMul, reinterpret_cast<void*>(&ModGetMaxHealthCoopMul));
        WriteJump(g_base + kRvaGetMeleeDamageCoopMul, reinterpret_cast<void*>(&ModGetMeleeDamageCoopMul));
        const int nStamina = PatchVftables(g_base + kRvaGetMaxStamina, reinterpret_cast<void*>(&ModGetMaxStamina));
        Log("D1 GetMaxHealthCoopMul et GetMeleeDamageCoopMul détournées ; GetMaxStamina remplacée dans %d "
            "vftable(s) (attendu 4)", nStamina);
    }

    // B3 : couleurs (trampoline sur les 15 premiers octets : mov [rsp+8],rbx ; mov [rsp+10h],rsi ; push rdi ;
    // sub rsp,30h — sans adresse relative).
    if (g_settings.colors) {
        g_origGetPlayerDrawColor =
            reinterpret_cast<GetPlayerDrawColorFn>(MakeTrampoline(g_base + kRvaGetPlayerDrawColor, 15));
        if (g_origGetPlayerDrawColor) {
            WriteJump(g_base + kRvaGetPlayerDrawColor, reinterpret_cast<void*>(&ModGetPlayerDrawColor));
            Log("B3 GetPlayerDrawColor détournée (trampoline %p)", g_origGetPlayerDrawColor);
        } else {
            Log("B3 : trampoline impossible, couleurs inchangées");
        }
    }

    // F1 : rassemblement (trampoline sur les 15 premiers octets : mov [rsp+10h],rdx ; push rsi ; push rdi ;
    // push r15 ; sub rsp,40h ; xor esi,esi — sans adresse relative).
    g_origGatherPlaceUpdate = reinterpret_cast<GatherPlaceUpdateFn>(MakeTrampoline(g_base + kRvaGatherPlaceUpdate, 15));
    if (g_origGatherPlaceUpdate) {
        WriteJump(g_base + kRvaGatherPlaceUpdate, reinterpret_cast<void*>(&ModGatherPlaceUpdate));
        Log("F1 GatherPlace::Update détournée : téléportation à %u %% des joueurs après %llu s (trampoline %p)",
            kGatherPercent, kGatherDelayMs / 1000, g_origGatherPlaceUpdate);
    } else {
        Log("F1 : trampoline impossible, rassemblement inchangé");
    }

    // G / Z : au-delà de 4 joueurs, valeurs prévues pour 4 au lieu du solo.
    InstallFourPlus();

    // C2 : HUD.
    if (g_settings.hud) {
        g_origHudPlayersListOnInit = At<OnInitFn>(kRvaHudPlayersListOnInit);
        const int nHud = PatchVftables(g_base + kRvaHudPlayersListOnInit, reinterpret_cast<void*>(&ModHudPlayersListOnInit));
        Log("C2 HudPlayersList::OnInit enveloppée dans %d vftable(s)", nHud);
        if (g_settings.diagnostic) {
            g_origElementUpdate = At<ElementUpdateFn>(kRvaHudPlayerListElementUpdate);
            const int nElem = PatchVftables(g_base + kRvaHudPlayerListElementUpdate,
                                            reinterpret_cast<void*>(&ModElementUpdate));
            Log("diagnostic HUD : HudPlayerListElement::Update enveloppée dans %d vftable(s)", nElem);
        }
    }
    Log("installation terminée");
}

}  // namespace

// AIDataSetWrapper::FindParam du jeu (non exportée) : appelée par son adresse.
AIDataParam* AIDataSetWrapper::FindParam(int id) const { return g_findParam(this, id); }

// Relevé de plantage (diagnostic) : code, adresse, registres et pile déroulée (module+décalage) écrits directement
// dans le journal par WriteFile, sans allocation (le tas peut être corrompu : c0000374).
HANDLE g_logHandle = INVALID_HANDLE_VALUE;

void CrashWrite(const char* fmt, ...)
{
    char line[512];
    va_list args;
    va_start(args, fmt);
    const int n = _vsnprintf_s(line, sizeof(line), _TRUNCATE, fmt, args);
    va_end(args);
    DWORD written = 0;
    if (n > 0 && g_logHandle != INVALID_HANDLE_VALUE)
        WriteFile(g_logHandle, line, static_cast<DWORD>(n), &written, nullptr);
}

void CrashWriteAddress(const char* label, DWORD64 address)
{
    HMODULE module = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(address), &module) && module) {
        GetModuleFileNameA(module, name, MAX_PATH);
        const char* base = strrchr(name, '\\');
        CrashWrite("  %s %016llx  %s+0x%llx\r\n", label, address, base ? base + 1 : name,
                   address - reinterpret_cast<DWORD64>(module));
    } else {
        CrashWrite("  %s %016llx\r\n", label, address);
    }
}

LONG CALLBACK CrashHandler(EXCEPTION_POINTERS* info)
{
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if (code != 0xC0000374 && code != 0xC0000005 && code != 0xC0000409 && code != 0xC000001D && code != 0xC0000096)
        return EXCEPTION_CONTINUE_SEARCH;
    static volatile LONG count = 0;
    if (InterlockedIncrement(&count) > 4)
        return EXCEPTION_CONTINUE_SEARCH;
    SYSTEMTIME t;
    GetLocalTime(&t);
    CONTEXT ctx = *info->ContextRecord;
    CrashWrite("%02d:%02d:%02d.%03d PLANTAGE code 0x%08lx, fil %lu\r\n", t.wHour, t.wMinute, t.wSecond,
               t.wMilliseconds, code, GetCurrentThreadId());
    CrashWriteAddress("adresse", reinterpret_cast<DWORD64>(info->ExceptionRecord->ExceptionAddress));
    if (code == 0xC0000005 && info->ExceptionRecord->NumberParameters >= 2)
        CrashWrite("  accès %llu à %016llx\r\n", info->ExceptionRecord->ExceptionInformation[0],
                   info->ExceptionRecord->ExceptionInformation[1]);
    CrashWrite("  rax %016llx rbx %016llx rcx %016llx rdx %016llx\r\n"
               "  rsi %016llx rdi %016llx rbp %016llx rsp %016llx\r\n"
               "  r8 %016llx r9 %016llx r12 %016llx r13 %016llx r14 %016llx r15 %016llx\r\n",
               ctx.Rax, ctx.Rbx, ctx.Rcx, ctx.Rdx, ctx.Rsi, ctx.Rdi, ctx.Rbp, ctx.Rsp, ctx.R8, ctx.R9, ctx.R12,
               ctx.R13, ctx.R14, ctx.R15);
    for (int frame = 0; frame < 48 && ctx.Rip; ++frame) {
        CrashWriteAddress("pile", ctx.Rip);
        DWORD64 imageBase = 0;
        PRUNTIME_FUNCTION function = RtlLookupFunctionEntry(ctx.Rip, &imageBase, nullptr);
        if (!function) {  // fonction feuille : l'adresse de retour est au sommet de la pile
            ctx.Rip = *reinterpret_cast<DWORD64*>(ctx.Rsp);
            ctx.Rsp += 8;
            continue;
        }
        void* handlerData = nullptr;
        DWORD64 establisher = 0;
        RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, ctx.Rip, function, &ctx, &handlerData, &establisher, nullptr);
    }
    // Repli : adresses de la pile brute qui tombent dans gamedll / engine (si le déroulement s'est perdu).
    const DWORD64* stack = reinterpret_cast<const DWORD64*>(info->ContextRecord->Rsp);
    const DWORD64 game = reinterpret_cast<DWORD64>(GetModuleHandleW(L"gamedll_x64_rwdi.dll"));
    const DWORD64 engine = reinterpret_cast<DWORD64>(GetModuleHandleW(L"engine_x64_rwdi.dll"));
    const DWORD64 stackBase = reinterpret_cast<DWORD64>(reinterpret_cast<NT_TIB*>(NtCurrentTeb())->StackBase);
    for (int i = 0, shown = 0; i < 2048 && shown < 40 && reinterpret_cast<DWORD64>(stack + i + 1) <= stackBase; ++i) {
        const DWORD64 v = stack[i];
        if ((v - game < 0x2000000) || (v - engine < 0x1000000)) {
            CrashWriteAddress("brut", v);
            ++shown;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

BOOL WINAPI DllMain(HINSTANCE self, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(self);
        GetModuleFileNameW(self, g_dir, MAX_PATH);
        if (wchar_t* slash = wcsrchr(g_dir, L'\\'))
            *slash = 0;
        wchar_t index[8] = L"";
        wchar_t logPath[MAX_PATH];
        if (GetEnvironmentVariableW(L"DLF_INDEX", index, 8) > 0)
            swprintf_s(logPath, L"%s\\coop6_instance_%s.log", g_dir, index);
        else
            swprintf_s(logPath, L"%s\\coop6.log", g_dir);
        g_log = _wfsopen(logPath, L"ab", _SH_DENYNO);  // ajout : relevés gardés d'une relance à l'autre
        if (g_log)
            g_logHandle = reinterpret_cast<HANDLE>(_get_osfhandle(_fileno(g_log)));
        AddVectoredExceptionHandler(1, CrashHandler);
        LoadSettings();
        Log("coop6.dll chargée (Dying Light 1.55, pid %lu)", GetCurrentProcessId());
        Install();  // dans DllMain : avant le point d'entrée du jeu ; aucun chargement de DLL ici
    }
    return TRUE;
}
