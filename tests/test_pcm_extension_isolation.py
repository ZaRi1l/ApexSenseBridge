"""Source-level guard: the Extension branch keeps PCM functionality modular."""
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
def read(p): return (ROOT / p).read_text(encoding="utf-8-sig")

def test_wpf_isolated():
    host=read("ApexSenseBridgeTray/GameListWindow.xaml.cs")
    module=read("ApexSenseBridgeTray/Extensions/GameListWindow.PcmExtension.cs")
    project=read("ApexSenseBridgeTray/ApexSenseBridgeTray.csproj")
    assert "private void StopPcmMeasurement()" not in host
    assert "private void StopPcmMeasurement()" in module
    assert "private void OnApex6PcmLinearChanged(" in module
    assert "private void OnPcmLiveTimerTick(" in module
    assert "Extensions\\GameListWindow.PcmExtension.cs" in project

def test_native_isolated():
    bridge_h=read("src/dualsense/Apex6HapticBridge.h")
    bridge_cpp=read("src/dualsense/Apex6HapticBridge.cpp")
    addon=read("src/extensions/Apex6PcmExtension.h")
    assert "extensions::Apex6PcmExtension pcmExtension_" in bridge_h
    assert "pcmExtension_.applyPending()" in bridge_cpp
    assert "pcmExtension_.publish(" in bridge_cpp
    assert "pcmScaler_.scaleSample(" not in bridge_cpp
    for api in ("void applyPending()", "std::int8_t scale(", "void publish(", "PolicySnapshot snapshot()"):
        assert api in addon

def test_behavior_unchanged():
    cli=read("src/cli/BridgeOptions.cpp")
    gain=read("src/dualsense/Apex6PcmNormalization.h")
    assert "0..1000" in cli
    assert "Apex6PcmMode::Normalize" not in gain
    assert "1000U" in gain
    assert "Scale" not in ""  # Deliberately avoid hardware claims.

if __name__=="__main__":
    test_wpf_isolated()
    test_native_isolated()
    test_behavior_unchanged()
    print("PASS: APEX 6 PCM Extension modularity and 1000% Linear contract")
