// Agent du relais Internet (serveur : tools/relais/relais.cpp, protocole décrit dans tools/relais/README.md).
//
// Même rôle que tools/fantome/src/pont.cpp, mais vers un serveur public au lieu d'un 2e PC du réseau local :
//   - le relais attribue à chaque instance du jeu lancée ici une adresse 0..32 ; le jeu parle sur 127.0.0.(N+1)
//     (dlcoop_reseau.dll, DLF_ADRESSE) ;
//   - pour chaque adresse active AILLEURS dans la session, l'agent tient le verrou Local\DLFantome_slot_N (le jeu
//     croit l'instance présente et lui envoie ses annonces) et occupe 127.0.0.(N+1):20101/20111 ; ce que le jeu y
//     envoie part au relais, et ce qui arrive du relais est renvoyé au jeu depuis l'adresse de l'émetteur distant.
// Le jeu ne voit que des adresses locales : aucun port à ouvrir, ni chez les joueurs ni chez l'hôte.
// Protocole v2 : 33 adresses (32 joueurs + 1 zombie), masques de 64 bits dans la charge de ROSTER ; un relais
// ancien (v1, 16 adresses) refuse la version 2 : l'agent se replie alors sur la v1.
using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Net;
using System.Net.Sockets;
using System.Text;
using System.Threading;

namespace DLCoop
{
    public sealed class Relay : IDisposable
    {
        public const int DefaultPort = 20300;
        const uint Magic = 0x31524C44;  // « DLR1 »
        const byte VersionMax = 2;
        const int SlotsV1 = 16, SlotsV2 = Launcher.MaxInstances;  // adresses : v1 0..15, v2 0..32
        const int RosterV2Size = 16;  // charge de ROSTER v2 (et de KEEPALIVE v2) : masques actifs et propres, 64 bits
        const byte NoSlot = 0xFF;
        const int GamePort = 20101, ScanPort = 20111;
        const int HeaderSize = 20;
        const int SioUdpConnReset = unchecked((int)0x9800000C);

        enum T : byte { Join = 1, Welcome, Refused, Claim, Slot, Release, Keepalive, Roster, Data, Bye }

        sealed class Remote
        {
            public Mutex Lock;
            public readonly Dictionary<int, Socket> Sockets = new Dictionary<int, Socket>();  // port -> socket
        }

        sealed class Game
        {
            public int Pid, Slot;
        }

        public string Address { get; }
        public string Password { get; }

        readonly IPEndPoint server;
        readonly Socket tunnel;
        readonly Mutex agentLock;
        readonly object gate = new object();
        readonly Dictionary<int, Remote> remotes = new Dictionary<int, Remote>();
        readonly List<Game> games = new List<Game>();
        readonly Dictionary<byte, int> claims = new Dictionary<byte, int>();  // requête -> adresse reçue
        readonly ManualResetEvent joined = new ManualResetEvent(false);
        readonly Random rnd = new Random();
        ulong nonce, token;
        byte version = VersionMax;  // version du protocole parlée avec ce relais
        int refused;           // raison du refus du relais (0 = aucun)
        ulong ownSlots;        // adresses des instances lancées ici
        ulong activeSlots;     // adresses actives de la session (dernier ROSTER)
        byte nextRequest;
        long lastFromServer;   // Environment.TickCount64 n'existe pas en .NET Framework : Stopwatch
        readonly Stopwatch clock = Stopwatch.StartNew();
        volatile bool disposed;
        long packetsUp, packetsDown;

        public event Action Changed;  // état modifié (connexion, joueurs, jeux)

        int Slots => version >= 2 ? SlotsV2 : SlotsV1;

        Relay(string address, string password, IPEndPoint endpoint, Mutex agentLock)
        {
            Address = address;
            Password = password;
            server = endpoint;
            this.agentLock = agentLock;
            tunnel = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
            tunnel.Bind(new IPEndPoint(IPAddress.Any, 0));
            NoConnReset(tunnel);
            tunnel.ReceiveBufferSize = 1 << 20;
        }

