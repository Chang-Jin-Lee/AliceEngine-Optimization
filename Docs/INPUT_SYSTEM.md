# Input 시스템 가이드

이 문서는 `Runtime/Input/InputSystem`과 윈도우 메시지 기반 입력 흐름을 정리합니다.  
특히 **마우스 잠금(Cursor Lock)**, **Alt+Tab/최소화 시 잠금 해제** 동작을 포함합니다.

---

## 1) 구조 요약

- 입력 상태 저장: `Engine/src/Runtime/Input/InputSystem.h`, `InputSystem.cpp`
- Win32 메시지 진입점: `Engine/src/Runtime/Engine/Engine.cpp` (`Engine::WindowProc`)
- 엔진 메시지 처리: `Engine/src/Runtime/Engine/EngineWindow.cpp` (`Engine::Impl::HandleMessage`)
- 카메라 입력 소비: `Engine/src/Runtime/Engine/CameraSystem.cpp`

흐름:
1. Win32 메시지 수신 (`WindowProc`)
2. DirectXTK `Keyboard::ProcessMessage`, `Mouse::ProcessMessage` 전달
3. `HandleMessage`에서 `WM_INPUT`를 `InputSystem::ProcessRawInput`으로 전달
4. 매 프레임 `InputSystem::Update()`에서 키/버튼/델타 확정
5. `CameraSystem` 등이 `InputSystem` 값을 사용

---

## 2) 마우스 델타 처리 방식

`InputSystem`은 두 모드를 지원합니다.

- 잠금 모드 (`m_isLocked=true`)
  - Raw Input 델타(`m_rawInputDeltaX/Y`)를 사용
  - 프레임마다 커서를 잠금 기준점(`m_lockedPos`)으로 되돌림 (`SetCursorPos`)
- 일반 모드 (`m_isLocked=false`)
  - 클라이언트 좌표 기준으로 이전 프레임 대비 델타 계산

스크롤은 DirectXTK 누적값 차이로 계산합니다.

---

## 3) 커서 잠금/표시

`InputSystem` API:

- `SetCursorVisible(bool)`
  - `ShowCursor` 카운터를 while 루프로 강제 정합
- `SetCursorLocked(bool)`
  - `true`: `ClipCursor`로 윈도우 클라이언트 영역 가둠
  - `false`: `ClipCursor(nullptr)`로 해제

안정성 처리:
- 잠금/해제 전환 시 `mouseDelta`, `rawInputDelta`를 0으로 리셋
- 잠금 해제 시 `m_hasPrevMousePos=false`로 초기화해 첫 프레임 튐 방지

---

## 4) Alt+Tab/최소화 시 잠금 해제 + 포커스 상태 노출 (신규 반영)

위치: `Engine/src/Runtime/Engine/EngineWindow.cpp`

아래 메시지에서 공통 처리로 **잠금을 강제 해제**하고,  
스크립트 쪽에서 포커스 복귀를 감지할 수 있도록 상태를 노출합니다.

- `WM_SIZE` + `SIZE_MINIMIZED`
- `WM_ACTIVATEAPP` + 비활성(`wParam == FALSE`)
- `WM_KILLFOCUS`
- `WM_ACTIVATEAPP` + 활성(`wParam == TRUE`)
- `WM_SETFOCUS`

실행 동작 (비활성화 시):
- `m_inputSystem.SetCursorLocked(false)`
- `m_inputSystem.SetCursorVisible(true)`
- `CameraFollowComponent.mouseLocked = false` 동기화
- `InputSystem` 내부에 앱 활성 상태 플래그 갱신

실행 동작 (복귀 시):
- `InputSystem`에 **활성화 이벤트**만 기록
- **재잠금은 스크립트가 처리** (예: 클릭 시 `SetCursorLocked(true)`)

의도:
- 최종 빌드에서 Alt+Tab/최소화 후에도 마우스가 붙잡히는 문제 방지
- 포커스 복귀 후 **콘텐츠 스크립트가 원하는 타이밍에 재잠금** 가능

---

## 5) 스크립트에서 포커스 복귀 처리 (권장)

스크립트 입력 API에 아래 상태가 추가됨:
- `IsAppActive()`
- `ConsumeAppDeactivated()`
- `ConsumeAppActivated()`

예시 흐름:
1. `ConsumeAppActivated()`가 true인 프레임부터 “복귀 상태”로 간주
2. 유저가 마우스를 클릭하면:
   - `Input()->SetCursorVisible(false)`
   - `Input()->SetCursorLocked(true)`
   - `CameraFollowComponent.mouseLocked = true`

---

## 5) CameraFollow와 입력 연동 규칙

`CameraSystem` 기준:

- Ctrl로 `follow.mouseLocked` 토글
- 잠금 시: `SetCursorVisible(false)`, `SetCursorLocked(true)`
- 해제 시: `SetCursorVisible(true)`, `SetCursorLocked(false)`
- 잠금 중에만 마우스 델타로 yaw/pitch 갱신

즉, **입력 잠금 상태의 단일 소스는 `CameraFollowComponent.mouseLocked` + `InputSystem` 동기 상태**입니다.

---

## 6) 디버깅 체크리스트

1. Alt+Tab/최소화 직후 커서가 보이는가?
2. `ClipCursor`가 해제되었는가? (OS 레벨 체감 확인)
3. 복귀 후 첫 프레임 카메라가 튀지 않는가?
4. `CameraFollowComponent.mouseLocked`가 강제로 `false` 되었는가?
5. Ctrl 재입력으로 정상 재잠금 되는가?

---

## 7) 주의사항

- `ShowCursor`는 참/거짓 1회 호출만으로 상태가 보장되지 않으므로 현재처럼 카운터 정합이 필요합니다.
- Raw Input 사용 시 `SetCursorPos`의 영향이 델타에 섞이지 않도록 현재 구조를 유지해야 합니다.
- 다른 입력 소비 시스템(UI, 스크립트) 추가 시에도
  - 포커스 상실 시 강제 unlock
  - 복귀 시 명시적 relock
  규칙을 유지하는 것이 안전합니다.
