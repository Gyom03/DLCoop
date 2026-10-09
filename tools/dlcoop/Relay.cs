// Internet relay agent (server: tools/relais/relais.cpp, protocol described in tools/relais/README.md).
//
// Same role as tools/fantome/src/pont.cpp, but toward a public server instead of a 2nd PC on the local network:
//   - the relay assigns each game instance launched here an address 0..32; the game talks on 127.0.0.(N+1)
//     (dlcoop_reseau.dll, DLF_ADRESSE);
//   - for each address active ELSEWHERE in the session, the agent holds the Local\DLFantome_slot_N lock (the game
//     believes the instance is present and sends it its announcements) and holds 127.0.0.(N+1):20101/20111; what the
//     game sends there goes to the relay, and what comes back from the relay is sent to the game from the remote
//     sender's address.
// The game only sees local addresses: no port to open, neither for the players nor for the host.
// Protocol v2: 33 addresses (32 players + 1 zombie), 64-bit masks in the ROSTER payload; an old relay (v1, 16
// addresses) refuses version 2: the agent then falls back to v1.
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
    /// <summary>Another DLCoop on this PC is already the relay agent.</summary>
    public sealed class RelayBusyException : InvalidOperationException
    {
        public RelayBusyException()
            : base("Another DLCoop is already connected to a relay on this PC.") { }
    }

    public sealed class Relay : IDisposable
    {
        public const int DefaultPort = 20300;
        const uint Magic = 0x31524C44;  // "DLR1"
        const byte VersionMax = 2;
        const int SlotsV1 = 16, SlotsV2 = Launcher.MaxInstances;  // addresses: v1 0..15, v2 0..32
        const int RosterV2Size = 16;  // v2 ROSTER (and v2 KEEPALIVE) payload: active and own masks, 64 bits
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
        readonly Dictionary<byte, int> claims = new Dictionary<byte, int>();  // request -> received address
        readonly ManualResetEvent joined = new ManualResetEvent(false);
        readonly Random rnd = new Random();
        ulong nonce, token;
        byte version = VersionMax;  // protocol version spoken with this relay
        int refused;           // relay refusal reason (0 = none)
        ulong ownSlots;        // addresses of the instances launched here
        ulong activeSlots;     // active addresses of the session (last ROSTER)
        byte nextRequest;
        long lastFromServer;   // Environment.TickCount64 does not exist in .NET Framework: Stopwatch
        readonly Stopwatch clock = Stopwatch.StartNew();
        volatile bool disposed;
        long packetsUp, packetsDown;

        public event Action Changed;  // state changed (connection, players, games)

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

        /// <summary>Connects to the relay (blocking, up to 6 s). Throws with a readable message.</summary>
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
                // Only one agent per PC (it holds the remote players' 127.0.0.N addresses): the caller then forwards
                // its launch to the already-open agent (LaunchPipe).
                agentLock.Dispose();
                throw new RelayBusyException();
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
            // No exception on receive after an ICMP "port unreachable" (closed instance).
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

        // ------------------------------------------------------------------ state for the window
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
                return "Relay " + Address + ": connection lost, reconnecting...";
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
                    ++nextRequest;  // 0: reserved for automatic retries
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

        /// <summary>Tracks the launched game: its address is returned to the relay when it closes.</summary>
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
                catch (Exception) { /* already ended */ }
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

        /// <summary>Waits for all tracked games to be closed.</summary>
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

        // ------------------------------------------------------------------ receiving from the relay
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
                    DropRemote(slot);  // the address now belongs to a game launched here
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
                        SendHeader(T.Release, (byte)i, 0, 0);       // lost RELEASE: repeat it
                    else if (mine && !serverMine)
                        SendHeader(T.Claim, (byte)i, 0, 0);         // relay restarted: reclaim the address
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

        // Packet from a remote game -> local game, from the sender's address (127.0.0.(src+1):srcPort).
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
                    s = OpenLocal(src, srcPort);  // unusual source port: address opened on demand
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
            catch (Exception) { /* local game closed */ }
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

        // Address 127.0.0.(slot+1):port held on behalf of a remote game; what the local game sends there goes to the relay.
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
                return null;  // address already taken on this PC (pont.exe, another launcher...)
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
                    return;  // socket closed
                }
                var ep = (IPEndPoint)from;
                byte[] a = ep.Address.GetAddressBytes();
                if (a.Length != 4 || a[0] != 127 || a[1] != 0 || a[2] != 0 || a[3] < 1 || a[3] > Slots)
                    continue;
                int src = a[3] - 1;
                if ((ownSlots & (1UL << src)) == 0)
                    continue;  // only games launched here go through the relay
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
                    // Relay restarted or connection lost: a new session, the addresses are requested again.
                    Changed?.Invoke();
                    JoinBlocking(4000);
                    continue;
                }
                if (version >= 2)
                {
                    // Empty payload of the v2 ROSTER reply size: the relay never replies larger.
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
            try { tunnel.SendTo(b, 0, n, SocketFlags.None, server); } catch (Exception) { /* network down */ }
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
