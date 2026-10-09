// Lancement de Dying Light 1.55 avec les DLL du mod chargées avant son point d'entrée (même méthode que
// tools/fantome/src/fantome.cpp) : jeu créé en pause, une APC « LoadLibraryW » par DLL sur son fil principal, puis
// reprise. Les options passent par l'environnement du jeu (DLF_* pour dlcoop_reseau.dll, COOP6_* pour coop6.dll).
// Le dossier du jeu n'est jamais modifié. Interface et messages : en anglais uniquement.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Text.RegularExpressions;
using System.Threading;
using Microsoft.Win32;

namespace DLCoop
{
    /// <summary>Choix de l'utilisateur, mémorisés dans %APPDATA%\DLCoop\settings.txt (clé=valeur).</summary>
    public sealed class Settings
    {
        public string Game = "";
        public bool HideName;
        public string Name = "";
        // Instance : remis aux valeurs par défaut à chaque démarrage (instance principale, son, textures complètes).
        public bool ExtraInstance;
        public int Instance;                // 0 = automatique (premier numéro libre) pour une instance supplémentaire
        public bool MultiInstance;          // adresses 127.0.0.N (plusieurs instances sur ce PC, réseau local)
        public bool NoSound;                // instance supplémentaire
        public bool ReducedTextures;        // instance supplémentaire
        public bool DisableEos = true;
        public bool CloseAfterLaunch = true;
        public int MaxPlayers = Launcher.MaxInstances - 1;  // joueurs humains au plus quand on héberge (le zombie BtZ en plus)
        public bool Internet;               // relais Internet (Relay.cs) : déduit du serveur (renseigné = relais)
        public string Server = "";          // adresse du relais (ip[:port]) ; défaut : server.txt à côté de l'exe
        public string Password = "";        // mot de passe de la session (le même pour tous les joueurs)

        static string FilePath => Path.Combine(
            Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "DLCoop", "settings.txt");

        public static Settings Load()
        {
            var s = new Settings();
            try
            {
                if (!File.Exists(FilePath))
                    return s;
                foreach (var line in File.ReadAllLines(FilePath, Encoding.UTF8))
                {
                    int i = line.IndexOf('=');
                    if (i <= 0)
                        continue;
                    string k = line.Substring(0, i).Trim(), v = line.Substring(i + 1).Trim();
                    var field = typeof(Settings).GetField(k);
                    if (field == null)
                        continue;
                    if (field.FieldType == typeof(bool))
                        field.SetValue(s, v == "1");
                    else if (field.FieldType == typeof(int) && int.TryParse(v, out int n))
                        field.SetValue(s, n);
                    else if (field.FieldType == typeof(string))
                        field.SetValue(s, v);
                }
            }
            catch (Exception) { /* réglages illisibles : valeurs par défaut */ }
            var defaults = new Settings();
            s.ExtraInstance = defaults.ExtraInstance;
            s.Instance = defaults.Instance;
            s.MultiInstance = defaults.MultiInstance;
            s.NoSound = defaults.NoSound;
            s.ReducedTextures = defaults.ReducedTextures;
            if (string.IsNullOrWhiteSpace(s.Server))
            {
                // Paquet préparé pour un groupe : adresse du relais fournie (build.ps1 -Relais).
                try
                {
                    string f = Path.Combine(Launcher.Folder, "server.txt");
                    if (File.Exists(f))
                        s.Server = File.ReadAllText(f).Trim();
                }
                catch (Exception) { }
            }
            s.Internet = !string.IsNullOrWhiteSpace(s.Server);
            s.MaxPlayers = Math.Max(2, Math.Min(Launcher.MaxInstances - 1, s.MaxPlayers));
            return s;
        }

        public void Save()
        {
            try
            {
                Directory.CreateDirectory(Path.GetDirectoryName(FilePath));
                var lines = typeof(Settings).GetFields().Where(f => !f.IsStatic).Select(f =>
                {
                    object v = f.GetValue(this);
                    return f.Name + "=" + (v is bool b ? (b ? "1" : "0") : Convert.ToString(v));
                });
                File.WriteAllLines(FilePath, lines, Encoding.UTF8);
            }
            catch (Exception) { /* non bloquant */ }
        }
    }

    public static class Launcher
    {
        public const string NetworkDll = "dlcoop_reseau.dll";
        public const string ModDll = "coop6.dll";
        const string GameAppId = "239140";
        static readonly string[] NamePrefixes = { "Runner", "Climber", "Prowler", "Watcher", "Nomad", "Scout",
                                                  "Tracker", "Survivor", "Courier", "Drifter" };

        public static string Folder => AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\');

