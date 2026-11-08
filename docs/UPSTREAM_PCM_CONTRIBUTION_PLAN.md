# Upstream contribution plan — APEX 6 Pro Linear PCM gain

Status: **LOCAL/FORK PREPARATION ONLY. NO UPSTREAM PR OR ISSUE HAS BEEN SENT.**

Upstream: https://github.com/ReynArts/ApexSenseBridge (currently stable/dev branches)
Working feature branch: `ZaRi1l/ApexSenseBridge:feature/upstream-ready-pcm-ui`
Source fork: https://github.com/ZaRi1l/ApexSenseBridge

## Why this is a proposal, not a ready-to-merge single PR

Upstream already offers an experimental **CLI-only** APEX 6 PCM companding gain
(`--apex6-haptic-gain 100..200`). The extension adds a genuinely different
**linear, saturating** mode (0..1000%) plus live measurement and game-specific
overrides. The native and WPF extension together touch several established
subsystems, including renderer, CLI, settings, game list, engine/session IPC and
telemetry. A large feature PR would be hard to review and accept as-is.

**Do not open a PR directly from this full feature branch.** First ask the
maintainer which base branch and feature scope they would accept. Rebuild a
narrow review branch from their current choice (`stable` or `dev`), not from
the entire extension history. Preserve this working branch for users.

## Intended UX

1. Settings > Controller Effects (APEX 6 Pro only): native PCM processing mode
   (Original, Companding, Linear), with a mode-specific gain slider. The global
   vibration-strength slider remains separate. Default **Original 100%**.
2. Settings > same section: a compact PCM monitor that measures the **active
   bridge game/session**, without depending on the selected game-list entry.
   Shows live original L/R and amplified L/R peaks; it does **not** measure
   physical actuator force. Explicit Start/Stop; Stop saves the observation to
   the measured game without altering its gain.
3. Game detail: optional per-game overrides (Use global PCM settings vs custom
   Original/Companding/Linear) and the existing detailed measurement,
   per-game global-strength bypass, reference display and suggested gain.
4. Existing game/controller behavior is preserved. No new HID writer,
   independent game scanner, automatic gain or Normalize mode is introduced.

## Suggested upstream review sequence (each independent)

### 0. Discussion / feature request (no code PR)
Explain the limitation in quiet native PCM games. Compare screenshots,
provide before/after 100%, 200%, 400% controlled captures, include
`Apex6PcmGain.h` behavior, and ask whether the maintainer is interested in
**optional linear PCM gain**; ask if UI or CLI-only is preferable.

### 1. Small core-only PR
Branch from freshly synchronized upstream base. Add the linear PCM scaler and
tests around `Apex6HapticBridge`'s native PCM transform. Keep existing
companding and default outputs bit-identical. No UI, per-game policies or IPC.
A simple opt-in CLI switch is often easier to review than a full UI feature.
Test signed boundaries, zero, unity path, saturation, monotonic gain,
preserved rumble/envelope/trigger paths, and APEX 4/5 isolation.

### 2. Optional native settings UI PR
If requested, add the mode selector and gain to the existing right-column
controller effects settings, model-gated to verified APEX 6 Pro. Use the existing
theme and gamepad navigation, locale resources and settings migration tests.
Describe global defaults vs per-game overrides explicitly.

### 3. Live meter & per-game overrides PR(s)
Only after agreement on scope: use the existing render thread, session-token
and game-key-validated non-blocking IPC; no extra HID writer. Ship separate
validation for start/stop, stale sessions, focus changes, disconnect/reconnect,
data contention, original/output peak units and persistence of measured
reference without silently modifying manual gain.

### 4. Optional global vibration strength bypass
Leave ordinary rumble, synthesized envelopes, triggers and other devices
unchanged; do not include this controversial option in the first proposal
unless upstream explicitly wants it.

## Evidence needed before any PR

- Successful Windows 2022 C++ and WPF builds, native CTest, source contracts,
  regression tests and an artifact built from a reproducible commit.
- Actual APEX 6 Pro wired and 2.4 GHz dongle checks, repeated Alt-Tab,
  controller replug, input switching, different games and prolonged
  user-controlled vibration. Watch for clipping, distortion, abnormal heat
  or sound; 1000% is a **digital sample multiplier**, not 10x physical force.
- Confirm 100% Original bit parity with the upstream release and preserve
  old saved settings. Show sample peak/counter recordings; avoid claims that
  an untested game or platform is compatible.
- Keep all new UI strings localized; verify keyboard AND gamepad navigation.
- Rebase/sync against the maintainer's latest selected base before preparing
  each PR. Re-run checks after conflict resolution.

## How to propose the change later (NOT done automatically)

1. Open a feature discussion/Issue in `ReynArts/ApexSenseBridge`, linking the
   public extension and a short video/capture; avoid implying endorsement.
2. After receiving a positive answer and preferred base, fetch/sync upstream.
3. Create a clean branch from the upstream base in the user's fork; port only
   stage 1 files/tests. Do not select this large working branch as PR head.
4. Run CI and hardware tests. Push that review branch to `ZaRi1l/ApexSenseBridge`.
5. GitHub > fork > Compare & pull request, base `ReynArts/ApexSenseBridge` /
   maintainer-selected branch, head `ZaRi1l/<review-branch>`.
6. Choose **Draft PR**, provide the problem, scope, exact tests, safety/compat
   notes, screenshots and any known limitations. Ask for review when ready.
7. Only after stage 1 is accepted discuss stage 2 and then stage 3.

## Present-branch validation boundaries

The working feature branch is **not yet confirmed hardware-validated**.
Its new Settings PCM meter shares the existing measurement state and the
underlying runtime bridge; CI validates build and source contract behavior,
not long-term physical reliability. This branch is for continued testing,
not a direct upstream merge request.
