# APEX 6 PCM 수정 검증 기록

## 실제 실행 결과

- `scripts/test-pcm-existing-compiler.ps1`: 설치된 PowerShell 7의 Roslyn 및 기존 Framework 런타임 어셈블리로 실제 C# 제품 서비스/설정/세션 소스와 `tray_learning_tests.cs` 컴파일 성공. 회귀 테스트 373 assertions 통과.
- 첫 실행은 `TestControllerCalibrations`의 reflection 인자 개수 불일치로 실패했다. BuildArguments의 gameKey 인자를 테스트에 추가한 뒤 통과했다.
- 컴파일러 진단은 기존 소스의 hidden CS8019(미사용 using) 4건. WPF 전체 프로젝트 빌드 결과가 아니다.
- `python -I tests/test_pcm_wpf_contract.py`: 2개 정적 계약 테스트 통과. XAML 이벤트 핸들러/이름 참조와 세션 토큰 매핑 문자열 검사이며 실제 IPC 통신 테스트가 아니다.
- 기존 Roslyn으로 `GameListWindow.xaml.cs` 구문 분석: 오류 없음. 타입 검사/바인딩 실행은 미검증.
- `git diff --check`: whitespace 오류 없음.
- C++ 컴파일러(cl/g++/clang++), CMake/CTest 없음. 실제 C++ 컴파일 및 회귀 테스트는 실행하지 못했다. `tests/test_pcm_header_standalone.cpp`는 실제 제품 헤더와 Windows 매핑을 테스트하도록 추가했으나 이 호스트에서 실행되지 않았다.
- WindowsApps 전체 검색은 접근 거부됐지만 정확한 PowerShell 설치 경로의 Roslyn DLL은 읽을 수 있었다. 추가 도구는 설치하지 않았다.

## 수정 사항

- BridgeOptions: calibration 전달 시 PCM의 --rumble 검사 지연, Original 허용, APEX4/5 rumble 옵션에 PCM 검증 영향 제거. APEX6 grips OFF에서는 새 PCM 정책은 dormant. legacy 플래그는 기존 APEX6 검사 유지.
- Apex6PcmNormalization: optional 직접 include, 조절 가능한 Companding 옵션.
- TraySettings: Original 불필요 인자 제거, 전역 Normalize에서도 게임별 reference 사용, abs16 CLI 전달 우선, decimal 표시값 보존, Companding 설정 저장, 역직렬화 후 대소문자 무시 dictionary 복원.
- BridgeSession/EngineSessionManager: 실제 생성 토큰 노출. UI는 같은 토큰의 `.Apex6Pcm` 매핑만 사용.
- GameListWindow: token + activeGameKey + 선택 게임 검증, 측정 generation 확인, 변경/종료 시 연결 해제, 전역/개별 Companding 및 숫자 입력 이벤트 추가. 측정 저장 시 상속 정책을 바꾸지 않음.
- LiveMetrics: 전역 fallback 제거, 실제 누적 카운터의 구간 차이 사용, 첫 측정 블록 처리량 포함. 렌더러는 실제 scaleSample 호출 수(활성 채널당 8)로 샘플을 계산.
- BridgeCommand/HapticBridge: 오디오 검증값을 4채널 정상으로 하드코딩하지 않고 실제 preflight 결과 전달.
- BridgeTelemetry: 기존 잘못된 C++ 문자열 삽입 구문 수정.
- tests: 실제 C# 설정/격리/정밀 reference/직렬화 회귀 보강, C++ CLI calibration/abs16/companding 회귀 추가.

## 미검증 및 남은 한계

- C++ 변경 전체 및 native 회귀 테스트 미실행. 별도 헤더 테스트도 미실행.
- WPF 전체 semantic/XAML 빌드, 실제 창 이벤트·실행 중 엔진 IPC lifecycle, 측정 stop 경계의 동시성, 하드웨어 회귀 미검증.
- 공유 메모리는 기존 단순 v1 프로토콜로 완전한 원자적 snapshot/stop acknowledgement를 제공하지 않는다. stop과 render가 동시에 진행하는 경계에서 마지막 블록이 포함될 수 있으며 실시간 IPC 스트레스 검증이 필요하다.
- 전역 Normalize 목표/최대배율의 전체 UI 편집, reference 수동입력/초기화 UX는 원래 초안의 부족한 부분이며 본 수정의 UI는 요청된 기존 target/linear/companding 입력에 집중했다.
- Playnite는 Tray 설정을 자동 상속하지 않는다.
- 48kHz raw는 기존 세션 최대값이며 independent raw 구간 측정으로 검증되지 않았다. 기준 정규화 값에는 사용하지 않는다.
- 이전 Python 예제의 PASS는 수용 기준 충족 증거가 아니다.

집에서 실행할 명령 및 실기 절차는 README 참조. commit/push/PR 및 드라이버·펌웨어 변경 없음.
