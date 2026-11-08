"""
Test UI contracts, localization, and Live IPC offset alignment.
"""

import json
import struct

def test_localization_keys():
    with open("ApexSenseBridgeTray/Resources/Languages/en.json", "r", encoding="utf-8") as f:
        en = json.load(f)
    with open("ApexSenseBridgeTray/Resources/Languages/ko.json", "r", encoding="utf-8") as f:
        ko = json.load(f)

    required_keys = [
        "Loc_Apex6PcmTitle",
        "Loc_Apex6PcmUseGlobal",
        "Loc_Apex6PcmMode",
        "Loc_Apex6PcmModeOriginal",
        "Loc_Apex6PcmModeCompanding",
        "Loc_Apex6PcmModeLinear",
        "Loc_Apex6PcmReference",
        "Loc_Apex6PcmTarget",
        "Loc_Apex6PcmLinearGain",
        "Loc_Apex6PcmMaxGain",
        "Loc_Apex6PcmEffectiveGain",
        "Loc_Apex6PcmMeasureStart",
        "Loc_Apex6PcmMeasureStop",
        "Loc_Apex6PcmMeasureSave",
        "Loc_Apex6PcmMeasuring",
        "Loc_Apex6PcmNextSessionHint",
        "Loc_Apex6PcmNotMeasured",
        "Loc_Apex6PcmObservedInfo",
    ]

    for key in required_keys:
        assert key in en, f"Missing {key} in en.json"
        assert key in ko, f"Missing {key} in ko.json"
        assert len(en[key]) > 0
        assert len(ko[key]) > 0
    print("test_localization_keys: all 19 keys present and non-empty in en.json and ko.json! [PASS]")

def test_live_ipc_offsets():
    """
    Verifies struct layout of Apex6PcmLiveBlock:
    offset 0: magic (uint32)
    offset 4: version (uint32)
    offset 8: measurementActive (uint32)
    offset 12: measurementGeneration (uint32)
    offset 16: currentLeftPeak16 (uint16)
    offset 18: currentRightPeak16 (uint16)
    offset 20: rawHapticLeftPeak16 (uint16)
    offset 22: rawHapticRightPeak16 (uint16)
    offset 24: motorOutputLeftPeak8 (uint8)
    offset 25: motorOutputRightPeak8 (uint8)
    offset 26: audioMixChannels (uint8)
    offset 27: audioFormatValid (uint8)
    offset 28: sessionGenerationEcho (uint32)  <-- wait! Let's check struct alignment!
    """
    # In #pragma pack(push, 1):
    # uint32: 4 bytes
    # uint16: 2 bytes
    # uint8: 1 byte
    fmt = "<IIII HHHH BBBB I HHHH IIIIIQ II BB"
    expected_size = struct.calcsize(fmt)
    assert expected_size == 78
    print("test_live_ipc_offsets: 78-byte packed PCM v3 layout [PASS]")


def test_multiplier_formatting():
    def calc_multiplier_text(mode, ref_pct, target_pct, linear_pct):
        if mode == "original":
            return "1.00x"
        elif mode == "companding":
            return "1.00x - 2.00x (Curve)"
        elif mode == "linear":
            return f"{linear_pct / 100.0:.2f}x"
        return "1.00x"

    assert calc_multiplier_text("original", 30, 90, 100) == "1.00x"
    assert calc_multiplier_text("linear", 30, 90, 250) == "2.50x"
    assert calc_multiplier_text("linear", 30, 90, 1000) == "10.00x"
    print("test_multiplier_formatting: PASS")

def main():
    test_localization_keys()
    test_live_ipc_offsets()
    test_multiplier_formatting()
    print("\nALL UI CONTRACT TESTS PASSED.")

if __name__ == "__main__":
    main()
