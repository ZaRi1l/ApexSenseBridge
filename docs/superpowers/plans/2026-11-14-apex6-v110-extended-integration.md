# ApexSenseBridge v1.1.0 + Extended Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Ship a reviewable branch combining official v1.1.0 with the user's Extended linear PCM, live monitoring, and per-game UI without losing either feature set.

**Architecture:** Merge the immutable upstream tag into the UI-restored Extended branch, then resolve code conflicts at native PCM writer/CLI and WPF session boundaries. Keep Extended processing restricted to native APEX 6 Pro grip PCM at both 1 kHz and 2 kHz; preserve upstream's new device routing, triggers, and VIIPER backend.

**Tech Stack:** C++20/CMake/CTest, .NET Framework WPF/C#, GitHub Actions Windows, Python contract tests, upstream libVIIPER.

**Spec:** `docs/superpowers/specs/2026-11-14-apex6-extended-v110-integration-design.md`

## Global Constraints

- Base `ZaRi1l/ApexSenseBridge:work/extended-nov6-ui-merged-20261108` (`faa7e39`); merge upstream tag `v1.1.0` (`5350989`) on a new branch.
- Preserve all Extended PCM modes (`Original`, `Companding`, `Linear`), 0–1000% linear gain, saved game override, meters and clipping telemetry.
- On real APEX 6 Pro, 2 kHz defaults **on** and 1 kHz remains selectable; never expose the option to APEX 4/5 or amplify synthetic trigger/hid rumble unexpectedly.
- Preserve 100% Original bit parity, ±127 saturation, controller identity checks, session tokens, existing settings, all nine locales.
- Do not modify the previous public release or force-update upstream/fork stable branches; publish no final release automatically.
- Hardware quality, ZZZ improvement and safety remain **unverified** until a physical APEX 6 Pro session.

## Review Focus

1. A saved Extended `Linear 400%` game override survives an upgrade even if upstream's new per-game vibration-strength fields coexist (Task 4).
2. Switching the PCM stream between 2 kHz and 1 kHz changes timing without applying the same linear gain twice (Task 2).
3. A disabled/grip-off or non-APEX-6 controller cannot be forced into unsafe 2 kHz frames from the CLI or saved settings (Tasks 1–2).
4. A stale live-monitor session or game key cannot leak peaks into another game's setting (Task 4).
5. A high-amplitude PCM sample clips to ±127 and increments clipping only in the actual stream path; 2 kHz stats distinguish native from upsampled frames (Tasks 2–3).

---

### Task 1: Baseline, integration regression, and upstream merge

**Files:**
- Modify: `tests/test_bridge_options.cpp`
- Modify: `tests/test_apex6_haptic_bridge.cpp`
- Merge: upstream `v1.1.0` into current integration branch

**Interfaces:**
- Produces: `BridgeCommandOptions::apex6SampleRateHz` (unsigned, default 2000) and `apex6SampleRateExplicit` (bool); CLI `--apex6-rate 1000|2000`.
- Preserves: `BridgeCommandOptions::apex6PcmConfig`, `--apex6-pcm-mode`, `--apex6-pcm-linear-gain-percent`, `--apex6-linear-gain` alias.

- [ ] Step 1: Run baseline `cmake -S . -B build/integration -DBUILD_TESTING=ON`, build, then `ctest --test-dir build/integration --output-on-failure`; record exact limitations.
- [ ] Step 2: Add failing option test: default 2000, explicit 1000/2000, reject 1500, reject 2000 when grip routing disabled, preserve `linear 400` parsing.
- [ ] Step 3: Run targeted bridge-options build/test to confirm the missing option fails on pre-merge code.
- [ ] Step 4: Merge `upstream/v1.1.0` with a regular non-forced Git merge; retain conflict markers for explicit resolution, then complete conflicts in Tasks 2–4.

### Task 2: Native PCM 2 kHz stream and Extended processing

**Files:**
- Modify: `src/dualsense/Apex6HapticBridge.h`
- Modify: `src/dualsense/Apex6HapticBridge.cpp`
- Keep: `src/extensions/Apex6PcmExtension.h`, `src/dualsense/Apex6PcmNormalization.h`
- Test: `tests/test_apex6_haptic_bridge.cpp`

**Interfaces:**
- Consumes: upstream `Apex6HapticBridge(..., unsigned sampleRateHz)` and Extended `Apex6PcmConfig` + session measurement API.
- Produces: unified constructor retaining `Apex6PcmConfig pcmConfig` and `unsigned sampleRateHz` with compatible callsites; `Apex6HapticBridgeStats::sampleRateHz`, `waveform2kBlocks`, `waveformUpsampledBlocks` plus Extended PCM counters.

