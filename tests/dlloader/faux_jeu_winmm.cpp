// Faux jeu pour tester le chargeur winmm.dll (tools/dlloader) hors de Dying Light (tests/dlloader/run.ps1).
// Importe winmm (comme engine/gamedll) et prend le verrou du jeu dès le début de main, sans attendre : pire cas
// pour le chargement des mods. Vérifie que dlcoop_reseau.dll est déjà chargée et que, pour une instance
// supplémentaire (DLF_INDEX > 0), le verrou est renommé (donc « libre » alors que l'hôte tient le vrai).
#include <windows.h>
#include <mmsystem.h>

#include <cstdio>
#include <cstdlib>

#pragma comment(lib, "winmm.lib")

int main()
{
    const bool early = GetModuleHandleW(L"dlcoop_reseau.dll") != nullptr;
    HANDLE m = CreateMutexA(nullptr, FALSE, "Global\\ChromeEngine4DIMutex");
    const bool taken = GetLastError() == ERROR_ALREADY_EXISTS;
    printf("mod avant main : %s ; verrou : %s ; winmm ok : %s\n", early ? "oui" : "NON",
           taken ? "DEJA PRIS" : "libre", timeGetTime() != 0 ? "oui" : "non");
    if (const char* wait = getenv("FAUX_ATTENTE"))  // hôte : garde le verrou
        Sleep(static_cast<DWORD>(atoi(wait)));
    CloseHandle(m);
    return taken ? 2 : (early ? 0 : 1);
}
