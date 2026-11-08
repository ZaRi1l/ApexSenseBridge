"""Static source contracts only; not a WPF/IPC runtime integration test."""
from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]

class PcmWpfContracts(unittest.TestCase):
    def test_event_handlers_and_control_names(self):
        tree = ET.parse(ROOT / 'ApexSenseBridgeTray/GameListWindow.xaml')
        source = (ROOT / 'ApexSenseBridgeTray/Extensions/GameListWindow.PcmExtension.cs').read_text(encoding='utf-8-sig')
        names = set()
        for element in tree.iter():
            for key, value in element.attrib.items():
                if key.endswith('}Name'):
                    names.add(value)
                if key in ('Click', 'ValueChanged', 'SelectionChanged', 'KeyDown', 'LostFocus', 'MouseLeftButtonDown') and 'Pcm' in value:
                    self.assertRegex(source, r'void\s+' + re.escape(value) + r'\s*\(')
        for name in set(re.findall(r'\b(?:TxtBox|Slider|Pnl|Prg|Row|Cmb|Chk|Txt|Btn)\w*Pcm\w*', source)):
            self.assertIn(name, names)

    def test_session_mapping_contract(self):
        cpp = (ROOT / 'src/platform/Apex6PcmLiveMetrics.h').read_text()
        cs = (ROOT / 'ApexSenseBridgeTray/Extensions/GameListWindow.PcmExtension.cs').read_text(encoding='utf-8-sig')
        self.assertIn('Local\\\\ApexSenseBridge.Session.', cpp)
        self.assertIn('Local\\\\ApexSenseBridge.Session.', cs)
        self.assertIn('".Apex6Pcm"', cpp)
        self.assertIn('".Apex6Pcm"', cs)
        self.assertIn('PcmSessionMatches()', cs)
        self.assertIn('BitConverter.ToUInt32(pcmStoppedSnapshot, 28) != activeMeasurementGen', cs)
        self.assertIn('using (new PcmGuard(pcmLiveMutex))', cs)
        self.assertIn('Apex6PcmLock lock(mutex_)', cpp)
        self.assertIn('version = 3', cpp)
        self.assertIn('ReadUInt32(4) == PcmLiveVersion', cs)
        self.assertIn('PcmLiveBlockSize = 78', cs)
        self.assertIn('measuredMotorOutputLeftPeak8', cpp)
        self.assertIn('measuredMotorOutputRightPeak8', cpp)
        self.assertIn('PrgApex6PcmOutputL', cs)
        self.assertIn('PrgApex6PcmOutputR', cs)

    def test_typed_linear_values_explicitly_commit_live_policy(self):
        source = (ROOT / 'ApexSenseBridgeTray/Extensions/GameListWindow.PcmExtension.cs').read_text(encoding='utf-8-sig')
        game = source.split('private void OnApex6PcmLinearTextLostFocus(', 1)[1].split('private void OnApex6PcmCompandingTextLostFocus(', 1)[0]
        global_edit = source.split('private void OnGlobalApex6PcmLinearTextLostFocus(', 1)[1].split('private void OnGlobalApex6PcmCompandingTextLostFocus(', 1)[0]
        helper = source.split('private void ApplyGamePcmLinearGain(', 1)[1].split('private void OnApex6PcmLinearChanged(', 1)[0]
        self.assertIn('ApplyGamePcmLinearGain(value)', game)
        self.assertIn('SliderApex6PcmLinear.Value = value;', game)
        self.assertIn('settings.SetApex6PcmGameSettings(', helper)
        self.assertIn('QueuePcmEdit();', helper)
        self.assertIn('settings.GetApex6PcmGlobal().LinearGainPercent = value;', global_edit)
        self.assertIn('QueuePcmEdit();', global_edit)
        self.assertIn('SliderGlobalApex6PcmLinear.Value = value;', global_edit)
        self.assertIn('OnApex6PcmLinearTextKeyDown', source)
        self.assertIn('OnGlobalApex6PcmLinearTextKeyDown', source)

    def test_window_preview_keydown_allows_pcm_enter_to_commit(self):
        host = (ROOT / 'ApexSenseBridgeTray/GameListWindow.xaml.cs').read_text(encoding='utf-8-sig')
        ui = (ROOT / 'ApexSenseBridgeTray/Extensions/GameListWindow.PcmExtension.cs').read_text(encoding='utf-8-sig')
        xaml = (ROOT / 'ApexSenseBridgeTray/GameListWindow.xaml').read_text(encoding='utf-8-sig')
        preview = host.split('private void OnWindowPreviewKeyDown(', 1)[1].split('private bool RevealKeyboardFocus(', 1)[0]
        # Previously ALL TextBoxes were treated as list-search inputs, so the
        # preview event swallowed Enter and the numeric KeyDown never ran.
        self.assertIn('focusedTextBox != TxtSearch && focusedTextBox != TxtSearchLearned', preview)
        self.assertIn('if (key != Key.Down && key != Key.Enter) return;', preview)
        self.assertIn('return;', preview.split('focusedTextBox != TxtSearch && focusedTextBox != TxtSearchLearned', 1)[1].split('if (key != Key.Down', 1)[0])
        self.assertIn('OnApex6PcmLinearTextKeyDown', xaml)
        self.assertIn('OnGlobalApex6PcmLinearTextKeyDown', xaml)
        game = ui.split('private void OnApex6PcmLinearTextLostFocus(', 1)[1].split('private void OnApex6PcmCompandingTextLostFocus(', 1)[0]
        global_edit = ui.split('private void OnGlobalApex6PcmLinearTextLostFocus(', 1)[1].split('private void OnGlobalApex6PcmCompandingTextLostFocus(', 1)[0]
        self.assertIn('SliderApex6PcmLinear.Value = value;', game)
        self.assertIn('ApplyGamePcmLinearGain(value);', game)
        self.assertIn('pcmEdits?.Flush();', game)
        self.assertIn('SliderGlobalApex6PcmLinear.Value = value;', global_edit)
        self.assertIn('pcmEdits?.Flush();', global_edit)
        self.assertIn('e.Key == Key.Enter', ui)

    def test_readonly_recommendation_and_maximum_output_display(self):
        source = (ROOT / 'ApexSenseBridgeTray/Extensions/GameListWindow.PcmExtension.cs').read_text(encoding='utf-8-sig')
        ui = (ROOT / 'ApexSenseBridgeTray/GameListWindow.xaml').read_text(encoding='utf-8-sig')
        model = (ROOT / 'ApexSenseBridgeTray/Models/TraySettings.cs').read_text(encoding='utf-8-sig')
        self.assertIn('GetRecommendedLinearGainPercent()', model)
        self.assertIn('Math.Ceiling(32767.0 * 100.0 / Math.Min(32768, peak))', model)
        self.assertIn('TxtApex6PcmLinearRecommendation', ui)
        self.assertIn('UpdateApex6PcmLinearRecommendation(policy)', source)
        self.assertIn('TxtApex6PcmMeasuredOutputPeak', ui)
        self.assertIn('UpdateApex6PcmMeasuredOutputPeak(boostedLeftPeak8, boostedRightPeak8)', source)
        self.assertIn('UpdateApex6PcmMeasuredOutputPeak(maxOutputLeft8, maxOutputRight8)', source)
        readonly = source.split('private void UpdateApex6PcmLinearRecommendation(', 1)[1].split('private void UpdateApex6PcmMeasuredOutputPeak(', 1)[0]
        self.assertNotIn('QueuePcmEdit', readonly)
        self.assertNotIn('SetApex6PcmGameSettings', readonly)

    def test_release_checks_remain_enabled(self):
        cmake = (ROOT / 'CMakeLists.txt').read_text()
        native = (ROOT / 'tests/test_pcm_header_standalone.cpp').read_text()
        self.assertNotRegex(native, r'\bassert\s*\(')
        self.assertIn('CHECK(frames.load() >= 10000)', native)
        self.assertIn('PRIVATE NDEBUG', cmake)
        self.assertIn('/UNDEBUG', cmake)
        self.assertIn('pcm_check_release_failure', cmake)

if __name__ == '__main__':
    unittest.main()
