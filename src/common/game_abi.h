// Outils d'interface avec le code d'origine du jeu (Dying Light 1.55, MSVC x64).
#pragma once
#if defined(__GNUC__)
// offsetof sur une classe polymorphe : accepté par GCC et MSVC, que l'on utilise pour vérifier les positions.
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
#include <cstddef>
#include <cstdint>

namespace dl {

// Appel d'une méthode virtuelle du jeu par sa place dans la vftable MSVC.
// Nos déclarations de classes sont partielles : on ne peut pas encore laisser le compilateur
// calculer les places, donc on les indique explicitement (vérifiées sur le binaire).
template <typename Ret, std::size_t Slot, typename Self, typename... Args>
inline Ret vcall(Self* self, Args... args)
{
    using Fn = Ret (*)(Self*, Args...);
    // Une seule expression : MSVC génère alors l'appel indirect direct `call [vftable+place*8]`,
    // comme pour une vraie méthode virtuelle.
    return (*reinterpret_cast<Fn* const*>(self))[Slot](self, args...);
}

}  // namespace dl

// Vérifie à la compilation qu'un champ est à la position attendue dans l'objet du jeu.
#define DL_CHECK_OFFSET(type, field, offset) \
    static_assert(offsetof(type, field) == (offset), #type "::" #field " doit être à " #offset)