        /// <summary>Connexion au relais (bloquant, 6 s au plus). Lève une exception au message lisible.</summary>
        public static Relay Connect(string address, string password)
        {
            if (string.IsNullOrWhiteSpace(address))
                throw new InvalidOperationException("Enter the relay server address.");
            if (string.IsNullOrEmpty(password))
                throw new InvalidOperationException("Enter the session password (the same for every player).");
            var endpoint = Resolve(address.Trim());
            var agentLock = new Mutex(false, @"Local\DLCoop_relais", out bool created);
            if (!created)
            {
                agentLock.Dispose();
                throw new InvalidOperationException(
                    "Another DLCoop window is already connected to a relay: use it to start more instances.");
            }
            Relay r = null;
            try
            {
                r = new Relay(address.Trim(), password, endpoint, agentLock);
                r.Start();
                return r;
            }
            catch
            {
                if (r != null)
                    r.Dispose();
                else
                    agentLock.Dispose();
                throw;
            }
        }

        static IPEndPoint Resolve(string address)
        {
            string host = address;
            int port = DefaultPort;
            int colon = address.LastIndexOf(':');
            if (colon > 0)
            {
                if (!int.TryParse(address.Substring(colon + 1), out port) || port <= 0 || port > 65535)
                    throw new InvalidOperationException("Invalid relay port in \"" + address + "\".");
                host = address.Substring(0, colon);
            }
            IPAddress ip;
            if (!IPAddress.TryParse(host, out ip))
            {
                try
                {
                    ip = Dns.GetHostAddresses(host).FirstOrDefault(a => a.AddressFamily == AddressFamily.InterNetwork);
                }
                catch (Exception)
                {
                    ip = null;
                }
                if (ip == null)
                    throw new InvalidOperationException("Relay server \"" + host + "\" not found.");
            }
            return new IPEndPoint(ip, port);
        }

        static void NoConnReset(Socket s)
        {
            // Pas d'exception à la réception après un ICMP « port inaccessible » (instance fermée…).
            try { s.IOControl(SioUdpConnReset, new byte[4], null); } catch (Exception) { }
        }

        void Start()
        {
            new Thread(ReceiveTunnel) { IsBackground = true, Name = "relais" }.Start();
            bool ok = JoinBlocking(6000);
            if (!ok && refused == 3 && version > 1)
            {
                version = 1;  // relais ancien : protocole v1, 16 adresses
                ok = JoinBlocking(6000);
            }
            if (!ok)
                throw new InvalidOperationException(refused == 1 ? "The relay refused the session password."
                                                  : refused == 2 ? "The relay is full."
                                                  : refused == 3 ? "The relay runs another version of DLCoop."
                                                  : "No answer from the relay server " + Address +
                                                    " (address, port or firewall?).");
            new Thread(KeepAlive) { IsBackground = true, Name = "relais maintien" }.Start();
        }

        bool JoinBlocking(int timeoutMs)
        {
            joined.Reset();
            refused = 0;
            nonce = (ulong)rnd.Next() << 32 | (uint)rnd.Next();
            var end = clock.ElapsedMilliseconds + timeoutMs;
            while (clock.ElapsedMilliseconds < end)
            {
                SendJoin();
                if (joined.WaitOne(500))
                    return refused == 0;
            }
            return false;
        }

        void SendJoin()
        {
            byte[] pw = Encoding.UTF8.GetBytes(Password);
            var b = new byte[HeaderSize + Math.Min(pw.Length, 64)];
            Header(b, T.Join, 0, 0, version, 0, 0, nonce);
            Buffer.BlockCopy(pw, 0, b, HeaderSize, b.Length - HeaderSize);
            SendToServer(b, b.Length);
        }

        // ------------------------------------------------------------------ état pour la fenêtre
        public bool Connected => token != 0 && clock.ElapsedMilliseconds - Interlocked.Read(ref lastFromServer) < 10000;

        public int PlayersInSession
        {
            get { lock (gate) return Bits(activeSlots); }
        }

        public int GamesRunning
        {
            get { lock (gate) return games.Count; }
        }

        public string Describe()
        {
            if (!Connected)
                return "Relay " + Address + ": connection lost, reconnecting…";
            int players = PlayersInSession;
            return "Relay " + Address + ": connected, " + players + " game" + (players > 1 ? "s" : "") +
                   " in the session (" + GamesRunning + " on this PC).";
        }

