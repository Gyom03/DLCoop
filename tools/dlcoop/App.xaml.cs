// Démarrage : fenêtre de réglages, ou lancement direct sans fenêtre (scripts) :
//   DLCoop.exe /launch [/instance N] [/multi] [/name Name] [/game path\DyingLightGame.exe]
//              [/internet | /lan] [/server ip[:port]] [/password X] [/maxplayers N] [/count N]
// Les autres options sont celles mémorisées par la fenêtre (serveur renseigné = relais Internet, sinon réseau local ;
// /internet et /lan forcent le mode). Code de sortie : 0 = jeu(x) lancé(s) avec le mod chargé.
// Relais Internet : le processus reste actif (sans fenêtre) tant qu'un des jeux lancés tourne. Un seul DLCoop par PC est
// l'agent du relais : un autre DLCoop (fenêtre ou /launch) lui transmet ses lancements (LaunchPipe).
// /count N : N instances (la première selon les réglages, les suivantes en instances supplémentaires), 10 s d'écart.
using System;
using System.Threading;
using System.Windows;

namespace DLCoop
{
    public partial class App : Application
    {
        /// <summary>Agent du relais Internet de ce processus (null : pas de relais).</summary>
        public static Relay Relay { get; private set; }

        static readonly object relayGate = new object();

        /// <summary>Agent connecté au relais des réglages ; un agent déjà ouvert vers un autre relais est remplacé
        /// s'il n'a plus de jeu en cours.</summary>
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
                    LaunchPipe.Serve();  // les autres DLCoop de ce PC passeront par cet agent
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
        /// Lancement par le relais dans CE processus (agent) : adresse demandée au relais, jeu lancé et suivi.
        /// Sérialisé : Launcher.Launch pose des variables d'environnement du processus (fenêtre et tube de
        /// lancement peuvent lancer en même temps). Lève RelayBusyException si un autre DLCoop est l'agent.
        /// </summary>
        public static int LaunchInRelay(Settings s, out int instance)
        {
            lock (launchGate)
            {
                var relay = ConnectRelay(s);
                Launcher.PickInstance(s);  // vérification avant de réserver une adresse
                int slot = relay.Claim();
                int pid;
                try
                {
                    pid = Launcher.Launch(s, out instance, out _, slot);
                }
                catch
                {
                    relay.Release(slot);  // jeu non lancé : adresse rendue
                    throw;
                }
                relay.Track(pid, slot);
                return pid;
            }
        }

        /// <summary>Relais : lance ici, ou transmet à l'agent déjà ouvert sur ce PC. <paramref name="forwarded"/> :
        /// le jeu est suivi par l'autre DLCoop (celui-ci peut se fermer).</summary>
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
            var args = e.Args;
            bool direct = false;
            int count = 1;
            var s = Settings.Load();
            string gameMessage = Launcher.CheckInstallation(s);  // à chaque démarrage : chemin toujours valable ?
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
                        Thread.Sleep(10000);  // 10 s entre deux instances (valeur validée avec tools/fantome)
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
                Relay?.WaitForGames();  // relais : l'agent reste actif jusqu'à la fermeture des jeux
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
