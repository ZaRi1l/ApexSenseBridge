# APEX 6 PCM Extension — architecture / upstream update notes

## Branch purpose

- **stable:** Keep the previously validated, integrated implementation unchanged.
- **Extension:** Rebase feature development on the latest `ReynArts/ApexSenseBridge` upstream stable commit, preserve its upstream `data/supported_games.json`, and move our PCM feature ownership out of large host classes.
- Never open upstream pull requests without explicit authorization.

## What this extension is

This is an **in-process, source-level extension module** compiled with ApexSenseBridge — **not** an independently installable DLL/plugin. The upstream project does not expose a public dynamic plugin interface for changing the quadraphonic DualSense haptic PCM path.

- `src/extensions/Apex6PcmExtension.h`: owns per-game runtime gain, Original/Companding/Linear scaling, live policy IPC, v3 real-time PCM metering IPC and thread-safe stats snapshots.
- `src/dualsense/Apex6HapticBridge.{h,cpp}`: retains existing HID transport, waveform scheduling and trigger behavior. Calls the PCM extension only at the native PCM transform, policy and telemetry boundaries.
- `ApexSenseBridgeTray/Extensions/GameListWindow.PcmExtension.cs`: isolates the ~800 lines of WPF PCM UI/event handlers and continuous measurement logic in a partial class; the main `GameListWindow.xaml.cs` is no longer the home for those features.
- `ApexSenseBridgeTray/Models/TraySettings.cs`: retains game-specific Linear gain persistence and the old-settings migration logic.
- `ApexSenseBridgeTray/Services/Apex6PcmControlClient.cs`: sends changes to the active game without stopping/restarting the engine.
- `src/platform/Apex6PcmLiveMetrics.h` / `src/platform/Apex6PcmControl.h`: existing non-blocking shared-memory transport.

Original (100%) leaves game PCM unchanged, Linear ranges 0–1000%, and the original Companding code remains available. **No automatic Normalize or recommended reference gain**. Metering starts/stops at user request and is separate from applied gain. The amplified peak is the output just before the HID write, not a measurement of physical motor force.

## Per-game PCM vs. global vibration strength

The game-detail PCM panel now has an **Apply global vibration strength to PCM** switch (Korean: `PCM에 전체 진동 크기 적용`).

- **On (default):** Native PCM passes through the ordinary APEX 6 global vibration strength scaling, exactly as before. Existing settings JSON missing this field remains **On**.
- **Off:** Native PCM channels bypass only that global strength step. The game's manual Linear/Companding gain and 8-bit clipping limits still apply. Ordinary rumble, synthesized envelopes, triggers and other controllers remain on their existing paths.
- The option is stored per game and can be edited while the engine is running through the PCM control IPC v2 (`Apex6PcmControlBlock`, 312 bytes). Both engine and Tray must be rebuilt together; old v1 clients cannot modify a v2 engine.
- The live **amplified output** meter displays the final digital PCM peak after the selected mode, clipping and whichever global strength policy is active; this is **not a physical motor force measurement**.
- Global strength at 0% does **not** mute native PCM when the switch is Off. Other vibration channels continue using global strength.

## Upstream compatibility audit (important)

This is **not** a drop-in binary DLC. Moving PCM code into `src/extensions/Apex6PcmExtension.h` and WPF event logic into a partial class reduces merge overlap, but it does not completely decouple the original repository.

The likely three-way merge conflict surfaces are:

| Upstream surface | Extension coupling | Risk if upstream modifies the same region |
|---|---|---|
| `src/dualsense/Apex6HapticBridge.{h,cpp}` | Calls PCM adapter around render, final scaling, telemetry | Medium/high |
| `ApexSenseBridgeTray/GameListWindow.xaml` | Adds the PCM controls and live meter | Medium/high |
| `ApexSenseBridgeTray/Models/TraySettings.cs` | Stores per-game gain/strength/measurement fields | Medium |
| `ApexSenseBridgeTray/Services/EngineSessionManager.cs` | Supplies active game/token and live update IPC | Medium |
| `src/cli/BridgeOptions.{h,cpp}`, `BridgeCommand.cpp`, `shared/BridgeArguments.cs` | Startup arguments/policy | Medium |
| `CMakeLists.txt` / Tray project | Compiles extension and support code | Medium |
| `src/extensions/*`, Tray `Extensions/*`, `src/platform/Apex6Pcm*` | Extension-owned implementation | Low if upstream never adopts same names |

The current extension was created directly from upstream `stable`, followed by grafting PCM changes and new isolated modules. **Being upstream-based does not guarantee painless merges**; a future upstream change to the same renderer/UI/protocol can still need manual conflict resolution.

For upgrades, synchronize into `Extension` only, keep a temporary safety branch/tag, inspect upstream commits and file overlap, merge (never overwrite user settings or changes blindly), and run Windows build + native, Tray, IPC and UI regression tests. Do **not** send an upstream PR without the owner's explicit consent.

## Building / testing

Build both parts of the same checkout using:

```powershell
git switch Extension
git pull origin Extension
.\scripts\build-windows.ps1
.\scripts\build-tray-app.ps1
ctest --test-dir .\build-win -C Release --output-on-failure
```

The `Extension` branch runs `.github/workflows/extension-validation.yml` on Windows 2022. Real controller, USB/2.4GHz switching, mouse-click/Alt-Tab and game-session disconnect checks still require a physical controller; CI tests cannot prove the actual device never reconnects.

## Future upstream sync

1. Read the new `ReynArts/ApexSenseBridge:stable` diff; never assume all upstream updates are related to PCM.
2. Merge **upstream into Extension**, resolving only connection points with the extension modules and preserving the upstream game's other behavior.
3. Re-run Windows builds, native tests, WPF tests and the game-specific gain persistence tests.
4. Compare `stable` and `Extension` with repeatable haptic scenes, alt-tab/focus changes, and longer manual measurement sessions.
5. Keep `stable` untouched until the user explicitly chooses to promote Extension.

`stable` and `Extension` share the user's existing PCM algorithms and data format. The primary difference under evaluation is the reduced coupling of PCM logic to the upstream game-list UI and native bridge.
