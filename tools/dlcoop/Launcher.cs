// Lancement de Dying Light 1.55 avec le mod, sans injection : le chargeur winmm.dll (tools/dlloader), installé dans
// le dossier du jeu avec dlcoop_reseau.dll et coop6.dll, est chargé par le jeu lui-même et charge les mods. Les options
// passent par l'environnement du jeu (DLF_* pour dlcoop_reseau.dll, COOP6_* pour coop6.dll), hérité du lanceur, et
// sont aussi écrites dans dlmods.ini (repli pour un lancement par Steam). Le dossier du jeu est modifié par
// Install / Uninstall. Interface et messages : en anglais uniquement.
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
                s.Apply(File.ReadAllLines(FilePath, Encoding.UTF8));
            }
            catch (Exception) { /* réglages illisibles : valeurs par défaut */ }
            var defaults = new Settings();
            s.ExtraInstance = defaults.ExtraInstance;
            s.Instance = defaults.Instance;
            s.MultiInstance = defaults.MultiInstance;
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
                File.WriteAllLines(FilePath, Lines(), Encoding.UTF8);
            }
            catch (Exception) { /* non bloquant */ }
        }

        /// <summary>Réglages en lignes « clé=valeur » (fichier settings.txt, tube de lancement LaunchPipe).</summary>
        public IEnumerable<string> Lines() => typeof(Settings).GetFields().Where(f => !f.IsStatic).Select(f =>
        {
            object v = f.GetValue(this);
            return f.Name + "=" + (v is bool b ? (b ? "1" : "0") : Convert.ToString(v));
        });

        /// <summary>Réglages tels quels, sans les retouches de Load (tube de lancement).</summary>
        public static Settings FromLines(IEnumerable<string> lines)
        {
            var s = new Settings();
            s.Apply(lines);
            return s;
        }

        void Apply(IEnumerable<string> lines)
        {
            foreach (var line in lines)
            {
                int i = line.IndexOf('=');
                if (i <= 0)
                    continue;
                string k = line.Substring(0, i).Trim(), v = line.Substring(i + 1).Trim();
                var field = typeof(Settings).GetField(k);
                if (field == null || field.IsStatic)
                    continue;
                if (field.FieldType == typeof(bool))
                    field.SetValue(this, v == "1");
                else if (field.FieldType == typeof(int) && int.TryParse(v, out int n))
                    field.SetValue(this, n);
                else if (field.FieldType == typeof(string))
                    field.SetValue(this, v);
            }
        }
    }

    public static class Launcher
    {
        public const string NetworkDll = "dlcoop_reseau.dll";
        public const string ModDll = "coop6.dll";
        public const string LoaderDll = "winmm.dll";       // relais posé dans le dossier du jeu (tools/dlloader)
        public const string ConfigIni = "dlmods.ini";      // lu par le relais, réécrit à chaque lancement
        // Fichiers du chargeur, installés dans le dossier du jeu.
        public static readonly string[] InstalledFiles = { LoaderDll, NetworkDll, ModDll };
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

        // ------------------------------------------------------------------ Chargeur de mods (winmm.dll)
        // Le jeu charge lui-même ses mods parce que winmm.dll (le relais) est dans son dossier : aucun
        // processus extérieur n'écrit dans sa mémoire (plus de motif « injection » signalé par les antivirus).

        public static string GameDir(Settings s) => Path.GetDirectoryName(s.Game) ?? "";

        /// <summary>Vrai si le chargeur et les deux mods sont posés dans le dossier du jeu ET identiques à ceux de
        /// ce lanceur (sinon une mise à jour de DLCoop ne remplacerait jamais les fichiers déjà installés).</summary>
        public static bool IsInstalled(Settings s)
        {
            string dir = GameDir(s);
            return dir.Length > 0 && InstalledFiles.All(f => SameFile(Path.Combine(Folder, f), Path.Combine(dir, f)));
        }

        /// <summary>Le mod est-il présent dans le dossier du jeu, à jour ou non ?</summary>
        public static bool IsPresent(Settings s)
        {
            string dir = GameDir(s);
            return dir.Length > 0 && File.Exists(Path.Combine(dir, LoaderDll));
        }

        static bool SameFile(string a, string b)
        {
            try
            {
                if (!File.Exists(a) || !File.Exists(b) || new FileInfo(a).Length != new FileInfo(b).Length)
                    return false;
                return File.ReadAllBytes(a).SequenceEqual(File.ReadAllBytes(b));  // quelques centaines de Ko
            }
            catch (IOException) { return false; }
            catch (UnauthorizedAccessException) { return false; }
        }

        /// <summary>
        /// Copie le chargeur (winmm.dll) et les mods (dlcoop_reseau.dll, coop6.dll), posés à côté de
        /// DLCoop.exe, dans le dossier du jeu. Modifie l'installation du jeu (exception assumée pour ce
        /// chargeur, comme la désactivation de DLML). Lève une exception avec un message lisible en cas d'échec.
        /// </summary>
        public static void Install(Settings s)
        {
            string dir = GameDir(s);
            if (dir.Length == 0 || !File.Exists(s.Game))
                throw new InvalidOperationException("Dying Light was not found: please select DyingLightGame.exe.");
            foreach (string f in InstalledFiles)
            {
                string src = Path.Combine(Folder, f);
                if (!File.Exists(src))
                    throw new InvalidOperationException(f + " is missing next to DLCoop.exe (reinstall DLCoop).");
            }
            // dsound.dll de DLML : s'il est actif il peut agir sur le lobby. On ne le touche pas, mais on
            // prévient en laissant le README le signaler ; ici on installe seulement notre chargeur winmm.
            foreach (string f in InstalledFiles)
            {
                string dst = Path.Combine(dir, f);
                try
                {
                    File.Copy(Path.Combine(Folder, f), dst, true);
                }
                catch (UnauthorizedAccessException)
                {
                    throw new InvalidOperationException("Could not write to the game folder (close the game and " +
                        "run DLCoop as administrator if needed): " + dst);
                }
                catch (IOException ex)
                {
                    throw new InvalidOperationException("Could not copy " + f + " (is the game running?): " + ex.Message);
                }
            }
        }

        /// <summary>Retire le chargeur, les mods et dlmods.ini du dossier du jeu. Ne touche pas à DLML.</summary>
        public static void Uninstall(Settings s)
        {
            string dir = GameDir(s);
            if (dir.Length == 0)
                throw new InvalidOperationException("Dying Light was not found: please select DyingLightGame.exe.");
            foreach (string f in InstalledFiles.Concat(new[] { ConfigIni }))
            {
                string path = Path.Combine(dir, f);
                try
                {
                    if (File.Exists(path))
                        File.Delete(path);
                }
                catch (Exception ex)
                {
                    throw new InvalidOperationException("Could not remove " + f + " (is the game running?): " + ex.Message);
                }
            }
        }

        /// <summary>
        /// Remet le son si audio.scr (Documents\DyingLight\out\settings, partagé par toutes les instances) contient
        /// API("NoSound"). Ce réglage n'est jamais choisi dans le jeu : il est écrit par une instance lancée avec
        /// /nosound (ancienne option « No sound » de DLCoop, fantômes de tools/fantome) puis relu par tous les
        /// lancements suivants, qui n'ont alors plus de son. Valeur rendue : celle du moteur (data/settings/
        /// defaultaudio.scr : FMOD). Renvoie vrai si le fichier a été corrigé.
        /// </summary>
        public static bool RepairAudio()
        {
            string f = Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.MyDocuments),
                                    "DyingLight", "out", "settings", "audio.scr");
            try
            {
                if (!File.Exists(f))
                    return false;
                string text = File.ReadAllText(f);
                var fixedText = Regex.Replace(text, "^(\\s*)API\\(\\s*\"NoSound\"\\s*\\)", "$1API(\"FMOD\")",
                                              RegexOptions.Multiline | RegexOptions.IgnoreCase);
                if (fixedText == text)
                    return false;
                File.WriteAllText(f, fixedText);
                return true;
            }
            catch (Exception) { return false; }  // non bloquant : au pire, pas de son
        }

        /// <summary>
        /// Écrit dlmods.ini dans le dossier du jeu : [loader] Mods= (les deux DLL) et [env] (les réglages).
        /// Le relais pose les variables de [env] seulement si elles ne sont pas déjà dans l'environnement du
        /// jeu : les valeurs par instance posées par ce lanceur (ci-dessous) priment, le fichier sert de repli
        /// pour un lancement direct (double-clic, Steam) et pour que l'utilisateur voie / modifie ses réglages.
        /// </summary>
        public static void WriteConfig(Settings s, IDictionary<string, string> env)
        {
            string dir = GameDir(s);
            var lines = new List<string>
            {
                "; Configuration du chargeur winmm.dll (réécrite par DLCoop à chaque lancement).",
                "[loader]",
                "Mods=" + NetworkDll + ";" + ModDll,
                "",
                "[env]",
            };
            foreach (var kv in env)
                if (!string.IsNullOrEmpty(kv.Value))
                    lines.Add(kv.Key + "=" + kv.Value);
            File.WriteAllLines(Path.Combine(dir, ConfigIni), lines, Encoding.UTF8);
        }

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
            // Le jeu charge lui-même le chargeur et les mods (winmm.dll dans son dossier) : on s'assure qu'ils
            // y sont (installation automatique si besoin), plus aucune injection par ce lanceur.
            // Un jeu déjà ouvert verrouille les DLL du dossier : on garde alors les fichiers installés (la nouvelle
            // instance charge les mêmes que la première) ; la mise à jour se fera au prochain lancement, jeux fermés.
            if (!IsInstalled(s))
            {
                if (!IsPresent(s) || Process.GetProcessesByName("DyingLightGame").Length == 0)
                    Install(s);
            }
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
            // Valeurs par instance posées dans l'environnement du lanceur : le jeu lancé ci-dessous en hérite
            // (CreateProcess, env hérité). Le relais ne les écrase pas avec dlmods.ini : elles priment donc
            // pour cette instance (DLF_INDEX, adresse… distincts quand plusieurs jeux tournent sur ce PC).
            foreach (var kv in env)
                Environment.SetEnvironmentVariable(kv.Key, kv.Value.Length > 0 ? kv.Value : null);
            // dlmods.ini : les réglages écrits dans le dossier du jeu (visibles, modifiables, et utilisés pour un
            // lancement direct, par Steam, sans le lanceur). Seulement ceux d'un lancement simple : instance
            // principale, sans adresse de relais (l'agent du relais vit dans le lanceur) ni journal local.
            // Sinon un lancement par Steam après une partie par le relais reprendrait une adresse 127.0.0.N morte.
            var fileEnv = new Dictionary<string, string>(env)
            {
                ["DLF_INDEX"] = "0",
                ["DLF_LOOPBACK"] = "0",
            };
            fileEnv.Remove("DLF_ADRESSE");
            fileEnv.Remove("DLF_LOG");
            WriteConfig(s, fileEnv);

            RepairAudio();  // réglage son relu au démarrage du jeu

            var cmd = new StringBuilder("\"" + s.Game + "\" -nologos");

            // Lancement normal (le jeu charge winmm.dll -> les mods tout seul) : plus de CREATE_SUSPENDED,
            // VirtualAllocEx, WriteProcessMemory ni QueueUserAPC. Rien n'est écrit dans la mémoire du jeu.
            var si = new STARTUPINFO { cb = Marshal.SizeOf(typeof(STARTUPINFO)) };
            if (!CreateProcessW(s.Game, cmd, IntPtr.Zero, IntPtr.Zero, false, 0, IntPtr.Zero,
                                Path.GetDirectoryName(s.Game), ref si, out PROCESS_INFORMATION pi))
                throw new InvalidOperationException("Could not start the game (error " + Marshal.GetLastWin32Error() + ").");
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return pi.dwProcessId;
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
        // Lancement normal du jeu (le jeu charge winmm.dll -> les mods lui-même). On garde CreateProcessW pour
        // viser précisément l'exe et le dossier de travail, mais sans aucun drapeau d'injection.
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
        [DllImport("kernel32.dll")]
        static extern bool CloseHandle(IntPtr h);
    }
}
