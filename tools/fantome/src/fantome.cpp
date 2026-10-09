// fantome.exe — lance une instance de Dying Light 1.55 avec fantome.dll chargée avant son point d'entrée.
//
// Usage :
//   fantome.exe          lance un fantôme (premier numéro libre de 1 à 7)
//   fantome.exe hote     lance l'hôte (numéro 0, identité Steam réelle)
//   fantome.exe 3        lance le fantôme numéro 3
// Réglages : fantome.ini à côté de l'exe (créé avec les valeurs par défaut s'il manque).
//
// Méthode : le jeu est créé en pause, une APC « LoadLibraryW(fantome.dll) » est mise en file sur son fil
// principal, puis le fil est relâché. L'APC s'exécute à la fin de l'initialisation du chargeur de Windows,
// avant le point d'entrée du jeu. Les DLL de mods (clé Mods=) suivent, une APC chacune. L'installation Steam
// n'est pas modifiée.
#include <windows.h>
#include <tlhelp32.h>

#include <fcntl.h>
#include <io.h>

#include <cstdio>
#include <cwchar>
#include <string>
#include <vector>

namespace {

std::wstring Dir(const std::wstring& path) { return path.substr(0, path.find_last_of(L"\\/")); }

std::wstring SelfDir()
{
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    return Dir(p);
}

std::wstring IniString(const std::wstring& ini, const wchar_t* key, const wchar_t* def)
{
    wchar_t v[1024];
    GetPrivateProfileStringW(L"fantome", key, def, v, 1024, ini.c_str());
    return v;
}

void WriteDefaultIni(const std::wstring& ini)
{
    if (GetFileAttributesW(ini.c_str()) != INVALID_FILE_ATTRIBUTES)
        return;
    FILE* f = _wfopen(ini.c_str(), L"w, ccs=UTF-8");
    if (!f)
        return;
    fwprintf(f,
             L"; Réglages de fantome.exe\n"
             L"[fantome]\n"
             L"; Exécutable du jeu (lancé tel quel, jamais modifié)\n"
             L"Jeu=C:\\Program Files (x86)\\Steam\\steamapps\\common\\Dying Light\\DyingLightGame.exe\n"
             L"; 1 = chaque instance prend l'adresse 127.0.0.(numéro+1) sur les ports du jeu (lancer aussi l'hôte\n"
             L";     avec « fantome.exe hote »)\n"
             L"Bouclage=1\n"
             L"; 0 = fantômes sans son (/nosound)\n"
             L"SonFantomes=0\n"
             L"; Arguments ajoutés pour toutes les instances (-nologos est toujours ajouté)\n"
             L"Arguments=\n"
             L"; Arguments ajoutés pour les fantômes seulement (moins de mémoire pour les textures)\n"
             L"ArgumentsFantomes=-texStreamMemMB 512\n"
             L"; DLL de mods chargées au démarrage après fantome.dll (séparées par « ; », relatives à fantome.exe)\n"
             L"Mods=\n");
    fclose(f);
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
        const std::size_t a = item.find_first_not_of(L" \t"), b = item.find_last_not_of(L" \t");
        if (a != std::wstring::npos)
            out.push_back(item.substr(a, b - a + 1));
        start = end + 1;
    }
    return out;
}

bool SlotTaken(int index)
{
    wchar_t name[64];
    swprintf(name, 64, L"Local\\DLFantome_slot_%d", index);
    HANDLE h = OpenMutexW(SYNCHRONIZE, FALSE, name);
    if (h)
        CloseHandle(h);
    return h != nullptr;
}

bool ModuleLoaded(DWORD pid, const wchar_t* module)
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE, pid);
    if (snap == INVALID_HANDLE_VALUE)
        return false;
    MODULEENTRY32W me{sizeof(me)};
    bool found = false;
    for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me))
        if (_wcsicmp(me.szModule, module) == 0) {
            found = true;
            break;
        }
    CloseHandle(snap);
    return found;
}

}  // namespace

