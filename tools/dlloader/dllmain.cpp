// winmm.dll relais — chargeur de mods pour Dying Light 1.55, SANS injection par un processus extérieur.
//
// Pourquoi : engine_x64_rwdi.dll et gamedll_x64_rwdi.dll importent winmm.dll (vérifié). En posant cette
// DLL dans le dossier du jeu, le chargeur de Windows la prend avant celle du système (winmm n'est pas une
// « KnownDLL »). C'est la même technique que DLML (dsound.dll) ou Ultimate ASI Loader. Le jeu charge donc
// nos mods tout seul : aucun CreateProcess suspendu, aucun VirtualAllocEx / WriteProcessMemory / APC dans
// un autre processus — plus de motif « injection » que les antivirus signalent.
//
// Réexpédition : tous les exports de winmm sont renvoyés vers la vraie winmm du système, par son chemin
// absolu (System32), grâce aux « forwarders » déclarés par des #pragma comment(linker, "/export:…") dans
// winmm_exports.h (généré par gen_def.py, inclus ci-dessous). Rien à copier à côté du jeu : pas de
// winmm_real.dll. Aucun code ici pour ces fonctions ; ce fichier ne contient que le point d'entrée.
//
// Au chargement, cette DLL :
//   1. lit dlmods.ini (à côté d'elle) :
//        [env]    CLE=VALEUR  -> variable d'environnement CLE posée dans le processus du jeu ;
//        [loader] Mods=a.dll;b.dll -> DLL de mods à charger (défaut : dlcoop_reseau.dll;coop6.dll) ;
//   2. charge ces DLL depuis son propre dossier, dans l'ordre.
//
// Les mods lisent leurs réglages exactement comme avant : coop6.dll lit COOP6_<CLÉ> puis coop6.ini,
// dlcoop_reseau.dll lit DLF_*. Le lanceur n'a donc qu'à écrire dlmods.ini (section [env]) : aucune
// modification des mods n'est nécessaire.
//
// Moment du chargement des mods : winmm est chargée pendant la résolution des imports statiques du jeu, verrou
// du chargeur tenu, tables d'imports pas encore toutes remplies : trop tôt pour les mods (qui modifient ces
// tables). On détourne donc le point d'entrée de l'exe : 12 octets « mov rax, EntryHook ; jmp rax », remis
// en place dès l'appel. EntryHook charge les mods sur le fil principal, verrou relâché, tous les imports
// résolus, AVANT la première instruction du jeu (même moment que l'ancienne APC de DLCoop / fantome).
// Nécessaire pour les instances supplémentaires : dlcoop_reseau.dll doit renommer le verrou du jeu
// (« Global\ChromeEngine4DIMutex ») avant que le jeu ne le prenne. Avec un simple fil, le jeu gagnait la
// course (tests/dlloader/run.ps1 : 20 refus sur 20). Repli sur un fil si winmm est chargée après le
// démarrage (LoadLibrary dynamique : le point d'entrée a déjà été exécuté).

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// Forwarders winmm -> winmm du système (System32), générés par gen_def.py. Doit être présent à la
// compilation (build.ps1 lance gen_def.py d'abord).
#include "winmm_exports.h"

namespace {

HMODULE g_self = nullptr;

// Dossier de cette DLL (là où sont winmm_real.dll, dlmods.ini et les mods).
std::wstring SelfDir()
{
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_self, path, MAX_PATH);
    std::wstring p(path, n);
    const std::size_t cut = p.find_last_of(L"\\/");
    return cut == std::wstring::npos ? L"." : p.substr(0, cut);
}

std::vector<std::wstring> Split(const std::wstring& s, wchar_t sep)
{
    std::vector<std::wstring> out;
    std::size_t start = 0;
    while (start <= s.size()) {
        std::size_t end = s.find(sep, start);
        if (end == std::wstring::npos)
            end = s.size();
        std::wstring item = s.substr(start, end - start);
        const std::size_t a = item.find_first_not_of(L" \t");
        const std::size_t b = item.find_last_not_of(L" \t");
        if (a != std::wstring::npos)
            out.push_back(item.substr(a, b - a + 1));
        start = end + 1;
    }
    return out;
}

