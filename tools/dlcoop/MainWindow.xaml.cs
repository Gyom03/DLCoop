using System;
using System.ComponentModel;
using System.IO;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Media;
using System.Windows.Threading;
using Microsoft.Win32;

namespace DLCoop
{
    public partial class MainWindow : Window
    {
        readonly Settings s;
        readonly DispatcherTimer relayTimer = new DispatcherTimer { Interval = TimeSpan.FromSeconds(1) };
        bool minimizedAfterLaunch;  // relay: window minimized instead of closed (the agent must stay alive)

        public MainWindow(Settings settings, string gameMessage = "")
        {
            InitializeComponent();
            s = settings;
            InstanceNumber.Items.Add("auto");
            for (int n = 1; n < Launcher.MaxInstances; ++n)
                InstanceNumber.Items.Add(n.ToString());
            for (int n = 2; n < Launcher.MaxInstances; ++n)
                MaxPlayers.Items.Add(n.ToString());
            Display(s);
            ServerAddress.TextChanged += (o, e) => UpdateState(null, null);
            UpdateState(null, null);
            string missing = Launcher.MissingFiles();
            if (missing.Length > 0)
                SetStatus(missing, "Erreur");  // right at startup, before the first "Install mod"
            else if (!string.IsNullOrEmpty(gameMessage))
                SetStatus(gameMessage, "Accent");
            relayTimer.Tick += (o, e) => UpdateRelay();
            relayTimer.Start();
        }

        void Display(Settings v)
        {
            GamePath.Text = v.Game;
            HideName.IsChecked = v.HideName;
            DisplayName.Text = v.Name;
            MainInstance.IsChecked = !v.ExtraInstance;
            ExtraInstance.IsChecked = v.ExtraInstance;
            InstanceNumber.SelectedIndex = Math.Max(0, Math.Min(Launcher.MaxInstances - 1, v.Instance));
            MultiInstance.IsChecked = v.MultiInstance;
            ServerAddress.Text = v.Server;
            SessionPassword.Text = v.Password;
            DisableEos.IsChecked = v.DisableEos;
            MaxPlayers.SelectedItem = v.MaxPlayers.ToString();
            CloseAfterLaunch.IsChecked = v.CloseAfterLaunch;
            UseVpn.IsChecked = v.Vpn;
            UpdateGame();
        }

        void Read()
        {
            s.Game = GamePath.Text;
            s.HideName = HideName.IsChecked == true;
            s.Name = DisplayName.Text.Trim();
            s.ExtraInstance = ExtraInstance.IsChecked == true;
            s.Instance = InstanceNumber.SelectedIndex;  // 0 = auto
            s.MultiInstance = MultiInstance.IsChecked == true;
            s.Vpn = UseVpn.IsChecked == true;
            s.Server = s.Vpn ? "" : ServerAddress.Text.Trim();
            s.Internet = s.Server.Length > 0;  // server filled in: relay; empty: local network / VPN
            s.Password = s.Vpn ? "" : SessionPassword.Text;
            s.DisableEos = DisableEos.IsChecked == true;
            if (MaxPlayers.SelectedItem is string max)
                s.MaxPlayers = int.Parse(max);
            s.CloseAfterLaunch = CloseAfterLaunch.IsChecked == true;
        }

        void SetStatus(string text, string brush)
        {
            Status.Text = text;
            Status.Foreground = (Brush)FindResource(brush);
            Status.Visibility = text.Length > 0 ? Visibility.Visible : Visibility.Collapsed;
        }

        void UpdateGame()
        {
            string version = Launcher.GameVersion(GamePath.Text, out bool supported);
            if (supported)
            {
                bool steam = Launcher.SteamRunning();
                GameState.Text = "Version 1.55 ✓" + (steam ? "" : "   —   Steam is not running");
                GameState.Foreground = (Brush)FindResource(steam ? "Ok" : "Accent");
            }
            else
            {
                GameState.Text = version.Length == 0
                    ? "Game not found: please select DyingLightGame.exe"
                    : "Version " + version + ": the mod only works with version 1.55 (it will not change anything)";
                GameState.Foreground = (Brush)FindResource("Erreur");
            }
            UpdateInstall();
        }