int wmain(int argc, wchar_t** argv)
{
    _setmode(_fileno(stdout), _O_U16TEXT);  // accents corrects dans la console
    _setmode(_fileno(stderr), _O_U16TEXT);
    const std::wstring self = SelfDir();
    const std::wstring ini = self + L"\\fantome.ini";
    WriteDefaultIni(ini);
    const std::wstring game = IniString(ini, L"Jeu", L"");
    const bool loopback = IniString(ini, L"Bouclage", L"1") != L"0";
    const bool ghostSound = IniString(ini, L"SonFantomes", L"0") != L"0";
    const std::wstring extra = IniString(ini, L"Arguments", L"");
    // Fantômes : moins de mémoire pour les textures (option du moteur lue dans CGame::Initialize). Sans ça,
    // la 4e instance dépassait la limite de mémoire engagée du système et plantait au démarrage.
    const std::wstring ghostExtra = IniString(ini, L"ArgumentsFantomes", L"-texStreamMemMB 512");
    const std::wstring dll = self + L"\\fantome.dll";

    int index = -1;
    if (argc >= 2) {
        if (_wcsicmp(argv[1], L"hote") == 0 || _wcsicmp(argv[1], L"hôte") == 0)
            index = 0;
        else
            index = _wtoi(argv[1]);
    } else {
        for (int i = 1; i < 33 && index < 0; ++i)
            if (!SlotTaken(i))
                index = i;
    }
    if (index < 0 || index > 32) {
        fwprintf(stderr, L"numéro d'instance invalide ou plus de place (1 à 32)\n");
        return 2;
    }
    if (SlotTaken(index)) {
        fwprintf(stderr, L"l'instance %d tourne déjà\n", index);
        return 2;
    }
    if (GetFileAttributesW(game.c_str()) == INVALID_FILE_ATTRIBUTES) {
        fwprintf(stderr, L"jeu introuvable : %s (voir %s)\n", game.c_str(), ini.c_str());
        return 1;
    }
    if (GetFileAttributesW(dll.c_str()) == INVALID_FILE_ATTRIBUTES) {
        fwprintf(stderr, L"fantome.dll introuvable à côté de fantome.exe\n");
        return 1;
    }

    // Environnement transmis au jeu : identifiant Steam du jeu (lancement hors client) + réglages de la DLL.
    CreateDirectoryW((self + L"\\journaux").c_str(), nullptr);
    wchar_t num[8], log[MAX_PATH];
    swprintf(num, 8, L"%d", index);
    swprintf(log, MAX_PATH, L"%s\\journaux\\instance_%d.log", self.c_str(), index);
    SetEnvironmentVariableW(L"SteamAppId", L"239140");
    SetEnvironmentVariableW(L"SteamGameId", L"239140");
    SetEnvironmentVariableW(L"DLF_INDEX", num);
    SetEnvironmentVariableW(L"DLF_LOOPBACK", loopback ? L"1" : L"0");
    SetEnvironmentVariableW(L"DLF_LOG", log);

    std::wstring cmd = L"\"" + game + L"\" -nologos";  // passe les vidéos d'introduction
    if (index > 0 && !ghostSound)
        cmd += L" /nosound";
    if (!extra.empty())
        cmd += L" " + extra;
    if (index > 0 && !ghostExtra.empty())
        cmd += L" " + ghostExtra;

    STARTUPINFOW si{sizeof(si)};
    PROCESS_INFORMATION pi{};
    std::wstring cmdBuf = cmd;
    if (!CreateProcessW(game.c_str(), &cmdBuf[0], nullptr, nullptr, FALSE, CREATE_SUSPENDED, nullptr,
                        Dir(game).c_str(), &si, &pi)) {
        fwprintf(stderr, L"CreateProcess a échoué (%lu)\n", GetLastError());
        return 1;
    }

    // fantome.dll puis les DLL de mods (clé Mods=, séparées par « ; », chemins relatifs au dossier de
    // fantome.exe) : une APC « LoadLibraryW » chacune, exécutées dans l'ordre avant le point d'entrée du jeu.
    std::vector<std::wstring> dlls{dll};
    for (const std::wstring& m : Split(IniString(ini, L"Mods", L""), L';')) {
        std::wstring path = (m.size() > 1 && m[1] == L':') ? m : self + L"\\" + m;
        if (GetFileAttributesW(path.c_str()) == INVALID_FILE_ATTRIBUTES) {
            fwprintf(stderr, L"mod introuvable : %s (ignoré)\n", path.c_str());
            continue;
        }
        dlls.push_back(path);
    }
    auto* loadLibrary = reinterpret_cast<PAPCFUNC>(GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW"));
    for (const std::wstring& path : dlls) {
        const SIZE_T bytes = (path.size() + 1) * sizeof(wchar_t);
        void* remote = VirtualAllocEx(pi.hProcess, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        if (!remote || !WriteProcessMemory(pi.hProcess, remote, path.c_str(), bytes, nullptr) ||
            !QueueUserAPC(loadLibrary, pi.hThread, reinterpret_cast<ULONG_PTR>(remote))) {
            fwprintf(stderr, L"préparation de l'injection impossible (%lu) : jeu arrêté\n", GetLastError());
            TerminateProcess(pi.hProcess, 1);
            return 1;
        }
        if (path != dll)
            wprintf(L"Mod chargé au démarrage : %s\n", path.c_str());
    }
    ResumeThread(pi.hThread);

    wprintf(L"Instance %d lancée (pid %lu) : %s\n", index, pi.dwProcessId, cmd.c_str());
    bool ok = false;
    for (int i = 0; i < 100 && !ok; ++i) {
        if (WaitForSingleObject(pi.hProcess, 100) == WAIT_OBJECT_0)
            break;
        ok = ModuleLoaded(pi.dwProcessId, L"fantome.dll");
    }
    DWORD code = STILL_ACTIVE;
    GetExitCodeProcess(pi.hProcess, &code);
    if (ok)
        wprintf(L"fantome.dll chargée. Journal : %s\n", log);
    else if (code != STILL_ACTIVE)
        wprintf(L"le jeu s'est arrêté tout de suite (code %lu). Journal : %s\n", code, log);
    else
        wprintf(L"fantome.dll non détectée après 10 s. Journal : %s\n", log);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return ok ? 0 : 1;
}
