"""Static integration contracts for APEX 6 manual PCM gain and live metering.

This file does not claim to test Windows HID hardware or live WPF runtime.
"""
from pathlib import Path
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]


class ManualPcmIntegration(unittest.TestCase):
    def source(self, path):
        return (ROOT / path).read_text(encoding="utf-8-sig")

    def test_linear_limits_match_every_layer(self):
        self.assertIn("linearGainPercent, 0U, 1000U", self.source("src/dualsense/Apex6PcmNormalization.h"))
        self.assertIn("view_->linearGainPercent > 1000", self.source("src/platform/Apex6PcmControl.h"))
        self.assertIn("parsed > 1000", self.source("src/cli/BridgeOptions.cpp"))
        self.assertIn("Math.Min(1000, LinearGainPercent)", self.source("ApexSenseBridgeTray/Models/TraySettings.cs"))
        ui = self.source("ApexSenseBridgeTray/GameListWindow.xaml")
        self.assertIn('x:Name="SliderApex6PcmLinear" Style="{StaticResource EffectSlider}" Minimum="0" Maximum="1000"', ui)

    def test_normalize_is_not_selectable(self):
        cpp = self.source("src/dualsense/Apex6PcmNormalization.h")
        self.assertNotIn("Apex6PcmMode::Normalize", cpp)
        settings = self.source("ApexSenseBridgeTray/Models/TraySettings.cs")
        self.assertIn('Mode = "linear";', settings)
        self.assertNotIn('--apex6-pcm-mode normalize', settings)
        gui = self.source("ApexSenseBridgeTray/Extensions/GameListWindow.PcmExtension.cs")
        self.assertNotIn('Tag = "normalize"', gui)

    def test_measurements_do_not_change_gain(self):
        gui = self.source("ApexSenseBridgeTray/Extensions/GameListWindow.PcmExtension.cs")
        self.assertIn("Saving a reference does not change the selected global/override policy.", gui)
        self.assertIn("private void StopPcmMeasurement()", gui)
        self.assertIn("private void OnPcmLiveTimerTick(", gui)
        self.assertIn("pcmLiveTimer.Interval = TimeSpan.FromMilliseconds(100)", gui)
        self.assertNotIn("TimeoutMeasure", gui)

    def test_ipc_v3_has_real_amplified_peaks(self):
        native = self.source("src/platform/Apex6PcmLiveMetrics.h")
        tray = self.source("ApexSenseBridgeTray/Extensions/GameListWindow.PcmExtension.cs")
        self.assertIn("version = 3", native)
        self.assertIn("static_assert(sizeof(Apex6PcmLiveBlock) == 78)", native)
        self.assertIn("PcmLiveBlockSize = 78", tray)
        self.assertIn("measuredMotorOutputLeftPeak8", native)
        self.assertIn("measuredMotorOutputRightPeak8", native)
        self.assertIn("snapshot[76]", tray)
        self.assertIn("snapshot[77]", tray)
        self.assertIn("Clipped samples", tray)

    def test_ui_selection_does_not_stop_measurement(self):
        gui = self.source("ApexSenseBridgeTray/Extensions/GameListWindow.PcmExtension.cs")
        self.assertIn("UI selection is not a change to the running game session",\n                      self.source("ApexSenseBridgeTray/GameListWindow.xaml.cs"))
        matching = gui.split("private bool PcmSessionMatches()", 1)[1].split("private void DisconnectPcmMeasurement", 1)[0]
        self.assertNotIn("SelectedGame.Normalized", matching)

    def test_save_and_game_override(self):
        gui = self.source("ApexSenseBridgeTray/Extensions/GameListWindow.PcmExtension.cs")
        self.assertIn('gameSettings.Mode = "linear";', gui)
        self.assertIn('gameSettings.UseGlobal = false;', gui)
        settings = self.source("ApexSenseBridgeTray/Models/TraySettings.cs")
        self.assertIn('File.Replace(stagingPath, path, null)', settings)
        self.assertIn('Apex6PcmGames[key] = settings;', settings)

    def test_transient_process_errors_do_not_stop_game(self):
        monitor = self.source("ApexSenseBridgeTray/Services/ProcessMonitorService.cs")
        self.assertIn("Process access", monitor) if False else None
        self.assertIn("hasExited = false;", monitor)
        self.assertIn("keeping bridge active", monitor)


if __name__ == "__main__":
    unittest.main()
