using ApexSenseBridgeTray.Common;
using ApexSenseBridgeTray.Models;
using ApexSenseBridgeTray.Services;
using ApexSenseBridge.Common;
using Microsoft.Win32;
using System;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.ComponentModel;
using System.Diagnostics;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using System.Windows;
using System.Windows.Controls;
using System.Windows.Input;
using System.Windows.Media;
using System.Windows.Media.Effects;
using System.Windows.Media.Imaging;
using System.Windows.Shapes;


namespace ApexSenseBridgeTray
{
    // PCM extension UI/measurement lives outside the upstream GameListWindow code.
    public partial class GameListWindow
    {
        private PcmEditDebouncer pcmEdits;
        private readonly System.Windows.Threading.DispatcherTimer pcmEditTimer = new System.Windows.Threading.DispatcherTimer { Interval = TimeSpan.FromMilliseconds(25) };
        private void QueuePcmEdit() { if (pcmEdits != null) { sessionManager?.InvalidatePcmPolicy(); pcmEdits.Schedule(); } }
        private void UpdatePcmControlStatus()
        {
            if (TxtApex6PcmControlStatus == null) return;
            string status = sessionManager == null ? "Disconnected" : sessionManager.PcmControlStatus;
            string label = status == "Applied" ? "Loc_Apex6PcmApplied" : status == "Pending" ? "Loc_Apex6PcmPending" :
                status.StartsWith("Disconnected", StringComparison.Ordinal) ? "Loc_Apex6PcmDisconnected" : "Loc_Apex6PcmFailed";
            TxtApex6PcmControlStatus.Text = LocalizationManager.Get(label) +
                (sessionManager != null && sessionManager.IsSessionActive ? " — " + sessionManager.ActiveGameTitle : string.Empty);
            TxtApex6PcmControlStatus.ToolTip = status;
        }
        private void OnPcmControlChanged()
        {
            Dispatcher.BeginInvoke(new Action(() => { UpdatePcmControlStatus(); UpdateSettingsPcmMonitor(); }));
        }


        #region APEX 6 PCM Extension UI

        private bool updatingApex6PcmUi = false;
        private System.Windows.Threading.DispatcherTimer pcmLiveTimer = null;
        private System.IO.MemoryMappedFiles.MemoryMappedFile pcmLiveMapping = null;
        private System.IO.MemoryMappedFiles.MemoryMappedViewAccessor pcmLiveAccessor = null;
        private bool isMeasuringPcm = false;
        private uint activeMeasurementGen = 0;
        private string pcmMeasurementToken;
        private string pcmMeasurementGameKey;
        private bool pcmMeasurementComplete;
        private System.Threading.Mutex pcmLiveMutex;
        private byte[] pcmStoppedSnapshot;

        private sealed class PcmGuard : IDisposable
        {
            private readonly System.Threading.Mutex mutex;
            public PcmGuard(System.Threading.Mutex value)
            {
                mutex = value;
                try
                {
                    if (mutex == null || !mutex.WaitOne(1000)) throw new TimeoutException("PCM snapshot lock unavailable.");
                }
                catch (System.Threading.AbandonedMutexException)
                {
                    mutex.ReleaseMutex();
                    throw new InvalidOperationException("PCM session lock was abandoned; restart measurement.");
                }
            }
            public void Dispose() { mutex.ReleaseMutex(); }
        }

        private const int PcmLiveBlockSize = 78;
        private const uint PcmLiveMagic = 0x50365041;
        private const uint PcmLiveVersion = 3;

        private bool PcmSessionMatches()
        {
            return sessionManager != null &&
                !string.IsNullOrEmpty(pcmMeasurementToken) &&
                string.Equals(pcmMeasurementToken, sessionManager.ActiveSessionToken, StringComparison.Ordinal) &&
                string.Equals(pcmMeasurementGameKey, sessionManager.ActiveGameKey, StringComparison.OrdinalIgnoreCase);
        }

        private void DisconnectPcmMeasurement()
        {
            if (pcmLiveTimer != null) pcmLiveTimer.Stop();
            if (pcmLiveAccessor != null)
            {
                try { using (new PcmGuard(pcmLiveMutex)) pcmLiveAccessor.Write(8, (uint)0); }
                catch (Exception ex) { AppLog.WriteLine("tray.log", "PCM disconnect: " + ex.Message); }
                pcmLiveAccessor.Dispose();
                pcmLiveAccessor = null;
            }
            if (pcmLiveMapping != null) { pcmLiveMapping.Dispose(); pcmLiveMapping = null; }
            if (pcmLiveMutex != null) { pcmLiveMutex.Dispose(); pcmLiveMutex = null; }
            pcmStoppedSnapshot = null;
            isMeasuringPcm = false;
            pcmMeasurementComplete = false;
            pcmMeasurementToken = null;
            pcmMeasurementGameKey = null;
            if (TxtApex6PcmMeasureAction != null)
                TxtApex6PcmMeasureAction.Text = LocalizationManager.Get("Loc_Apex6PcmMeasureStart");
            UpdateSettingsPcmMonitor();
        }

        // One measurement state, two views. The settings view follows the active
        // engine session and mirrors the existing game-detail live meters.
        private void UpdateSettingsPcmMonitor()
        {
            if (BtnSettingsPcmMeasure == null) return; // may run before InitializeComponent
            var activeGame = sessionManager == null ? null : sessionManager.ActiveGameKey;
            var hasSession = !string.IsNullOrWhiteSpace(activeGame) &&
                             sessionManager != null &&
                             !string.IsNullOrWhiteSpace(sessionManager.ActiveSessionToken);
            BtnSettingsPcmMeasure.IsEnabled = isMeasuringPcm || hasSession;
            TxtSettingsPcmMeasureAction.Text = LocalizationManager.Get(
                isMeasuringPcm ? "Loc_Apex6PcmMeasureStop" : "Loc_Apex6PcmMeasureStart");
            TxtSettingsPcmGame.Text = hasSession ? activeGame : LocalizationManager.Get("Loc_Apex6PcmDisconnected");
            TxtSettingsPcmStatus.Text = (isMeasuringPcm || pcmStoppedSnapshot != null) &&
                                         TxtApex6PcmLiveStatus != null
                ? TxtApex6PcmLiveStatus.Text : LocalizationManager.Get("Loc_Apex6PcmMonitorIdle");
            if (PrgApex6PcmLiveL != null) PrgSettingsPcmInputL.Value = PrgApex6PcmLiveL.Value;
            if (PrgApex6PcmLiveR != null) PrgSettingsPcmInputR.Value = PrgApex6PcmLiveR.Value;
            if (PrgApex6PcmOutputL != null) PrgSettingsPcmOutputL.Value = PrgApex6PcmOutputL.Value;
            if (PrgApex6PcmOutputR != null) PrgSettingsPcmOutputR.Value = PrgApex6PcmOutputR.Value;
            TxtSettingsPcmPeak.Text = TxtApex6PcmMeasuredOutputPeak == null
                ? string.Empty : TxtApex6PcmMeasuredOutputPeak.Text;
        }

