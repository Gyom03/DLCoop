// Faux jeu pour tester fantome.exe / fantome.dll hors de Dying Light (tools/fantome/tests/run.ps1).
// Vérifie : DLL chargée avant main, verrou du jeu renommé pour un fantôme, bind forcé sur 127.0.0.(n+1),
// port de destination 0 corrigé en 20101, diffusion remplacée par des envois directs.
#include <winsock2.h>
#include <windows.h>

#include <cstdio>
#include <cstdlib>

int main()
{
    const bool early = GetModuleHandleW(L"fantome.dll") != nullptr;
    if (const char* out = getenv("FAUX_SORTIE"))  // lancé par fantome.exe : pas de console héritée
        freopen(out, "w", stdout);
    printf("dll chargee avant main : %s\n", early ? "oui" : "NON");

    HANDLE m = CreateMutexA(nullptr, FALSE, "Global\\ChromeEngine4DIMutex");
    printf("verrou : %s\n", GetLastError() == ERROR_ALREADY_EXISTS ? "DEJA PRIS" : "libre");

    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
    SOCKET s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    BOOL yes = TRUE;
    setsockopt(s, SOL_SOCKET, SO_BROADCAST, reinterpret_cast<char*>(&yes), sizeof(yes));
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_port = htons(20101);
    a.sin_addr.s_addr = INADDR_ANY;
    int r = bind(s, reinterpret_cast<sockaddr*>(&a), sizeof(a));
    sockaddr_in got{};
    int gl = sizeof(got);
    getsockname(s, reinterpret_cast<sockaddr*>(&got), &gl);
    printf("bind : %s, adresse %s:%u\n", r == 0 ? "ok" : "echec", inet_ntoa(got.sin_addr), ntohs(got.sin_port));

    const char* target = getenv("FAUX_CIBLE");  // ex. 127.0.0.1
    if (target) {
        sockaddr_in d{};
        d.sin_family = AF_INET;
        d.sin_port = 0;  // le défaut de la 1.55
        d.sin_addr.s_addr = inet_addr(target);
        int n = sendto(s, "port0", 5, 0, reinterpret_cast<sockaddr*>(&d), sizeof(d));
        printf("envoi vers %s:0 : %d\n", target, n);
        d.sin_port = htons(20101);
        d.sin_addr.s_addr = INADDR_BROADCAST;
        WSABUF b{9, const_cast<char*>("diffusion")};
        DWORD sent = 0;
        int n2 = WSASendTo(s, &b, 1, &sent, 0, reinterpret_cast<sockaddr*>(&d), sizeof(d), nullptr, nullptr);
        printf("diffusion 255.255.255.255:20101 : %d (%lu octets)\n", n2, sent);
        // Réponse de l'hôte : doit être présentée comme venant du port 0, comme l'adresse mémorisée.
        DWORD t = 3000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&t), sizeof(t));
        char rb[64] = {};
        sockaddr_in from{};
        int fl = sizeof(from);
        int rn = recvfrom(s, rb, 63, 0, reinterpret_cast<sockaddr*>(&from), &fl);
        if (rn > 0)
            printf("reponse \"%s\" de %s:%u\n", rb, inet_ntoa(from.sin_addr), ntohs(from.sin_port));
        else
            printf("pas de reponse\n");
        // Nouvelle session : le même pair est maintenant connu sous sa vraie adresse (port 20101).
        sockaddr_in d2{};
        d2.sin_family = AF_INET;
        d2.sin_port = htons(20101);
        d2.sin_addr.s_addr = inet_addr(target);
        sendto(s, "port20101", 9, 0, reinterpret_cast<sockaddr*>(&d2), sizeof(d2));
        memset(rb, 0, sizeof(rb));
        fl = sizeof(from);
        rn = recvfrom(s, rb, 63, 0, reinterpret_cast<sockaddr*>(&from), &fl);
        if (rn > 0)
            printf("reponse \"%s\" de %s:%u\n", rb, inet_ntoa(from.sin_addr), ntohs(from.sin_port));
        else
            printf("pas de reponse (2)\n");
    } else {
        // Récepteur : affiche ce qui arrive (12 s sans paquet = fin).
        DWORD t = 12000;
        setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<char*>(&t), sizeof(t));
        for (;;) {
            char buf[64] = {};
            sockaddr_in from{};
            int fl = sizeof(from);
            int n = recvfrom(s, buf, 63, 0, reinterpret_cast<sockaddr*>(&from), &fl);
            if (n <= 0)
                break;
            printf("recu \"%s\" de %s:%u\n", buf, inet_ntoa(from.sin_addr), ntohs(from.sin_port));
            if (strcmp(buf, "port0") == 0 || strcmp(buf, "port20101") == 0)
                sendto(s, "reponse", 7, 0, reinterpret_cast<sockaddr*>(&from), fl);
        }
    }
    fflush(stdout);
    if (m)
        CloseHandle(m);
    return 0;
}
