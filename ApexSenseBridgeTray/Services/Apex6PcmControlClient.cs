using ApexSenseBridgeTray.Models;
using System;
using System.Diagnostics;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace ApexSenseBridgeTray.Services
{
    internal sealed class PcmEditDebouncer
    {
        private readonly Action commit;
        private readonly Stopwatch clock = Stopwatch.StartNew();
        private long due;
        private bool pending;
        internal PcmEditDebouncer(Action commit) { this.commit = commit; }
        internal void Schedule() { pending = true; due = clock.ElapsedMilliseconds + 150; }
        internal void Tick() { if (pending && clock.ElapsedMilliseconds >= due) Flush(); }
        internal void Flush() { if (!pending) return; pending = false; commit(); }
    }

    internal sealed class Apex6PcmControlClient
    {
        internal const int BlockSize = 308;
        internal const uint Magic = 0x43365041;
        private readonly SemaphoreSlim gate = new SemaphoreSlim(1, 1);

        internal static string NormalizeGameKey(string key)
        {
            key = (key ?? string.Empty).Trim().ToLowerInvariant();
            if (key.IndexOf('\0') >= 0 || Encoding.UTF8.GetByteCount(key) > 255)
                throw new ArgumentException("PCM game key exceeds the protocol limit.");
            return key;
        }

        internal static byte[] CreateRequest(string gameKey, Apex6PcmGameSettings policy)
        {
            var bytes = new byte[BlockSize];
            var key = Encoding.UTF8.GetBytes(NormalizeGameKey(gameKey));
            Array.Copy(key, 0, bytes, 20, key.Length);
            string mode = policy.Mode;
            using (var writer = new BinaryWriter(new MemoryStream(bytes)))
            {
                writer.Write(Magic); writer.Write(1u);
                writer.BaseStream.Position = 276;
                writer.Write(mode == "linear" ? 2u : mode == "companding" ? 1u : 0u);
                writer.Write((uint)policy.LinearGainPercent);
                writer.Write((uint)Math.Round(policy.MeasuredReferencePercent));
                writer.Write((uint)policy.MeasuredReferencePeakAbs16);
                writer.Write((uint)policy.TargetPeakPercent);
                writer.Write((uint)policy.MaxGainPercent);
                writer.Write(100u);
                writer.Write((uint)policy.CompandingGainPercent);
            }
            return bytes;
        }

        internal async Task<string> ApplyAsync(string token, byte[] request, int timeoutMs = 800)
        {
            // Never queue unbounded requests behind a slow or unavailable engine.
            if (!await gate.WaitAsync(0).ConfigureAwait(false)) return "Pending";
            try
            {
                return await Task.Run(async () =>
                {
                    string name = @"Local\ApexSenseBridge.Session." + token + ".Apex6PcmControl";
                    try
                    {
                        using (var mapping = MemoryMappedFile.OpenExisting(name))
                        using (var mutex = Mutex.OpenExisting(name + ".Mutex"))
                        using (var view = mapping.CreateViewAccessor(0, BlockSize))
                        {
                            uint revision = 0;
                            bool published = false;
                            var clock = Stopwatch.StartNew();
                            while (clock.ElapsedMilliseconds < timeoutMs)
                            {
                                bool owned = false;
                                try
                                {
                                    try { owned = mutex.WaitOne(0); }
                                    catch (AbandonedMutexException) { owned = true; return "Error: abandoned control mutex"; }
                                    if (owned)
                                    {
                                        if (view.ReadUInt32(0) != Magic || view.ReadUInt32(4) != 1) return "Error: InvalidProtocol";
                                        if (view.ReadUInt32(16) == 6) return "Disconnected";
                                        if (!published)
                                        {
                                            var existingKey = new byte[256];
                                            view.ReadArray(20, existingKey, 0, 256);
                                            int end = Array.IndexOf(existingKey, (byte)0);
                                            if (end < 0) return "Error: InvalidProtocol";
                                            string serverKey = Encoding.UTF8.GetString(existingKey, 0, end);
                                            int requestedEnd = Array.IndexOf(request, (byte)0, 20, 256);
                                            string requestedKey = Encoding.UTF8.GetString(request, 20, requestedEnd - 20);
                                            if (serverKey != requestedKey) return "Error: WrongGame";
                                            uint last = Math.Max(view.ReadUInt32(8), view.ReadUInt32(12));
                                            if (last == uint.MaxValue) return "Error: revision exhausted";
                                            revision = last + 1;
                                            // Preserve ACK fields; only the engine may publish application status.
                                            view.WriteArray(20, request, 20, BlockSize - 20);
                                            view.Write(16, 0u);
                                            view.Write(8, revision);
                                            published = true;
                                        }
                                        else
                                        {
                                            uint status = view.ReadUInt32(16);
                                            if (status == 1 && view.ReadUInt32(12) == revision) return "Applied";
                                            if (status == 6) return "Disconnected";
                                            if (status >= 2) return "Error: engine status " + status;
                                        }
                                    }
                                }
                                finally { if (owned) mutex.ReleaseMutex(); }
                                await Task.Delay(20).ConfigureAwait(false);
                            }
                            return "Disconnected: acknowledgement timeout";
                        }
                    }
                    catch (FileNotFoundException) { return "Disconnected"; }
                    catch (WaitHandleCannotBeOpenedException) { return "Disconnected"; }
                    catch (Exception ex) { return "Error: " + ex.Message; }
                }).ConfigureAwait(false);
            }
            finally { gate.Release(); }
        }
    }
}
