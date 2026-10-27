"""Manual APEX 6 PCM CLI contract: Original, Companding, and Linear (0..1000%)."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class BridgeOptionsParser:
    def __init__(self):
        self.route_rumble = False
        self.apex6_haptic_gain_percent = 100
        self.apex6_haptic_gain_explicit = False
        self.apex6_pcm_mode = "original"
        self.apex6_pcm_linear_gain = 100
        self.apex6_pcm_options_explicit = False

    def parse(self, args):
        i = 0
        while i < len(args):
            val = args[i]
            if val == "--rumble":
                self.route_rumble = True
                i += 1
                continue
            if val not in ("--apex6-haptic-gain", "--apex6-pcm-mode",
                           "--apex6-pcm-linear-gain-percent",
                           "--apex6-pcm-companding-gain-percent"):
                return False, "Unknown bridge-triggers option: " + val
            i += 1
            if i >= len(args):
                return False, "Missing value for " + val
            raw = args[i]
            if val == "--apex6-pcm-mode":
                if raw not in ("original", "linear", "companding"):
                    return False, "Invalid APEX 6 PCM mode"
                self.apex6_pcm_mode = raw
                self.apex6_pcm_options_explicit = True
            else:
                try:
                    if raw.startswith("-") or not raw.isdigit():
                        raise ValueError()
                    gain = int(raw)
                except ValueError:
                    return False, "Invalid integer gain"
                if val == "--apex6-pcm-linear-gain-percent":
                    if not 0 <= gain <= 1000:
                        return False, "Linear gain must be 0..1000"
                    self.apex6_pcm_linear_gain = gain
                    self.apex6_pcm_options_explicit = True
                elif val == "--apex6-pcm-companding-gain-percent":
                    if not 100 <= gain <= 200:
                        return False, "Companding gain must be 100..200"
                    self.apex6_pcm_options_explicit = True
                else:
                    if not 100 <= gain <= 200:
                        return False, "Legacy gain must be 100..200"
                    self.apex6_haptic_gain_explicit = True
                    self.apex6_haptic_gain_percent = gain
            i += 1
        if self.apex6_haptic_gain_explicit and self.apex6_pcm_options_explicit:
            return False, "Cannot combine legacy --apex6-haptic-gain with --apex6-pcm-* options."
        if (self.apex6_haptic_gain_explicit or
            (self.apex6_pcm_options_explicit and self.apex6_pcm_mode != "original")) and not self.route_rumble:
            return False, "--apex6-pcm-* options require --rumble."
        if self.apex6_haptic_gain_explicit:
            self.apex6_pcm_mode = "companding"
        return True, ""


def test_linear_limits():
    for gain in (0, 100, 400, 1000):
        parser = BridgeOptionsParser()
        ok, reason = parser.parse(["--rumble", "--apex6-pcm-mode", "linear",
                                   "--apex6-pcm-linear-gain-percent", str(gain)])
        assert ok, reason
        assert parser.apex6_pcm_linear_gain == gain
    for gain in ("-1", "1001", "999999999999999999999999"):
        assert not BridgeOptionsParser().parse(
            ["--rumble", "--apex6-pcm-linear-gain-percent", gain])[0]


def test_removed_normalize_rejected():
    for opts in (["--rumble", "--apex6-pcm-mode", "normalize"],
                 ["--rumble", "--apex6-pcm-reference-percent", "30"],
                 ["--rumble", "--apex6-pcm-target-percent", "90"]):
        assert not BridgeOptionsParser().parse(opts)[0]


def test_legacy_unchanged():
    parser = BridgeOptionsParser()
    assert parser.parse(["--rumble", "--apex6-haptic-gain", "150"])[0]
    assert parser.apex6_pcm_mode == "companding"
    assert not BridgeOptionsParser().parse([
        "--rumble", "--apex6-haptic-gain", "150", "--apex6-pcm-mode", "linear"])[0]


def test_source_contract():
    source = (ROOT / "src/cli/BridgeOptions.cpp").read_text(encoding="utf-8")
    assert "parsed > 1000" in source
    assert "0..1000" in source
    assert 'original|companding|linear]' in source
    assert '"--apex6-pcm-reference-percent"' not in source


if __name__ == "__main__":
    test_linear_limits()
    test_removed_normalize_rejected()
    test_legacy_unchanged()
    test_source_contract()
    print("PASS: manual PCM CLI, legacy compatibility, and 1000% boundaries")
