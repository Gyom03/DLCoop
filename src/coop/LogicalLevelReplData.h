// Classes partielles : index des joueurs de la partie (coop) — gamedll_x64_rwdi.dll 1.55.
// LogicalLevelReplData : objet répliqué (0x2a8 octets, fabrique @ 0x18048e8b0) qui associe à chaque index de
// joueur (0..4 : 4 humains + le joueur zombie) l'identifiant réseau de son propriétaire. L'index sert aux
// couleurs des joueurs (HUDDIManager::GetPlayerDrawColor), aux balises et à la carte.
#pragma once
#include "EngineObjects.h"

class LogicalPlayer : public IGSObject {};

class LogicalLevelReplData {
public:
    static constexpr unsigned MAX_PLAYERS = 5;  // 4 humains + 1 joueur zombie (Be the Zombie)
    static constexpr int NO_OWNER = 0xffff;     // valeur d'un index libre (constructeur)

    std::uint8_t _pad000[0x290];
    int m_ReplPlayerIndex[MAX_PLAYERS];  // +0x290 — répliqué ; identifiant réseau du joueur de chaque index
    std::uint8_t _pad2a4[0x2a8 - 0x2a4];

    int GetPlayerIndex(const LogicalPlayer* player) const;
};
DL_CHECK_OFFSET(LogicalLevelReplData, m_ReplPlayerIndex, 0x290);
static_assert(sizeof(LogicalLevelReplData) == 0x2a8, "taille de LogicalLevelReplData");
