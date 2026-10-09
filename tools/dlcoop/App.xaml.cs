// Démarrage : fenêtre de réglages, ou lancement direct sans fenêtre (scripts) :
//   DLCoop.exe /launch [/instance N] [/multi] [/name Name] [/game path\DyingLightGame.exe]
//              [/internet | /lan] [/server ip[:port]] [/password X] [/maxplayers N] [/count N]
// Les autres options sont celles mémorisées par la fenêtre (serveur renseigné = relais Internet, sinon réseau local ;
// /internet et /lan forcent le mode). Code de sortie : 0 = jeu(x) lancé(s) avec le mod chargé.
// Relais Internet : le processus reste actif (sans fenêtre) tant qu'un des jeux lancés tourne.
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
                    Relay = r = Relay.Connect(s.Server, s.Password);
                return r;
            }
        }

        public static void DisconnectRelay()
        {
            lock (relayGate)
            {
                Relay?.Dispose();
                Relay = null;
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
                        s.NoSound = s.ReducedTextures = true;  // plusieurs jeux sur un PC : mémoire
                        s.Name = s.HideName ? Launcher.RandomName() : s.Name;
                    }
                    int slot = -1;
                    Relay relay = null;
                    if (s.Internet)
                    {
                        relay = ConnectRelay(s);
                        Launcher.PickInstance(s);
                        slot = relay.Claim();
                    }
                    int pid;
                    try
                    {
                        pid = Launcher.Launch(s, out _, out _, slot);
                    }
                    catch
                    {
                        if (slot >= 0)
                            relay.Release(slot);
                        throw;
                    }
                    relay?.Track(pid, slot);
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
