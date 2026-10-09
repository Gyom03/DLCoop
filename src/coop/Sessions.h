// Partial classes of the session / lobby system (co-op) — gamedll_x64_rwdi.dll 1.55.
// Offsets verified against the Windows machine code (SessionCooperative::InitializeLobbyInfo @ 0x1804a62c0,
// SessionDI::Create @ 0x1804b2da0); names from the Linux build (SettingsDI::GetLobbySettings,
// SessionCooperative::InitializeLobbyInfo, Sessions::LobbyInfo::SetMaxSlots).
#pragma once
#include "../common/game_abi.h"
#include "Vpad.h"

class SessionDI;
class SettingsDI;

// Lobby settings ("online" menu): copied into the game settings and into the current session.
struct LobbySettings {
    std::uint8_t _pad00[0x10];
    std::int32_t m_GameVisibility;  // +0x10 — 0..2: online game (public / friends / private), 3: LAN
};
DL_CHECK_OFFSET(LobbySettings, m_GameVisibility, 0x10);

class GameDI {
public:
    std::uint8_t _pad000[0x50];
    SettingsDI* m_Settings;  // +0x50
    std::uint8_t _pad058[0x540 - 0x58];
    SessionDI* m_Session;    // +0x540 — current session (null at the menu)

    static GameDI* GetSingleton();
};
DL_CHECK_OFFSET(GameDI, m_Settings, 0x50);
DL_CHECK_OFFSET(GameDI, m_Session, 0x540);

// Game singleton (gamedll .data @ 0x181c16348; Linux: Game::sm_cSingleton).
extern GameDI* g_GameDISingleton;
inline GameDI* GameDI::GetSingleton() { return g_GameDISingleton; }

class SessionDI {
public:
    std::uint8_t _pad0000[0x7d58];
    LobbySettings m_LobbySettings;  // +0x7d58 (Linux +0x7d18)
};
DL_CHECK_OFFSET(SessionDI, m_LobbySettings, 0x7d58);

class SettingsDI {
public:
    std::uint8_t _pad00[0xc];
    std::uint8_t m_DirtyFlags;      // +0xc — bit 1: lobby settings read for modification
    std::uint8_t _pad0d[0xe0 - 0xd];
    LobbySettings m_LobbySettings;  // +0xe0

    // Linux: SettingsDI::GetLobbySettings(bool) (inlined on Windows).
    // The current session's settings if there is one, otherwise the game's (marked dirty).
    LobbySettings& GetLobbySettings(bool fromSession)
    {
        SessionDI* session = GameDI::GetSingleton()->m_Session;
        if (session != nullptr && fromSession)
            return session->m_LobbySettings;
        m_DirtyFlags |= 2;
        return m_LobbySettings;
    }
};
DL_CHECK_OFFSET(SettingsDI, m_DirtyFlags, 0xc);
DL_CHECK_OFFSET(SettingsDI, m_LobbySettings, 0xe0);

// Interface for the game-specific lobby constants (Linux: ILobbyGameSpecific).
class ILobbyGameSpecific {
public:
    DL_VPAD10(0) DL_VPAD10(1) DL_VPAD10(2) DL_VPAD10(3) DL_VPAD10(4) DL_VPAD10(5)  // slots 0..59
    DL_VPAD1(60) DL_VPAD1(61) DL_VPAD1(62) DL_VPAD1(63) DL_VPAD1(64) DL_VPAD1(65) DL_VPAD1(66)
    // Slot 67 (+0x218) — Linux: ILobbyGameSpecific::Get_SESSION_MATCH_QUERY_FIND_COOPERATIVE().
    virtual int Get_SESSION_MATCH_QUERY_FIND_COOPERATIVE();
};
// gamedll .data @ 0x181c11ee8 (Linux: g_LobbyGameSpecific).
extern ILobbyGameSpecific* g_LobbyGameSpecific;

namespace EGamePart { enum TYPE : int { Undefined = 0, Prologue = 1 }; }

namespace Sessions {
// Description of the lobby to create (Linux: Sessions::LobbyInfo, 0x14 bytes).
struct LobbyInfo {
    std::int32_t m_SessionType;    // +0x0 — 0: solo, 1: online, 2: LAN (CLobby::CreateSessionObject)
    std::uint32_t m_PublicSlots;   // +0x4
    std::uint32_t m_PrivateSlots;  // +0x8
    std::int32_t m_Unknown0C;      // +0xc — 1, or 2 if bit 1 of SessionCooperative+0x2c20 is set
    std::int32_t m_SearchQuery;    // +0x10 — co-op game search query
};
DL_CHECK_OFFSET(LobbyInfo, m_PublicSlots, 0x4);
DL_CHECK_OFFSET(LobbyInfo, m_SearchQuery, 0x10);
}  // namespace Sessions

class SessionCooperative {
public:
    std::uint8_t _pad0000[0x2c20];
    std::uint8_t m_Flags2C20;  // +0x2c20 (Linux +0x2c00) — bit 1 read by InitializeLobbyInfo

    // Vtable (SessionCooperative and SessionCooperativeDI); Linux slot 236.
    void InitializeLobbyInfo(Sessions::LobbyInfo& info, EGamePart::TYPE part);
};
DL_CHECK_OFFSET(SessionCooperative, m_Flags2C20, 0x2c20);
