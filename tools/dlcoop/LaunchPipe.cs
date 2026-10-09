// A launch forwarded to the relay agent already open on this PC.
// Only one DLCoop per PC can be the relay agent (it holds the remote players' 127.0.0.N addresses, lock
// Local\DLCoop_relais). But the user may reopen DLCoop (shortcut) during a game, or the agent may run headless
// (DLCoop.exe /launch): the second DLCoop then sends its settings over a local named pipe, and the agent launches the
// game itself (address requested from the relay, game tracked). Reply: "OK <pid> <instance>" or "ERR <message>".
// A pipe local to the user's session; default ACL (write: the user and administrators).
using System;
using System.Collections.Generic;
using System.IO;
using System.IO.Pipes;
using System.Text;
using System.Threading;

namespace DLCoop
{
    public static class LaunchPipe
    {
        const string PipeName = "DLCoop_relais_lancement";
        static Thread server;
        static volatile bool stopping;

        /// <summary>The relay agent: accepts launches from the other DLCoop instances on this PC.</summary>
        public static void Serve()
        {
            if (server != null)
                return;
            stopping = false;
            server = new Thread(Loop) { IsBackground = true, Name = "launch pipe" };
            server.Start();
        }

        public static void Stop()
        {
            if (server == null)
                return;
            stopping = true;
            try
            {
                // Unblocks WaitForConnection.
                using (var c = new NamedPipeClientStream(".", PipeName, PipeDirection.InOut))
                    c.Connect(200);
            }
            catch (Exception) { }
            server = null;
        }

        static void Loop()
        {
            while (!stopping)
            {
                try
                {
                    using (var pipe = new NamedPipeServerStream(PipeName, PipeDirection.InOut, 1, PipeTransmissionMode.Byte))
                    {
                        pipe.WaitForConnection();
                        if (stopping)
                            return;
                        var reader = new StreamReader(pipe, Encoding.UTF8, false, 4096, true);
                        var writer = new StreamWriter(pipe, new UTF8Encoding(false), 4096, true) { AutoFlush = true };
                        var lines = new List<string>();
                        for (string l; (l = reader.ReadLine()) != null && l != "END";)
                            lines.Add(l);
                        string answer;
                        try
                        {
                            var s = Settings.FromLines(lines);
                            s.Internet = true;
                            int pid = App.LaunchInRelay(s, out int instance);
                            answer = "OK " + pid + " " + instance;
                        }
                        catch (Exception ex)
                        {
                            answer = "ERR " + ex.Message.Replace('\r', ' ').Replace('\n', ' ');
                        }
                        writer.WriteLine(answer);
                        pipe.WaitForPipeDrain();
                    }
                }
                catch (Exception)
                {
                    Thread.Sleep(200);  // the client left mid-way: wait for the next one
                }
            }
        }

        /// <summary>
        /// Asks this PC's relay agent to launch the game with these settings; returns the game's pid.
        /// Throws with a readable message.
        /// </summary>
        public static int Request(Settings s, out int instance)
        {
            instance = 0;
            using (var pipe = new NamedPipeClientStream(".", PipeName, PipeDirection.InOut))
            {
                try
                {
                    pipe.Connect(3000);
                }
                catch (TimeoutException)
                {
                    throw new InvalidOperationException("The DLCoop already connected to the relay does not answer " +
                                                        "(close its games, then try again).");
                }
                var writer = new StreamWriter(pipe, new UTF8Encoding(false), 4096, true) { AutoFlush = true };
                var reader = new StreamReader(pipe, Encoding.UTF8, false, 4096, true);
                foreach (string l in s.Lines())
                    writer.WriteLine(l);
                writer.WriteLine("END");
                string answer = reader.ReadLine() ?? "ERR No answer from the DLCoop connected to the relay.";
                if (answer.StartsWith("OK "))
                {
                    var p = answer.Split(' ');
                    instance = int.Parse(p[2]);
                    return int.Parse(p[1]);
                }
                throw new InvalidOperationException(answer.StartsWith("ERR ") ? answer.Substring(4) : answer);
            }
        }
    }
}