        void UpdateInstall()
        {
            if (InstallState == null)
                return;
            s.Game = GamePath.Text;
            bool game = File.Exists(s.Game);
            bool installed = game && Launcher.IsInstalled(s);
            bool present = game && Launcher.IsPresent(s);
            InstallState.Text = !game ? "" : installed ? "Mod installed ✓"
                              : present ? "Update available (Install or Launch)" : "Mod not installed";
            InstallState.Foreground = (Brush)FindResource(installed ? "Ok" : present ? "Accent" : "Pale");
            InstallButton.IsEnabled = game;
            UninstallButton.IsEnabled = present;
        }

        void InstallMod(object sender, RoutedEventArgs e)
        {
            Read();
            try
            {
                Launcher.Install(s);
                SetStatus("Mod installed in the game folder.", "Ok");
            }
            catch (Exception ex)
            {
                SetStatus(ex.Message, "Erreur");
            }
            UpdateInstall();
        }

        void UninstallMod(object sender, RoutedEventArgs e)
        {
            Read();
            try
            {
                Launcher.Uninstall(s);
                SetStatus("Mod removed from the game folder.", "Ok");
            }
            catch (Exception ex)
            {
                SetStatus(ex.Message, "Erreur");
            }
            UpdateInstall();
        }

        void UpdateState(object sender, RoutedEventArgs e)
        {
            if (DisplayName == null || InstanceNumber == null || ServerAddress == null)
                return;  // during InitializeComponent
            bool hide = HideName.IsChecked == true;
            DisplayName.IsEnabled = RandomButton.IsEnabled = hide;
            if (hide && string.IsNullOrWhiteSpace(DisplayName.Text))
                DisplayName.Text = Launcher.RandomName();
            bool extra = ExtraInstance.IsChecked == true;
            bool internet = UseVpn.IsChecked != true && ServerAddress.Text.Trim().Length > 0;
            InstanceNumber.IsEnabled = extra;
            if (extra && !internet)
                MultiInstance.IsChecked = true;  // a 2nd instance on this PC requires the local addresses
            MultiInstance.IsEnabled = !internet;  // relay: local addresses always used
            // ServerAddress and SessionPassword stay enabled outside VPN mode: disabling them while the address is empty
            // made the field impossible to fill in (package without server.txt, 1.3.7). Without a server the password is
            // ignored. Only the VPN checkbox greys them out (UseVpnChanged).
            UpdateRelay();
        }

        // Values cleared by the VPN checkbox, given back if it is unticked in the same session (never saved).
        string serverBeforeVpn = "", passwordBeforeVpn = "";

        /// <summary>VPN / local network: server and password cleared and greyed out, the game keeps its usual
        /// addresses (no relay, no 127.0.0.N address); only the port 0 fix stays active.</summary>
        void UseVpnChanged(object sender, RoutedEventArgs e)
        {
            if (ServerAddress == null || SessionPassword == null)
                return;  // during InitializeComponent
            bool vpn = UseVpn.IsChecked == true;
            if (vpn)
            {
                if (ServerAddress.Text.Length > 0 || SessionPassword.Text.Length > 0)
                {
                    serverBeforeVpn = ServerAddress.Text;
                    passwordBeforeVpn = SessionPassword.Text;
                }
                ServerAddress.Text = "";
                SessionPassword.Text = "";
            }
            else if (ServerAddress.Text.Length == 0 && SessionPassword.Text.Length == 0)
            {
                ServerAddress.Text = serverBeforeVpn;
                SessionPassword.Text = passwordBeforeVpn;
            }
            ServerAddress.IsEnabled = SessionPassword.IsEnabled = !vpn;
            UpdateState(null, null);
        }

