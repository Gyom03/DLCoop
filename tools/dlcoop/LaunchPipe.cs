// Lancement transmis à l'agent du relais déjà ouvert sur ce PC.
// Un seul DLCoop par PC peut être l'agent du relais (il tient les adresses 127.0.0.N des joueurs distants, verrou
// Local\DLCoop_relais). Mais l'utilisateur peut rouvrir DLCoop (raccourci) pendant une partie, ou l'agent peut tourner
// sans fenêtre (DLCoop.exe /launch) : le 2e DLCoop envoie alors ses réglages par un tube nommé local, et l'agent lance
// lui-même le jeu (adresse demandée au relais, jeu suivi). Réponse : « OK <pid> <instance> » ou « ERR <message> ».
// Tube local à la session de l'utilisateur ; ACL par défaut (écriture : l'utilisateur et les administrateurs).
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

        /// <summary>Agent du relais : accepte les lancements des autres DLCoop de ce PC.</summary>
        public static void Serve()
        {
            if (server != null)
                return;
            stopping = false;
            server = new Thread(Loop) { IsBackground = true, Name = "tube de lancement" };
            server.Start();
        }

        public static void Stop()
        {
            if (server == null)
                return;
            stopping = true;
            try
            {
                // Débloque WaitForConnection.
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
                    Thread.Sleep(200);  // client parti en cours de route : on attend le suivant
                }
            }
        }

        /// <summary>
        /// Demande à l'agent du relais de ce PC de lancer le jeu avec ces réglages ; renvoie le pid du jeu.
        /// Lève une exception au message lisible.
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
