# APEX 6 PCM 수정 검증 기록

## 1. 최신 작업 요약 (6개 요청 항목 반영)

1. **전역 Normalize 목표 진폭 및 최대 증폭 배율 UI 구현:**
   - `GameListWindow.xaml`: 전역 설정에 `PnlGlobalApex6PcmNormalize` 추가 (목표 진폭 10..100% 슬라이더/숫자입력, 최대 배율 100..400% 슬라이더/숫자입력).
   - `GameListWindow.xaml.cs`: 슬라이더 변경 및 텍스트 직접 입력(Enter/LostFocus) 이벤트 연결, `TraySettings.Apex6PcmGlobal` 영구 저장 연동.
2. **게임별 측정 피크 수동 입력 및 초기화 기능 추가:**
   - `GameListWindow.xaml`: 기준 피크 표시 영역에 진폭 직접 입력 텍스트박스(`TxtBoxApex6PcmManualRef`) 및 `[초기화]`(`BtnApex6PcmReset`) 버튼 추가.
   - `GameListWindow.xaml.cs`: 직접 입력 시 1..32768 진폭 또는 1..100 퍼센트를 자동 판정하여 정밀 16-bit 진폭 및 퍼센트로 저장 후 배율 재계산. `[초기화]` 클릭 시 해당 게임의 설정을 삭제/초기화하여 전역 설정으로 복원.
3. **실시간 48kHz 원본 피크의 구간별 독립 집계 및 UI 구분:**
   - `src/dualsense/Apex6HapticBridge.h/.cpp`: 세션 전체 누적 최대값을 넘기던 문제를 해결하기 위해, `WaveformBlock`에 해당 프레임의 48kHz 원본 피크(`rawHapticLeftPeak`, `rawHapticRightPeak`, `hasRawMeasurements`)를 보존하여 전달.
   - `src/platform/Apex6PcmLiveMetrics.h`: 측정 시작 시 48kHz 피크를 0부터 시작하여 측정 구간 내에서만 독립 집계(`rawMeasuredHapticPeak16`).
   - `GameListWindow.xaml.cs`: 측정 중 및 종료 시 `1kHz 변환 피크`(정규화 기준)와 `48kHz 원본 피크`(대역제한 전 진단용)를 명확히 구분하여 표시. 원본 데이터 미제공 시 "48kHz 원본: 측정 불가" 명시.
4. **PCM 공유 메모리 시작/종료 동시성 테스트 보강:**
   - `tests/test_pcm_header_standalone.cpp`: 오디오 렌더 스레드(1kHz 고주기 업데이트)와 UI 클라이언트 스레드(연속 500회 측정 시작/종료/폴링)를 동시 실행하는 멀티스레드 스트레스 테스트(`testPcmLiveConcurrency`) 작성. 언더플로우, 데이터 파손, 데드락 검증.
5. **test_pcm_header_standalone.cpp의 CMake/CTest 등록:**
   - `CMakeLists.txt`: `if(WIN32)` 블록에 `ApexSenseBridgePcmHeaderStandaloneTests` 실행 파일 및 `pcm_header_standalone` CTest 등록 완료.
6. **기존 APEX 4/5 및 트리거 회귀 위험 점검:**
   - APEX 4/5는 `Apex6HapticBridge` 인스턴스를 생성하지 않으며 기존 `RumbleBridge`/`AdaptiveTriggerBridge` 경로만 사용함을 코드 레벨 재확인.
   - `BridgeOptions.cpp`: APEX 4/5의 럼블 비활성화 상태에서 전역 PCM 설정이 있어도 부팅이 거부되지 않도록 검증 완료.
   - 실제 C# 회귀 테스트: `tray_learning_tests.cs`에서 APEX 4/5 캘리브레이션, 트리거, 럼블, 자이로 회귀를 포함한 375개 assertion 전건 통과.

---

## 2. 실제 실행한 검증 결과

- `scripts/test-pcm-existing-compiler.ps1`:
  - 호스트에 이미 설치된 PowerShell 7의 Roslyn 라이브러리 및 Framework 런타임 어셈블리를 활용하여 실제 C# 제품 소스(`TraySettings.cs`, `EngineSessionManager.cs`, `BridgeSession.cs` 등)와 `tray_learning_tests.cs` 컴파일 성공.
  - 회귀 테스트 375 assertions 100% 통과 (초기화 및 전역 Normalize 배율 생성 테스트 2건 추가 검증).
- `tests/test_pcm_wpf_contract.py`:
  - XAML 컨트롤 이름, 이벤트 핸들러 바인딩, 세션 토큰 IPC 매핑 문자열 정적 계약 테스트 2건 통과.
- Roslyn 구문 분석:
  - `GameListWindow.xaml.cs` C# 구문 분석 0 error 통과.
- `git diff --check`:
  - 공백/인코딩 오류 없음.

---

## 3. 기능 누락 및 미검증 항목 (중요 안내)

사용자의 "도구 설치 금지" 지침을 준수하였으므로, 아래 항목들은 이 호스트에서 실행되지 않았으며 **미검증** 상태입니다:

1. **C++ 엔진의 실제 네이티브 컴파일 및 CTest 실행 (미검증):**
   - 호스트에 MSVC(`cl.exe`) 및 CMake가 없어 `ApexSenseBridge.exe` 바이너리 생성 및 `tests/test_pcm_header_standalone.cpp`, `tests/test_apex6_haptic_bridge.cpp`의 실제 실행 바이너리 테스트는 수행하지 못했습니다. (집에서 `.\scripts\build-windows.ps1` 및 `ctest`로 검증 필요)
2. **WPF UI의 전체 XAML 시맨틱 빌드 (미검증):**
   - .NET Framework 4.6.2 타기팅 팩 및 WPF BAML 컴파일러 부재로 `ApexSenseBridgeTray.exe`의 전체 릴리즈 바이너리 빌드는 수행하지 못했습니다. (집에서 `.\scripts\build-tray-app.ps1`으로 검증 필요)
3. **실제 APEX 6 Pro 컨트롤러 하드웨어 체감 및 물리적 진동 (미검증):**
   - 물리 컨트롤러 연결 환경이 아니므로, 모터 응답, 고주파/저주파 질감, 발열 여부는 집에서 실기 A/B 테스트 가이드에 따라 확인해야 합니다.
4. **Playnite 자동 반영 (지원 범위 제외):**
   - Playnite 런처 경로는 독립 실행 경로이므로 Tray의 PCM 설정/측정 마법사가 자동 공유되지 않습니다.