        private void UpdateGlobalApex6PcmUi()
        {
            if (CmbGlobalApex6PcmMode == null || settings == null) return;
            updatingEffectSettings = true;
            try
            {
                var global = settings.GetApex6PcmGlobal();
                if (CmbGlobalApex6PcmMode.Items.Count == 0)
                {
                    CmbGlobalApex6PcmMode.Items.Add(new ComboBoxItem { Content = LocalizationManager.Get("Loc_Apex6PcmModeOriginal"), Tag = "original" });
                    CmbGlobalApex6PcmMode.Items.Add(new ComboBoxItem { Content = LocalizationManager.Get("Loc_Apex6PcmModeCompanding"), Tag = "companding" });
                    CmbGlobalApex6PcmMode.Items.Add(new ComboBoxItem { Content = LocalizationManager.Get("Loc_Apex6PcmModeLinear"), Tag = "linear" });
                    }

                foreach (ComboBoxItem item in CmbGlobalApex6PcmMode.Items)
                {
                    if (string.Equals(item.Tag as string, global.Mode, StringComparison.OrdinalIgnoreCase))
                    {
                        CmbGlobalApex6PcmMode.SelectedItem = item;
                        break;
                    }
                }

                if (PnlGlobalApex6PcmLinear != null)
                {
                    PnlGlobalApex6PcmLinear.Visibility = global.Mode == "linear" ? Visibility.Visible : Visibility.Collapsed;
                }
                if (PnlGlobalApex6PcmCompanding != null)
                {
                    PnlGlobalApex6PcmCompanding.Visibility = global.Mode == "companding" ? Visibility.Visible : Visibility.Collapsed;
                }
                if (PnlGlobalApex6PcmNormalize != null)
                {
                    PnlGlobalApex6PcmNormalize.Visibility = global.Mode == "normalize" ? Visibility.Visible : Visibility.Collapsed;
                }
                if (SliderGlobalApex6PcmCompanding != null) SliderGlobalApex6PcmCompanding.Value = global.CompandingGainPercent;
                if (TxtBoxGlobalApex6PcmCompanding != null) TxtBoxGlobalApex6PcmCompanding.Text = global.CompandingGainPercent.ToString(CultureInfo.InvariantCulture);
                if (SliderGlobalApex6PcmLinear != null) SliderGlobalApex6PcmLinear.Value = global.LinearGainPercent;
                if (TxtBoxGlobalApex6PcmLinear != null) TxtBoxGlobalApex6PcmLinear.Text = global.LinearGainPercent.ToString(CultureInfo.InvariantCulture);
                if (SliderGlobalApex6PcmTarget != null) SliderGlobalApex6PcmTarget.Value = global.TargetPeakPercent;
                if (TxtBoxGlobalApex6PcmTarget != null) TxtBoxGlobalApex6PcmTarget.Text = global.TargetPeakPercent.ToString(CultureInfo.InvariantCulture);
                if (SliderGlobalApex6PcmMaxGain != null) SliderGlobalApex6PcmMaxGain.Value = global.MaxGainPercent;
                if (TxtBoxGlobalApex6PcmMaxGain != null) TxtBoxGlobalApex6PcmMaxGain.Text = global.MaxGainPercent.ToString(CultureInfo.InvariantCulture);
            }
            finally
            {
                updatingEffectSettings = false;
            }
        }

        private void OnGlobalApex6PcmModeChanged(object sender, SelectionChangedEventArgs e)
        {
            if (updatingEffectSettings || settings == null) return;
            var item = CmbGlobalApex6PcmMode.SelectedItem as ComboBoxItem;
            if (item == null) return;
            var global = settings.GetApex6PcmGlobal();
            global.Mode = (item.Tag as string) ?? "original";
            QueuePcmEdit();
            if (PnlGlobalApex6PcmCompanding != null)
            {
                PnlGlobalApex6PcmCompanding.Visibility = global.Mode == "companding" ? Visibility.Visible : Visibility.Collapsed;
            }
            if (PnlGlobalApex6PcmLinear != null)
            {
                PnlGlobalApex6PcmLinear.Visibility = global.Mode == "linear" ? Visibility.Visible : Visibility.Collapsed;
            }
            if (PnlGlobalApex6PcmNormalize != null)
            {
                PnlGlobalApex6PcmNormalize.Visibility = global.Mode == "normalize" ? Visibility.Visible : Visibility.Collapsed;
            }
            UpdateApex6PcmDetailPane();
        }

        private void OnGlobalApex6PcmSettingChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            if (updatingEffectSettings || settings == null) return;
            var global = settings.GetApex6PcmGlobal();
            global.LinearGainPercent = (int)SliderGlobalApex6PcmLinear.Value;
            if (TxtBoxGlobalApex6PcmLinear != null)
            {
                TxtBoxGlobalApex6PcmLinear.Text = global.LinearGainPercent.ToString(CultureInfo.InvariantCulture);
            }
            QueuePcmEdit();
            UpdateApex6PcmDetailPane();
        }

