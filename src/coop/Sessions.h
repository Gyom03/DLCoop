// Classes partielles du système de session / lobby (coop) — gamedll_x64_rwdi.dll 1.55.
// Positions vérifiées dans le code machine Windows (SessionCooperative::InitializeLobbyInfo @ 0x1804a62c0,
// SessionDI::Create @ 0x1804b2da0) ; noms d'après la version Linux (SettingsDI::GetLobbySettings,
// SessionCooperative::InitializeLobbyInfo, Sessions::LobbyInfo::SetMaxSlots).
#pragma once
#include "../common/game_abi.h"
#include "Vpad.h"

class SessionDI;
class SettingsDI;

// Réglages du lobby (menu « en ligne ») : copie dans les réglages du jeu et dans la session en cours.
struct LobbySettings {
    std::uint8_t _pad00[0x10];
    std::int32_t m_GameVisibility;  // +0x10 — 0..2 : partie en ligne (publique / amis / privée), 3 : LAN
};
DL_CHECK_OFFSET(LobbySettings, m_GameVisibility, 0x10);

class GameDI {
public:
    std::uint8_t _pad000[0x50];
    SettingsDI* m_Settings;  // +0x50
    std::uint8_t _pad058[0x540 - 0x58];
    SessionDI* m_Session;    // +0x540 — session en cours (nulle au menu)

    static GameDI* GetSingleton();
};
DL_CHECK_OFFSET(GameDI, m_Settings, 0x50);
DL_CHECK_OFFSET(GameDI, m_Session, 0x540);

// Singleton du jeu (gamedll .data @ 0x181c16348 ; Linux : Game::sm_cSingleton).
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
    std::uint8_t m_DirtyFlags;      // +0xc — bit 1 : réglages du lobby lus pour modification
    std::uint8_t _pad0d[0xe0 - 0xd];
    LobbySettings m_LobbySettings;  // +0xe0

    // Linux : SettingsDI::GetLobbySettings(bool) (inlinée sous Windows).
    // Les réglages de la session en cours s'il y en a une, sinon ceux du jeu (marqués modifiés).
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

// Interface des constantes du lobby propres au jeu (Linux : ILobbyGameSpecific).
class ILobbyGameSpecific {
public:
    DL_VPAD10(0) DL_VPAD10(1) DL_VPAD10(2) DL_VPAD10(3) DL_VPAD10(4) DL_VPAD10(5)  // places 0..59
    DL_VPAD1(60) DL_VPAD1(61) DL_VPAD1(62) DL_VPAD1(63) DL_VPAD1(64) DL_VPAD1(65) DL_VPAD1(66)
    // Place 67 (+0x218) — Linux : ILobbyGameSpecific::Get_SESSION_MATCH_QUERY_FIND_COOPERATIVE().
    virtual int Get_SESSION_MATCH_QUERY_FIND_COOPERATIVE();
};
// gamedll .data @ 0x181c11ee8 (Linux : g_LobbyGameSpecific).
extern ILobbyGameSpecific* g_LobbyGameSpecific;

namespace EGamePart { enum TYPE : int { Undefined = 0, Prologue = 1 }; }

namespace Sessions {
// Description du lobby à créer (Linux : Sessions::LobbyInfo, 0x14 octets).
struct LobbyInfo {
    std::int32_t m_SessionType;    // +0x0 — 0 : solo, 1 : en ligne, 2 : LAN (CLobby::CreateSessionObject)
    std::uint32_t m_PublicSlots;   // +0x4
    std::uint32_t m_PrivateSlots;  // +0x8
    std::int32_t m_Unknown0C;      // +0xc — 1, ou 2 si le bit 1 de SessionCooperative+0x2c20 est posé
    std::int32_t m_SearchQuery;    // +0x10 — requête de recherche des parties coop
};
DL_CHECK_OFFSET(LobbyInfo, m_PublicSlots, 0x4);
DL_CHECK_OFFSET(LobbyInfo, m_SearchQuery, 0x10);
}  // namespace Sessions

class SessionCooperative {
public:
    std::uint8_t _pad0000[0x2c20];
    std::uint8_t m_Flags2C20;  // +0x2c20 (Linux +0x2c00) — bit 1 lu par InitializeLobbyInfo

    // Vtable (SessionCooperative et SessionCooperativeDI) ; Linux place 236.
    void InitializeLobbyInfo(Sessions::LobbyInfo& info, EGamePart::TYPE part);
};
DL_CHECK_OFFSET(SessionCooperative, m_Flags2C20, 0x2c20);