- [ ] Step 1: Write failing bridge tests for real 2 kHz waveform packets with `Linear 400%`, `Original 100%`, ±127 saturation, left/right independence, non-native trigger path, and 1 kHz fallback.
- [ ] Step 2: Confirm targeted tests fail for unsupported behavior or missing interface.
- [ ] Step 3: Resolve C++ conflicts by retaining upstream's 2 kHz writer and report pacing while calling the Extended PCM scaler exactly once per native grip sample before packet output.
- [ ] Step 4: Build and run `apex6_haptic_bridge` and pre-existing trigger/PCM tests; commit the native integration when green.

### Task 3: CLI, telemetry, and per-controller isolation

**Files:**
- Modify: `src/cli/BridgeCommand.cpp`, `src/cli/BridgeOptions.h`, `src/cli/BridgeOptions.cpp`, `src/cli/BridgeTelemetry.cpp`
- Modify: `src/dualsense/Apex6HapticBridge.h` when telemetry declarations must match
- Test: `tests/test_bridge_options.cpp`, `tests/test_bridge_telemetry.cpp`

**Interfaces:**
- Consumes: Task 2's rate-aware PCM constructor/stats.
- Produces: `--apex6-rate 1000|2000`, existing `--apex6-pcm-*` and legacy gain mutual-exclusion policy, JSON `sample_rate_hz` and Extended clipping/peak metrics.

- [ ] Step 1: Add failing tests for rate CLI, invalid rates, non-APEX6 rate downgrade, non-rumble downgrade, Extended/legacy conflicting switches, and 2 kHz telemetry.
- [ ] Step 2: Run targeted tests and capture expected failure.
- [ ] Step 3: Resolve CLI/telemetry merge conflicts to preserve upstream's hardware gating and all Extended settings/measurement behavior.
- [ ] Step 4: Build and run targeted `bridge_options`, `bridge_telemetry` and `apex6_haptic_bridge` tests; commit when green.

### Task 4: WPF/Playnite settings, live monitor, and game overrides

**Files:**
- Modify: `ApexSenseBridgeTray/Models/TraySettings.cs`
- Modify: `ApexSenseBridgeTray/GameListWindow.xaml`, `ApexSenseBridgeTray/GameListWindow.xaml.cs`
- Keep: `ApexSenseBridgeTray/Extensions/GameListWindow.PcmExtension.cs`, `ApexSenseBridgeTray/Services/Apex6PcmControlClient.cs`
- Modify: `ApexSenseBridgeTray/Services/EngineSessionManager.cs`, `shared/BridgeArguments.cs`, applicable Playnite settings and locales
- Test: `tests/tray_learning_tests.cs`, `tests/test_pcm_settings_monitor_contract.py`, `tests/test_pcm_wpf_contract.py`, `scripts/test-tray-controller-settings.ps1`

**Interfaces:**
- Consumes: upstream `TraySettings.Apex6Rate2kHz` (bool default true), `BridgeArguments` optional rate flag and Extended PCM argument string.
- Produces: preserved global/per-game PCM settings, live meters and session token, plus APEX-6-only 2 kHz toggle passed to each launch path.

- [ ] Step 1: Add or update failing settings/CLI contract tests: migrate old `Linear 400%` settings; retain per-game override; persist 2 kHz default and user opt-out; stale meters remain isolated.
- [ ] Step 2: Run available contract tests and verify red.
- [ ] Step 3: Resolve WPF/C# conflicts, reconciling upstream's controller picker, game detail sliders, report/update UI and nine locales with Extended UI elements.
- [ ] Step 4: Run Python/PowerShell/Tray learning tests where toolchains permit; commit when green.

### Task 5: Full regression, packaging, and handoff

**Files:**
- Modify as required: `CMakeLists.txt`, `.github/workflows/*.yml`, `README.md`, `THIRD_PARTY_NOTICES.md`, build/portable scripts
- Create: test results and release handoff notes under `docs/` if needed

**Interfaces:**
- Consumes: all prior changes.
- Produces: reproducible tested integration branch, Windows CI/Portable artifact when available.

- [ ] Step 1: Run full CMake/CTest, Python contract and WPF-compatible tests; record counts, exact failures and skipped platform-specific tests.
- [ ] Step 2: Reconcile release metadata/attribution and verify updated MIT/GPL references against actual files; check build/package includes the required VIIPER 2 kHz dependency.
- [ ] Step 3: Check `git diff --check`, inspect diff for accidental overwrites; commit finalized changes.
- [ ] Step 4: Push **new** feature branch, open reviewable draft PR against the user's fork's appropriate base, inspect Windows CI and build artifacts; do not merge or publish final Release.
- [ ] Step 5: Hand off Portable ZIP or CI artifact link if build succeeded, exact known limitations and physical APEX 6 Pro test matrix.
