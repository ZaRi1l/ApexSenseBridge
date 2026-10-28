using System;
using System.Collections.Generic;
using System.IO;
using System.Web.Script.Serialization;

namespace ApexSenseBridgeTray.Models
{
    public sealed class ControllerCalibration
    {
        public int TriggerStrengthPercent { get; set; } = 100;
        public int VibrationStrengthPercent { get; set; } = 100;
        public int HapticThresholdPercent { get; set; } = 12;
        public bool EnableRumble { get; set; } = true;
        public bool SyncLightbar { get; set; }
        public int GyroStrengthPercent { get; set; } = 100;
        public int GyroYawStrengthPercent { get; set; } = 100;

        internal void Normalize(string model)
        {
            TriggerStrengthPercent = Math.Max(0, Math.Min(100, TriggerStrengthPercent));
            VibrationStrengthPercent = Math.Max(0, Math.Min(model == "apex6" ? 100 : 200, VibrationStrengthPercent));
            HapticThresholdPercent = model == "apex6" ? 0 : Math.Max(0, Math.Min(95, HapticThresholdPercent));
            SyncLightbar = model == "apex5" && SyncLightbar;
            GyroStrengthPercent = model == "apex4" ? Math.Max(25, Math.Min(400, GyroStrengthPercent)) : 100;
            GyroYawStrengthPercent = model == "apex4" ? Math.Max(25, Math.Min(400, GyroYawStrengthPercent)) : 100;
        }
    }

    public sealed class Apex6PcmGlobalSettings
    {
        public string Mode { get; set; }
        public int LinearGainPercent { get; set; }
        public int CompandingGainPercent { get; set; }
        public int TargetPeakPercent { get; set; }
        public int MaxGainPercent { get; set; }

        public Apex6PcmGlobalSettings()
        {
            Mode = "original";
            LinearGainPercent = 100;
            CompandingGainPercent = 100;
            TargetPeakPercent = 90;
            MaxGainPercent = 400;
        }

        public void Normalize()
        {
            if (string.IsNullOrWhiteSpace(Mode)) Mode = "original";
            Mode = Mode.Trim().ToLowerInvariant();
            if (Mode != "original" && Mode != "companding" && Mode != "linear")
                Mode = "original";
            LinearGainPercent = Math.Max(0, Math.Min(1000, LinearGainPercent));
            CompandingGainPercent = Math.Max(100, Math.Min(200, CompandingGainPercent));
            TargetPeakPercent = Math.Max(1, Math.Min(100, TargetPeakPercent));
            MaxGainPercent = Math.Max(100, Math.Min(400, MaxGainPercent));
        }
    }

    public sealed class Apex6PcmGameSettings
    {
        public bool UseGlobal { get; set; }
        public string Mode { get; set; }
        public int MeasuredReferencePeakAbs16 { get; set; }
        public double MeasuredReferencePercent { get; set; }
        public int TargetPeakPercent { get; set; }
        public int MaxGainPercent { get; set; }
        public int LinearGainPercent { get; set; }
        public int CompandingGainPercent { get; set; }
        // Existing profile JSON without this property must keep using global strength.
        public bool ApplyGlobalVibrationStrength { get; set; } = true;
        public string MeasuredUtc { get; set; }
        public int MeasurementVersion { get; set; }
        public int ObservedLeftPeakAbs16 { get; set; }
        public int ObservedRightPeakAbs16 { get; set; }
        public int ObservedActiveBlocks { get; set; }
        public string ObservedStatus { get; set; }

        public Apex6PcmGameSettings()
        {
            UseGlobal = false;
            Mode = "linear";
            TargetPeakPercent = 90;
            MaxGainPercent = 400;
            LinearGainPercent = 100;
            CompandingGainPercent = 100;
            MeasurementVersion = 1;
            ObservedStatus = "none";
        }

