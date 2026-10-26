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
        self.assertIn('pcmLiveAccessor.ReadUInt32(28) != activeMeasurementGen', cs)

if __name__ == '__main__':
    unittest.main()