        void UpdateRelay()
        {
            if (RelayState == null)
                return;
            var relay = App.Relay;
            if (relay != null)
            {
                RelayState.Text = relay.Describe();
                RelayState.Foreground = (Brush)FindResource(relay.Connected ? "Ok" : "Accent");
                // Relay: the window minimized after launch closes itself when the last game is closed.
                if (minimizedAfterLaunch && relay.GamesRunning == 0)
                {
                    minimizedAfterLaunch = false;
                    Close();
                }
            }
            else
            {
                RelayState.Text = "";
            }
            // Empty line (no relay): hidden, otherwise it leaves a blank at the bottom of the section.
            RelayState.Visibility = RelayState.Text.Length > 0 ? Visibility.Visible : Visibility.Collapsed;
        }

        void RandomName(object sender, RoutedEventArgs e) => DisplayName.Text = Launcher.RandomName();

        void Browse(object sender, RoutedEventArgs e)
        {
            var dlg = new OpenFileDialog
            {
                Title = "Select DyingLightGame.exe",
                Filter = "Dying Light|DyingLightGame.exe|Programs|*.exe",
                FileName = "DyingLightGame.exe",
            };
            if (File.Exists(GamePath.Text))
                dlg.InitialDirectory = Path.GetDirectoryName(GamePath.Text);
            if (dlg.ShowDialog(this) == true)
            {
                GamePath.Text = dlg.FileName;
                s.Game = dlg.FileName;
                s.Save();  // manually chosen path: saved right away
                UpdateGame();
            }
        }

        async void Launch(object sender, RoutedEventArgs e)
        {
            Read();
            s.Save();
            LaunchButton.IsEnabled = false;
            SetStatus("Launching…", "Pale");
            try
            {
                bool forwarded = false;  // game launched by the other DLCoop on this PC (the relay agent)
                int pid;
                if (s.Internet)
                {
                    SetStatus("Connecting to the server…", "Pale");
                    var r = await Task.Run(() =>
                    {
                        int p = App.LaunchWithRelay(s, out _, out bool f);
                        return (Pid: p, Forwarded: f);
                    });
                    pid = r.Pid;
                    forwarded = r.Forwarded;
                }
                else
                {
                    pid = Launcher.Launch(s, out _, out _);
                }
                SetStatus("Launching…", "Pale");
                bool ok = await Task.Run(() => Launcher.WaitForMod(pid));
                if (ok)
                {
                    SetStatus(forwarded ? "Game launched (through the DLCoop already connected to the relay)."
                                        : "Game launched.", "Ok");
                    if (s.CloseAfterLaunch)
                    {
                        await Task.Delay(1200);
                        if (s.Internet && !forwarded)
                        {
                            // The relay agent must stay alive during the game.
                            minimizedAfterLaunch = true;
                            WindowState = WindowState.Minimized;
                        }
                        else
                        {
                            Close();
                        }
                    }
                }
                else
                {
                    SetStatus("The mod was not detected in the game (see the logs folder).", "Erreur");
                }
            }
            catch (Exception ex)
            {
                SetStatus(ex.Message, "Erreur");
            }
            finally
            {
                LaunchButton.IsEnabled = true;
                UpdateRelay();
            }
        }

        protected override void OnClosing(CancelEventArgs e)
        {
            var relay = App.Relay;
            if (relay != null && relay.GamesRunning > 0 &&
                MessageBox.Show(this, "Closing DLCoop disconnects your game from the relay (the other players will " +
                                      "no longer see you). Close anyway?", "DLCoop", MessageBoxButton.YesNo,
                                MessageBoxImage.Warning) != MessageBoxResult.Yes)
            {
                e.Cancel = true;
                return;
            }
            base.OnClosing(e);
        }

        protected override void OnClosed(EventArgs e)
        {
            relayTimer.Stop();
            App.DisconnectRelay();
            base.OnClosed(e);
        }
    }
}