        public static bool TryParseManualReference(string text, string unit, out int peakAbs16)
        {
            peakAbs16 = 0;
            double value;
            if (!double.TryParse(text, System.Globalization.NumberStyles.Float,
                System.Globalization.CultureInfo.InvariantCulture, out value) || double.IsNaN(value) || double.IsInfinity(value) || value < 0)
                return false;
            if (unit == "%")
            {
                if (value > 100) return false;
                peakAbs16 = (int)Math.Round(value * 327.67);
                return true;
            }
            if (unit != "abs16" || value > 32768 || value != Math.Truncate(value)) return false;
            peakAbs16 = (int)value;
            return true;
        }

        public bool TrySetManualReference(string text, string unit)
        {
            int peak;
            if (!TryParseManualReference(text, unit, out peak)) return false;
            MeasuredReferencePeakAbs16 = peak;
            MeasuredReferencePercent = peak / 327.67;
            ObservedStatus = "manual";
            return true;
        }

        public double GetEffectiveGainRatio()
        {
            if (Mode == "linear") return LinearGainPercent / 100.0;
            if (Mode == "companding") return CompandingGainPercent / 100.0;
            return 1.0;
        }

        public void Normalize()
        {
            // Migrating an old Normalize profile must preserve its saved manual
            // Linear gain instead of silently inheriting a different global policy.
            var wasLegacyNormalize = string.Equals(Mode, "normalize", StringComparison.OrdinalIgnoreCase);
            if (string.IsNullOrWhiteSpace(Mode)) Mode = "linear";
            Mode = Mode.Trim().ToLowerInvariant();
            if (Mode != "original" && Mode != "companding" && Mode != "linear")
                Mode = "linear";
            if (wasLegacyNormalize || (Mode == "linear" && LinearGainPercent != 100))
                UseGlobal = false;
            MeasuredReferencePeakAbs16 = Math.Max(0, Math.Min(32768, MeasuredReferencePeakAbs16));
            MeasuredReferencePercent = Math.Max(0.0, Math.Min(100.0, MeasuredReferencePercent));
            TargetPeakPercent = Math.Max(1, Math.Min(100, TargetPeakPercent));
            MaxGainPercent = Math.Max(100, Math.Min(400, MaxGainPercent));
            LinearGainPercent = Math.Max(0, Math.Min(1000, LinearGainPercent));
            CompandingGainPercent = Math.Max(100, Math.Min(200, CompandingGainPercent));
            if (string.IsNullOrWhiteSpace(ObservedStatus)) ObservedStatus = "none";
        }
    }

    public class TraySettings
    {
        public bool AutoDetectGames { get; set; }
        public bool TriggerOnAdaptiveTriggers { get; set; }
        public bool TriggerOnHapticFeedback { get; set; }
        public bool EnableNotifications { get; set; }
        public bool EnableRumble { get; set; }
        public bool SyncLightbar { get; set; }
        public int HapticThresholdPercent { get; set; }
        public int TriggerStrengthPercent { get; set; }
        public int VibrationStrengthPercent { get; set; }
        public int Apex4GyroStrengthPercent { get; set; }
        public int Apex4GyroYawStrengthPercent { get; set; }
        public Dictionary<string, string> GameExecutables { get; set; }
        public int InitializationTimeoutSeconds { get; set; }
        // Manual bridge mode describes the current process session, not a
        // durable preference. Persisting it leaves automatic detection paused
        // after a Tray restart without recreating the manual engine session.
        [ScriptIgnore]
        public string ForcedProfile { get; set; }
        public string Language { get; set; }
        // The support callout is introduced once, then never again.
        public bool SupportHintShown { get; set; }
        public List<string> ExcludedGames { get; set; }
        public List<string> ManualFixAllowedGames { get; set; }
        public Dictionary<string, int> ApexProfileSlots { get; set; }
        // Legacy scalar preferences remain for migration, not as an active
        // cross-model calibration. Never persist the last connected model.
        public Dictionary<string, ControllerCalibration> ControllerCalibrations { get; set; }
        public Apex6PcmGlobalSettings Apex6PcmGlobal { get; set; }
        public Dictionary<string, Apex6PcmGameSettings> Apex6PcmGames { get; set; }

