"""Source-level regression guards for session continuity and PCM UI behavior.

These checks cannot emulate physical HID enumeration, mouse hotplug or game output.
They guard the intended host/Extension boundaries and stop conditions.
"""
from pathlib import Path
import unittest

ROOT = Path(__file__).resolve().parents[1]

def src(path):
    return (ROOT / path).read_text(encoding="utf-8-sig")

def method(source, start, end):
    return source.split(start, 1)[1].split(end, 1)[0]

class SessionContinuityContracts(unittest.TestCase):
    def setUp(self):
        self.host = src("ApexSenseBridgeTray/GameListWindow.xaml.cs")
        self.ui = src("ApexSenseBridgeTray/Extensions/GameListWindow.PcmExtension.cs")
        self.monitor = src("ApexSenseBridgeTray/Services/ProcessMonitorService.cs")

    def test_ui_selection_does_not_disconnect_or_stop_engine(self):
        pane = method(self.host, "private void UpdateDetailPane()", "private void UpdateExecutableChoices()")
        self.assertIn("UpdateApex6PcmDetailPane();", pane)
        self.assertNotIn("DisconnectPcmMeasurement();", pane)
        self.assertNotIn("StopSession(", pane)
        self.assertNotIn("StartSession(", pane)
        self.assertNotIn("ForceCheck(", pane)

    def test_measurement_identity_tracks_actual_session_not_selected_ui(self):
        check = method(self.ui, "private bool PcmSessionMatches()", "private void DisconnectPcmMeasurement()")
        self.assertIn("ActiveSessionToken", check)
        self.assertIn("ActiveGameKey", check)
        self.assertNotIn("SelectedGame", check)
        self.assertNotIn("Window", check)

    def test_timed_measurement_only_checks_active_session(self):
        poll = method(self.ui, "private void OnPcmLiveTimerTick(", "private void OnApex6PcmSaveMeasureClick(")
        self.assertIn("!PcmSessionMatches()", poll)
        self.assertIn("ReadArray(", poll)
        self.assertNotIn("StopPcmMeasurement(", poll)
        self.assertNotIn("StopSession(", poll)
        self.assertNotIn("ElapsedMilliseconds", poll)

    def test_user_stop_auto_saves_without_changing_manual_gain(self):
        toggle = method(self.ui, "private void OnApex6PcmStartMeasureClick(", "private void StopPcmMeasurement()")
        self.assertIn("pcmLiveTimer.Start();", toggle)
        self.assertIn("StopPcmMeasurement();", toggle)
        self.assertIn("OnApex6PcmSaveMeasureClick(sender, e);", toggle)
        save = method(self.ui, "private void OnApex6PcmSaveMeasureClick(", "private void OnApex6PcmManualRefLostFocus(")
        self.assertIn("pcmMeasurementGameKey", save)
        self.assertIn("settings.Save();", save)
        self.assertNotIn("LinearGainPercent =", save)
        self.assertNotIn("RefreshPcmPolicy", save)

    def test_foreground_switch_does_not_directly_stop_running_game(self):
        focus = method(self.monitor, "var hwnd = NativeMethods.GetForegroundWindow();", "private bool ScanRunningProcesses(")
        self.assertIn('CheckCandidateProcess(', focus)
        self.assertNotIn("StopSession(", focus)
        self.assertNotIn("HandleProcessStopped(", focus)
        self.assertNotIn("DisconnectPcmMeasurement(", focus)

    def test_transient_process_query_errors_do_not_end_session(self):
        pruning = method(self.monitor, "private void PruneExitedTrackedProcesses()", "private bool IsTrackedProcess(")
        self.assertIn("catch (Exception ex)", pruning)
        self.assertIn("hasExited = false;", pruning)
        self.assertIn("if (hasExited)", pruning)

    def test_controller_probe_does_not_steal_active_engine_hid(self):
        det = src("ApexSenseBridgeTray/Services/ControllerDetectionService.cs")
        scan = method(det, "internal void Scan(object state)", "private void Downgrade(")
        self.assertIn("activeSessionReader()", scan)
        self.assertIn("ownerProbe()", scan)
        self.assertIn("verifiedCandidate = string.Empty;", scan)
        self.assertIn("return;", scan)

    def test_extension_does_not_own_extra_hid_writer(self):
        module = src("src/extensions/Apex6PcmExtension.h")
        native = src("src/dualsense/Apex6HapticBridge.cpp")
        self.assertNotIn("writeApex6Haptics", module)
        self.assertIn("pcmExtension_.applyPending();", native)
        self.assertIn("pcmExtension_.publish(", native)
        self.assertIn("output[index].leftGrip = pcmExtension_.scale(", native)
        self.assertIn("output[index].leftGrip = scaleEffectStrength(", native)

if __name__ == "__main__":
    unittest.main(verbosity=2)