        static int Bits(ulong m)
        {
            int n = 0;
            for (; m != 0; m &= m - 1)
                ++n;
            return n;
        }

        // ------------------------------------------------------------------ adresses des instances locales
        /// <summary>Demande une adresse au relais (bloquant, 5 s au plus).</summary>
        public int Claim()
        {
            byte req;
            lock (gate)
            {
                if (++nextRequest == 0)
                    ++nextRequest;  // 0 : réservé aux reprises automatiques
                req = nextRequest;
                claims.Remove(req);
            }
            var end = clock.ElapsedMilliseconds + 5000;
            while (clock.ElapsedMilliseconds < end)
            {
                SendHeader(T.Claim, NoSlot, 0, req);
                var until = clock.ElapsedMilliseconds + 500;
                lock (gate)
                {
                    while (!claims.ContainsKey(req) && clock.ElapsedMilliseconds < until)
                        Monitor.Wait(gate, 100);
                    if (claims.TryGetValue(req, out int slot))
                    {
                        claims.Remove(req);
                        if (slot == NoSlot)
                            throw new InvalidOperationException("The session is full (" + Slots + " games).");
                        return slot;
                    }
                }
            }
            throw new InvalidOperationException("The relay did not answer (no address for this game).");
        }

        /// <summary>Suit le jeu lancé : son adresse est rendue au relais quand il se ferme.</summary>
        public void Track(int pid, int slot)
        {
            lock (gate)
                games.Add(new Game { Pid = pid, Slot = slot });
            new Thread(() =>
            {
                try
                {
                    using (var p = Process.GetProcessById(pid))
                        p.WaitForExit();
                }
                catch (Exception) { /* déjà terminé */ }
                lock (gate)
                    games.RemoveAll(g => g.Pid == pid);
                Release(slot);
            }) { IsBackground = true, Name = "suivi du jeu " + pid }.Start();
            Changed?.Invoke();
        }

        public void Release(int slot)
        {
            lock (gate)
                ownSlots &= ~(1UL << slot);
            SendHeader(T.Release, (byte)slot, 0, 0);
            Changed?.Invoke();
        }

        /// <summary>Attend que tous les jeux suivis soient fermés.</summary>
        public void WaitForGames()
        {
            while (!disposed)
            {
                lock (gate)
                    if (games.Count == 0)
                        return;
                Thread.Sleep(500);
            }
        }

        // ------------------------------------------------------------------ réception du relais
        void ReceiveTunnel()
        {
            var buf = new byte[65536];
            EndPoint from = new IPEndPoint(IPAddress.Any, 0);
            while (!disposed)
            {
                int n;
                try
                {
                    n = tunnel.ReceiveFrom(buf, ref from);
                }
                catch (SocketException)
                {
                    continue;
                }
                catch (ObjectDisposedException)
                {
                    return;
                }
                var ep = (IPEndPoint)from;
                if (n < HeaderSize || !ep.Address.Equals(server.Address) || ep.Port != server.Port ||
                    BitConverter.ToUInt32(buf, 0) != Magic)
                    continue;
                var type = (T)buf[4];
                byte src = buf[5], dst = buf[6], aux = buf[7];
                ushort srcPort = BitConverter.ToUInt16(buf, 8), dstPort = BitConverter.ToUInt16(buf, 10);
                ulong tok = BitConverter.ToUInt64(buf, 12);
                if (type == T.Refused)
                {
                    if (tok == nonce)
                    {
                        refused = aux;
                        joined.Set();
                    }
                    continue;
                }
                if (type == T.Welcome)
                {
                    if (!joined.WaitOne(0) && tok != 0)
                    {
                        bool rejoin = token != 0;
                        token = tok;
                        Interlocked.Exchange(ref lastFromServer, clock.ElapsedMilliseconds);
                        joined.Set();
                        if (rejoin)
                            Reclaim();
                        Changed?.Invoke();
                    }
                    continue;
                }
                if (tok != token || token == 0)
                    continue;
                Interlocked.Exchange(ref lastFromServer, clock.ElapsedMilliseconds);
                switch (type)
                {
                case T.Data:
                    Deliver(buf, n, src, dst, srcPort, dstPort);
                    break;
                case T.Slot:
                    OnSlot(src, aux);
                    break;
                case T.Roster:
                    if (version >= 2 && n >= HeaderSize + RosterV2Size)
                        OnRoster(BitConverter.ToUInt64(buf, HeaderSize), BitConverter.ToUInt64(buf, HeaderSize + 8));
                    else
                        OnRoster(srcPort, dstPort);
                    break;
                }
            }
        }

