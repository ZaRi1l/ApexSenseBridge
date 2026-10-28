"""Tray PCM settings persistence contracts (manual per-game Linear)."""
from pathlib import Path
import json
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def test_default_and_legacy_migration():
    settings = (ROOT / "ApexSenseBridgeTray/Models/TraySettings.cs").read_text(encoding="utf-8")
    assert 'UseGlobal = false;' in settings
    assert 'Mode = "linear";' in settings
    assert 'if (Mode != "original" && Mode != "companding" && Mode != "linear")' in settings
    assert "Math.Min(1000, LinearGainPercent)" in settings
    assert "--apex6-pcm-mode normalize" not in settings


def test_game_specific_automatic_save():
    ui = (ROOT / "ApexSenseBridgeTray/Extensions/GameListWindow.PcmExtension.cs").read_text(encoding="utf-8")
    ui += (ROOT / "ApexSenseBridgeTray/GameListWindow.xaml.cs").read_text(encoding="utf-8")
    # Both slider and numeric entry use the same explicit save/live-apply helper.
    helper_start = ui.index("private void ApplyGamePcmLinearGain")
    slider_start = ui.index("private void OnApex6PcmLinearChanged", helper_start)
    numeric_start = ui.index("private void OnApex6PcmLinearTextLostFocus", slider_start)
    numeric_end = ui.index("private void OnApex6PcmCompandingTextLostFocus", numeric_start)
    handler = ui[helper_start:slider_start]
    slider_handler = ui[slider_start:ui.index("private void OnApex6PcmCompandingChanged", slider_start)]
    numeric_handler = ui[numeric_start:numeric_end]
    assert 'gameSettings.UseGlobal = false;' in handler
    assert 'gameSettings.Mode = "linear";' in handler
    assert 'gameSettings.LinearGainPercent = value;' in handler
    assert 'settings.SetApex6PcmGameSettings(game.Normalized, gameSettings);' in handler
    assert 'QueuePcmEdit();' in handler
    assert 'ApplyGamePcmLinearGain((int)SliderApex6PcmLinear.Value);' in slider_handler
    assert 'ApplyGamePcmLinearGain(value);' in numeric_handler
    assert "pcmEdits.Flush()" in ui or "pcmEdits.Flush()" in (ROOT / "ApexSenseBridgeTray/GameListWindow.xaml.cs").read_text(encoding="utf-8")
    assert "settings.Save(); sessionManager?.RefreshPcmPolicy(settings);" in ui


def test_game_isolation_and_roundtrip():
    profile = {"Apex6PcmGames": {
        "zenless-zone-zero": {"UseGlobal": False, "Mode": "linear", "LinearGainPercent": 1000},
        "another-game": {"UseGlobal": False, "Mode": "linear", "LinearGainPercent": 200}}}
    with tempfile.TemporaryDirectory() as directory:
        path = Path(directory) / "tray_settings.json"
        path.write_text(json.dumps(profile), encoding="utf-8")
        loaded = json.loads(path.read_text(encoding="utf-8"))
        assert loaded["Apex6PcmGames"]["zenless-zone-zero"]["LinearGainPercent"] == 1000
        assert loaded["Apex6PcmGames"]["another-game"]["LinearGainPercent"] == 200


def test_auto_save_is_atomic():
    code = (ROOT / "ApexSenseBridgeTray/Models/TraySettings.cs").read_text(encoding="utf-8")
    assert 'File.WriteAllText(stagingPath, json);' in code
    assert 'File.Replace(stagingPath, path, null);' in code
    assert 'File.Move(stagingPath, path);' in code


if __name__ == "__main__":
    test_default_and_legacy_migration()
    test_game_specific_automatic_save()
    test_game_isolation_and_roundtrip()
    test_auto_save_is_atomic()
    print("PASS: game-specific Linear persistence, legacy migration and atomic write")
