// Be the Zombie au-delà de 4 chasseurs : une fonction simple, valable pour tout nombre N.
//
// Les réglages BtZ existent pour 1, 2, 3 et 4 chasseurs (Data0/data/scripts/varlist_be_the_zombie_*.scr, clés
// "<nom>_<N>_human" ; zombie_levels.xml, "…1v1".."…4v1"). Au-delà, le jeu lisait des réglages absents (0) ou des
// tableaux hors bornes. Correctif de coop6 : loi de puissance qui passe par les valeurs du jeu à 1 et 4 chasseurs,
//     v(N) = v4 · (N / 4)^b,  b = ln(v4 / v1) / ln 4,
// identique au jeu à N = 4, proche de ses valeurs à 2 et 3, jamais négative ; v1 = v4 donne une constante. Si v1 ou
// v4 n'est pas strictement positif, la valeur à 4 est gardée. Sans dépendance (testé par tests/mod_install/coop6_host.cpp).
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

// "<préfixe>_<N>_human" : renvoie vrai, `prefix` = longueur jusqu'au '_' avant N inclus, `hunters` = N.
inline bool ParseHumansKey(const char* name, std::size_t& prefix, int& hunters)
{
    static const char kSuffix[] = "_human";
    const std::size_t suffix = sizeof(kSuffix) - 1;
    const std::size_t len = std::strlen(name);
    if (len < suffix + 3 || std::strcmp(name + len - suffix, kSuffix) != 0)
        return false;
    const std::size_t end = len - suffix;  // fin des chiffres
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

// Écrit "<préfixe>_<hunters>_human" (hunters de 1 à 9) dans `out` ; faux si la place manque.
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
