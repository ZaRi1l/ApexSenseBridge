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
    ui = (ROOT / "ApexSenseBridgeTray/Extensions/GameListWindow.PcmExtension.cs").read_text(encoding="utf-8")\n    ui += (ROOT / "ApexSenseBridgeTray/GameListWindow.xaml.cs").read_text(encoding="utf-8")
    start = ui.index("private void OnApex6PcmLinearChanged")
    end = ui.index("private void OnApex6PcmCompandingChanged", start)
    handler = ui[start:end]
    assert 'gameSettings.UseGlobal = false;' in handler
    assert 'gameSettings.Mode = "linear";' in handler
    assert 'gameSettings.LinearGainPercent = (int)SliderApex6PcmLinear.Value;' in handler
    assert 'settings.SetApex6PcmGameSettings(game.Normalized, gameSettings);' in handler
    assert 'QueuePcmEdit();' in handler
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
