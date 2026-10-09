// Partial classes: the game's player indices (co-op) — gamedll_x64_rwdi.dll 1.55.
// LogicalLevelReplData: a replicated object (0x2a8 bytes, factory @ 0x18048e8b0) that maps each player index
// (0..4: 4 humans + the zombie player) to the network id of its owner. The index drives the player colors
// (HUDDIManager::GetPlayerDrawColor), the markers and the map.
#pragma once
#include "EngineObjects.h"

class LogicalPlayer : public IGSObject {};

class LogicalLevelReplData {
public:
    static constexpr unsigned MAX_PLAYERS = 5;  // 4 humans + 1 zombie player (Be the Zombie)
    static constexpr int NO_OWNER = 0xffff;     // value of a free index (constructor)

    std::uint8_t _pad000[0x290];
    int m_ReplPlayerIndex[MAX_PLAYERS];  // +0x290 — replicated; network id of the player at each index
    std::uint8_t _pad2a4[0x2a8 - 0x2a4];

    int GetPlayerIndex(const LogicalPlayer* player) const;
};
DL_CHECK_OFFSET(LogicalLevelReplData, m_ReplPlayerIndex, 0x290);
static_assert(sizeof(LogicalLevelReplData) == 0x2a8, "LogicalLevelReplData size");
