// Helpers for interfacing with the game's original code (Dying Light 1.55, MSVC x64).
#pragma once
#if defined(__GNUC__)
// offsetof on a polymorphic class: accepted by GCC and MSVC, used here to verify field offsets.
#pragma GCC diagnostic ignored "-Winvalid-offsetof"
#endif
#include <cstddef>
#include <cstdint>

namespace dl {

// Calls a game virtual method by its slot in the MSVC vftable.
// Our class declarations are partial, so the compiler cannot compute the slots yet; we give them
// explicitly (verified against the binary).
template <typename Ret, std::size_t Slot, typename Self, typename... Args>
inline Ret vcall(Self* self, Args... args)
{
    using Fn = Ret (*)(Self*, Args...);
    // A single expression, so MSVC emits the direct indirect call `call [vftable+slot*8]`,
    // just like a real virtual method.
    return (*reinterpret_cast<Fn* const*>(self))[Slot](self, args...);
}

}  // namespace dl

// Checks at compile time that a field sits at the expected offset in the game object.
#define DL_CHECK_OFFSET(type, field, offset) \
    static_assert(offsetof(type, field) == (offset), #type "::" #field " must be at " #offset)
