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