        public static bool IsCalibrationModel(string model)
        {
            return model == "apex4" || model == "apex5" || model == "apex6";
        }

        public ControllerCalibration GetControllerCalibration(string model)
        {
            if (!IsCalibrationModel(model)) return null;
            lock (this)
            {
                if (ControllerCalibrations == null)
                    ControllerCalibrations = new Dictionary<string, ControllerCalibration>();
                ControllerCalibration calibration;
                if (!ControllerCalibrations.TryGetValue(model, out calibration) || calibration == null)
                {
                    // Preserve existing users' strengths/rumble on first migration.
                    // Hardware-specific settings only migrate to their own model.
                    calibration = new ControllerCalibration
                    {
                        TriggerStrengthPercent = TriggerStrengthPercent,
                        VibrationStrengthPercent = VibrationStrengthPercent,
                        HapticThresholdPercent = HapticThresholdPercent,
                        EnableRumble = EnableRumble,
                        SyncLightbar = SyncLightbar,
                        GyroStrengthPercent = Apex4GyroStrengthPercent,
                        GyroYawStrengthPercent = Apex4GyroYawStrengthPercent
                    };
                    ControllerCalibrations[model] = calibration;
                }
                calibration.Normalize(model);
                return calibration;
            }
        }

        internal string BuildControllerCalibrationArguments()
        {
            lock (this)
            {
                var result = new System.Text.StringBuilder();
                foreach (var model in new[] { "apex4", "apex5", "apex6" })
                {
                    var c = GetControllerCalibration(model);
                    result.AppendFormat(System.Globalization.CultureInfo.InvariantCulture,
                        " --controller-calibration {0}:{1}:{2}:{3}:{4}:{5}:{6}:{7}",
                        model, c.TriggerStrengthPercent, c.VibrationStrengthPercent,
                        c.HapticThresholdPercent, c.EnableRumble ? 1 : 0, c.SyncLightbar ? 1 : 0,
                        c.GyroStrengthPercent, c.GyroYawStrengthPercent);
                }
                return result.ToString();
            }
        }

        public Apex6PcmGlobalSettings GetApex6PcmGlobal()
        {
            lock (this)
            {
                if (Apex6PcmGlobal == null) Apex6PcmGlobal = new Apex6PcmGlobalSettings();
                Apex6PcmGlobal.Normalize();
                return Apex6PcmGlobal;
            }
        }

        public Apex6PcmGameSettings GetApex6PcmGameSettings(string normalizedKey)
        {
            if (string.IsNullOrWhiteSpace(normalizedKey)) return null;
            lock (this)
            {
                if (Apex6PcmGames == null)
                    Apex6PcmGames = new Dictionary<string, Apex6PcmGameSettings>(StringComparer.OrdinalIgnoreCase);
                else if (!Equals(Apex6PcmGames.Comparer, StringComparer.OrdinalIgnoreCase))
                {
                    var normalized = new Dictionary<string, Apex6PcmGameSettings>(StringComparer.OrdinalIgnoreCase);
                    foreach (var pair in Apex6PcmGames) normalized[pair.Key] = pair.Value;
                    Apex6PcmGames = normalized;
                }

                Apex6PcmGameSettings settings;
                if (Apex6PcmGames.TryGetValue(normalizedKey.Trim(), out settings) && settings != null)
                {
                    settings.Normalize();
                    return settings;
                }
                return null;
            }
        }

        public void SetApex6PcmGameSettings(string normalizedKey, Apex6PcmGameSettings settings)
        {
            if (string.IsNullOrWhiteSpace(normalizedKey)) return;
            lock (this)
            {
                if (Apex6PcmGames == null)
                    Apex6PcmGames = new Dictionary<string, Apex6PcmGameSettings>(StringComparer.OrdinalIgnoreCase);
                else if (!Equals(Apex6PcmGames.Comparer, StringComparer.OrdinalIgnoreCase))
                {
                    var normalized = new Dictionary<string, Apex6PcmGameSettings>(StringComparer.OrdinalIgnoreCase);
                    foreach (var pair in Apex6PcmGames) normalized[pair.Key] = pair.Value;
                    Apex6PcmGames = normalized;
                }

                string key = normalizedKey.Trim();
                if (settings == null)
                {
                    Apex6PcmGames.Remove(key);
                }
                else
                {
                    settings.Normalize();
                    Apex6PcmGames[key] = settings;
                }
            }
        }

