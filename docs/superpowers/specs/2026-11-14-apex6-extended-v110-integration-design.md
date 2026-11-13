# ApexSenseBridge Extended × upstream v1.1.0 — integration design

Date: 2026-11-14 (KST)
Status: **Implementation candidate — Linux regressions passed; Windows CI and physical hardware pending**

## Intent

Make a testable successor to the user's APEX 6 Pro Extended build: retain the
user's opt-in linear PCM amplification, live meters, clipping diagnostics, and
per-game settings while incorporating the **entire** official
`ReynArts/ApexSenseBridge` **v1.1.0** release, notably native 2 kHz PCM, revised
trigger rendering, motion, battery, RGB, controller selection, and recovery.
The primary test case is weak/occasionally interrupted PCM feedback in ZZZ;
there is **no claim** that upstream v1.1.0 already fixes that game's volume.

## Starting points and constraints

- Preserve `ZaRi1l/ApexSenseBridge:work/extended-nov6-ui-merged-20261108`
  (`faa7e39`) and the previous `Extension` branch. Start a **new** branch;
  never force-update `stable`, `develop`, `Extension`, or the existing release.
- Upstream version is immutable Git tag `v1.1.0` (`5350989`). Both branches
  share ancestry (`1927d78`). A read-only merge preview identifies **11
  conflicts**, mostly C++ PCM/CLI and WPF settings/engine session code.
- The current fork contains the Extended `Original / Companding / Linear`
  modes, 0–1000% linear gain, live L/R PCM measurements, per-game overrides,
  clipping telemetry, and WPF interface. Don't replace it with the narrower
  November 6 CLI-only linear patch.
- APEX 6 Pro PCM output remains signed and bounded to ±127. A percentage is
  **signal amplification**, not verified actuator force or a safety guarantee.
- Preserve APEX 4/5 behavior, conventional rumble, trigger routing, verified
  hardware gating, and existing supported-game settings. Long-session 2 kHz
  safety and ZZZ's real-world behavior need actual Windows/APEX 6 Pro testing.
- Keep the user's fork/license notices and upstream attribution intact;
  reconcile any existing `README` license text against actual `LICENSE`,
  and retain third-party component notices before distributing binaries.

## Approaches considered

1. **Chosen: full v1.1.0 merge into the latest UI-restored Extended branch.**
   Preserves the existing user's work and incorporates upstream features,
   at the cost of manual conflict resolution and broad regression testing.
2. Selectively backport only upstream's 2 kHz pipeline. Smaller scope but
   misses other v1.1.0 features, and a 2 kHz backport still depends on the
   updated VIIPER/native PCM interfaces.
3. Restart on clean upstream v1.1.0 and re-port every Extended subsystem.
   Clean upstream base but significant risk of forgetting saved UI/IPC features.

## Architecture and behavior

1. Merge **the tagged v1.1.0 commit** into the new integration branch.
   Resolve conflicts semantically, retaining upstream's 2 kHz stream writer,
   trigger behavior, VIIPER changes, gyro/RGB, reliability fixes, security and
   updates while preserving the existing Extended feature surface.
2. Apply Extended `Original`, `Companding`, or `Linear` processing exactly
   once to **native APEX 6 Pro grip PCM**. At 2 kHz, process the actual new
   decoded samples before output packing; at optional 1 kHz, preserve the
   previous processing path. Do not boost synthetic trigger signals or
   conventional rumble accidentally. Preserve left/right channel separation,
   signed zero, saturation, and the unmodified 100% path.
3. Expose upstream's APEX-6-only **2 kHz grip haptics** toggle in the WPF
   Settings UI, on by default for APEX 6 Pro, reversible to 1 kHz. Keep the
   CLI `--apex6-rate 1000|2000` and ensure Tray and Playnite propagate the
   effective value. Older saved settings should migrate without losing PCM
   gain or per-game overrides.
4. Preserve the Extended session-scoped live PCM meter and per-game gain
   rules: verified controller/game key, explicit start/stop, original/output
   L/R peaks, clipping counts and isolation from stale sessions; never confuse
   raw 48 kHz PCM peaks, streamed 2 kHz peaks, or physical grip force.
5. Reconcile upstream's new per-game tuning, controller picker, reporting,
   updater and WPF resource changes with Extended controls, gamepad
   navigation, all nine locales, and existing data migration. Avoid exposing
   controls for an unverified/mismatched controller.

## Verification and handoff

- **Baseline:** run existing project-native tests before edits, record any
  environment/toolchain limitations, and capture known conflicts.
- **Red/green regression tests:** 1 kHz/2 kHz stream, rate fallback, unity
  parity, 0%/1000% linear gain, ±127 clipping, no PCM double-processing,
  real vs synthesized effects, stereo separation, and APEX 4/5 isolation.
- **WPF/CLI tests:** settings migration, saved global/per-game PCM mode,
  extended meter lifecycle and stale session isolation, locale controls,
  `--apex6-rate`, and upstream controller selection/trigger behavior.
- **Windows CI:** build C++ engine, integrated VIIPER, WPF Tray, Playnite,
  test suites and a portable ZIP. A Linux-only test run is not proof that
  the full Windows distribution works. Record actual passing/failing checks.
- **Hardware validation checklist:** compare 1 kHz vs 2 kHz and Original vs
  Linear in repeatable ZZZ scenes; test 2.4 GHz and USB, low/strong intensity,
  silence, clipping, interruption, trigger release, heat and controller
  reconnection. No claim of hardware verification until the user tests it.
- Deliver a new reviewable GitHub branch/PR, CI status, and a portable build
  when tests allow. **Do not merge into `stable`, overwrite an existing
  release, or publish a final public release automatically.**

## Deliberate non-goals

- No auto-normalization or per-game AI gain tuning.
- No bypass of motor-command limits, hardware protections, or verified-device
  checks; no firmware changes.
- No guarantee of stronger ZZZ feedback from 2 kHz alone and no guarantee
  that 1000% linear gain is physically safe in every scene.

## Remaining implementation choices

The implementer may choose conflict-resolution mechanics, exact internal
helper signatures, test fixtures and CI filenames as long as the contracts
above are met. Release publishing awaits a separate user decision.
