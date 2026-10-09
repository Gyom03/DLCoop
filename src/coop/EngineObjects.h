// Objets du moteur (engine_x64_rwdi.dll) utilisés par les réécritures coop : seules les méthodes exportées
// appelées sont déclarées (appels par la table d'imports, `call [rip+X]`, comme dans le jeu).
#pragma once
#include "../common/game_abi.h"

class ILevel;

class IGSObject {
public:
    // engine : ?GetNetOwnerID@IGSObject@@QEBAHXZ — identifiant réseau du joueur propriétaire.
    __declspec(dllimport) int GetNetOwnerID() const;
    // engine : ?GetLevel@IGSObject@@QEBAPEAVILevel@@XZ (Linux : GameObject::GetLevel).
    __declspec(dllimport) ILevel* GetLevel() const;
};

class ILevel {};

// Vecteur du moteur (ttl::vector) : données, nombre d'éléments, capacité.
template <typename T>
struct TtlVector {
    T* m_Data;
    std::uint32_t m_Size;
    std::uint32_t m_Capacity;
};

class PlayerDI;

// Niveau de jeu (partiel). Positions vérifiées dans le code machine Windows.
class LevelDI : public ILevel {
public:
    std::uint8_t _pad000[0xe18];
    TtlVector<PlayerDI*> m_AllPlayers;  // +0xe18 — tous les joueurs (côté 0 ; Linux +0xe10)

    // Linux : LevelDI::GetAllPlayersCount(EPlayerConflictSide::TYPE) avec le côté 0 (tous) : seul cas utilisé
    // ici, réduit par le compilateur à la lecture de la taille de la liste (`mov edx,[rax+0xe20]`).
    std::uint32_t GetAllPlayersCount() const { return m_AllPlayers.m_Size; }
};
DL_CHECK_OFFSET(LevelDI, m_AllPlayers, 0xe18);

// Objet de jeu (Linux : ObjectDI) dont la partie IGSObject est à +0x28 (RTTI MSVC : GameObject/IGSObject
// mdisp 0x28 pour HumanAI).
class ObjectDI {
public:
    virtual void _vpad_0();  // objet polymorphe : vftable à +0 (places suivantes déclarées par les dérivées)

    std::uint8_t _pad08[0x28 - 0x8];
    IGSObject m_GSObject;  // +0x28 (base IGSObject, objet vide : 1 octet)

    // Linux : ObjectDI::GetLevelDI() = GameObject::GetLevel() sur la base +0x28.
    LevelDI* GetLevelDI() const { return static_cast<LevelDI*>(m_GSObject.GetLevel()); }
};
DL_CHECK_OFFSET(ObjectDI, m_GSObject, 0x28);
