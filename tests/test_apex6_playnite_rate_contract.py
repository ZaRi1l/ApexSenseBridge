"""Source contracts for the Playnite APEX 6 sample-rate toggle.

The Windows WPF learning suite exercises the shared argument builder; this
contract checks that Playnite actually feeds its saved setting into it.
"""

from pathlib import Path
import unittest


ROOT = Path(__file__).resolve().parents[1]


class PlayniteApex6RateContracts(unittest.TestCase):
    def test_saved_setting_defaults_to_two_kilohertz(self):
        settings = (ROOT / "playnite/ApexSenseBridge/ApexSenseBridgeSettings.cs").read_text()
        self.assertIn("private bool apex6Rate2kHz = true;", settings)
        self.assertIn("public bool Apex6Rate2kHz", settings)

    def test_ui_exposes_reversible_rate(self):
        view = (ROOT / "playnite/ApexSenseBridge/ApexSenseBridgeSettingsView.xaml").read_text()
        self.assertIn("IsChecked=\"{Binding Settings.Apex6Rate2kHz}\"", view)

    def test_launch_propagates_saved_rate(self):
        playnite = (ROOT / "playnite/ApexSenseBridge/ApexSenseBridge.cs").read_text()
        self.assertIn("apex6Rate2k: settings.Settings.Apex6Rate2kHz", playnite)
        shared = (ROOT / "shared/BridgeArguments.cs").read_text()
        self.assertIn('arguments.Add("--apex6-rate");', shared)
        self.assertIn('arguments.Add("1000");', shared)


if __name__ == "__main__":
    unittest.main()
