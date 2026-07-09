# 에디터 QA 2차 반영 구현 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 사용자 QA 5건 반영 — ①②에디터 시작 3분50초·창 멈춤(원인: 매 프레임 FBX 재임포트), ③Unity식 Play/Pause/Stop/Step, ④우클릭+WASD 중 휠로 카메라 속도, ⑤ImGui 멀티 뷰포트로 패널 분리 창.

**Architecture:** 기존 브랜치 `refactor/engine-2-6`에 이어서 커밋(PR #8에 누적). 신규 모듈 없음 — EngineRender/EngineUpdate/EngineImpl/EditorMainMenuBar/EditorCore 수정.

**승인된 설계 (2026-07-08 사용자 승인):**
- QA-1/2: `RenderOnDemandSkinnedMeshLoading`(EngineRender.cpp:388)이 매 프레임 레지스트리 미스 메시를 재임포트. 로그 근거: Rapi.fbx 1개가 시작 중 7,628회 임포트(총 3분50초). 키 불일치 근본 수정 + 시도 캐시 안전망.
- QA-3: Play 시 월드 메모리 스냅샷 → Stop 시 복원(진짜 초기 상태 복귀). Pause는 틱만 정지. **Step: Pause 중 한 프레임 진행(사용자 추가 요청).** 툴바 Play/Pause/Step/Stop.
- QA-4: 우클릭 유지 중 휠 → `m_cameraMoveSpeed` 노치당 ±10%(0.1~100 클램프). CameraPanel과 같은 변수라 자동 동기.
- QA-5: `ImGuiConfigFlags_ViewportsEnable` + `UpdatePlatformWindows/RenderPlatformWindowsDefault` + 스타일 보정 (표준 win32/dx11 백엔드 사용 중 — EditorCore.cpp:273에서 RenderDrawData 호출 확인됨).

## Global Constraints

- 새 서드파티 의존성 추가 금지. 엔진 코드는 C++17.
- 검증 기준: `cmake --build D:\Github\EGOSIS_Refactoring\build --config Release --target Launch -- /m` 성공 + 에디터 25초 스모크(로그 `[Error]` 0건). 유닛 테스트 프레임워크 없음. GUI 상호작용 검증은 컨트롤러/사용자 몫.
- 커밋: `[fix]`/`[feat]` 한국어 + 마지막 줄 `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.
- 조사된 앵커의 줄 번호는 근사치 — 코드 스니펫으로 매칭할 것.

## 에디터 스모크 절차 (각 태스크 공통, "스모크"로 지칭)

```powershell
$before = Get-Date
$p = Start-Process -FilePath "D:\Github\EGOSIS_Refactoring\build\bin\Release\Launch.exe" -WorkingDirectory "D:\Github\EGOSIS_Refactoring\build\bin\Release" -PassThru
Start-Sleep -Seconds 25
if ($p.HasExited) { throw "editor exited early: $($p.ExitCode)" }
Stop-Process -Id $p.Id -Force; Start-Sleep -Seconds 2
$log = Get-ChildItem D:\Github\EGOSIS_Refactoring\build\bin\Release\Logs\*.log | Where-Object LastWriteTime -gt $before | Sort-Object LastWriteTime | Select-Object -Last 1
(Select-String -Path $log -Pattern "\[Error\]").Count   # 기대: 0
```

---

### Task 1: QA-1/2 — 온디맨드 스킨드메시 재임포트 폭주 수정

**Files:**
- Modify: `EngineSource/Engine/src/Runtime/Engine/EngineRender.cpp` (`RenderOnDemandSkinnedMeshLoading`, ~388행)
- Modify: `EngineSource/Engine/src/Runtime/Engine/EngineImpl.h` (시도 캐시 멤버)
- 조사 대상: `Runtime/Gameplay/Animation/SkinnedMeshRegistry(또는 동명 클래스).LoadFromFbxAsset` / `FbxImporter.cpp:375 ResolveUniqueMeshKey`

**Interfaces:**
- Produces: 동작 변화만. 같은 meshAssetPath는 세션당 1회만 임포트 시도.

- [ ] **Step 1: 근본 원인 조사(리포트에 기록)**

`m_skinnedMeshRegistry.LoadFromFbxAsset(comp.meshAssetPath, comp.instanceAssetPath, ...)`가 임포트 후 등록하는 키가 `comp.meshAssetPath`와 왜 어긋나는지 추적한다:

```
grep -rn "LoadFromFbxAsset" EngineSource/Engine/src --include=*.cpp --include=*.h
grep -n "ResolveUniqueMeshKey" EngineSource/Engine/src/Runtime/Importing/FbxImporter.cpp
```

Rapi 재현 정보: 시작 로그에서 `comp.meshAssetPath`가 요구하는 키와 `[FbxImporter] Registered mesh key="..."`가 등록한 키를 비교하면 어긋난 지점이 보인다(로그: `Import start: path="Resource/Test/fbx/Rapi/Rapi.fbx"` 7,628회, 그러나 `Has(comp.meshAssetPath)`는 계속 false).

- [ ] **Step 2: 근본 수정 — 요청 키로 별칭 등록**

`LoadFromFbxAsset` 성공 후에도 요청 키가 레지스트리에 없으면, 임포트가 실제 등록한 키의 GPU 데이터를 **요청 키로도 별칭 등록**한다. 레지스트리에 별칭 API가 없으면 추가한다(예: `Register(requestedKey, existingGpuSharedPtr)` 재사용 — LoadFromFbxAsset의 반환값/등록 키 조회 API는 조사 결과에 맞춰 최소로 확장). 별칭 등록이 구조적으로 불가능하면(공유 포인터가 아님 등) Step 3의 시도 캐시만으로도 QA 목표(1회 임포트)는 달성되므로 근본 수정은 리포트에 사유와 함께 이연 표기.

- [ ] **Step 3: 안전망 — 임포트 시도 캐시**

`EngineImpl.h`에 멤버 추가(다른 캐시/레지스트리 멤버 근처):

```cpp
		// 온디맨드 스킨드메시 임포트를 시도한 키 기록.
		// 키 불일치/임포트 실패 시 매 프레임 재임포트(시작 지연·프리즈의 원인)를 차단한다.
		std::unordered_set<std::string> m_onDemandMeshAttempted;
```

`RenderOnDemandSkinnedMeshLoading`의 임포트 분기를 다음으로 교체:

```cpp
			if (!m_skinnedMeshRegistry.Has(comp.meshAssetPath) && !comp.instanceAssetPath.empty())
			{
				if (m_onDemandMeshAttempted.count(comp.meshAssetPath) != 0)
					continue; // 이미 시도한 키 — 매 프레임 재임포트 금지

				m_onDemandMeshAttempted.insert(comp.meshAssetPath);

				ALICE_LOG_INFO("[Engine] On-demand loading mesh: meshKey=\"%s\" instanceAssetPath=\"%s\"",
					comp.meshAssetPath.c_str(), comp.instanceAssetPath.c_str());

				m_skinnedMeshRegistry.LoadFromFbxAsset(
					comp.meshAssetPath, comp.instanceAssetPath,
					m_resourceManager, importer, device
				);

				if (!m_skinnedMeshRegistry.Has(comp.meshAssetPath))
				{
					ALICE_LOG_WARN("[Engine] On-demand mesh key mismatch: requested=\"%s\" was not registered by import. (1회만 시도)",
						comp.meshAssetPath.c_str());
				}
			}
```

씬 전환/리로드로 새 메시가 생길 수 있으므로, 씬 로드 커밋 지점(`UpdateCommitPendingSceneChanges`의 `sceneChangedThisFrame = true;` 직전/직후)에서 `m_onDemandMeshAttempted.clear();` 호출을 추가한다.

- [ ] **Step 4: 검증**

빌드 + 스모크. 추가로 시작 로그에서:

```powershell
(Select-String -Path $log -Pattern "Import start: path=").Count   # 기대: 파일당 1회 수준(수십 이하), Rapi 폭주 소멸
```

에디터 시작 소요시간(로그 첫 줄~마지막 초기화 로그 간격)을 리포트에 기록(개선 전 230초).

- [ ] **Step 5: Commit**

```powershell
git add EngineSource/Engine/src/Runtime/Engine/EngineRender.cpp EngineSource/Engine/src/Runtime/Engine/EngineImpl.h EngineSource/Engine/src/Runtime/Engine/EngineUpdate.cpp
git commit -m "[fix] 온디맨드 스킨드메시 재임포트 폭주 수정 - 시작 3분50초/창 프리즈 해소"
```

(별칭 등록 구현 시 해당 레지스트리/임포터 파일도 함께 add)

---

### Task 2: QA-4 — 우클릭+WASD 중 휠로 카메라 속도 조절

**Files:**
- Modify: `EngineSource/Engine/src/Runtime/Engine/EngineUpdate.cpp` (~266행 `if (!input.IsRightButtonDown()) return;` 이후 블록)

**Interfaces:**
- Consumes: `InputSystem`의 휠 델타 — `m_mouseScrollDelta`(InputSystem.cpp:111에서 갱신)의 public 접근자를 grep으로 확인(`grep -n "ScrollDelta" InputSystem.h`). 접근자가 없으면 `float GetMouseScrollDelta() const { return m_mouseScrollDelta; }` 추가.

- [ ] **Step 1: 휠 속도 조절 구현**

`if (!input.IsRightButtonDown()) return;` 바로 아래에 추가:

```cpp
		// 우클릭 비행 모드 중 휠 스크롤로 이동 속도를 조절한다 (Unity 에디터와 동일).
		{
			const float wheel = input.GetMouseScrollDelta(); // 1노치 = ±120
			if (wheel != 0.0f)
			{
				const float notches = wheel / 120.0f;
				m_cameraMoveSpeed *= std::pow(1.1f, notches);
				m_cameraMoveSpeed = std::clamp(m_cameraMoveSpeed, 0.1f, 100.0f);
			}
		}
```

`<cmath>`/`<algorithm>` include 확인. CameraPanel의 Move Speed는 같은 `m_cameraMoveSpeed`를 쓰므로 자동 반영(확인만).

주의: 휠이 다른 곳(줌 등)에서도 소비되는지 확인 — `grep -rn "ScrollDelta\|scrollWheel" EngineSource/Engine/src/Runtime/Engine EngineSource/Engine/src/Editor --include=*.cpp`. 우클릭 중에는 속도 조절이 우선하도록 기존 소비처와 충돌 시 우클릭 가드로 분기.

- [ ] **Step 2: 빌드 + 스모크 + Commit**

```powershell
git add EngineSource/Engine/src/Runtime/Engine/EngineUpdate.cpp EngineSource/Engine/src/Runtime/Input/InputSystem.h
git commit -m "[feat] 에디터 카메라 우클릭 비행 중 휠로 이동속도 조절"
```

---

### Task 3: QA-3 — Unity식 Play/Pause/Step/Stop

**Files:**
- Modify: `EngineSource/Engine/src/Runtime/Engine/EngineImpl.h` (m_isPaused, m_stepOneFrame, m_playModeSnapshot)
- Modify: `EngineSource/Engine/src/Runtime/Engine/EngineUpdate.cpp` (게이트 확장)
- Modify: `EngineSource/Engine/src/Editor/UI/EditorMainMenuBar.cpp` (버튼 3→4개 + 스냅샷/복원 호출)
- 조사 대상: `SceneFile::SaveToJsonString(const World&, std::string&)` (SceneFile.h:24 확인됨) + 대응 로드 API(`SceneFile` 내 LoadFromJson/문자열 로드 탐색; 없으면 임시 파일 경유)

**Interfaces:**
- Produces: `Engine::Impl`에 `bool m_isPaused{false}; bool m_stepOneFrame{false}; std::string m_playModeSnapshot;` + 에디터에서 접근할 setter/getter(기존 isPlaying 전달 방식과 동일 경로 — `DrawMainMenuBar`가 `bool& isPlaying`을 받으므로 pause/step도 같은 방식으로 references 전달하거나 EditorCore 경유. 기존 배선을 조사해 같은 패턴 사용).

- [ ] **Step 1: 틱 게이트 확장 (EngineUpdate.cpp)**

`UpdateShouldUpdateFromScene()`(63행)을 교체:

```cpp
	bool Engine::Impl::UpdateShouldUpdateFromScene() const
	{
		if (!m_editorMode)
			return true;
		if (!m_isPlaying)
			return false;
		// 일시정지 중에는 Step 버튼이 준 한 프레임만 진행한다.
		return !m_isPaused || m_stepOneFrame;
	}
```

프레임 말미(`m_prevIsPlaying = m_isPlaying;` 315행 근처)에 `m_stepOneFrame = false;` 추가. 물리·게임플레이가 이 게이트 밖에서 도는 경로가 있는지 확인(`grep -n "UpdateShouldUpdateFromScene" EngineUpdate.cpp` — 호출부에서 물리 스텝도 게이트되는지 추적)하고, 게이트 밖이면 동일 조건을 물리 스텝에도 적용한다.

- [ ] **Step 2: Play 스냅샷 / Stop 복원**

스냅샷 저장(Play 성공 직후, EditorMainMenuBar Play 핸들러의 `isPlaying = true;` 직전):

```cpp
					// Unity처럼: Play 순간의 월드를 스냅샷 → Stop 시 복원
					std::string snapshot;
					if (SceneFile{}.SaveToJsonString(world, snapshot))
						SetPlayModeSnapshot(std::move(snapshot)); // Engine::Impl 접근 경로는 기존 배선 패턴 사용
					else
						ALICE_LOG_WARN("Play snapshot failed. Stop will not restore the scene.");
```

Stop 핸들러(`isPlaying = false;`)를 확장:

```cpp
				if (ImGui::Button("Stop"))
				{
					isPlaying = false;
					// (pause/step 플래그 리셋)
					RestorePlayModeSnapshot(world); // 스냅샷 있으면 월드 재로드
				}
```

복원 구현: `SceneFile`에 문자열 로드 API가 있으면 사용, 없으면 최소 추가(`bool LoadFromJsonString(World&, const std::string&)` — 기존 파일 로드 구현의 json 파싱부를 재사용해 얇게 분리). 복원은 **씬 전환과 동일한 안전 지점**을 타야 한다: 직접 즉시 로드가 위험하면 기존 SceneManager의 pending-scene 경로(임시 파일 `%TEMP%\Alice_PlaySnapshot.scene`에 저장 후 기존 씬 로드 요청 API 사용)로 우회 — 어느 쪽을 택했는지와 근거를 리포트에 기록. 복원 후 스냅샷 문자열은 비운다.

- [ ] **Step 3: 툴바 버튼 (EditorMainMenuBar.cpp)**

Play 중일 때 기존 Stop 단일 버튼을 Pause/Step/Stop 3개로:

```cpp
			else
			{
				const bool paused = GetIsPaused();
				if (ImGui::Button(paused ? "Resume" : "Pause"))
					SetIsPaused(!paused);
				ImGui::SameLine();
				ImGui::BeginDisabled(!paused);
				if (ImGui::Button("Step"))
					RequestStepOneFrame(); // m_stepOneFrame = true
				ImGui::EndDisabled();
				ImGui::SameLine();
				if (ImGui::Button("Stop"))
				{
					isPlaying = false;
					SetIsPaused(false);
					RestorePlayModeSnapshot(world);
				}
			}
```

접근자 배선은 기존 `isPlaying`이 에디터로 전달되는 방식을 조사해 동일 패턴으로(참조 전달이면 참조 추가, EditorCore 멤버면 멤버 추가). Play 시작 시 `m_isPaused=false` 보장.

- [ ] **Step 4: 검증 + Commit**

빌드 + 스모크(버튼 동작은 컨트롤러/사용자 GUI 검증). 리포트에 스냅샷 복원 방식(직접/펜딩 경유)과 게이트 적용 범위(스크립트/물리) 명시.

```powershell
git add EngineSource/Engine/src/Runtime/Engine/ EngineSource/Engine/src/Editor/UI/EditorMainMenuBar.cpp EngineSource/Engine/src/Runtime/Resources/SceneFile.*
git commit -m "[feat] Unity식 Play/Pause/Step/Stop - Play 스냅샷과 Stop 복원 분리"
```

---

### Task 4: QA-5 — ImGui 멀티 뷰포트 (패널 분리 창)

**Files:**
- Modify: `EngineSource/Engine/src/Editor/Core/EditorCore.cpp` (초기화 162행 부근 + 렌더 273-274행 부근)

- [ ] **Step 1: 플래그 + 스타일**

`io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;`(162행) 아래에 추가:

```cpp
		io.ConfigFlags |= ImGuiConfigFlags_ViewportsEnable; // 패널을 별도 OS 창으로 분리 가능
		ImGuiStyle& style = ImGui::GetStyle();
		if (io.ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			// 분리 창에서는 반투명/라운딩이 OS 창 경계와 어긋나 보이므로 보정
			style.WindowRounding = 0.0f;
			style.Colors[ImGuiCol_WindowBg].w = 1.0f;
		}
```

- [ ] **Step 2: 플랫폼 창 갱신/렌더**

`ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());`(274행) 직후에 추가:

```cpp
		// 멀티 뷰포트: 메인 창 밖으로 분리된 패널들을 갱신/렌더
		if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable)
		{
			ImGui::UpdatePlatformWindows();
			ImGui::RenderPlatformWindowsDefault();
		}