        public Apex6PcmGameSettings ResolveApex6PcmPolicy(string normalizedKey)
        {
            lock (this)
            {
                var global = GetApex6PcmGlobal();
                var game = GetApex6PcmGameSettings(normalizedKey);
                bool inherit = game == null || game.UseGlobal;
                return new Apex6PcmGameSettings
                {
                    UseGlobal = inherit,
                    Mode = inherit ? global.Mode : game.Mode,
                    LinearGainPercent = inherit ? global.LinearGainPercent : game.LinearGainPercent,
                    CompandingGainPercent = inherit ? global.CompandingGainPercent : game.CompandingGainPercent,
                    ApplyGlobalVibrationStrength = game == null || game.ApplyGlobalVibrationStrength,
                    TargetPeakPercent = inherit ? global.TargetPeakPercent : game.TargetPeakPercent,
                    MaxGainPercent = inherit ? global.MaxGainPercent : game.MaxGainPercent,
                    MeasuredReferencePeakAbs16 = game != null ? game.MeasuredReferencePeakAbs16 : 0,
                    MeasuredReferencePercent = game != null ? game.MeasuredReferencePercent : 0
                };
            }
        }

        public string BuildApex6PcmArguments(string normalizedKey)
        {
            lock (this)
            {
                var policy = ResolveApex6PcmPolicy(normalizedKey);
                string mode = policy.Mode;
                int linear = policy.LinearGainPercent;
                int companding = policy.CompandingGainPercent;
                string strengthArg = policy.ApplyGlobalVibrationStrength ? string.Empty : " --apex6-pcm-ignore-vibration-strength";

                if (string.Equals(mode, "original", StringComparison.OrdinalIgnoreCase))
                {
                    // Original is default in engine: omit to avoid --rumble requirement (P0-1)
                    return strengthArg;
                }
                if (string.Equals(mode, "companding", StringComparison.OrdinalIgnoreCase))
                {
                    return string.Format(System.Globalization.CultureInfo.InvariantCulture,
                        " --apex6-pcm-mode companding --apex6-pcm-companding-gain-percent {0}{1}", companding, strengthArg);
                }
                if (string.Equals(mode, "linear", StringComparison.OrdinalIgnoreCase))
                {
                    return string.Format(System.Globalization.CultureInfo.InvariantCulture,
                        " --apex6-pcm-mode linear --apex6-pcm-linear-gain-percent {0}{1}", linear, strengthArg);
                }

                return string.Empty;
            }
        }

        public TraySettings()
        {
            AutoDetectGames = true;
            TriggerOnAdaptiveTriggers = true;
            TriggerOnHapticFeedback = true;
            EnableNotifications = true;
            EnableRumble = true;
            SyncLightbar = false;
            HapticThresholdPercent = 12;
            TriggerStrengthPercent = 100;
            VibrationStrengthPercent = 100;
            Apex4GyroStrengthPercent = 100;
            Apex4GyroYawStrengthPercent = 100;
            GameExecutables = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            InitializationTimeoutSeconds = 20;
            ForcedProfile = "none";
            Language = "auto";
            ExcludedGames = new List<string>();
            ManualFixAllowedGames = new List<string>();
            ApexProfileSlots = new Dictionary<string, int>();
            Apex6PcmGlobal = new Apex6PcmGlobalSettings();
            Apex6PcmGames = new Dictionary<string, Apex6PcmGameSettings>(StringComparer.OrdinalIgnoreCase);
        }

