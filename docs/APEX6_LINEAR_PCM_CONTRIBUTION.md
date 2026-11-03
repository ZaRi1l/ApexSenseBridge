# APEX 6 Pro linear PCM — upstream contribution notes / 원본 기여 안내

## English

### Motivation and boundaries

Some native DualSense games have very quiet 1 kHz haptic PCM when played through
APEX 6 Pro. The upstream `--apex6-haptic-gain` (100–200%) uses **nonlinear
companding**, which changes relative signal texture. This proposed opt-in mode
uses **linear scaling with saturation** instead.

This review branch starts directly from an upstream `stable` commit and changes
only native APEX 6 Pro PCM processing, its CLI parameter and regression tests.
It deliberately avoids UI, shared-memory IPC, runtime measurement, settings
migration, per-game overrides, Playnite changes or packaging changes.

**Preservation guarantee sought by tests:**
- Without `--apex6-linear-gain`, default quantization and companding are unchanged.
- At `--apex6-linear-gain 100`, every signed 16-bit PCM input matches upstream.
- At 0%, native PCM is silent. At 1000%, results remain bounded to [-127, 127].
- The switch requires APEX 6 Pro identity verification and `--rumble`.
- Both gain modes are mutually exclusive. Ordinary trigger effects, fallback
  rumble, and APEX 4/5 paths are not modified.
- The existing Tray UI and nine languages retain their original appearance.

### Test evidence (automated versus hardware)

The native regression tests exhaustively inspect all 65,536 signed PCM inputs
for the unity path, gain limits, sign, and bounded monotonic amplitude.
Existing fake-HID tests are extended to check actual outgoing motor report
amplitudes at representative linear gain settings. CLI parsing includes
valid and invalid boundaries, mutual exclusion, and `--rumble` requirement.

See GitHub Actions for the **actual CI outcome**; a source test is not proof
that every device/firmware/game combination works.

### Manual hardware matrix (APEX 6 Pro, Windows)

1. Record OS, APEX 6 Pro firmware, dongle firmware, connection type, game,
   bridge commit and whether Steam Input is enabled.
2. Exit the original Tray/Playnite bridge. Connect via USB, run a new branch
   build in an elevated session only if required for driver setup. Start
   `bridge-triggers --rumble --apex6-linear-gain 100`, then launch the game.
3. Repeat the same short game scene with 100%, 150%, 200%, 300% linear gain
   as separate bridge sessions. Compare left/right response, clipping/buzzing,
   trigger response, and whether silence remains silent.
4. Verify `--apex6-haptic-gain 150` remains the original companding result.
   Verify no-option baseline. Do not attempt both switches simultaneously.
5. Repeat with the 2.4 GHz dongle, Alt+Tab, game restart, bridge restart,
   reconnect, and at least two native DualSense games. Stop immediately on
   overheating, abnormal noise, or uncomfortable vibration.
6. Verify APEX 4/5 unchanged if available. Actual long-duration reliability,
   game-specific user interfaces and Playnite settings remain **out of scope**
   for this focused review branch.

### Suggested upstream review approach

If the maintainer wants the capability, this branch can serve as a **small
core-only contribution**. Please confirm preferred base (`stable` vs `dev`)
and rerun integration checks after rebasing. Separate UI, per-game settings and
live measurement into later optional contributions. No upstream issue or PR
was created by this preparation.

## 한국어

### 목적과 변경 범위

일부 네이티브 DualSense 게임의 APEX 6 Pro 햅틱 PCM 신호가 작아 체감이
약할 수 있습니다. 원본의 `--apex6-haptic-gain` 100~200%는 **비선형 압축형**
증폭입니다. 제안 기능은 그와 별개로 **선형 증폭 후 포화 처리**를 지원합니다.

이 기여 브랜치는 원본 `stable`에서 바로 시작했고, APEX 6 Pro PCM 처리,
CLI 옵션, 회귀 테스트만 변경했습니다. 원본 화면 디자인, WPF/Playnite,
테마, 다국어, IPC, 게임별 설정, 측정 UI는 **변경하지 않습니다**.

- 새 옵션이 없으면 기존 원본 출력·압축형 증폭이 그대로 유지됩니다.
- 100%는 전체 16비트 PCM 입력 범위에서 원본과 동일해야 합니다.
- 0%는 네이티브 PCM 무음, 1000%도 [-127,127] 신호 범위 안으로 제한됩니다.
- APEX 6 Pro 확인과 `--rumble`이 필요하며 두 증폭 옵션은 동시 사용 불가입니다.
- APEX 4/5, 트리거, 일반 럼블 및 엔벌로프 처리 경로는 그대로 둡니다.

### 자동 테스트와 실물 테스트의 구분

자동 테스트는 16비트 PCM 전체 입력, 증폭 경계·부호·기본 출력 보존,
가짜 HID 모터 출력, CLI 오류 입력을 다룹니다. Windows CI의 실제 통과
여부는 GitHub Actions 기록을 확인해야 합니다. 실물 장치의 체감·발열
및 펌웨어 호환성까지 증명하는 것은 아닙니다.

### 집에서 확인할 항목

1. Windows, 패드·동글 펌웨어, 연결 방식, 게임, Steam Input 설정을 기록합니다.
2. 기존 ASB/Tray/Playnite 브릿지를 완전히 종료합니다.
3. 새 브랜치로 빌드한 파일에서 위 명령을 100% → 150% → 200% → 300%
   순으로 **각각 별도 실행**한 후 동일 장면을 비교합니다.
4. 좌우 분리, 무음 유지, 클리핑·잡음, 트리거 정상 동작을 확인합니다.
5. 새 옵션 없는 원본값과 `--apex6-haptic-gain 150` 압축형도 비교합니다.
6. USB와 2.4GHz 동글을 각각 확인하고 Alt+Tab, 재연결, 게임 재시작을
   반복합니다. 발열·이상음·거친 진동이 있으면 즉시 중단합니다.

기존 Extended 베타의 게임별 설정·실시간 측정 UI는 **다른 브랜치에
보존되어 있습니다**. 이를 원작자에게 기여할 때는 별도 변경으로
분리하는 것이 리뷰와 병합에 유리합니다.

**PR 및 이슈 자동 생성 없음.**
