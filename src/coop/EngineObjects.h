// Engine objects (engine_x64_rwdi.dll) used by the co-op rewrites: only the exported methods that are called
// are declared (calls through the import table, `call [rip+X]`, like in the game).
#pragma once
#include "../common/game_abi.h"

class ILevel;

class IGSObject {
public:
    // engine: ?GetNetOwnerID@IGSObject@@QEBAHXZ — network id of the owning player.
    __declspec(dllimport) int GetNetOwnerID() const;
    // engine: ?GetLevel@IGSObject@@QEBAPEAVILevel@@XZ (Linux: GameObject::GetLevel).
    __declspec(dllimport) ILevel* GetLevel() const;
};

class ILevel {};

// Engine vector (ttl::vector): data, element count, capacity.
template <typename T>
struct TtlVector {
    T* m_Data;
    std::uint32_t m_Size;
    std::uint32_t m_Capacity;
};

class PlayerDI;

// Game level (partial). Offsets verified against the Windows machine code.
class LevelDI : public ILevel {
public:
    std::uint8_t _pad000[0xe18];
    TtlVector<PlayerDI*> m_AllPlayers;  // +0xe18 — all players (side 0; Linux +0xe10)

    // Linux: LevelDI::GetAllPlayersCount(EPlayerConflictSide::TYPE) with side 0 (all): the only case used
    // here, reduced by the compiler to reading the list size (`mov edx,[rax+0xe20]`).
    std::uint32_t GetAllPlayersCount() const { return m_AllPlayers.m_Size; }
};
DL_CHECK_OFFSET(LevelDI, m_AllPlayers, 0xe18);

// Game object (Linux: ObjectDI) whose IGSObject part is at +0x28 (MSVC RTTI: GameObject/IGSObject
// mdisp 0x28 for HumanAI).
class ObjectDI {
public:
    virtual void _vpad_0();  // polymorphic object: vftable at +0 (later slots declared by the subclasses)

    std::uint8_t _pad08[0x28 - 0x8];
    IGSObject m_GSObject;  // +0x28 (IGSObject base, empty object: 1 byte)

    // Linux: ObjectDI::GetLevelDI() = GameObject::GetLevel() on the +0x28 base.
    LevelDI* GetLevelDI() const { return static_cast<LevelDI*>(m_GSObject.GetLevel()); }
};
DL_CHECK_OFFSET(ObjectDI, m_GSObject, 0x28);
