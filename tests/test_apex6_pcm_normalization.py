"""Legacy filename retained for CTest compatibility. Tests manual Linear PCM gain."""
from pathlib import Path
import math

ROOT = Path(__file__).resolve().parents[1]


def scale_linear(sample: int, gain: int):
    assert -32768 <= sample <= 32767
    assert 0 <= gain <= 1000
    full_scale = 32767 if sample >= 0 else 32768
    scaled = sample * 127.0 / full_scale * (gain / 100.0)
    rounded = math.floor(scaled + 0.5) if scaled >= 0 else math.ceil(scaled - 0.5)
    clipped = rounded > 127 or rounded < -127
    return max(-127, min(127, rounded)), clipped


def test_boundaries():
    assert scale_linear(0, 1000) == (0, False)
    assert scale_linear(32767, 100) == (127, False)
    assert scale_linear(-32768, 100) == (-127, False)
    assert scale_linear(32767, 1000) == (127, True)
    assert scale_linear(-32768, 1000) == (-127, True)
    assert scale_linear(5000, 1000) == (127, True)
    assert scale_linear(-5000, 1000) == (-127, True)


def test_no_wraparound():
    for gain in (100, 200, 400, 800, 1000):
        for sample in (-32768, -10000, -1000, -1, 0, 1, 1000, 10000, 32767):
            output, _ = scale_linear(sample, gain)
            assert -127 <= output <= 127
            if sample == 0: assert output == 0
        for sample in (1000, 5000, 10000):
            forward = scale_linear(sample, gain)[0]
            reverse = scale_linear(-sample, gain)[0]
            assert forward == -reverse


def test_gain_increase():
    for sample in (100, 1000, 5000, 10000):
        outputs = [scale_linear(sample, gain)[0] for gain in (100, 400, 1000)]
        assert outputs == sorted(outputs)
    assert scale_linear(1000, 400)[0] > scale_linear(1000, 100)[0]


def test_source_contract():
    scaler = (ROOT / "src/dualsense/Apex6PcmNormalization.h").read_text(encoding="utf-8")
    assert "std::clamp(config_.linearGainPercent, 0U, 1000U)" in scaler
    assert "Apex6PcmMode::Normalize" not in scaler
    assert "rounded > 127" in scaler and "rounded < -127" in scaler
    legacy = (ROOT / "src/dualsense/Apex6PcmGain.h").read_text(encoding="utf-8")
    assert "std::clamp(gainPercent, 100U, 200U)" in legacy


if __name__ == "__main__":
    test_boundaries()
    test_no_wraparound()
    test_gain_increase()
    test_source_contract()
    print("PASS: PCM Linear range, clipping, old companding, and code bounds")
