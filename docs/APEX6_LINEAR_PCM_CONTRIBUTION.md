# APEX 6 Pro linear PCM gain / APEX 6 Pro 선형 PCM 증폭

## English

This opt-in contribution adds **linear digital scaling** for native 1 kHz DualSense haptic PCM on a *verified* Flydigi APEX 6 Pro. The original nonlinear `--apex6-haptic-gain 100..200` and default PCM behavior are preserved.

```powershell
.\ApexSenseBridge.exe bridge-triggers --rumble --apex6-linear-gain 100
.\ApexSenseBridge.exe bridge-triggers --rumble --apex6-linear-gain 200
```

- Range: **0–1000%**. 100% is bit-identical to upstream; 0% silences native PCM. Outputs saturate at [-127, 127].
- Requires `--rumble` and verified APEX 6 Pro identity. Mutually exclusive with `--apex6-haptic-gain`.
- Triggers, conventional rumble, envelope fallback, APEX 4/5 paths and the existing Tray UI stay unchanged.
- Digital PCM gain is **not** a guarantee of proportionally stronger physical vibration. Strong gains may clip.

**Validation:** Windows CI builds native code and the WPF Tray, runs CTest and repeats the APEX 6/CLI regressions. Exhaustive tests cover all 65,536 signed PCM samples; fake-HID tests inspect motor output. Real controller and game tests are still required. Compare identical scenes at 100/150/200/300% with USB and 2.4 GHz, and check left/right separation, trigger feedback, clipping and reconnects. Stop if you notice overheating or abnormal vibrations.

This focused branch is based on upstream `dev` from November 5, 2026. Extended's per-game metering, normalization, IPC and UI extensions remain on their separate branches.

## 한국어

검증된 **APEX 6 Pro 전용** 네이티브 DualSense 1 kHz 햅틱 PCM 선형 증폭 기능입니다. 기존 비선형 옵션인 `--apex6-haptic-gain 100..200`과 기본 출력을 그대로 유지합니다.

- `--apex6-linear-gain 0..1000`: 100%에서 원본 비트 단위 동일, 0%에서 PCM 무음, 최대값에서도 출력 [-127, 127] 포화 처리
- `--rumble` 필수, APEX 6 Pro 장치 신원 확인 필수, 비선형 증폭 옵션과 동시 사용 불가
- 트리거, 일반 럼블, 엔벌로프, APEX 4/5, 기존 Tray 디자인 변경 없음
- 디지털 진폭을 올리는 기능이며, 물리적 진동력이 동일한 배수로 강해지는 것은 아님

**실기 확인:** Windows CI 통과 후 100% → 150% → 200% → 300%로 각각 따로 실행하면서 같은 장면의 좌우 진동, 트리거, 클리핑/잡음, 재연결을 USB와 2.4GHz 모두에서 확인하세요. 발열이나 이상 진동이 있으면 즉시 중지하세요. Extended의 게임별 측정·정규화 UI는 별도 브랜치에 그대로 보존됩니다.