        public bool IsGameExcluded(string normalizedOrTitle)
        {
            if (string.IsNullOrWhiteSpace(normalizedOrTitle) || ExcludedGames == null) return false;
            foreach (var item in ExcludedGames)
            {
                if (string.Equals(item, normalizedOrTitle, StringComparison.OrdinalIgnoreCase))
                {
                    return true;
                }
            }
            return false;
        }

        public void SetGameExcluded(string normalizedOrTitle, bool excluded)
        {
            if (string.IsNullOrWhiteSpace(normalizedOrTitle)) return;
            if (ExcludedGames == null) ExcludedGames = new List<string>();

            for (int i = ExcludedGames.Count - 1; i >= 0; i--)
            {
                if (string.Equals(ExcludedGames[i], normalizedOrTitle, StringComparison.OrdinalIgnoreCase))
                {
                    if (!excluded)
                    {
                        ExcludedGames.RemoveAt(i);
                    }
                    else
                    {
                        return;
                    }
                }
            }

            if (excluded)
            {
                ExcludedGames.Add(normalizedOrTitle);
            }
        }

        public bool IsManualFixAllowed(string normalized)
        {
            if (string.IsNullOrWhiteSpace(normalized) || ManualFixAllowedGames == null) return false;
            foreach (var item in ManualFixAllowedGames)
            {
                if (string.Equals(item, normalized, StringComparison.OrdinalIgnoreCase)) return true;
            }
            return false;
        }

        public void SetManualFixAllowed(string normalized, bool allowed)
        {
            if (string.IsNullOrWhiteSpace(normalized)) return;
            if (ManualFixAllowedGames == null) ManualFixAllowedGames = new List<string>();
            ManualFixAllowedGames.RemoveAll(
                item => string.Equals(item, normalized, StringComparison.OrdinalIgnoreCase));
            if (allowed) ManualFixAllowedGames.Add(normalized);
        }

        public void SetGameExcludedAliases(string normalized, string title, bool excluded)
        {
            SetGameExcluded(normalized, excluded);
            SetGameExcluded(title, excluded);
        }

        public int GetApexProfileSlot(string normalizedOrTitle)
        {
            if (string.IsNullOrWhiteSpace(normalizedOrTitle) || ApexProfileSlots == null)
            {
                return 0;
            }

            foreach (var item in ApexProfileSlots)
            {
                if (string.Equals(item.Key, normalizedOrTitle, StringComparison.OrdinalIgnoreCase))
                {
                    return item.Value >= 1 && item.Value <= 4 ? item.Value : 0;
                }
            }
            return 0;
        }

        public void SetApexProfileSlot(string normalizedOrTitle, int slot)
        {
            if (string.IsNullOrWhiteSpace(normalizedOrTitle)) return;
            if (slot < 0 || slot > 4)
            {
                throw new ArgumentOutOfRangeException("slot", "The Apex profile slot must be between 0 and 4.");
            }
            if (ApexProfileSlots == null)
            {
                ApexProfileSlots = new Dictionary<string, int>();
            }

            string existingKey = null;
            foreach (var key in ApexProfileSlots.Keys)
            {
                if (string.Equals(key, normalizedOrTitle, StringComparison.OrdinalIgnoreCase))
                {
                    existingKey = key;
                    break;
                }
            }
            if (existingKey != null) ApexProfileSlots.Remove(existingKey);
            if (slot != 0) ApexProfileSlots[normalizedOrTitle.Trim()] = slot;
        }

        internal void ResetTransientState()
        {
            ForcedProfile = "none";
        }

        public string GetGameExecutable(string game)
        {
            var current = GameExecutables;
            if (current == null || string.IsNullOrWhiteSpace(game)) return string.Empty;
            foreach (var entry in current)
                if (string.Equals(entry.Key, game, StringComparison.OrdinalIgnoreCase)) return entry.Value ?? string.Empty;
            return string.Empty;
        }

