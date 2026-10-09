// Launches Dying Light 1.55 with the mod, no injection: the winmm.dll loader (tools/dlloader), installed in the
// game folder with dlcoop_reseau.dll and coop6.dll, is loaded by the game itself and loads the mods. The options
// go through the game's environment (DLF_* for dlcoop_reseau.dll, COOP6_* for coop6.dll), inherited from the
// launcher, and are also written to dlmods.ini (a fallback for a Steam launch). The game folder is modified by
// Install / Uninstall. UI and messages: English only.
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
    /// <summary>User choices, saved in %APPDATA%\DLCoop\settings.txt (key=value).</summary>
    public sealed class Settings
    {
        public string Game = "";
        public bool HideName;
        public string Name = "";
        // Instance: reset to defaults on each start (main instance).
        public bool ExtraInstance;
        public int Instance;                // 0 = automatic (first free number) for an additional instance
        public bool MultiInstance;          // 127.0.0.N addresses (several instances on this PC, local network)
        public bool DisableEos = true;
        public bool CloseAfterLaunch = true;
        public int MaxPlayers = Launcher.MaxInstances - 1;  // max human players when hosting (the BtZ zombie on top)
        public bool Internet;               // Internet relay (Relay.cs): inferred from the server (filled in = relay)
        public string Server = "";          // relay address (ip[:port]); default: server.txt next to the exe
        public string Password = "";        // session password (the same for every player)

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
            catch (Exception) { /* unreadable settings: defaults */ }
            var defaults = new Settings();
            s.ExtraInstance = defaults.ExtraInstance;
            s.Instance = defaults.Instance;
            s.MultiInstance = defaults.MultiInstance;
            if (string.IsNullOrWhiteSpace(s.Server))
            {
                // Package prepared for a group: relay address provided (build.ps1 -Relais).
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
            catch (Exception) { /* non-blocking */ }
        }

        /// <summary>Settings as "key=value" lines (settings.txt file, the LaunchPipe launch pipe).</summary>
        public IEnumerable<string> Lines() => typeof(Settings).GetFields().Where(f => !f.IsStatic).Select(f =>
        {
            object v = f.GetValue(this);
            return f.Name + "=" + (v is bool b ? (b ? "1" : "0") : Convert.ToString(v));
        });

        /// <summary>Settings as-is, without Load's adjustments (the launch pipe).</summary>
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
        public const string LoaderDll = "winmm.dll";       // proxy placed in the game folder (tools/dlloader)
        public const string ConfigIni = "dlmods.ini";      // read by the loader, rewritten on each launch
        // Loader files, installed in the game folder.
        public static readonly string[] InstalledFiles = { LoaderDll, NetworkDll, ModDll };
        const string GameAppId = "239140";
        static readonly string[] NamePrefixes = { "Runner", "Climber", "Prowler", "Watcher", "Nomad", "Scout",
                                                  "Tracker", "Survivor", "Courier", "Drifter" };

        public static string Folder => AppDomain.CurrentDomain.BaseDirectory.TrimEnd('\\');

        /// <summary>All DyingLightGame.exe found: Steam uninstall key, Steam libraries
        /// (libraryfolders.vdf + appmanifest_239140.acf for the real folder name), default folder.</summary>
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
                catch (Exception) { /* key absent or unreadable */ }
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
                catch (Exception) { /* unreadable file */ }
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
                catch (Exception) { /* unreadable manifest */ }
                folders.Add(Path.Combine(lib, "steamapps", "common", sub));
            }
            return folders.Select(d => Path.Combine(d, "DyingLightGame.exe"))
                          .Where(File.Exists)
                          .Distinct(StringComparer.OrdinalIgnoreCase)
                          .ToList();
        }

        /// <summary>First DyingLightGame.exe found, preferably 1.55 (or an empty string).</summary>
        public static string FindGame()
        {
            var all = FindGames();
            return all.FirstOrDefault(e => { GameVersion(e, out bool ok); return ok; }) ?? all.FirstOrDefault() ?? "";
        }

        /// <summary>Game version ("1.55"...); <paramref name="supported"/> true for 1.55 only.
        /// Empty string if the file does not exist.</summary>
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
        /// On each start: the saved path is kept if it exists and is 1.55; otherwise a new search (moved install,
        /// another library...), with the path saved as soon as it is found.
        /// Returns a message for the window (empty if there is nothing to report).
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
                return "";  // first detection: nothing to report
            return exists ? "A version 1.55 installation was found and saved (the previous one was not 1.55)."
                          : "The game has moved: new location saved (previous: " + previous + ").";
        }

        public static bool SteamRunning() => Process.GetProcessesByName("steam").Length > 0;

        // ------------------------------------------------------------------ Mod loader (winmm.dll)
        // The game loads its mods itself because winmm.dll (the proxy) is in its folder: no outside process
        // writes into its memory (no "injection" pattern flagged by antivirus).

        public static string GameDir(Settings s) => Path.GetDirectoryName(s.Game) ?? "";

        /// <summary>True if the loader and both mods are in the game folder AND identical to this launcher's
        /// (otherwise a DLCoop update would never replace the already-installed files).</summary>
        public static bool IsInstalled(Settings s)
        {
            string dir = GameDir(s);
            return dir.Length > 0 && InstalledFiles.All(f => SameFile(Path.Combine(Folder, f), Path.Combine(dir, f)));
        }

        /// <summary>Is the mod present in the game folder, up to date or not?</summary>
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
                return File.ReadAllBytes(a).SequenceEqual(File.ReadAllBytes(b));  // a few hundred KB
            }
            catch (IOException) { return false; }
            catch (UnauthorizedAccessException) { return false; }
        }

        /// <summary>
        /// Copies the loader (winmm.dll) and the mods (dlcoop_reseau.dll, coop6.dll), sitting next to DLCoop.exe,
        /// into the game folder. Modifies the game install. Throws with a readable message on failure.
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
            // DLML's dsound.dll: if active it may affect the lobby. We do not touch it (the README warns about
            // it); here we only install our winmm loader.
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

        /// <summary>Removes the loader, the mods and dlmods.ini from the game folder. Does not touch DLML.</summary>
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
        /// Restores sound if audio.scr (Documents\DyingLight\out\settings, shared by all instances) contains
        /// API("NoSound"). That setting is never chosen in-game: it is written by an instance started with
        /// /nosound (the old DLCoop "No sound" option, or the tools/fantome ghosts) then re-read by every later
        /// launch, which then has no sound. Restored value: the engine's (data/settings/defaultaudio.scr: FMOD).
        /// Returns true if the file was fixed.
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
            catch (Exception) { return false; }  // non-blocking: at worst, no sound
        }

        /// <summary>
        /// Writes dlmods.ini into the game folder: [loader] Mods= (both DLLs) and [env] (the settings).
        /// The loader sets the [env] variables only if they are not already in the game's environment: the
        /// per-instance values set by this launcher (below) win; the file is a fallback for a direct launch
        /// (double-click, Steam) and so the user can see / edit their settings.
        /// </summary>
        public static void WriteConfig(Settings s, IDictionary<string, string> env)
        {
            string dir = GameDir(s);
            var lines = new List<string>
            {
                "; winmm.dll loader configuration (rewritten by DLCoop on each launch).",
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

        /// <summary>Instance number already taken by a running game (lock set by dlcoop_reseau.dll).</summary>
        public static bool InstanceTaken(int n)
        {
            if (Mutex.TryOpenExisting(@"Local\DLFantome_slot_" + n, out Mutex m))
            {
                m.Dispose();
                return true;
            }
            return false;
        }

        /// <summary>Instances per PC and addresses of a session: 0..32 = 32 players + 1 zombie player.</summary>
        public const int MaxInstances = 33;

        public static int FirstFreeInstance()
        {
            for (int n = 1; n < MaxInstances; ++n)
                if (!InstanceTaken(n))
                    return n;
            return -1;
        }

        /// <summary>
        /// Identity already used by a game on this PC, from the game's own lock ("Global\ChromeEngine4DIMutex",
        /// renamed "_fantomeN" by dlcoop_reseau.dll for an additional instance). With the relay, the
        /// Local\DLFantome_slot_N locks map to network addresses, including remote players': unusable here.
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
                return true;  // exists, but protected
            }
        }

        public static int FirstFreeIdentity()
        {
            for (int n = 1; n < MaxInstances; ++n)
                if (!IdentityTaken(n))
                    return n;
            return -1;
        }

        /// <summary>Identity number (0 = main) that the next instance will use.</summary>
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
        /// Launches the game; returns the pid. Throws with a readable message on failure.
        /// <paramref name="slot"/>: network address assigned by the Internet relay (-1: local network / VPN).
        /// </summary>
        public static int Launch(Settings s, out int instance, out string displayName, int slot = -1)
        {
            instance = 0;
            displayName = "";
            if (string.IsNullOrEmpty(s.Game) || !File.Exists(s.Game))
                throw new InvalidOperationException("Dying Light was not found: please select DyingLightGame.exe.");
            // The game loads the loader and the mods itself (winmm.dll in its folder): we make sure they are
            // there (auto-install if needed), with no injection by this launcher.
            // A running game locks the folder's DLLs: we then keep the installed files (the new instance loads the
            // same ones as the first); the update happens on the next launch, with the games closed.
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
                // Relay: the game always talks on its own address 127.0.0.(slot+1); the other players are on the
                // neighboring addresses (held by Relay.cs).
                ["DLF_LOOPBACK"] = s.MultiInstance || slot >= 0 ? "1" : "0",
                ["DLF_ADRESSE"] = slot >= 0 ? slot.ToString() : "",
                ["DLF_LOG"] = Path.Combine(logs, "instance_" + instance + ".log"),
                ["DLF_NOM"] = displayName,
                // Lobby slots when this game hosts: chosen players + 1 slot for the zombie (Be the Zombie).
                ["COOP6_Places"] = (Math.Max(2, Math.Min(MaxInstances - 1, s.MaxPlayers)) + 1).ToString(),
                ["COOP6_Difficulte"] = "1",
                ["COOP6_Couleurs"] = "1",
                ["COOP6_Hud"] = "1",
                ["COOP6_Diagnostic"] = "1",
                ["COOP6_SansEos"] = s.DisableEos ? "1" : "0",
            };
            // Per-instance values set in the launcher's environment: the game launched below inherits them
            // (CreateProcess, inherited env). The loader does not overwrite them with dlmods.ini, so they win for
            // this instance (DLF_INDEX, address... distinct when several games run on this PC).
            foreach (var kv in env)
                Environment.SetEnvironmentVariable(kv.Key, kv.Value.Length > 0 ? kv.Value : null);
            // dlmods.ini: the settings written into the game folder (visible, editable, and used for a direct
            // launch, via Steam, without the launcher). Only those of a plain launch: main instance, no relay
            // address (the relay agent lives in the launcher) and no local log.
            // Otherwise a Steam launch after a relay game would reuse a dead 127.0.0.N address.
            var fileEnv = new Dictionary<string, string>(env)
            {
                ["DLF_INDEX"] = "0",
                ["DLF_LOOPBACK"] = "0",
            };
            fileEnv.Remove("DLF_ADRESSE");
            fileEnv.Remove("DLF_LOG");
            WriteConfig(s, fileEnv);

            RepairAudio();  // the sound setting is re-read at game startup

            var cmd = new StringBuilder("\"" + s.Game + "\" -nologos");

            // Normal launch (the game loads winmm.dll -> the mods by itself): no more CREATE_SUSPENDED,
            // VirtualAllocEx, WriteProcessMemory or QueueUserAPC. Nothing is written into the game's memory.
            var si = new STARTUPINFO { cb = Marshal.SizeOf(typeof(STARTUPINFO)) };
            if (!CreateProcessW(s.Game, cmd, IntPtr.Zero, IntPtr.Zero, false, 0, IntPtr.Zero,
                                Path.GetDirectoryName(s.Game), ref si, out PROCESS_INFORMATION pi))
                throw new InvalidOperationException("Could not start the game (error " + Marshal.GetLastWin32Error() + ").");
            CloseHandle(pi.hThread);
            CloseHandle(pi.hProcess);
            return pi.dwProcessId;
        }

        /// <summary>Waits (up to 10 s) for both DLLs to be loaded in the game.</summary>
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
                catch (ArgumentException) { return false; }      // process ended
                catch (System.ComponentModel.Win32Exception) { /* modules not readable yet */ }
                catch (InvalidOperationException) { return false; }
                Thread.Sleep(100);
            }
            return false;
        }

        // ------------------------------------------------------------------ Win32
        // Normal game launch (the game loads winmm.dll -> the mods itself). We keep CreateProcessW to target the
        // exe and working directory precisely, but with no injection flags.
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
