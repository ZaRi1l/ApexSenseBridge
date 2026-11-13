# APEX 6 Pro Extended × upstream v1.1.0 integration candidate

**Status:** review-only integration; **not a release**. Base: Extended UI-restored
fork (`faa7e39`); upstream: `ReynArts/ApexSenseBridge` tag `v1.1.0` (`5350989`).

The integration combines the official 1.1.0 changes with the fork's Extended
native-grip-PCM processing and live meter, without changing the previously
published `stable` or `Extension` branches.

## What stays and what is added

- Extended `Original`, `Companding` and `Linear` PCM modes, including 0–1000%
  gain, global settings, per-game overrides, native raw/streamed peak measurements,
  session tokens and output clipping counters.
- Upstream APEX 6 Pro **2 kHz native grip PCM**, with a reversible 1 kHz fallback
  in both the Tray and Playnite settings. The CLI supports `--apex6-rate 1000|2000`.
  APEX 6 Pro starts at 2 kHz by default when its verified integrated backend
  and rumble routing permit it; other controllers/backend paths fall back.
- Upstream adaptive trigger improvements, grip ducking, trigger ceiling,
  controller selection, gyro/motion, lighting, battery, session recovery,
  updater and dependency work from the upstream tag.
- Extended amplification is applied **once to native grip PCM only**, at the
  actual sample rate, and does not intentionally amplify synthetic rumble or
  trigger signals. The signed grip output remains within ±127.

## Automated verification

On Linux, the integrated native engine builds with C++20/CMake and the full
**36/36 CTest** suite passes. The new regression includes 2 kHz with 400%
linear gain, exact packet samples, left/right separation, clipping and
processed-sample metrics. **89 Python unittest contracts** pass, along with
separate PCM extension, WPF source and per-game meter contracts. The original
Extended fork's 29 native tests passed before the merge.

Windows WPF and Playnite **compilation, integrated libVIIPER binary, and portable
packaging require the dedicated Windows GitHub Actions job** in
`.github/workflows/validate-apex6-extended-v110.yml`; passing Linux tests
does not validate those runtime paths. Download its temporary `apex6-v110-
extended-windows-candidate` artifact only if all jobs are green. Do not
install this candidate over an existing working build without a backup.

## Physical APEX 6 Pro validation (pending)

1. Back up existing Tray/Playnite settings and isolate the candidate portable
   build. Stop other controller-bridge instances.
2. Test `Original 100%` first at both **1 kHz and 2 kHz** using the same ZZZ
   scene, audio output and device connection; check normal playback, gaps and
   silence. Repeat by USB and 2.4 GHz if available.
3. Test `Linear 100%`, then cautiously `Linear 200%` and `400%`, noting the
   L/R raw peaks, effective output peaks and clipping counts. Use high gain
   briefly; stop if haptics become harsh or the controller heats unusually.
4. Check the per-game override after restarting the Tray, and that moving the
   game selection does not alter a running session's measurements.
5. Check simultaneous grip+trigger events, release-to-zero behavior, controller
   reconnection, manual stop and automatic game detection. Check that APEX 4/5
   behavior did not change when the 2 kHz setting is saved.

**Not established:** that ZZZ's previously weak haptics are fixed, that 2 kHz
is better for every game, or that prolonged 1000% software gain is safe for
physical hardware. A higher number means digital scaling, not verified force.

No existing release is replaced automatically. A maintainer should review the
draft PR, the Windows candidate, driver prerequisites, version metadata and
license notices before deciding whether to publish anything.
