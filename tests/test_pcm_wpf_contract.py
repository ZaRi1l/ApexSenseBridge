"""Static source contracts only; not a WPF/IPC runtime integration test."""
from pathlib import Path
import re
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]

class PcmWpfContracts(unittest.TestCase):
    def test_event_handlers_and_control_names(self):
        tree = ET.parse(ROOT / 'ApexSenseBridgeTray/GameListWindow.xaml')
        source = (ROOT / 'ApexSenseBridgeTray/GameListWindow.xaml.cs').read_text(encoding='utf-8-sig')
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
        cs = (ROOT / 'ApexSenseBridgeTray/GameListWindow.xaml.cs').read_text(encoding='utf-8-sig')
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