        void OnSlot(byte slot, byte req)
        {
            lock (gate)
            {
                if (slot != NoSlot)
                {
                    ownSlots |= 1UL << slot;
                    DropRemote(slot);  // l'adresse est désormais celle d'un jeu lancé ici
                }
                if (req != 0)
                {
                    claims[req] = slot;
                    Monitor.PulseAll(gate);
                }
            }
        }

        void OnRoster(ulong active, ulong serverOwn)
        {
            bool changed;
            lock (gate)
            {
                changed = active != activeSlots;
                activeSlots = active;
                for (int i = 0; i < Slots; ++i)
                {
                    ulong bit = 1UL << i;
                    bool mine = (ownSlots & bit) != 0, serverMine = (serverOwn & bit) != 0;
                    if (serverMine && !mine)
                        SendHeader(T.Release, (byte)i, 0, 0);       // RELEASE perdu : on le répète
                    else if (mine && !serverMine)
                        SendHeader(T.Claim, (byte)i, 0, 0);         // relais redémarré : on reprend l'adresse
                    bool remote = (active & bit) != 0 && !mine && !serverMine;
                    if (remote)
                        EnsureRemote(i);
                    else if (!mine)
                        DropRemote(i);
                }
            }
            if (changed)
                Changed?.Invoke();
        }

        void Reclaim()
        {
            ulong mine;
            lock (gate)
                mine = ownSlots;
            for (int i = 0; i < Slots; ++i)
                if ((mine & (1UL << i)) != 0)
                    SendHeader(T.Claim, (byte)i, 0, 0);
        }

        // Paquet d'un jeu distant -> jeu local, depuis l'adresse de l'émetteur (127.0.0.(src+1):srcPort).
        void Deliver(byte[] buf, int n, byte src, byte dst, ushort srcPort, ushort dstPort)
        {
            if (src >= Slots || dst >= Slots)
                return;
            Socket s;
            lock (gate)
            {
                if ((ownSlots & (1UL << dst)) == 0 || (ownSlots & (1UL << src)) != 0)
                    return;
                var r = EnsureRemote(src);
                if (r == null)
                    return;
                if (!r.Sockets.TryGetValue(srcPort, out s))
                {
                    s = OpenLocal(src, srcPort);  // port source inhabituel : adresse ouverte à la demande
                    if (s == null)
                        return;
                    r.Sockets[srcPort] = s;
                }
            }
            try
            {
                s.SendTo(buf, HeaderSize, n - HeaderSize, SocketFlags.None,
                         new IPEndPoint(Loopback(dst), dstPort));
                Interlocked.Increment(ref packetsDown);
            }
            catch (Exception) { /* jeu local fermé */ }
        }

        static IPAddress Loopback(int slot) => new IPAddress(new byte[] { 127, 0, 0, (byte)(slot + 1) });

        Remote EnsureRemote(int slot)
        {
            if (remotes.TryGetValue(slot, out Remote r))
                return r;
            r = new Remote { Lock = new Mutex(false, @"Local\DLFantome_slot_" + slot) };
            foreach (int port in new[] { GamePort, ScanPort })
            {
                var s = OpenLocal(slot, port);
                if (s != null)
                    r.Sockets[port] = s;
            }
            remotes[slot] = r;
            return r;
        }

        void DropRemote(int slot)
        {
            if (!remotes.TryGetValue(slot, out Remote r))
                return;
            remotes.Remove(slot);
            foreach (var s in r.Sockets.Values)
                s.Close();
            r.Lock.Dispose();
        }