        public void SetGameExecutable(string game, string executable)
        {
            if (string.IsNullOrWhiteSpace(game)) return;
            var path = string.IsNullOrWhiteSpace(executable) ? string.Empty : NormalizeExecutable(executable);
            if (!string.IsNullOrWhiteSpace(executable) && path == null)
                throw new ArgumentException("Select an absolute .exe path.", "executable");
            var next = new Dictionary<string, string>(StringComparer.OrdinalIgnoreCase);
            if (GameExecutables != null)
                foreach (var item in GameExecutables) next[item.Key] = item.Value;
            next.Remove(game);
            if (path.Length > 0) next[game] = path;
            GameExecutables = next;
        }

        public bool TryGetGameForExecutable(string executable, out string game)
        {
            game = null;
            var path = NormalizeExecutable(executable);
            var current = GameExecutables;
            if (path == null || current == null) return false;
            foreach (var item in current)
            {
                if (!string.Equals(path, NormalizeExecutable(item.Value), StringComparison.OrdinalIgnoreCase)) continue;
                // A path assigned to several games is ambiguous.
                if (game != null) { game = null; return false; }
                game = item.Key;
            }
            return game != null;
        }

        private static string NormalizeExecutable(string path)
        {
            try
            {
                if (string.IsNullOrWhiteSpace(path) || !Path.IsPathRooted(path) ||
                    !string.Equals(Path.GetExtension(path), ".exe", StringComparison.OrdinalIgnoreCase)) return null;
                return Path.GetFullPath(path.Trim());
            }
            catch { return null; }
        }

        private static string SettingsFilePath
        {
            get
            {
                return Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData),
                                    "ApexSenseBridge", "tray_settings.json");
            }
        }

        public static TraySettings Load()
        {
            try
            {
                var path = SettingsFilePath;
                if (File.Exists(path))
                {
                    var json = File.ReadAllText(path);
                    var serializer = new JavaScriptSerializer();
                    var settings = serializer.Deserialize<TraySettings>(json);
                    if (settings != null)
                    {
                        settings.ResetTransientState();
                        settings.TriggerStrengthPercent = Math.Max(0, Math.Min(100, settings.TriggerStrengthPercent));
                        settings.VibrationStrengthPercent = Math.Max(0, Math.Min(200, settings.VibrationStrengthPercent));
                        settings.Apex4GyroStrengthPercent = Math.Max(25, Math.Min(400, settings.Apex4GyroStrengthPercent));
                        settings.Apex4GyroYawStrengthPercent = Math.Max(25, Math.Min(400, settings.Apex4GyroYawStrengthPercent));
                        settings.HapticThresholdPercent = Math.Max(0, Math.Min(95, settings.HapticThresholdPercent));
                        if (settings.GameExecutables == null) settings.GameExecutables = new Dictionary<string, string>();
                        if (settings.ExcludedGames == null) settings.ExcludedGames = new List<string>();
                        if (settings.ManualFixAllowedGames == null) settings.ManualFixAllowedGames = new List<string>();
                        if (settings.ApexProfileSlots == null)
                        {
                            settings.ApexProfileSlots = new Dictionary<string, int>();
                        }
                        return settings;
                    }
                }
            }
            catch
            {
            }
            return new TraySettings();
        }

        public void Save()
        {
            try
            {
                // Serialize AND write under one lock so overlapping UI/autodetect
                // saves cannot overwrite or race the same staging file.
                lock (this)
                {
                    var path = SettingsFilePath;
                    var dir = Path.GetDirectoryName(path);
                    if (!Directory.Exists(dir)) Directory.CreateDirectory(dir);
                    var json = new JavaScriptSerializer().Serialize(this);
                    var stagingPath = path + ".tmp";
                    File.WriteAllText(stagingPath, json);
                    if (File.Exists(path)) File.Replace(stagingPath, path, null);
                    else File.Move(stagingPath, path);
                }
            }
            catch (Exception ex)
            {
                System.Diagnostics.Trace.TraceError("ApexSenseBridge settings save failed: " + ex);
                try { ApexSenseBridgeTray.Common.AppLog.WriteLine("tray.log", "Settings save failed: " + ex.Message); }
                catch { /* Never crash the game bridge because a diagnostic log is unavailable. */ }
            }
        }
    }
}
