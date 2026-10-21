// Startup: the settings window, or a direct headless launch (scripts):
//   DLCoop.exe /launch [/instance N] [/multi] [/name Name] [/game path\DyingLightGame.exe]
//              [/internet | /lan] [/server ip[:port]] [/password X] [/maxplayers N] [/count N]
// The other options are the ones saved by the window (a filled-in server = Internet relay, otherwise local network;
// /internet and /lan force the mode). Exit code: 0 = game(s) launched with the mod loaded.
// Internet relay: the process stays alive (headless) as long as one of the launched games runs. Only one DLCoop per PC
// is the relay agent: another DLCoop (window or /launch) forwards its launches to it (LaunchPipe).
// /count N: N instances (the first from the settings, the later ones as additional instances), 10 s apart.
using System;
using System.Threading;
using System.Windows;

namespace DLCoop
{
    public partial class App : Application
    {
        /// <summary>This process's Internet relay agent (null: no relay).</summary>
        public static Relay Relay { get; private set; }

        static readonly object relayGate = new object();

        /// <summary>Agent connected to the settings' relay; an agent already open to another relay is replaced
        /// if it has no running game left.</summary>
        public static Relay ConnectRelay(Settings s)
        {
            lock (relayGate)
            {
                var r = Relay;
                if (r != null && (!string.Equals(r.Address, s.Server.Trim(), StringComparison.OrdinalIgnoreCase) ||
                                  r.Password != s.Password))
                {
                    if (r.GamesRunning > 0)
                        throw new InvalidOperationException(
                            "Your game is still connected to " + r.Address + ": close it before changing the relay.");
                    r.Dispose();
                    Relay = r = null;
                }
                if (r == null)
                {
                    Relay = r = Relay.Connect(s.Server, s.Password);
                    LaunchPipe.Serve();  // the other DLCoop instances on this PC will go through this agent
                }
                return r;
            }
        }

        public static void DisconnectRelay()
        {
            lock (relayGate)
            {
                LaunchPipe.Stop();
                Relay?.Dispose();
                Relay = null;
            }
        }

        static readonly object launchGate = new object();

        /// <summary>
        /// Relay launch in THIS process (the agent): an address is requested from the relay, the game launched and
        /// tracked. Serialized: Launcher.Launch sets process environment variables (the window and the launch pipe can
        /// launch at the same time). Throws RelayBusyException if another DLCoop is the agent.
        /// </summary>
        public static int LaunchInRelay(Settings s, out int instance)
        {
            lock (launchGate)
            {
                var relay = ConnectRelay(s);
                Launcher.PickInstance(s);  // check before reserving an address
                int slot = relay.Claim();
                int pid;
                try
                {
                    pid = Launcher.Launch(s, out instance, out _, slot);
                }
                catch
                {
                    relay.Release(slot);  // game not launched: address returned
                    throw;
                }
                relay.Track(pid, slot);
                return pid;
            }
        }

        /// <summary>Relay: launches here, or forwards to the agent already open on this PC. <paramref name="forwarded"/>:
        /// the game is tracked by the other DLCoop (this one may close).</summary>
        public static int LaunchWithRelay(Settings s, out int instance, out bool forwarded)
        {
            try
            {
                forwarded = false;
                return LaunchInRelay(s, out instance);
            }
            catch (RelayBusyException)
            {
                forwarded = true;
                return LaunchPipe.Request(s, out instance);
            }
        }

        void OnStartup(object sender, StartupEventArgs e)
        {
            Launcher.PreloadSystemWinmm();  // first of all: never the package's loader in this process
            var args = e.Args;
            bool direct = false;
            int count = 1;
            var s = Settings.Load();
            string gameMessage = Launcher.CheckInstallation(s);  // on each start: is the path still valid?
            for (int i = 0; i < args.Length; ++i)
            {
                string a = args[i].TrimStart('/', '-').ToLowerInvariant();
                if (a == "launch")
                    direct = true;
                else if (a == "instance" && i + 1 < args.Length && int.TryParse(args[++i], out int n))
                {
                    s.ExtraInstance = n > 0;
                    s.Instance = n;
                }
                else if (a == "multi")
                    s.MultiInstance = true;
                else if (a == "name" && i + 1 < args.Length)
                {
                    s.HideName = true;
                    s.Name = args[++i];
                }
                else if (a == "game" && i + 1 < args.Length)
                    s.Game = args[++i];
                else if (a == "internet")
                    s.Internet = true;
                else if (a == "lan")
                    s.Internet = false;
                else if (a == "server" && i + 1 < args.Length)
                    s.Server = args[++i];
                else if (a == "password" && i + 1 < args.Length)
                    s.Password = args[++i];
                else if (a == "maxplayers" && i + 1 < args.Length && int.TryParse(args[++i], out int m))
                    s.MaxPlayers = Math.Max(2, Math.Min(Launcher.MaxInstances - 1, m));
                else if (a == "count" && i + 1 < args.Length && int.TryParse(args[++i], out int c))
                    count = Math.Max(1, Math.Min(Launcher.MaxInstances, c));
            }
            if (!direct)
            {
                MainWindow = new MainWindow(s, gameMessage);
                MainWindow.Show();
                return;
            }
            ShutdownMode = ShutdownMode.OnExplicitShutdown;
            int code = 1;
            try
            {
                for (int k = 0; k < count; ++k)
                {
                    if (k > 0)
                    {
                        Thread.Sleep(10000);  // 10 s between two instances (value validated with tools/fantome)
                        s.ExtraInstance = true;
                        s.Instance = 0;
                        s.MultiInstance = true;
                        s.Name = s.HideName ? Launcher.RandomName() : s.Name;
                    }
                    int pid = s.Internet ? LaunchWithRelay(s, out _, out _) : Launcher.Launch(s, out _, out _);
                    if (!Launcher.WaitForMod(pid))
                        throw new InvalidOperationException("The mod was not detected in the game (see the logs folder).");
                }
                code = 0;
                Relay?.WaitForGames();  // relay: the agent stays alive until the games are closed
            }
            catch (Exception ex)
            {
                MessageBox.Show(ex.Message, "DLCoop", MessageBoxButton.OK, MessageBoxImage.Error);
                Relay?.WaitForGames();
            }
            DisconnectRelay();
            Shutdown(code);
        }
    }
}
