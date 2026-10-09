// HumanAI (human enemies) — partial, for the co-op difficulty multipliers. gamedll 1.55.
#pragma once
#include "EngineObjects.h"
#include "Vpad.h"

// A parameter of an AI data set (Linux: the entry returned by AIDataSetWrapper::FindParam).
class AIDataParam {
public:
    virtual void _vpad_0();
    virtual void _vpad_1();
    virtual void _vpad_2();
    virtual void _vpad_3();
    virtual void _vpad_4();
    // Slot 5 (+0x28; Linux +0x30, GCC having 2 destructors): address of the value.
    virtual const void* GetValuePtr() const;

    std::uint8_t _pad08[0x18 - 0x8];
    std::uint8_t m_Flags;  // +0x18 — bit 0: value is set
};
DL_CHECK_OFFSET(AIDataParam, m_Flags, 0x18);

// Access to a character's AI parameters (EAIParam).
class AIDataSetWrapper {
public:
    AIDataParam* FindParam(int id) const;  // gamedll @ 0x180233c60

    // Linux: AIDataSetWrapper::GetParam<T>(int) const (inlined on Windows): the parameter value, or a static
    // zero value if it is not set.
    template <typename T>
    const T& GetParam(int id) const
    {
        AIDataParam* param = FindParam(id);
        if (param->m_Flags & 1)
            return *static_cast<const T*>(param->GetValuePtr());
        static T temp;
        return temp;
    }
};

namespace EAIParam {
enum TYPE : int {
    STAMINA_MUL = 118,
    STAMINA_TO_HEALTH_RATIO = 119,
    COOP_DAMAGE_MUL_2 = 641,  // `coop_damage_mul_2` (human_data_params.scr: 1.1)
    COOP_DAMAGE_MUL_3 = 642,  // 1.2
    COOP_DAMAGE_MUL_4 = 643,  // 1.3
};
}

namespace EHellraidAIType {
enum TYPE : int { None = -1 };
// Linux: EHellraidAIType::IsHellEnemy(TYPE) (inlined).
inline bool IsHellEnemy(TYPE type) { return type != None; }
}

class HumanAI : public ObjectDI {
public:
    // Slots 1..40 not used here (slot 0 is declared by ObjectDI).
    DL_VPAD1(1) DL_VPAD1(2) DL_VPAD1(3) DL_VPAD1(4) DL_VPAD1(5) DL_VPAD1(6) DL_VPAD1(7) DL_VPAD1(8) DL_VPAD1(9)
    DL_VPAD10(1) DL_VPAD10(2) DL_VPAD10(3) DL_VPAD1(40)
    // Slot 41 (+0x148; gamedll 0x1805ea9d0) — Linux: HumanAI::GetMaxHealth(int) const.
    virtual float GetMaxHealth(int difficulty) const;

    std::uint8_t _pad029[0x9c0 - sizeof(ObjectDI)];
    AIDataSetWrapper* m_AIData;  // +0x9c0 (Linux +0x9b0)
    std::uint8_t _pad9c8[0xc24 - 0x9c8];
    EHellraidAIType::TYPE m_HellraidType;  // +0xc24 — reflection "m_HellraidType" (AI)
    std::uint8_t _padc28[0x1058 - 0xc28];
    // +0x1058 (Linux +0x1038) — unknown name; with a null Hellraid type, no co-op health bonus
    // (also read by HumanAI::UpdateBossFight on Linux).
    bool m_Flag1058;
    std::uint8_t _pad1059[0x10d8 - 0x1059];
    // +0x10d8 (Linux +0x10a8) — unknown name; true by default (constructor): health scales with the player
    // count.
    bool m_ScaleHealthInCoop;

    float GetMaxHealthCoopMul(int playersCount) const;
    float GetMeleeDamageCoopMul() const;
    // Slot 317 (+0x9e8) — Linux: HumanAI::GetMaxStamina(int) const.
    float GetMaxStamina(int difficulty) const;

    // Linux: HumanAI::GetMaxStaminaCoopMul() const (inlined into GetMaxStamina on Windows).
    float GetMaxStaminaCoopMul() const
    {
        switch (GetLevelDI()->GetAllPlayersCount()) {
        case 2:
            return 1.5f;
        case 3:
            return 2.1f;
        case 4:
            return 2.75f;
        }
        return 1.0f;
    }
};
DL_CHECK_OFFSET(HumanAI, m_AIData, 0x9c0);
DL_CHECK_OFFSET(HumanAI, m_HellraidType, 0xc24);
DL_CHECK_OFFSET(HumanAI, m_Flag1058, 0x1058);
DL_CHECK_OFFSET(HumanAI, m_ScaleHealthInCoop, 0x10d8);