        private void OnGlobalApex6PcmTargetChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            if (updatingEffectSettings || settings == null) return;
            var global = settings.GetApex6PcmGlobal();
            global.TargetPeakPercent = (int)SliderGlobalApex6PcmTarget.Value;
            if (TxtBoxGlobalApex6PcmTarget != null)
                TxtBoxGlobalApex6PcmTarget.Text = global.TargetPeakPercent.ToString(CultureInfo.InvariantCulture);
            QueuePcmEdit();
            UpdateApex6PcmDetailPane();
        }

        private void OnGlobalApex6PcmMaxGainChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            if (updatingEffectSettings || settings == null) return;
            var global = settings.GetApex6PcmGlobal();
            global.MaxGainPercent = (int)SliderGlobalApex6PcmMaxGain.Value;
            if (TxtBoxGlobalApex6PcmMaxGain != null)
                TxtBoxGlobalApex6PcmMaxGain.Text = global.MaxGainPercent.ToString(CultureInfo.InvariantCulture);
            QueuePcmEdit();
            UpdateApex6PcmDetailPane();
        }

        private void OnGlobalApex6PcmTargetTextLostFocus(object s, RoutedEventArgs e) { CommitPcmNumber(TxtBoxGlobalApex6PcmTarget, SliderGlobalApex6PcmTarget); }
        private void OnGlobalApex6PcmMaxGainTextLostFocus(object s, RoutedEventArgs e) { CommitPcmNumber(TxtBoxGlobalApex6PcmMaxGain, SliderGlobalApex6PcmMaxGain); }
        private void OnGlobalApex6PcmTargetTextKeyDown(object s, KeyEventArgs e) { if (e.Key == Key.Enter) { OnGlobalApex6PcmTargetTextLostFocus(s, e); e.Handled = true; } }
        private void OnGlobalApex6PcmMaxGainTextKeyDown(object s, KeyEventArgs e) { if (e.Key == Key.Enter) { OnGlobalApex6PcmMaxGainTextLostFocus(s, e); e.Handled = true; } }

        private void UpdateApex6PcmDetailPane()
        {
            if (PnlGameDetailApex6Pcm == null || SelectedGame == null || settings == null) return;
            updatingApex6PcmUi = true;
            try
            {
                var game = SelectedGame;
                var gameSettings = settings.GetApex6PcmGameSettings(game.Normalized);
                var policy = settings.ResolveApex6PcmPolicy(game.Normalized);
                if (ChkApex6PcmApplyGlobalStrength != null)
                    ChkApex6PcmApplyGlobalStrength.IsChecked = policy.ApplyGlobalVibrationStrength;

                bool useGlobal = policy.UseGlobal;
                if (ChkApex6PcmUseGlobal != null) ChkApex6PcmUseGlobal.IsChecked = useGlobal;
                if (PnlApex6PcmCustomControls != null) PnlApex6PcmCustomControls.Visibility = Visibility.Visible;
                if (CmbApex6PcmMode != null) CmbApex6PcmMode.IsEnabled = !useGlobal;
                if (RowApex6PcmTarget != null) RowApex6PcmTarget.IsEnabled = !useGlobal;
                if (RowApex6PcmLinear != null) RowApex6PcmLinear.IsEnabled = !useGlobal;
                if (RowApex6PcmCompanding != null) RowApex6PcmCompanding.IsEnabled = !useGlobal;

                if (CmbApex6PcmMode != null && CmbApex6PcmMode.Items.Count == 0)
                {
                                        CmbApex6PcmMode.Items.Add(new ComboBoxItem { Content = LocalizationManager.Get("Loc_Apex6PcmModeLinear"), Tag = "linear" });
                    CmbApex6PcmMode.Items.Add(new ComboBoxItem { Content = LocalizationManager.Get("Loc_Apex6PcmModeCompanding"), Tag = "companding" });
                    CmbApex6PcmMode.Items.Add(new ComboBoxItem { Content = LocalizationManager.Get("Loc_Apex6PcmModeOriginal"), Tag = "original" });
                }

                string currentMode = policy.Mode;
                if (CmbApex6PcmMode != null)
                {
                    foreach (ComboBoxItem item in CmbApex6PcmMode.Items)
                    {
                        if (string.Equals(item.Tag as string, currentMode, StringComparison.OrdinalIgnoreCase))
                        {
                            CmbApex6PcmMode.SelectedItem = item;
                            break;
                        }
                    }
                }

                int refPeak = gameSettings != null ? gameSettings.MeasuredReferencePeakAbs16 : 0;
                double refPct = refPeak > 0 ? refPeak / 327.67 : (gameSettings != null ? gameSettings.MeasuredReferencePercent : 0);

                if (TxtBoxApex6PcmManualRef != null)
                {
                    var unit = CmbApex6PcmManualRefUnit.SelectedItem as ComboBoxItem;
                    TxtBoxApex6PcmManualRef.Text = unit != null && (string)unit.Tag == "%"
                        ? Math.Min(100.0, refPct).ToString("R", CultureInfo.InvariantCulture)
                        : refPeak.ToString(CultureInfo.InvariantCulture);
                    TxtBoxApex6PcmManualRef.Tag = TxtBoxApex6PcmManualRef.Text;
                }

                if (TxtApex6PcmReference != null)
                {
                    if (refPct > 0)
                    {
                        TxtApex6PcmReference.Text = string.Format("{0:F1}% ({1})", refPct, refPeak > 0 ? refPeak.ToString() : "~");
                        if (TxtApex6PcmMeasuredDetail != null)
                        {
                            if (gameSettings != null && gameSettings.ObservedActiveBlocks > 0)
                            {
                                TxtApex6PcmMeasuredDetail.Text = string.Format(LocalizationManager.Get("Loc_Apex6PcmObservedInfo"),
                                    Math.Round(gameSettings.ObservedLeftPeakAbs16 / 327.67, 1),
                                    Math.Round(gameSettings.ObservedRightPeakAbs16 / 327.67, 1),
                                    Math.Round(refPct, 1));
                            }
                            else
                            {
                                TxtApex6PcmMeasuredDetail.Text = string.Empty;
                            }
                        }
                    }
                    else
                    {
                        TxtApex6PcmReference.Text = LocalizationManager.Get("Loc_Apex6PcmNotMeasured");
                        if (TxtApex6PcmMeasuredDetail != null) TxtApex6PcmMeasuredDetail.Text = string.Empty;
                    }
                }

                RowApex6PcmMaxGain.IsEnabled = false;
                RowApex6PcmMaxGain.Visibility = Visibility.Collapsed;
                SliderApex6PcmMaxGain.Value = policy.MaxGainPercent;
                TxtBoxApex6PcmMaxGain.Text = policy.MaxGainPercent.ToString(CultureInfo.InvariantCulture);
                int targetPct = policy.TargetPeakPercent;
                if (SliderApex6PcmTarget != null) SliderApex6PcmTarget.Value = targetPct;
                if (TxtBoxApex6PcmTarget != null) TxtBoxApex6PcmTarget.Text = targetPct.ToString(CultureInfo.InvariantCulture);

                int linearPct = policy.LinearGainPercent;
                if (SliderApex6PcmLinear != null) SliderApex6PcmLinear.Value = linearPct;
                if (TxtBoxApex6PcmLinear != null) TxtBoxApex6PcmLinear.Text = linearPct.ToString(CultureInfo.InvariantCulture);

                int companding = policy.CompandingGainPercent;
                SliderApex6PcmCompanding.Value = companding;
                TxtBoxApex6PcmCompanding.Text = companding.ToString(CultureInfo.InvariantCulture);
                RowApex6PcmCompanding.Visibility = currentMode == "companding" ? Visibility.Visible : Visibility.Collapsed;
                if (RowApex6PcmReference != null) RowApex6PcmReference.Visibility = Visibility.Visible;
                if (RowApex6PcmTarget != null) RowApex6PcmTarget.Visibility = Visibility.Collapsed;
                if (RowApex6PcmLinear != null) RowApex6PcmLinear.Visibility = currentMode == "linear" ? Visibility.Visible : Visibility.Collapsed;

                UpdateApex6PcmEffectiveGainDisplay(currentMode, refPct, targetPct, linearPct);
                UpdateApex6PcmLinearRecommendation(policy);
            }
            finally
            {
                updatingApex6PcmUi = false;
            }
        }

        private void UpdateApex6PcmEffectiveGainDisplay(string mode, double refPct, int targetPct, int linearPct)
        {
            if (TxtApex6PcmEffectiveGain == null || SelectedGame == null || settings == null) return;
            var policy = settings.ResolveApex6PcmPolicy(SelectedGame.Normalized);
            mode = policy.Mode;
            refPct = policy.MeasuredReferencePeakAbs16 > 0 ? policy.MeasuredReferencePeakAbs16 / 327.67 : policy.MeasuredReferencePercent;
            targetPct = policy.TargetPeakPercent;
            linearPct = policy.LinearGainPercent;
            if (string.Equals(mode, "original", StringComparison.OrdinalIgnoreCase))
            {
                TxtApex6PcmEffectiveGain.Text = "1.00x";
            }
            else if (string.Equals(mode, "companding", StringComparison.OrdinalIgnoreCase))
            {
                TxtApex6PcmEffectiveGain.Text = string.Format(CultureInfo.InvariantCulture, "1.00x - {0:F2}x (Curve)", policy.GetEffectiveGainRatio());
            }
            else if (string.Equals(mode, "linear", StringComparison.OrdinalIgnoreCase))
            {
                TxtApex6PcmEffectiveGain.Text = string.Format(System.Globalization.CultureInfo.InvariantCulture, "{0:F2}x", linearPct / 100.0);
            }

        }

        private void UpdateApex6PcmLinearRecommendation(Apex6PcmGameSettings policy)
        {
            if (TxtApex6PcmLinearRecommendation == null) return;
            int suggested = policy.GetRecommendedLinearGainPercent();
            var key = suggested <= 0 ? "Loc_Apex6PcmRecommendedUnavailable" :
                suggested > 1000 ? "Loc_Apex6PcmRecommendedExceedsLimit" : "Loc_Apex6PcmRecommendedLinear";
            TxtApex6PcmLinearRecommendation.Text = suggested <= 0
                ? LocalizationManager.Get(key)
                : string.Format(CultureInfo.CurrentCulture, LocalizationManager.Get(key), suggested);
        }

        private void UpdateApex6PcmMeasuredOutputPeak(byte leftPeak8, byte rightPeak8)
        {
            if (TxtApex6PcmMeasuredOutputPeak == null) return;
            TxtApex6PcmMeasuredOutputPeak.Text = string.Format(
                CultureInfo.CurrentCulture, LocalizationManager.Get("Loc_Apex6PcmMeasuredOutputPeak"),
                leftPeak8 * 100.0 / 127.0, rightPeak8 * 100.0 / 127.0);
        }

        private void OnApex6PcmUseGlobalRowClick(object sender, MouseButtonEventArgs e)
        {
            if (ChkApex6PcmUseGlobal == null) return;
            ChkApex6PcmUseGlobal.IsChecked = !ChkApex6PcmUseGlobal.IsChecked.GetValueOrDefault(false);
            OnApex6PcmUseGlobalChanged(sender, e);
        }

        private void OnApex6PcmUseGlobalChanged(object sender, RoutedEventArgs e)
        {
            if (updatingApex6PcmUi || SelectedGame == null || settings == null) return;
            var game = SelectedGame;
            var gameSettings = settings.GetApex6PcmGameSettings(game.Normalized);
            if (gameSettings == null) gameSettings = new Apex6PcmGameSettings();

            gameSettings.UseGlobal = ChkApex6PcmUseGlobal.IsChecked.GetValueOrDefault(true);
            settings.SetApex6PcmGameSettings(game.Normalized, gameSettings);
            QueuePcmEdit();

            if (PnlApex6PcmCustomControls != null)
            {
                PnlApex6PcmCustomControls.Visibility = Visibility.Visible;
            }
            UpdateApex6PcmDetailPane();
        }

        private void OnApex6PcmModeChanged(object sender, SelectionChangedEventArgs e)
        {
            if (updatingApex6PcmUi || SelectedGame == null || settings == null) return;
            var item = CmbApex6PcmMode.SelectedItem as ComboBoxItem;
            if (item == null) return;

            var game = SelectedGame;
            var gameSettings = settings.GetApex6PcmGameSettings(game.Normalized);
            if (gameSettings == null) gameSettings = new Apex6PcmGameSettings();

            gameSettings.Mode = (item.Tag as string) ?? "linear";
            gameSettings.UseGlobal = false;
            settings.SetApex6PcmGameSettings(game.Normalized, gameSettings);
            QueuePcmEdit();

            UpdateApex6PcmDetailPane();
        }

        private void OnApex6PcmTargetChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            if (updatingApex6PcmUi || SelectedGame == null || settings == null) return;
            var game = SelectedGame;
            var gameSettings = settings.GetApex6PcmGameSettings(game.Normalized);
            if (gameSettings == null) gameSettings = new Apex6PcmGameSettings();

            gameSettings.TargetPeakPercent = (int)SliderApex6PcmTarget.Value;
            if (TxtBoxApex6PcmTarget != null) TxtBoxApex6PcmTarget.Text = gameSettings.TargetPeakPercent.ToString(CultureInfo.InvariantCulture);
            settings.SetApex6PcmGameSettings(game.Normalized, gameSettings);
            QueuePcmEdit();

            double refPct = gameSettings.MeasuredReferencePeakAbs16 > 0
                ? gameSettings.MeasuredReferencePeakAbs16 / 327.67 : gameSettings.MeasuredReferencePercent;
            UpdateApex6PcmEffectiveGainDisplay(gameSettings.Mode, refPct, gameSettings.TargetPeakPercent, gameSettings.LinearGainPercent);
        }

        private void OnApex6PcmMaxGainChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            if (updatingApex6PcmUi || SelectedGame == null || settings == null) return;
            var value = settings.GetApex6PcmGameSettings(SelectedGame.Normalized) ?? new Apex6PcmGameSettings();
            value.MaxGainPercent = (int)SliderApex6PcmMaxGain.Value;
            TxtBoxApex6PcmMaxGain.Text = value.MaxGainPercent.ToString(CultureInfo.InvariantCulture);
            settings.SetApex6PcmGameSettings(SelectedGame.Normalized, value);
            QueuePcmEdit();
            UpdateApex6PcmEffectiveGainDisplay(value.Mode, value.MeasuredReferencePercent, value.TargetPeakPercent, value.LinearGainPercent);
        }
        private void OnApex6PcmMaxGainTextLostFocus(object s, RoutedEventArgs e) { CommitPcmNumber(TxtBoxApex6PcmMaxGain, SliderApex6PcmMaxGain); }
        private void OnApex6PcmMaxGainTextKeyDown(object s, KeyEventArgs e) { if (e.Key == Key.Enter) { OnApex6PcmMaxGainTextLostFocus(s, e); e.Handled = true; } }

        private void OnApex6PcmApplyGlobalStrengthChanged(object sender, RoutedEventArgs e)
        {
            if (updatingApex6PcmUi || SelectedGame == null || settings == null || ChkApex6PcmApplyGlobalStrength == null) return;
            var key = SelectedGame.Normalized;
            var game = settings.GetApex6PcmGameSettings(key);
            if (game == null)
                game = new Apex6PcmGameSettings { UseGlobal = true }; // preserve global mode inheritance
            game.ApplyGlobalVibrationStrength = ChkApex6PcmApplyGlobalStrength.IsChecked.GetValueOrDefault(true);
            settings.SetApex6PcmGameSettings(key, game);
            QueuePcmEdit(); // live policy refresh, no engine reconnect
        }

        private void ApplyGamePcmLinearGain(int value)
        {
            if (SelectedGame == null || settings == null) return;
            var game = SelectedGame;
            var gameSettings = settings.GetApex6PcmGameSettings(game.Normalized) ?? new Apex6PcmGameSettings();

            // Both slider and typed entry explicitly save and send the same policy.
            gameSettings.UseGlobal = false;
            gameSettings.Mode = "linear";
            gameSettings.LinearGainPercent = value;
            if (TxtBoxApex6PcmLinear != null)
                TxtBoxApex6PcmLinear.Text = value.ToString(CultureInfo.InvariantCulture);
            settings.SetApex6PcmGameSettings(game.Normalized, gameSettings);
            QueuePcmEdit();
            UpdateApex6PcmEffectiveGainDisplay(gameSettings.Mode, gameSettings.MeasuredReferencePercent,
                gameSettings.TargetPeakPercent, value);
        }

        private void OnApex6PcmLinearChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            if (updatingApex6PcmUi || SelectedGame == null || settings == null) return;
            ApplyGamePcmLinearGain((int)SliderApex6PcmLinear.Value);
        }

        private void OnApex6PcmCompandingChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            if (updatingApex6PcmUi || SelectedGame == null || settings == null) return;
            var value = settings.GetApex6PcmGameSettings(SelectedGame.Normalized) ?? new Apex6PcmGameSettings();
            value.CompandingGainPercent = (int)SliderApex6PcmCompanding.Value;
            TxtBoxApex6PcmCompanding.Text = value.CompandingGainPercent.ToString(CultureInfo.InvariantCulture);
            settings.SetApex6PcmGameSettings(SelectedGame.Normalized, value);
            QueuePcmEdit();
        }

        private void OnGlobalApex6PcmCompandingChanged(object sender, RoutedPropertyChangedEventArgs<double> e)
        {
            if (updatingEffectSettings || settings == null) return;
            var value = settings.GetApex6PcmGlobal();
            value.CompandingGainPercent = (int)SliderGlobalApex6PcmCompanding.Value;
            TxtBoxGlobalApex6PcmCompanding.Text = value.CompandingGainPercent.ToString(CultureInfo.InvariantCulture);
            QueuePcmEdit();
            UpdateApex6PcmDetailPane();
        }

        private static bool TryReadPcmNumber(TextBox input, Slider slider, out int value)
        {
            return int.TryParse(input.Text, NumberStyles.Integer, CultureInfo.InvariantCulture, out value) &&
                value >= slider.Minimum && value <= slider.Maximum;
        }

        private static void CommitPcmNumber(TextBox input, Slider slider)
        {
            int value;
            if (TryReadPcmNumber(input, slider, out value)) slider.Value = value;
            input.Text = ((int)slider.Value).ToString(CultureInfo.InvariantCulture);
        }

        private void OnApex6PcmTargetTextLostFocus(object s, RoutedEventArgs e) { CommitPcmNumber(TxtBoxApex6PcmTarget, SliderApex6PcmTarget); }
        private void OnApex6PcmLinearTextLostFocus(object s, RoutedEventArgs e)
        {
            if (updatingApex6PcmUi || SelectedGame == null || settings == null) return;
            int value;
            if (!TryReadPcmNumber(TxtBoxApex6PcmLinear, SliderApex6PcmLinear, out value))
            {
                TxtBoxApex6PcmLinear.Text = ((int)SliderApex6PcmLinear.Value).ToString(CultureInfo.InvariantCulture);
                return;
            }
            // ValueChanged can be suppressed during a detail refresh or fail to fire
            // when the slider already displays the same number. Never rely on it for
            // committing typed input to saved settings or the running PCM engine.
            updatingApex6PcmUi = true;
            try { SliderApex6PcmLinear.Value = value; }
            finally { updatingApex6PcmUi = false; }
            ApplyGamePcmLinearGain(value);
            // Enter and LostFocus are explicit commits, not a drag: persist and
            // refresh the active PCM engine immediately instead of waiting for
            // the debounce timer or a slider-mouseup event.
            pcmEdits?.Flush();
        }
        private void OnApex6PcmCompandingTextLostFocus(object s, RoutedEventArgs e) { CommitPcmNumber(TxtBoxApex6PcmCompanding, SliderApex6PcmCompanding); }
        private void OnGlobalApex6PcmLinearTextLostFocus(object s, RoutedEventArgs e)
        {
            if (updatingEffectSettings || settings == null) return;
            int value;
            if (!TryReadPcmNumber(TxtBoxGlobalApex6PcmLinear, SliderGlobalApex6PcmLinear, out value))
            {
                TxtBoxGlobalApex6PcmLinear.Text = ((int)SliderGlobalApex6PcmLinear.Value).ToString(CultureInfo.InvariantCulture);
                return;
            }
            updatingEffectSettings = true;
            try { SliderGlobalApex6PcmLinear.Value = value; }
            finally { updatingEffectSettings = false; }
            // Explicit commit; don't depend on the slider's ValueChanged event.
            settings.GetApex6PcmGlobal().LinearGainPercent = value;
            TxtBoxGlobalApex6PcmLinear.Text = value.ToString(CultureInfo.InvariantCulture);
            QueuePcmEdit();
            pcmEdits?.Flush();
            UpdateApex6PcmDetailPane();
        }
        private void OnGlobalApex6PcmCompandingTextLostFocus(object s, RoutedEventArgs e) { CommitPcmNumber(TxtBoxGlobalApex6PcmCompanding, SliderGlobalApex6PcmCompanding); }
        private void OnApex6PcmTargetTextKeyDown(object s, KeyEventArgs e) { if (e.Key == Key.Enter) { OnApex6PcmTargetTextLostFocus(s, e); e.Handled = true; } }
        private void OnApex6PcmLinearTextKeyDown(object s, KeyEventArgs e) { if (e.Key == Key.Enter) { OnApex6PcmLinearTextLostFocus(s, e); e.Handled = true; } }
        private void OnApex6PcmCompandingTextKeyDown(object s, KeyEventArgs e) { if (e.Key == Key.Enter) { OnApex6PcmCompandingTextLostFocus(s, e); e.Handled = true; } }
        private void OnGlobalApex6PcmLinearTextKeyDown(object s, KeyEventArgs e) { if (e.Key == Key.Enter) { OnGlobalApex6PcmLinearTextLostFocus(s, e); e.Handled = true; } }
        private void OnGlobalApex6PcmCompandingTextKeyDown(object s, KeyEventArgs e) { if (e.Key == Key.Enter) { OnGlobalApex6PcmCompandingTextLostFocus(s, e); e.Handled = true; } }

        private void OnApex6PcmStartMeasureClick(object sender, RoutedEventArgs e)
        {
            TogglePcmMeasurement(SelectedGame == null ? null : SelectedGame.Normalized, sender, e);
            UpdateSettingsPcmMonitor();
        }

        // Settings monitors the actual engine session rather than whichever game
        // happens to be highlighted in the catalogue.
        private void OnSettingsPcmMeasureClick(object sender, RoutedEventArgs e)
        {
            var gameKey = isMeasuringPcm ? pcmMeasurementGameKey :
                sessionManager == null ? null : sessionManager.ActiveGameKey;
            TogglePcmMeasurement(gameKey, sender, e);
            UpdateSettingsPcmMonitor();
        }

        private void TogglePcmMeasurement(string requestedGameKey, object sender, RoutedEventArgs e)
        {
            if (string.IsNullOrWhiteSpace(requestedGameKey) || settings == null) return;

            if (!isMeasuringPcm)
            {
                DisconnectPcmMeasurement();
                pcmMeasurementToken = sessionManager != null ? sessionManager.ActiveSessionToken : null;
                pcmMeasurementGameKey = requestedGameKey;
                if (!PcmSessionMatches())
                {
                    MessageBox.Show(this, "Start the bridge for the selected game before measuring.", "PCM measurement");
                    return;
                }
                try
                {
                    if (pcmLiveAccessor == null)
                    {
                        pcmLiveMutex = System.Threading.Mutex.OpenExisting(
                            "Local\\ApexSenseBridge.Session." + pcmMeasurementToken + ".Apex6Pcm.Mutex");
                        pcmLiveMapping = System.IO.MemoryMappedFiles.MemoryMappedFile.OpenExisting(
                            "Local\\ApexSenseBridge.Session." + pcmMeasurementToken + ".Apex6Pcm",
                            System.IO.MemoryMappedFiles.MemoryMappedFileRights.ReadWrite);
                        pcmLiveAccessor = pcmLiveMapping.CreateViewAccessor(
                            0, PcmLiveBlockSize, System.IO.MemoryMappedFiles.MemoryMappedFileAccess.ReadWrite);
                    }
                }
                catch
                {
                    if (TxtApex6PcmLiveStatus != null)
                        TxtApex6PcmLiveStatus.Text = "Bridge session not active. Launch bridge first!";
                    if (PnlApex6PcmLiveMeter != null)
                        PnlApex6PcmLiveMeter.Visibility = Visibility.Visible;
                    return;
                }

                if (pcmLiveAccessor != null)
                {
                    bool compatible;
                    try
                    {
                        using (new PcmGuard(pcmLiveMutex))
                        {
                            compatible = pcmLiveAccessor.Capacity >= PcmLiveBlockSize &&
                                         pcmLiveAccessor.ReadUInt32(0) == PcmLiveMagic &&
                                         pcmLiveAccessor.ReadUInt32(4) == PcmLiveVersion;
                            if (compatible)
                            {
                                activeMeasurementGen = unchecked(pcmLiveAccessor.ReadUInt32(12) + 1);
                                pcmLiveAccessor.Write(12, activeMeasurementGen);
                                pcmLiveAccessor.Write(8, (uint)1);
                                pcmStoppedSnapshot = null;
                            }
                        }
                    }
                    catch (Exception ex)
                    {
                        DisconnectPcmMeasurement();
                        TxtApex6PcmLiveStatus.Text = ex.Message;
                        return;
                    }
                    if (!compatible)
                    {
                        DisconnectPcmMeasurement();
                        if (TxtApex6PcmLiveStatus != null)
                            TxtApex6PcmLiveStatus.Text = "IPC version/size mismatch. Please restart bridge.";
                        return;
                    }

                    isMeasuringPcm = true;
                    if (TxtApex6PcmMeasuredOutputPeak != null)
                        TxtApex6PcmMeasuredOutputPeak.Text = string.Empty;
                    if (TxtApex6PcmMeasureAction != null)
                        TxtApex6PcmMeasureAction.Text = LocalizationManager.Get("Loc_Apex6PcmMeasureStop");
                    if (PnlApex6PcmLiveMeter != null)
                        PnlApex6PcmLiveMeter.Visibility = Visibility.Visible;
                    if (TxtApex6PcmLiveStatus != null)
                        TxtApex6PcmLiveStatus.Text = LocalizationManager.Get("Loc_Apex6PcmMeasuring");

                    if (pcmLiveTimer == null)
                    {
                        pcmLiveTimer = new System.Windows.Threading.DispatcherTimer();
                        pcmLiveTimer.Interval = TimeSpan.FromMilliseconds(100);
                        pcmLiveTimer.Tick += OnPcmLiveTimerTick;
                    }
                    pcmLiveTimer.Start();
                }
            }
            else
            {
                // Stop is exclusively controlled by the user. Preserve the result
                // immediately; no separate Save click and no engine policy change.
                StopPcmMeasurement();
                if (pcmMeasurementComplete && pcmStoppedSnapshot != null)
                    OnApex6PcmSaveMeasureClick(sender, e);
            }
        }

        private void StopPcmMeasurement()
        {
            if (!isMeasuringPcm) return;
            if (!PcmSessionMatches()) { DisconnectPcmMeasurement(); return; }
            isMeasuringPcm = false;
            pcmMeasurementComplete = false;
            // Keep the timer alive to invalidate results when the session changes.

            if (pcmLiveAccessor != null)
            {
                try
                {
                    // 1. Signal StopPending (2) so audio thread can flush any in-flight deferred peaks
                    try
                    {
                        using (new PcmGuard(pcmLiveMutex))
                        {
                            if (pcmLiveAccessor.ReadUInt32(8) == 1)
                            {
                                pcmLiveAccessor.Write(8, (uint)2); // StopPending
                            }
                        }
                    }
                    catch { }

                    // 2. Poll up to 150ms in 5ms intervals (without holding lock, so render thread can acquire and flush)
                    bool flushed = false;
                    for (int wait = 0; wait < 30; ++wait)
                    {
                        System.Threading.Thread.Sleep(5);
                        uint state = pcmLiveAccessor.ReadUInt32(8);
                        if (state == 0)
                        {
                            flushed = true;
                            break;
                        }
                    }

                    // 3. Finalize stop under mutex lock and capture snapshot
                    using (new PcmGuard(pcmLiveMutex))
                    {
                        pcmLiveAccessor.Write(8, (uint)0);
                        if (!flushed)
                        {
                            pcmLiveAccessor.Write(72, (uint)1); // mark measurementIncomplete
                        }
                        pcmStoppedSnapshot = new byte[PcmLiveBlockSize];
                        pcmLiveAccessor.ReadArray(0, pcmStoppedSnapshot, 0, PcmLiveBlockSize);
                        pcmMeasurementComplete = BitConverter.ToUInt32(pcmStoppedSnapshot, 28) == activeMeasurementGen;
                    }
                    ushort left16 = BitConverter.ToUInt16(pcmStoppedSnapshot, 32);
                    ushort right16 = BitConverter.ToUInt16(pcmStoppedSnapshot, 34);
                    ushort max16 = BitConverter.ToUInt16(pcmStoppedSnapshot, 36);
                    ushort rawPeak16 = BitConverter.ToUInt16(pcmStoppedSnapshot, 38);
                    uint blocks = BitConverter.ToUInt32(pcmStoppedSnapshot, 40);
                    uint missedUpdates = BitConverter.ToUInt32(pcmStoppedSnapshot, 68);
                    uint incomplete = BitConverter.ToUInt32(pcmStoppedSnapshot, 72);
                    byte boostedLeftPeak8 = pcmStoppedSnapshot[76];
                    byte boostedRightPeak8 = pcmStoppedSnapshot[77];
                    UpdateApex6PcmMeasuredOutputPeak(boostedLeftPeak8, boostedRightPeak8);

                    double maxPct = max16 / 327.67;
                    double rawPct = rawPeak16 / 327.67;
                    if (max16 > 0)
                    {
                        if (TxtApex6PcmReference != null)
                            TxtApex6PcmReference.Text = string.Format("{0:F1}% ({1})", maxPct, max16);
                        if (TxtBoxApex6PcmManualRef != null)
                            TxtBoxApex6PcmManualRef.Text = max16.ToString(CultureInfo.InvariantCulture);
                        if (TxtApex6PcmMeasuredDetail != null)
                        {
                            string detailText;
                            if (rawPeak16 > 0)
                            {
                                detailText = string.Format(
                                    LocalizationManager.Get("Loc_Apex6PcmObservedInfoWithRaw"),
                                    Math.Round(left16 / 327.67, 1),
                                    Math.Round(right16 / 327.67, 1),
                                    Math.Round(maxPct, 1),
                                    Math.Round(rawPct, 1));
                            }
                            else
                            {
                                detailText = string.Format(
                                    LocalizationManager.Get("Loc_Apex6PcmObservedInfo"),
                                    Math.Round(left16 / 327.67, 1),
                                    Math.Round(right16 / 327.67, 1),
                                    Math.Round(maxPct, 1)) + " (" + LocalizationManager.Get("Loc_Apex6PcmRawUnavailable") + ")";
                            }
                            if (incomplete != 0 || missedUpdates > 0)
                            {
                                detailText += " [!] Incomplete (" + missedUpdates + " missed)";
                            }
                            TxtApex6PcmMeasuredDetail.Text = detailText;
                        }
                        UpdateApex6PcmEffectiveGainDisplay("normalize", maxPct, (int)SliderApex6PcmTarget.Value, (int)SliderApex6PcmLinear.Value);
                    }

                    if (TxtApex6PcmLiveStatus != null)
                    {
                        if (incomplete != 0)
                        {
                            TxtApex6PcmLiveStatus.Text = string.Format("Measurement INCOMPLETE: {0} blocks, Peak: {1:F1}% ({2} missed, please remeasure)", blocks, maxPct, missedUpdates);
                        }
                        else
                        {
                            TxtApex6PcmLiveStatus.Text = string.Format(
                                "Measurement complete: {0} blocks | Original peak {1:F1}% | Amplified L/R peaks {2:F1}% / {3:F1}%{4}",
                                blocks, maxPct, boostedLeftPeak8 * 100.0 / 127.0, boostedRightPeak8 * 100.0 / 127.0,
                                boostedLeftPeak8 == 127 || boostedRightPeak8 == 127 ? " | Output reached 100%" : "");
                        }
                    }
                }
                catch (Exception ex)
                {
                    pcmMeasurementComplete = false;
                    pcmStoppedSnapshot = null;
                    TxtApex6PcmLiveStatus.Text = "PCM stop failed: " + ex.Message;
                }
            }

            if (TxtApex6PcmMeasureAction != null)
                TxtApex6PcmMeasureAction.Text = LocalizationManager.Get("Loc_Apex6PcmMeasureStart");
            UpdateSettingsPcmMonitor();
        }

        private void OnPcmLiveTimerTick(object sender, EventArgs e)
        {
            if (pcmLiveAccessor == null) return;
            if (!PcmSessionMatches()) { DisconnectPcmMeasurement(); return; }
            if (!isMeasuringPcm) return;
            try
            {
                byte[] snapshot = new byte[PcmLiveBlockSize];
                using (new PcmGuard(pcmLiveMutex)) pcmLiveAccessor.ReadArray(0, snapshot, 0, PcmLiveBlockSize);
                ushort curL = BitConverter.ToUInt16(snapshot, 16);
                ushort curR = BitConverter.ToUInt16(snapshot, 18);
                ushort maxPeak = BitConverter.ToUInt16(snapshot, 36);
                ushort rawMaxPeak = BitConverter.ToUInt16(snapshot, 38);
                byte outputLeft8 = snapshot[24];
                byte outputRight8 = snapshot[25];
                byte maxOutputLeft8 = snapshot[76];
                byte maxOutputRight8 = snapshot[77];
                UpdateApex6PcmMeasuredOutputPeak(maxOutputLeft8, maxOutputRight8);
                uint clippedSamples = BitConverter.ToUInt32(snapshot, 52);

                double pctL = Math.Min(100.0, curL / 327.67);
                double pctR = Math.Min(100.0, curR / 327.67);
                if (PrgApex6PcmLiveL != null) PrgApex6PcmLiveL.Value = pctL;
                if (PrgApex6PcmLiveR != null) PrgApex6PcmLiveR.Value = pctR;
                if (PrgApex6PcmOutputL != null) PrgApex6PcmOutputL.Value = outputLeft8 * 100.0 / 127.0;
                if (PrgApex6PcmOutputR != null) PrgApex6PcmOutputR.Value = outputRight8 * 100.0 / 127.0;

                double maxPct = maxPeak / 327.67;
                double rawMaxPct = rawMaxPeak / 327.67;
                if (TxtApex6PcmLiveStatus != null)
                {
                    TxtApex6PcmLiveStatus.Text = string.Format(
                        CultureInfo.InvariantCulture,
                        "Measuring... Original peak {0:F1}% | Amplified L/R peak {1:F1}% / {2:F1}% | Clipped samples {3}{4}",
                        maxPct, maxOutputLeft8 * 100.0 / 127.0, maxOutputRight8 * 100.0 / 127.0,
                        clippedSamples, (maxOutputLeft8 == 127 || maxOutputRight8 == 127) ? " | 100% reached" : "");
                }
                UpdateSettingsPcmMonitor();
            }
            catch { }
        }

        private void OnApex6PcmSaveMeasureClick(object sender, RoutedEventArgs e)
        {
            if (settings == null || string.IsNullOrEmpty(pcmMeasurementGameKey)) return;
            if (!PcmSessionMatches()) { DisconnectPcmMeasurement(); return; }
            if (isMeasuringPcm) StopPcmMeasurement();
            if (!pcmMeasurementComplete || pcmStoppedSnapshot == null ||
                BitConverter.ToUInt32(pcmStoppedSnapshot, 28) != activeMeasurementGen) return;

            var gameKey = pcmMeasurementGameKey;
            var gameSettings = settings.GetApex6PcmGameSettings(gameKey);
            if (gameSettings == null) gameSettings = new Apex6PcmGameSettings();

            if (pcmLiveAccessor != null)
            {
                try
                {
                    ushort left16 = BitConverter.ToUInt16(pcmStoppedSnapshot, 32);
                    ushort right16 = BitConverter.ToUInt16(pcmStoppedSnapshot, 34);
                    ushort max16 = BitConverter.ToUInt16(pcmStoppedSnapshot, 36);
                    uint blocks = BitConverter.ToUInt32(pcmStoppedSnapshot, 40);
                    uint incomplete = BitConverter.ToUInt32(pcmStoppedSnapshot, 72);

                    if (incomplete != 0)
                    {
                        TxtApex6PcmLiveStatus.Text = "Cannot save: measurement had contention data loss. Please remeasure.";
                        return;
                    }

                    if (max16 == 0 || blocks == 0 || pcmStoppedSnapshot[27] != 1)
                    {
                        TxtApex6PcmLiveStatus.Text = "Measurement unavailable: no active PCM or unverified audio format.";
                        return;
                    }
                    if (max16 > 0)
                    {
                        gameSettings.MeasuredReferencePeakAbs16 = max16;
                        gameSettings.MeasuredReferencePercent = max16 / 327.67;
                        gameSettings.ObservedLeftPeakAbs16 = left16;
                        gameSettings.ObservedRightPeakAbs16 = right16;
                        gameSettings.ObservedActiveBlocks = (int)blocks;
                        gameSettings.ObservedStatus = "valid";
                        gameSettings.MeasuredUtc = DateTime.UtcNow.ToString("o");
                    }
                }
                catch (Exception ex)
                {
                    TxtApex6PcmLiveStatus.Text = "PCM save failed: " + ex.Message;
                    return;
                }
            }

            // Saving a reference does not change the selected global/override policy.

            settings.SetApex6PcmGameSettings(gameKey, gameSettings);
            // A measured peak is informational. Do not resend or restart the PCM effect policy.
            settings.Save();

            UpdateApex6PcmDetailPane();
        }

        private void OnApex6PcmManualRefLostFocus(object sender, RoutedEventArgs e)
        {
            if (SelectedGame == null || settings == null || TxtBoxApex6PcmManualRef == null) return;
            if (updatingApex6PcmUi || TxtBoxApex6PcmManualRef.Text == TxtBoxApex6PcmManualRef.Tag as string) return;
            var unit = CmbApex6PcmManualRefUnit.SelectedItem as ComboBoxItem;
            var gameSettings = settings.GetApex6PcmGameSettings(SelectedGame.Normalized) ?? new Apex6PcmGameSettings();
            if (unit == null || !gameSettings.TrySetManualReference(TxtBoxApex6PcmManualRef.Text.Trim(), unit.Tag as string))
            {
                UpdateApex6PcmDetailPane();
                return;
            }
            settings.SetApex6PcmGameSettings(SelectedGame.Normalized, gameSettings);
            QueuePcmEdit();
            UpdateApex6PcmDetailPane();
        }

        private void OnApex6PcmManualRefUnitChanged(object sender, SelectionChangedEventArgs e)
        {
            if (!viewInitialized || updatingApex6PcmUi) return;
            UpdateApex6PcmDetailPane();
        }

        private void OnApex6PcmManualRefKeyDown(object sender, KeyEventArgs e)
        {
            if (e.Key == Key.Enter)
            {
                OnApex6PcmManualRefLostFocus(sender, e);
                e.Handled = true;
            }
        }

        private void OnApex6PcmResetClick(object sender, RoutedEventArgs e)
        {
            if (SelectedGame == null || settings == null) return;
            DisconnectPcmMeasurement();
            settings.SetApex6PcmGameSettings(SelectedGame.Normalized, null);
            QueuePcmEdit();
            UpdateApex6PcmDetailPane();
        }

        #endregion
    }
}