// [env] du dlmods.ini -> variables d'environnement du processus du jeu. GetPrivateProfileSectionW renvoie
// « CLE=VALEUR\0CLE=VALEUR\0\0 ». On n'écrase PAS une variable déjà posée : si le lanceur a lancé le jeu en
// posant lui-même COOP6_*/DLF_* (valeurs par instance, héritées par le processus enfant), elles priment ;
// le fichier ne sert alors que de repli pour un lancement direct (double-clic, Steam) sans lanceur.
void ApplyEnv(const std::wstring& ini)
{
    std::vector<wchar_t> buf(32768);
    DWORD n = GetPrivateProfileSectionW(L"env", buf.data(), static_cast<DWORD>(buf.size()), ini.c_str());
    if (n == 0)
        return;
    const wchar_t* p = buf.data();
    while (*p) {
        std::wstring entry(p);
        const std::size_t eq = entry.find(L'=');
        if (eq != std::wstring::npos) {
            std::wstring key = entry.substr(0, eq);
            std::wstring val = entry.substr(eq + 1);
            if (GetEnvironmentVariableW(key.c_str(), nullptr, 0) == 0)  // absente : on la pose
                SetEnvironmentVariableW(key.c_str(), val.c_str());
        }
        p += entry.size() + 1;
    }
}

// Charge les mods : depuis le point d'entrée détourné, ou dans un fil (voir l'en-tête du fichier).
DWORD WINAPI LoadMods(LPVOID)
{
    const std::wstring dir = SelfDir();
    const std::wstring ini = dir + L"\\dlmods.ini";

    ApplyEnv(ini);

    wchar_t mods[2048];
    GetPrivateProfileStringW(L"loader", L"Mods", L"dlcoop_reseau.dll;coop6.dll",
                             mods, 2048, ini.c_str());
    for (const std::wstring& m : Split(mods, L';')) {
        if (m.empty())
            continue;
        // Chemin absolu tel quel (« X:\… ») ; sinon relatif au dossier de cette DLL.
        std::wstring path = (m.size() > 1 && m[1] == L':') ? m : dir + L"\\" + m;
        LoadLibraryW(path.c_str());
    }
    return 0;
}

// ---------------------------------------------------------------- point d'entrée de l'exe
using Entry_t = DWORD(WINAPI*)(void*);
constexpr std::size_t kPatch = 12;  // 48 B8 imm64 (mov rax, imm64) ; FF E0 (jmp rax)
uint8_t* g_entry = nullptr;
uint8_t g_saved[kPatch];

void Write(uint8_t* at, const uint8_t* bytes)
{
    DWORD old;
    VirtualProtect(at, kPatch, PAGE_EXECUTE_READWRITE, &old);
    memcpy(at, bytes, kPatch);
    VirtualProtect(at, kPatch, old, &old);
    FlushInstructionCache(GetCurrentProcess(), at, kPatch);
}

// Appelé par Windows à la place du point d'entrée de l'exe (rcx = PEB, transmis tel quel).
DWORD WINAPI EntryHook(void* peb)
{
    Write(g_entry, g_saved);  // octets d'origine remis : l'exe s'exécute ensuite sans aucune modification
    LoadMods(nullptr);
    return reinterpret_cast<Entry_t>(g_entry)(peb);
}

bool HookEntry()
{
    auto* base = reinterpret_cast<uint8_t*>(GetModuleHandleW(nullptr));
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + reinterpret_cast<IMAGE_DOS_HEADER*>(base)->e_lfanew);
    if (nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR64_MAGIC || !nt->OptionalHeader.AddressOfEntryPoint)
        return false;
    g_entry = base + nt->OptionalHeader.AddressOfEntryPoint;
    memcpy(g_saved, g_entry, kPatch);
    uint8_t jump[kPatch] = {0x48, 0xB8, 0, 0, 0, 0, 0, 0, 0, 0, 0xFF, 0xE0};
    const auto target = reinterpret_cast<uint64_t>(&EntryHook);
    memcpy(jump + 2, &target, sizeof(target));
    Write(g_entry, jump);
    return true;
}

}  // namespace

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID reserved)
{
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        DisableThreadLibraryCalls(inst);
        // reserved != nullptr : chargement statique, au démarrage du processus (point d'entrée pas encore
        // exécuté). Sinon (LoadLibrary tardif) : fil à part.
        if (!(reserved && HookEntry())) {
            HANDLE t = CreateThread(nullptr, 0, LoadMods, nullptr, 0, nullptr);
            if (t)
                CloseHandle(t);
        }
    }
    return TRUE;
}