        // Adresse 127.0.0.(slot+1):port occupée au nom d'un jeu distant ; ce que le jeu local y envoie part au relais.
        Socket OpenLocal(int slot, int port)
        {
            var s = new Socket(AddressFamily.InterNetwork, SocketType.Dgram, ProtocolType.Udp);
            try
            {
                s.Bind(new IPEndPoint(Loopback(slot), port));
            }
            catch (SocketException)
            {
                s.Close();
                return null;  // adresse déjà prise sur ce PC (pont.exe, autre lanceur…)
            }
            NoConnReset(s);
            s.ReceiveBufferSize = 1 << 20;
            int dst = slot, localPort = port;
            new Thread(() => ForwardFromGame(s, dst, localPort)) { IsBackground = true, Name = "relais " + slot + ":" + port }
                .Start();
            return s;
        }

        void ForwardFromGame(Socket s, int dst, int dstPort)
        {
            var buf = new byte[65536];
            EndPoint from = new IPEndPoint(IPAddress.Any, 0);
            while (!disposed)
            {
                int n;
                try
                {
                    n = s.ReceiveFrom(buf, HeaderSize, buf.Length - HeaderSize, SocketFlags.None, ref from);
                }
                catch (SocketException e) when (e.SocketErrorCode != SocketError.NotSocket &&
                                                e.SocketErrorCode != SocketError.Interrupted &&
                                                e.SocketErrorCode != SocketError.OperationAborted)
                {
                    continue;
                }
                catch (Exception)
                {
                    return;  // socket fermé
                }
                var ep = (IPEndPoint)from;
                byte[] a = ep.Address.GetAddressBytes();
                if (a.Length != 4 || a[0] != 127 || a[1] != 0 || a[2] != 0 || a[3] < 1 || a[3] > Slots)
                    continue;
                int src = a[3] - 1;
                if ((ownSlots & (1UL << src)) == 0)
                    continue;  // seuls les jeux lancés ici passent par le relais
                Header(buf, T.Data, (byte)src, (byte)dst, 0, (ushort)ep.Port, (ushort)dstPort, token);
                SendToServer(buf, n + HeaderSize);
                Interlocked.Increment(ref packetsUp);
            }
        }

        // ------------------------------------------------------------------ maintien et reconnexion
        void KeepAlive()
        {
            while (!disposed)
            {
                Thread.Sleep(2000);
                if (disposed)
                    return;
                if (clock.ElapsedMilliseconds - Interlocked.Read(ref lastFromServer) > 10000)
                {
                    // Relais redémarré ou connexion coupée : nouvelle session, les adresses sont redemandées.
                    Changed?.Invoke();
                    JoinBlocking(4000);
                    continue;
                }
                if (version >= 2)
                {
                    // Charge vide de la taille de la réponse ROSTER v2 : le relais ne répond jamais plus gros.
                    var b = new byte[HeaderSize + RosterV2Size];
                    Header(b, T.Keepalive, 0, 0, 0, 0, 0, token);
                    SendToServer(b, b.Length);
                }
                else
                {
                    SendHeader(T.Keepalive, 0, 0, 0);
                }
            }
        }

        // ------------------------------------------------------------------ envoi
        static void Header(byte[] b, T type, byte src, byte dst, byte aux, ushort srcPort, ushort dstPort, ulong tok)
        {
            b[0] = 0x44; b[1] = 0x4C; b[2] = 0x52; b[3] = 0x31;  // Magic, petit-boutiste
            b[4] = (byte)type; b[5] = src; b[6] = dst; b[7] = aux;
            b[8] = (byte)srcPort; b[9] = (byte)(srcPort >> 8);
            b[10] = (byte)dstPort; b[11] = (byte)(dstPort >> 8);
            for (int i = 0; i < 8; ++i)
                b[12 + i] = (byte)(tok >> (8 * i));
        }

        void SendHeader(T type, byte src, byte dst, byte aux)
        {
            var b = new byte[HeaderSize];
            Header(b, type, src, dst, aux, 0, 0, token);
            SendToServer(b, b.Length);
        }

        void SendToServer(byte[] b, int n)
        {
            try { tunnel.SendTo(b, 0, n, SocketFlags.None, server); } catch (Exception) { /* réseau absent */ }
        }

        public void Dispose()
        {
            if (disposed)
                return;
            if (token != 0)
                for (int i = 0; i < 2; ++i)
                    SendHeader(T.Bye, 0, 0, 0);
            disposed = true;
            lock (gate)
                foreach (int slot in remotes.Keys.ToList())
                    DropRemote(slot);
            tunnel.Close();
            agentLock.Dispose();
        }
    }
}