        /// <summary>Tous les DyingLightGame.exe trouvés : clé de désinstallation Steam, bibliothèques Steam
        /// (libraryfolders.vdf + appmanifest_239140.acf pour le nom réel du dossier), dossier par défaut.</summary>
        public static List<string> FindGames()
        {
            var folders = new List<string>();
            foreach (var view in new[] { RegistryView.Registry64, RegistryView.Registry32 })
            {
                try
                {
                    using (var hklm = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, view))
                    using (var k = hklm.OpenSubKey(@"SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall\Steam App " + GameAppId))
                        if (k?.GetValue("InstallLocation") is string loc && loc.Length > 0)
                            folders.Add(loc);
                }
                catch (Exception) { /* clé absente ou illisible */ }
            }
            var libraries = new List<string>();
            string steam = Registry.GetValue(@"HKEY_CURRENT_USER\Software\Valve\Steam", "SteamPath", null) as string;
            if (!string.IsNullOrEmpty(steam))
            {
                libraries.Add(steam.Replace('/', '\\'));
                string vdf = Path.Combine(steam, "steamapps", "libraryfolders.vdf");
                try
                {
                    if (File.Exists(vdf))
                        foreach (Match m in Regex.Matches(File.ReadAllText(vdf), "\"path\"\\s+\"([^\"]+)\""))
                            libraries.Add(m.Groups[1].Value.Replace(@"\\", @"\"));
                }
                catch (Exception) { /* fichier illisible */ }
            }
            libraries.Add(@"C:\Program Files (x86)\Steam");
            foreach (string lib in libraries.Distinct(StringComparer.OrdinalIgnoreCase))
            {
                string acf = Path.Combine(lib, "steamapps", "appmanifest_" + GameAppId + ".acf");
                string sub = "Dying Light";
                try
                {
                    if (File.Exists(acf))
                    {
                        var m = Regex.Match(File.ReadAllText(acf), "\"installdir\"\\s+\"([^\"]+)\"");
                        if (m.Success)
                            sub = m.Groups[1].Value;
                    }
                }
                catch (Exception) { /* manifeste illisible */ }
                folders.Add(Path.Combine(lib, "steamapps", "common", sub));
            }
            return folders.Select(d => Path.Combine(d, "DyingLightGame.exe"))
                          .Where(File.Exists)
                          .Distinct(StringComparer.OrdinalIgnoreCase)
                          .ToList();
        }

        /// <summary>Premier DyingLightGame.exe trouvé, en 1.55 de préférence (ou chaîne vide).</summary>
        public static string FindGame()
        {
            var all = FindGames();
            return all.FirstOrDefault(e => { GameVersion(e, out bool ok); return ok; }) ?? all.FirstOrDefault() ?? "";
        }

        /// <summary>Version du jeu (« 1.55 »…) ; <paramref name="supported"/> vrai pour la 1.55 seulement.
        /// Chaîne vide si le fichier n'existe pas.</summary>
        public static string GameVersion(string exe, out bool supported)
        {
            supported = false;
            if (string.IsNullOrEmpty(exe) || !File.Exists(exe))
                return "";
            var v = FileVersionInfo.GetVersionInfo(exe);
            supported = v.FileMajorPart == 1 && v.FileMinorPart == 55;
            return v.FileMajorPart + "." + v.FileMinorPart;
        }

        /// <summary>
        /// À chaque démarrage : le chemin mémorisé est gardé s'il existe et que c'est la 1.55 ; sinon nouvelle
        /// recherche (installation déplacée, autre bibliothèque…), chemin enregistré dès qu'il est trouvé.
        /// Renvoie un message pour la fenêtre (vide si rien n'est à signaler).
        /// </summary>
        public static string CheckInstallation(Settings s)
        {
            bool saved = !string.IsNullOrEmpty(s.Game);
            bool exists = saved && File.Exists(s.Game);
            bool supported = false;
            if (exists)
                GameVersion(s.Game, out supported);
            if (exists && supported)
                return "";
            string found = FindGame();
            if (found.Length == 0 || string.Equals(found, s.Game, StringComparison.OrdinalIgnoreCase))
                return saved && !exists ? "The saved game location no longer exists: " + s.Game : "";
            string previous = s.Game;
            s.Game = found;
            s.Save();
            if (!saved)
                return "";  // première détection : rien à signaler
            return exists ? "A version 1.55 installation was found and saved (the previous one was not 1.55)."
                          : "The game has moved: new location saved (previous: " + previous + ").";
        }

        public static bool SteamRunning() => Process.GetProcessesByName("steam").Length > 0;

        public static string RandomName()
        {
            var rnd = new Random();
            return NamePrefixes[rnd.Next(NamePrefixes.Length)] + "-" + rnd.Next(100, 10000);
        }

        /// <summary>Numéro d'instance déjà pris par un jeu lancé (verrou posé par dlcoop_reseau.dll).</summary>
        public static bool InstanceTaken(int n)
        {
            if (Mutex.TryOpenExisting(@"Local\DLFantome_slot_" + n, out Mutex m))
            {
                m.Dispose();
                return true;
            }
            return false;
        }

        /// <summary>Instances par PC et adresses d'une session : 0..32 = 32 joueurs + 1 joueur zombie.</summary>
        public const int MaxInstances = 33;

        public static int FirstFreeInstance()
        {
            for (int n = 1; n < MaxInstances; ++n)
                if (!InstanceTaken(n))
                    return n;
            return -1;
        }

        /// <summary>
        /// Identité déjà utilisée par un jeu de ce PC, d'après le verrou du jeu lui-même (« Global\ChromeEngine4DIMutex »,
        /// renommé « _fantomeN » par dlcoop_reseau.dll pour une instance supplémentaire). Avec le relais, les verrous
        /// Local\DLFantome_slot_N désignent des adresses réseau, dont celles des joueurs distants : inutilisables ici.
        /// </summary>
        public static bool IdentityTaken(int n)
        {
            string name = @"Global\ChromeEngine4DIMutex" + (n > 0 ? "_fantome" + n : "");
            try
            {
                if (Mutex.TryOpenExisting(name, out Mutex m))
                {
                    m.Dispose();
                    return true;
                }
                return false;
            }
            catch (UnauthorizedAccessException)
            {
                return true;  // existe, mais protégé
            }
        }

        public static int FirstFreeIdentity()
        {
            for (int n = 1; n < MaxInstances; ++n)
                if (!IdentityTaken(n))
                    return n;
            return -1;
        }

        /// <summary>Numéro d'identité (0 = principale) que la prochaine instance utilisera.</summary>
        public static int PickInstance(Settings s)
        {
            if (!s.ExtraInstance)
            {
                if (s.Internet ? IdentityTaken(0) : InstanceTaken(0))
                    throw new InvalidOperationException("The main instance is already running.");
                return 0;
            }
            int instance = s.Instance > 0 ? s.Instance : s.Internet ? FirstFreeIdentity() : FirstFreeInstance();
            if (instance < 1 || instance >= MaxInstances)
                throw new InvalidOperationException("No free instance number left (1 to " + (MaxInstances - 1) + ").");
            if (s.Internet ? IdentityTaken(instance) : InstanceTaken(instance))
                throw new InvalidOperationException("Instance " + instance + " is already running.");
            return instance;
        }

        /// <summary>
        /// Lance le jeu ; renvoie le pid. Lève une exception avec un message lisible en cas d'échec.
        /// <paramref name="slot"/> : adresse réseau attribuée par le relais Internet (-1 : réseau local / VPN).
        /// </summary>
        public static int Launch(Settings s, out int instance, out string displayName, int slot = -1)
        {
            instance = 0;
            displayName = "";
            if (string.IsNullOrEmpty(s.Game) || !File.Exists(s.Game))
                throw new InvalidOperationException("Dying Light was not found: please select DyingLightGame.exe.");
            foreach (string dll in new[] { NetworkDll, ModDll })
                if (!File.Exists(Path.Combine(Folder, dll)))
                    throw new InvalidOperationException(dll + " is missing (it must be next to DLCoop.exe).");
            instance = PickInstance(s);

            if (s.HideName)
                displayName = string.IsNullOrWhiteSpace(s.Name) ? RandomName() : s.Name.Trim();
            string logs = Path.Combine(Folder, "logs");
            Directory.CreateDirectory(logs);
            var env = new Dictionary<string, string>
            {
                ["SteamAppId"] = GameAppId,
                ["SteamGameId"] = GameAppId,
                ["DLF_INDEX"] = instance.ToString(),
                // Relais : le jeu parle toujours sur son adresse 127.0.0.(slot+1), les autres joueurs sont sur
                // les adresses voisines (occupées par Relay.cs).
                ["DLF_LOOPBACK"] = s.MultiInstance || slot >= 0 ? "1" : "0",
                ["DLF_ADRESSE"] = slot >= 0 ? slot.ToString() : "",
                ["DLF_LOG"] = Path.Combine(logs, "instance_" + instance + ".log"),
                ["DLF_NOM"] = displayName,
                // Places du lobby quand ce jeu héberge : joueurs choisis + 1 place pour le zombie (Be the Zombie).
                ["COOP6_Places"] = (Math.Max(2, Math.Min(MaxInstances - 1, s.MaxPlayers)) + 1).ToString(),
                ["COOP6_Difficulte"] = "1",
                ["COOP6_Couleurs"] = "1",
                ["COOP6_Hud"] = "1",
                ["COOP6_Diagnostic"] = "1",
                ["COOP6_SansEos"] = s.DisableEos ? "1" : "0",
            };
            foreach (var kv in env)
                Environment.SetEnvironmentVariable(kv.Key, kv.Value.Length > 0 ? kv.Value : null);

            var cmd = new StringBuilder("\"" + s.Game + "\" -nologos");
            if (instance > 0 && s.NoSound)
                cmd.Append(" /nosound");
            if (instance > 0 && s.ReducedTextures)
                cmd.Append(" -texStreamMemMB 512");

            var si = new STARTUPINFO { cb = Marshal.SizeOf(typeof(STARTUPINFO)) };
            if (!CreateProcessW(s.Game, cmd, IntPtr.Zero, IntPtr.Zero, false, CREATE_SUSPENDED, IntPtr.Zero,
                                Path.GetDirectoryName(s.Game), ref si, out PROCESS_INFORMATION pi))
                throw new InvalidOperationException("Could not start the game (error " + Marshal.GetLastWin32Error() + ").");
            try
            {
                IntPtr loadLibrary = GetProcAddress(GetModuleHandleW("kernel32.dll"), "LoadLibraryW");
                foreach (string dll in new[] { NetworkDll, ModDll })
                {
                    byte[] path = Encoding.Unicode.GetBytes(Path.Combine(Folder, dll) + "\0");
                    IntPtr remote = VirtualAllocEx(pi.hProcess, IntPtr.Zero, (IntPtr)path.Length,
                                                   MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
                    if (remote == IntPtr.Zero ||
                        !WriteProcessMemory(pi.hProcess, remote, path, (IntPtr)path.Length, out _) ||
                        QueueUserAPC(loadLibrary, pi.hThread, remote) == 0)
                    {
                        int err = Marshal.GetLastWin32Error();
                        TerminateProcess(pi.hProcess, 1);
                        throw new InvalidOperationException("Could not prepare loading of " + dll + " (error " + err + ").");
                    }
                }
                ResumeThread(pi.hThread);
                return pi.dwProcessId;
            }
            finally
            {
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
            }
        }

        /// <summary>Attend (10 s au plus) que les deux DLL soient chargées dans le jeu.</summary>
        public static bool WaitForMod(int pid)
        {
            for (int i = 0; i < 100; ++i)
            {
                try
                {
                    using (var p = Process.GetProcessById(pid))
                    {
                        if (p.HasExited)
                            return false;
                        p.Refresh();
                        var names = p.Modules.Cast<ProcessModule>().Select(m => m.ModuleName.ToLowerInvariant()).ToList();
                        if (names.Contains(NetworkDll) && names.Contains(ModDll))
                            return true;
                    }
                }
                catch (ArgumentException) { return false; }      // processus terminé
                catch (System.ComponentModel.Win32Exception) { /* modules pas encore lisibles */ }
                catch (InvalidOperationException) { return false; }
                Thread.Sleep(100);
            }
            return false;
        }

        // ------------------------------------------------------------------ Win32
        const uint CREATE_SUSPENDED = 0x4, MEM_COMMIT = 0x1000, MEM_RESERVE = 0x2000, PAGE_READWRITE = 0x4;

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Unicode)]
        struct STARTUPINFO
        {
            public int cb;
            public string lpReserved, lpDesktop, lpTitle;
            public int dwX, dwY, dwXSize, dwYSize, dwXCountChars, dwYCountChars, dwFillAttribute, dwFlags;
            public short wShowWindow, cbReserved2;
            public IntPtr lpReserved2, hStdInput, hStdOutput, hStdError;
        }

