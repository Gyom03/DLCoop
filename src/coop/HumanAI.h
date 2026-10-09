// HumanAI (ennemis humains) — partiel, pour les multiplicateurs de difficulté coop. gamedll 1.55.
#pragma once
#include "EngineObjects.h"
#include "Vpad.h"

// Paramètre d'un jeu de données d'IA (Linux : entrée renvoyée par AIDataSetWrapper::FindParam).
class AIDataParam {
public:
    virtual void _vpad_0();
    virtual void _vpad_1();
    virtual void _vpad_2();
    virtual void _vpad_3();
    virtual void _vpad_4();
    // Place 5 (+0x28 ; Linux +0x30, GCC ayant 2 destructeurs) : adresse de la valeur.
    virtual const void* GetValuePtr() const;

    std::uint8_t _pad08[0x18 - 0x8];
    std::uint8_t m_Flags;  // +0x18 — bit 0 : valeur définie
};
DL_CHECK_OFFSET(AIDataParam, m_Flags, 0x18);

// Accès aux paramètres d'IA (EAIParam) d'un personnage.
class AIDataSetWrapper {
public:
    AIDataParam* FindParam(int id) const;  // gamedll @ 0x180233c60

    // Linux : AIDataSetWrapper::GetParam<T>(int) const (inlinée sous Windows) : la valeur du paramètre, ou
    // une valeur nulle statique s'il n'est pas défini.
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
    COOP_DAMAGE_MUL_2 = 641,  // `coop_damage_mul_2` (human_data_params.scr : 1,1)
    COOP_DAMAGE_MUL_3 = 642,  // 1,2
    COOP_DAMAGE_MUL_4 = 643,  // 1,3
};
}

namespace EHellraidAIType {
enum TYPE : int { None = -1 };
// Linux : EHellraidAIType::IsHellEnemy(TYPE) (inlinée).
inline bool IsHellEnemy(TYPE type) { return type != None; }
}

class HumanAI : public ObjectDI {
public:
    // Places 1..40 non utilisées ici (la place 0 est déclarée par ObjectDI).
    DL_VPAD1(1) DL_VPAD1(2) DL_VPAD1(3) DL_VPAD1(4) DL_VPAD1(5) DL_VPAD1(6) DL_VPAD1(7) DL_VPAD1(8) DL_VPAD1(9)
    DL_VPAD10(1) DL_VPAD10(2) DL_VPAD10(3) DL_VPAD1(40)
    // Place 41 (+0x148 ; gamedll 0x1805ea9d0) — Linux : HumanAI::GetMaxHealth(int) const.
    virtual float GetMaxHealth(int difficulty) const;

    std::uint8_t _pad029[0x9c0 - sizeof(ObjectDI)];
    AIDataSetWrapper* m_AIData;  // +0x9c0 (Linux +0x9b0)
    std::uint8_t _pad9c8[0xc24 - 0x9c8];
    EHellraidAIType::TYPE m_HellraidType;  // +0xc24 — réflexion « m_HellraidType » (AI)
    std::uint8_t _padc28[0x1058 - 0xc28];
    // +0x1058 (Linux +0x1038) — nom inconnu ; avec un type Hellraid nul, pas de bonus de santé coop
    // (aussi lu par HumanAI::UpdateBossFight sous Linux).
    bool m_Flag1058;
    std::uint8_t _pad1059[0x10d8 - 0x1059];
    // +0x10d8 (Linux +0x10a8) — nom inconnu ; vrai par défaut (constructeur) : la santé suit le nombre de
    // joueurs.
    bool m_ScaleHealthInCoop;

    float GetMaxHealthCoopMul(int playersCount) const;
    float GetMeleeDamageCoopMul() const;
    // Place 317 (+0x9e8) — Linux : HumanAI::GetMaxStamina(int) const.
    float GetMaxStamina(int difficulty) const;

    // Linux : HumanAI::GetMaxStaminaCoopMul() const (inlinée dans GetMaxStamina sous Windows).
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
