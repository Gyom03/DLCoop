// Fake game to test the winmm.dll loader (tools/dlloader) outside Dying Light (tests/dlloader/run.ps1).
// Imports winmm (like engine/gamedll) and takes the game lock at the very start of main, without waiting: the worst
// case for loading the mods. Checks that dlcoop_reseau.dll is already loaded and that, for an additional instance
// (DLF_INDEX > 0), the lock is renamed (so "free" while the host holds the real one).
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
    printf("mod before main: %s ; lock: %s ; winmm ok: %s\n", early ? "yes" : "NO",
           taken ? "ALREADY TAKEN" : "free", timeGetTime() != 0 ? "yes" : "no");
    if (const char* wait = getenv("FAUX_ATTENTE"))  // host: keeps the lock
        Sleep(static_cast<DWORD>(atoi(wait)));
    CloseHandle(m);
    return taken ? 2 : (early ? 0 : 1);
}