        [StructLayout(LayoutKind.Sequential)]
        struct PROCESS_INFORMATION
        {
            public IntPtr hProcess, hThread;
            public int dwProcessId, dwThreadId;
        }

        [DllImport("kernel32.dll", SetLastError = true, CharSet = CharSet.Unicode)]
        static extern bool CreateProcessW(string app, StringBuilder cmd, IntPtr pa, IntPtr ta, bool inherit, uint flags,
                                          IntPtr env, string dir, ref STARTUPINFO si, out PROCESS_INFORMATION pi);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern IntPtr VirtualAllocEx(IntPtr proc, IntPtr addr, IntPtr size, uint type, uint protect);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool WriteProcessMemory(IntPtr proc, IntPtr addr, byte[] data, IntPtr size, out IntPtr written);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern uint QueueUserAPC(IntPtr fn, IntPtr thread, IntPtr data);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern uint ResumeThread(IntPtr thread);
        [DllImport("kernel32.dll", SetLastError = true)]
        static extern bool TerminateProcess(IntPtr proc, uint code);
        [DllImport("kernel32.dll")]
        static extern bool CloseHandle(IntPtr h);
        [DllImport("kernel32.dll", CharSet = CharSet.Unicode)]
        static extern IntPtr GetModuleHandleW(string name);
        [DllImport("kernel32.dll", CharSet = CharSet.Ansi)]
        static extern IntPtr GetProcAddress(IntPtr module, string name);
    }
}
