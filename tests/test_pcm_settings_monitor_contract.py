"""Static guardrails for the upstream-friendly APEX 6 PCM settings UX.

The monitor must follow the actual engine session, keep existing game-detail
features, and never modify unrelated controller/PCM behavior. This is not a
hardware test or a substitute for a WPF build.
"""
from pathlib import Path
import json
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]

def read(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")

class PcmSettingsMonitorContracts(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.xaml = read("ApexSenseBridgeTray/GameListWindow.xaml")
        cls.host = read("ApexSenseBridgeTray/GameListWindow.xaml.cs")
        cls.pcm = read("ApexSenseBridgeTray/Extensions/GameListWindow.PcmExtension.cs")
        cls.settings = read("ApexSenseBridgeTray/Models/TraySettings.cs")

    def test_wpf_markup_and_control_names(self):
        ET.fromstring(self.xaml)
        for name in (
            "PnlApex6PcmGlobalSettings", "CmbGlobalApex6PcmMode",
            "SliderGlobalApex6PcmLinear", "BtnSettingsPcmMeasure",
            "PnlSettingsPcmMeter", "PrgSettingsPcmInputL", "PrgSettingsPcmInputR",
            "PrgSettingsPcmOutputL", "PrgSettingsPcmOutputR",
            "TxtSettingsPcmStatus", "TxtSettingsPcmPeak",
            "ChkApex6PcmUseGlobal", "CmbApex6PcmMode",
            "BtnApex6PcmStartMeasure", "PrgApex6PcmLiveL",
        ):
            self.assertIn(f'x:Name="{name}"', self.xaml)

    def test_model_scoped_defaults_not_global_vibration_slider(self):
        self.assertIn('PnlApex6PcmGlobalSettings.Visibility = lastControllerStatus == "apex6"', self.host)
        self.assertIn('UpdateGlobalApex6PcmUi();', self.host)
        self.assertIn('SliderGlobalApex6PcmLinear', self.host)
        self.assertIn('CmbGlobalApex6PcmMode', self.host)
        self.assertIn('VibrationStrengthPercent', self.settings)
        self.assertIn('Apex6PcmGlobal', self.settings)
        self.assertIn('Apex6PcmGames', self.settings)

    def test_settings_measurement_uses_actual_engine_session(self):
        method = self.pcm.split("private void OnSettingsPcmMeasureClick(", 1)[1].split(
            "private void TogglePcmMeasurement(", 1)[0]
        self.assertIn('sessionManager.ActiveGameKey', method)
        self.assertNotIn("SelectedGame", method)
        self.assertIn('TogglePcmMeasurement(gameKey, sender, e);', method)
        self.assertIn('pcmMeasurementGameKey = requestedGameKey;', self.pcm)
        self.assertIn('PcmSessionMatches()', self.pcm)

    def test_no_cross_game_peak_display_contamination(self):
        self.assertIn('string.Equals(SelectedGame.Normalized, pcmMeasurementGameKey,', self.pcm)
        self.assertIn('settings.SetApex6PcmGameSettings(gameKey, gameSettings);', self.pcm)
        self.assertIn('UpdateSettingsPcmMonitor();', self.pcm)
        self.assertIn('PnlSettingsPcmMeter.Visibility = (isMeasuringPcm || pcmStoppedSnapshot != null)', self.pcm)
        self.assertIn('x:Name="PnlSettingsPcmMeter" Visibility="Collapsed"', self.xaml)

    def test_game_override_and_existing_meter_preserved(self):
        self.assertIn('gameSettings.UseGlobal = false;', self.pcm)
        self.assertIn('ResolveApex6PcmPolicy', self.settings)
        self.assertIn('private void OnApex6PcmStartMeasureClick(', self.pcm)
        self.assertIn('private void StopPcmMeasurement()', self.pcm)
        self.assertIn('private void OnPcmLiveTimerTick(', self.pcm)
        self.assertIn('private void OnApex6PcmSaveMeasureClick(', self.pcm)
        self.assertIn('private void UpdateApex6PcmLinearRecommendation(', self.pcm)

    def test_new_strings_in_all_locales(self):
        keys = (
            "Loc_Apex6PcmMonitorTitle", "Loc_Apex6PcmInputChannels",
            "Loc_Apex6PcmOutputChannels", "Loc_Apex6PcmGlobalScopeHint",
            "Loc_Apex6PcmMonitorIdle",
        )
        for lang in ("en", "ko", "ja", "es", "fr", "pt", "ru", "vi", "zh"):
            entries = json.loads(read(f"ApexSenseBridgeTray/Resources/Languages/{lang}.json"))
            for key in keys:
                self.assertTrue(entries.get(key), f"{lang}: {key}")

if __name__ == "__main__":
    unittest.main(verbosity=2)