```

- [ ] **Step 3: 확인 사항 (조사 후 필요 시 보완)**

1. WndProc이 `ImGui_ImplWin32_WndProcHandler`를 호출하는지(`grep -rn "WndProcHandler" EngineSource/Engine/src`) — 이미 있으면 그대로.
2. `ImGui_ImplWin32_Init` 호출부에서 플랫폼 창 지원이 자동 활성화됨(도킹 브랜치 기본). 별도 작업 불필요 확인.
3. 스모크에서 크래시 없는지 — 분리 자체는 GUI 검증(사용자).
4. 알려진 제약: `RenderPlatformWindowsDefault`는 자체 스왑체인을 만들므로 엔진 `EndFrame/Present` 순서와 충돌하지 않는지 — RenderDrawData가 메인 백버퍼에 그린 뒤 호출되면 표준 패턴 그대로 OK.

- [ ] **Step 4: 빌드 + 스모크 + Commit**

```powershell
git add EngineSource/Engine/src/Editor/Core/EditorCore.cpp
git commit -m "[feat] ImGui 멀티 뷰포트 활성화 - 에디터 패널을 별도 OS 창으로 분리 가능"
```

---

## 최종 검증 (전 태스크 후)

- [ ] Launch/AlicePlayer/AliceScripts Release 빌드 성공
- [ ] 스모크 [Error] 0건 + FBX Import start 횟수 정상(파일당 1회)
- [ ] 시작 소요시간 기록(개선 전 230초 대비)
- [ ] PR #8에 푸시
- [ ] 사용자 GUI QA: 시작 속도/창 반응, Play→수정→Stop 복원, Pause/Step, 휠 속도, 패널 분리
