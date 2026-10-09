// Be the Zombie beyond 4 hunters: a simple function, valid for any number N.
//
// BtZ settings exist for 1, 2, 3 and 4 hunters (Data0/data/scripts/varlist_be_the_zombie_*.scr, keys
// "<name>_<N>_human"; zombie_levels.xml, "…1v1".."…4v1"). Beyond that, the game read missing settings (0) or
// out-of-bounds arrays. coop6 fix: a power law passing through the game's values at 1 and 4 hunters,
//     v(N) = v4 · (N / 4)^b,  b = ln(v4 / v1) / ln 4,
// identical to the game at N = 4, close to its values at 2 and 3, never negative; v1 = v4 gives a constant. If v1
// or v4 is not strictly positive, the value at 4 is kept. No dependency (tested by tests/mod_install/coop6_host.cpp).
#pragma once
#include <cmath>
#include <cstddef>
#include <cstring>

inline float HuntersScale(float v1, float v4, int hunters)
{
    if (hunters <= 4 || !(v1 > 0.0f) || !(v4 > 0.0f))
        return v4;
    const double b = std::log(static_cast<double>(v4) / v1) / std::log(4.0);
    return static_cast<float>(v4 * std::pow(hunters / 4.0, b));
}

// "<prefix>_<N>_human": returns true, `prefix` = length up to and including the '_' before N, `hunters` = N.
inline bool ParseHumansKey(const char* name, std::size_t& prefix, int& hunters)
{
    static const char kSuffix[] = "_human";
    const std::size_t suffix = sizeof(kSuffix) - 1;
    const std::size_t len = std::strlen(name);
    if (len < suffix + 3 || std::strcmp(name + len - suffix, kSuffix) != 0)
        return false;
    const std::size_t end = len - suffix;  // end of the digits
    std::size_t start = end;
    while (start > 0 && name[start - 1] >= '0' && name[start - 1] <= '9')
        --start;
    if (start == end || end - start > 4 || start == 0 || name[start - 1] != '_')
        return false;
    int n = 0;
    for (std::size_t i = start; i < end; ++i)
        n = n * 10 + (name[i] - '0');
    prefix = start;
    hunters = n;
    return true;
}

// Writes "<prefix>_<hunters>_human" (hunters 1 to 9) into `out`; false if there is not enough room.
inline bool MakeHumansKey(const char* name, std::size_t prefix, int hunters, char* out, std::size_t outSize)
{
    static const char kSuffix[] = "_human";
    if (hunters < 1 || hunters > 9 || prefix + 1 + sizeof(kSuffix) > outSize)
        return false;
    std::memcpy(out, name, prefix);
    out[prefix] = static_cast<char>('0' + hunters);
    std::memcpy(out + prefix + 1, kSuffix, sizeof(kSuffix));
    return true;
}
