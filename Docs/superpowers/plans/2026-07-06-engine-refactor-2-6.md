# EGOSIS 엔진 리팩토링 ②~⑥ 구현 계획

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 에디터 Play 즉시 시작(③), 핫리로드 메모리 안전 관문(④), 애셋 로딩 성능(②⑤), 정직하고 빠른 로딩 화면(⑥), 증분 쿠킹·드래그&드롭 임포트(⑤)를 스펙(`Docs/superpowers/specs/2026-07-06-engine-refactor-2-6-design.md`) 순서대로 구현한다.

**Architecture:** 기존 ResourceManager/ScriptHotReload를 유지·확장한다(전면 재설계 없음). 신규 모듈은 ScriptDomain(핫리로드 관문), ScriptInstanceTracker(인스턴스 레지스트리), AsyncBlobLoader(백그라운드 IO)의 3개.

**Tech Stack:** C++17(엔진)/MSVC VS2022, CMake, D3D11, RTTR(shared), nlohmann::json, Win32.

## Global Constraints

- 새 서드파티 의존성 추가 금지.
- 엔진 코드는 C++17 기준(ScriptsBuild만 C++20).
- 검증 기준 빌드: `cmake --build D:\Github\EGOSIS_Refactoring\build --config Release --target Launch -- /m` 성공 + 에디터 25초 스모크에서 로그 `[Error]` 0건.
- 커밋 메시지는 저장소 관례를 따른다: `[feat] ...` / `[fix] ...` 한국어 요약 + `Co-Authored-By: Claude Fable 5 <noreply@anthropic.com>`.
- `ID3D11DeviceContext`(immediate context)는 절대 워커 스레드에서 사용하지 않는다. `ID3D11Device` 호출만 스레드 세이프.
- **스펙 수정 사항(계획 시점 발견)**: `ResourceLoader<SRV>::Load`가 밉맵 생성에 immediate context를 사용하므로, 비동기 로더는 **blob(IO+복호화) 수준까지만** 워커에서 수행하고 GPU 리소스 생성은 메인 스레드에 남긴다. 프리로드의 지배 비용은 디스크 IO이므로 목표(⑥ UI 무정지)는 그대로 달성된다.
- **스펙 수정 사항 2**: 증분 쿠킹은 게임 빌드가 실제 사용하는 `CookResourceToChunkStore`에만 적용한다(`CookDirectoryRecursive`는 빌드 경로에서 미사용 — YAGNI).

## 에디터 스모크 테스트 절차 (여러 태스크에서 "스모크"로 지칭)

```powershell
Remove-Item D:\Github\EGOSIS_Refactoring\build\bin\Release\Logs\*.log -Force -ErrorAction SilentlyContinue
$p = Start-Process -FilePath "D:\Github\EGOSIS_Refactoring\build\bin\Release\Launch.exe" -WorkingDirectory "D:\Github\EGOSIS_Refactoring\build\bin\Release" -PassThru
Start-Sleep -Seconds 25
if ($p.HasExited) { throw "editor exited early: $($p.ExitCode)" }
Stop-Process -Id $p.Id -Force; Start-Sleep -Seconds 2
$log = Get-ChildItem D:\Github\EGOSIS_Refactoring\build\bin\Release\Logs\*.log | Sort-Object LastWriteTime | Select-Object -Last 1
(Select-String -Path $log -Pattern "\[Error\]").Count   # 기대값: 0
```

---

### Task 1: ③ Play 빌드/리로드 스킵

**Files:**
- Modify: `EngineSource/Engine/src/Runtime/Scripting/ScriptHotReload.h` (IsLoaded 추가)
- Modify: `EngineSource/Engine/src/Runtime/Scripting/ScriptHotReload.cpp`
- Modify: `EngineSource/Engine/src/Editor/Scripting/ScriptReloadHelpers.cpp`

**Interfaces:**
- Produces: `bool ScriptHotReload_IsLoaded();` (Task 3의 ScriptDomain이 이관해 사용)

- [ ] **Step 1: ScriptHotReload에 로드 상태 조회 추가**

`ScriptHotReload.h`의 `void ScriptHotReload_Unload();` 아래에 추가:

```cpp
    /// 현재 스크립트 DLL이 로드되어 있는지 여부
    bool ScriptHotReload_IsLoaded();
```

`ScriptHotReload.cpp`의 `ScriptHotReload_Unload` 함수 뒤에 추가:

```cpp
    bool ScriptHotReload_IsLoaded()
    {
        return g_ScriptModule != nullptr;
    }
```

- [ ] **Step 2: ScriptReloadHelpers에 스킵 판정 헬퍼 추가**

`ScriptReloadHelpers.cpp`의 익명 네임스페이스(`ExecuteCommandWithConsole` 위)에 추가:

```cpp
		// dir 아래 스크립트 소스(.cpp/.h/.hpp)의 가장 최근 수정시각을 구합니다.
		// 소스가 하나도 없거나 순회에 실패하면 anyFound=false.
		std::filesystem::file_time_type NewestScriptSourceTime(
			const std::filesystem::path& dir, bool& anyFound)
		{
			namespace fs = std::filesystem;
			anyFound = false;
			fs::file_time_type newest{};
			std::error_code ec;
			for (fs::recursive_directory_iterator it(dir, ec), end; it != end; it.increment(ec))
			{
				if (ec) { ec.clear(); continue; }
				if (!it->is_regular_file(ec) || ec) { ec.clear(); continue; }
				const auto ext = it->path().extension();
				if (ext != ".cpp" && ext != ".h" && ext != ".hpp")
					continue;
				const auto t = fs::last_write_time(it->path(), ec);
				if (ec) { ec.clear(); continue; }
				if (!anyFound || t > newest) { newest = t; anyFound = true; }
			}
			return newest;
		}

		bool SameFileSizeAndTime(const std::filesystem::path& a, const std::filesystem::path& b)
		{
			namespace fs = std::filesystem;
			std::error_code ec;
			const auto sa = fs::file_size(a, ec); if (ec) return false;
			const auto sb = fs::file_size(b, ec); if (ec) return false;
			if (sa != sb) return false;
			const auto ta = fs::last_write_time(a, ec); if (ec) return false;
			const auto tb = fs::last_write_time(b, ec); if (ec) return false;
			return ta == tb;
		}
```

- [ ] **Step 3: ReloadScripts_FromButton에 2단계 스킵 로직 삽입**

`ReloadScripts_FromButton`에서 `#ifdef _DEBUG ... kConfig ... #endif` 블록 **직후**, `// 1: Configure` 주석 **직전**에 삽입:

```cpp
		// ----------------------------------------------------------------------
		// 0: 스킵 판정 — 소스가 산출물보다 오래됐으면 빌드 생략,
		//    배포본(dll/)이 산출물과 동일하면 리로드 전체 생략.
		// ----------------------------------------------------------------------
		const path intermediateDll = scriptsBuildDir / "bin" / path(kConfig) / "AliceScripts.dll";
		const path deployedDll = exeDir / "dll" / "AliceScripts.dll";

		bool needBuild = true;
		{
			std::error_code ec;
			if (exists(intermediateDll, ec) && !ec)
			{
				bool anySource = false;
				const auto newestSrc = NewestScriptSourceTime(projectRoot / "Assets" / "Scripts", anySource);
				auto newestInput = newestSrc;
				const auto cmakeTime = last_write_time(scriptsCMakePath, ec);
				if (!ec && (!anySource || cmakeTime > newestInput))
				{
					newestInput = cmakeTime;
					anySource = true;
				}
				ec.clear();
				const auto dllTime = last_write_time(intermediateDll, ec);
				if (!ec && anySource && newestInput <= dllTime)
					needBuild = false;
			}
		}

		if (!needBuild)
		{
			if (ScriptHotReload_IsLoaded() && SameFileSizeAndTime(intermediateDll, deployedDll))
			{
				ALICE_LOG_INFO("Reload Scripts: no changes detected. Skipping build and reload.");
				return true;
			}
			ALICE_LOG_INFO("Reload Scripts: sources unchanged. Skipping build (reload only).");
		}
```

- [ ] **Step 4: configure/build 블록을 needBuild로 감싸기**

기존 `// 1: Configure` 블록(`if (!exists(scriptsBuildDir / "CMakeCache.txt")) { ... }`)과 `// 2: Build` 블록(`std::wstring cmdBuild = ...` 부터 `if (buildResult != 0) { ... return false; }` 까지) 전체를 다음으로 감싼다:

```cpp
		if (needBuild)
		{
			// (기존 Configure 블록 그대로)
			// (기존 Build 블록 그대로)
		}
```

기존 `path builtDll = scriptsBuildDir / "bin" / path(kConfig) / "AliceScripts.dll";` 줄은 `path builtDll = intermediateDll;` 로 교체한다(중복 제거, 레거시 폴백 `scriptsBuildDir / path(kConfig)` probe는 유지).

- [ ] **Step 5: 빌드 및 검증**

```powershell
cmake --build D:\Github\EGOSIS_Refactoring\build --config Release --target Launch -- /m
```
기대: `Launch.vcxproj -> ...Launch.exe`, exit 0.

수동 검증(에디터 실행 후):
1. Play → 로그에 `Skipping build and reload` 확인, 콘솔 창 안 뜸, 즉시 시작.
2. `Assets/Scripts`의 아무 .cpp를 touch 후 Play → 빌드 콘솔 뜨고 리로드 수행.

- [ ] **Step 6: Commit**

```powershell
git add EngineSource/Engine/src/Runtime/Scripting/ScriptHotReload.h EngineSource/Engine/src/Runtime/Scripting/ScriptHotReload.cpp EngineSource/Engine/src/Editor/Scripting/ScriptReloadHelpers.cpp
git commit -m "[feat] Play 시 스크립트 무변경이면 빌드/리로드 스킵"
```

---

### Task 2: ④a ScriptInstanceTracker — IScript 인스턴스 레지스트리

**Files:**
- Create: `EngineSource/Engine/src/Runtime/Scripting/ScriptInstanceTracker.h`
- Create: `EngineSource/Engine/src/Runtime/Scripting/ScriptInstanceTracker.cpp`
- Modify: `EngineSource/Engine/src/Runtime/Scripting/IScript.h` (생성자/소멸자 선언)
- Modify: `EngineSource/Engine/src/Runtime/Scripting/IScript.cpp` (등록/해제 구현)

**Interfaces:**
- Produces:
  - `std::size_t ScriptInstanceTracker::AliveCount();`
  - `std::vector<std::string> ScriptInstanceTracker::AliveNames();` — DLL 언로드 **전에만** 호출 안전(vtable 필요)
  - (내부용) `ScriptInstanceTracker::OnCreated(IScript*)` / `OnDestroyed(IScript*)`

- [ ] **Step 1: ScriptInstanceTracker.h 작성**

```cpp
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace Alice
{
    class IScript;

    /// 살아있는 모든 IScript 인스턴스를 추적합니다.
    /// - 목적: 스크립트 DLL 언로드 전에 "인스턴스 0개" 불변식을 검사해
    ///   vtable 소실로 인한 누수/크래시를 원천 차단합니다.
    namespace ScriptInstanceTracker
    {
        void OnCreated(IScript* instance);
        void OnDestroyed(IScript* instance);

        std::size_t AliveCount();

        /// 살아있는 인스턴스들의 클래스 이름 목록.
        /// 주의: GetName()은 vtable을 사용하므로 DLL 언로드 전에만 호출할 것.
        std::vector<std::string> AliveNames();
    }
}
```

- [ ] **Step 2: ScriptInstanceTracker.cpp 작성**

```cpp
#include "Runtime/Scripting/ScriptInstanceTracker.h"
#include "Runtime/Scripting/IScript.h"

#include <mutex>
#include <unordered_set>

namespace Alice::ScriptInstanceTracker
{
    namespace
    {
        std::mutex g_mutex;
        std::unordered_set<IScript*> g_alive;
    }

    void OnCreated(IScript* instance)
    {
        if (!instance) return;
        std::lock_guard<std::mutex> lock(g_mutex);
        g_alive.insert(instance);
    }

    void OnDestroyed(IScript* instance)
    {
        if (!instance) return;
        std::lock_guard<std::mutex> lock(g_mutex);
        g_alive.erase(instance);
    }

    std::size_t AliveCount()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_alive.size();
    }

    std::vector<std::string> AliveNames()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        std::vector<std::string> names;
        names.reserve(g_alive.size());
        for (IScript* s : g_alive)
            names.emplace_back(s->GetName());
        return names;
    }
}
```

- [ ] **Step 3: IScript에 훅 연결**

`IScript.h`에서 `virtual ~IScript() = default;` 를 다음으로 교체:

```cpp
        IScript();
        virtual ~IScript();
```

`IScript.cpp`(기존 파일)의 `namespace Alice {` 안, 파일 상단부에 추가:

```cpp
    IScript::IScript()
    {
        ScriptInstanceTracker::OnCreated(this);
    }

    IScript::~IScript()
    {
        ScriptInstanceTracker::OnDestroyed(this);
    }
```

`IScript.cpp` 상단 include에 추가:

```cpp
#include "Runtime/Scripting/ScriptInstanceTracker.h"
```

- [ ] **Step 4: 빌드 확인**

```powershell
cmake -S D:\Github\EGOSIS_Refactoring -B D:\Github\EGOSIS_Refactoring\build -G "Visual Studio 17 2022"
cmake --build D:\Github\EGOSIS_Refactoring\build --config Release --target Launch -- /m
```
기대: 성공. (신규 .cpp는 GLOB 기반이면 configure 재실행으로 잡힘 — 실패 시 CMakeLists의 소스 수집 방식 확인)

주의: **ScriptsBuild(AliceScripts.dll)도 재빌드 필요** — IScript 생성자가 엔진 Engine.lib에 있으므로:

```powershell
cmake --build D:\Github\EGOSIS_Refactoring\EngineSource\ScriptsBuild\build --config Release --target AliceScripts -- /m
```

- [ ] **Step 5: 스모크 + Commit**

스모크 통과 후:

```powershell
git add EngineSource/Engine/src/Runtime/Scripting/ScriptInstanceTracker.h EngineSource/Engine/src/Runtime/Scripting/ScriptInstanceTracker.cpp EngineSource/Engine/src/Runtime/Scripting/IScript.h EngineSource/Engine/src/Runtime/Scripting/IScript.cpp
git commit -m "[feat] IScript 인스턴스 레지스트리(ScriptInstanceTracker) 추가"
```

---

### Task 3: ④b ScriptDomain — 핫리로드 단일 관문 + 복사본 로드

**Files:**
- Create: `EngineSource/Engine/src/Runtime/Scripting/ScriptDomain.h`
- Create: `EngineSource/Engine/src/Runtime/Scripting/ScriptDomain.cpp`
- Modify: `EngineSource/Engine/src/Runtime/Scripting/ScriptHotReload.cpp` (복사본 로드 + 불변식)
- Modify: `EngineSource/Engine/src/Runtime/Scripting/ScriptHotReload.h` (Unload 반환형 bool)
- Modify: `EngineSource/Engine/src/Editor/Scripting/ScriptReloadHelpers.cpp` (스냅샷/복원을 ScriptDomain 호출로 교체)
- Modify: `EngineSource/Engine/src/Editor/Tools/BuildGameWindow.cpp:782` (직접 Unload → ScriptDomain::Unload)

**Interfaces:**
- Consumes: Task 2의 `ScriptInstanceTracker::AliveCount()/AliveNames()`
- Produces:
  - `bool ScriptDomain::Reload(World& world);` — 스냅샷→파괴→언로드→로드→복원
  - `bool ScriptDomain::Unload(World& world);` — 안전 파괴 후 언로드(복원 없음)
  - `ScriptHotReload_Unload()`의 시그니처가 `bool`로 변경됨(인스턴스 잔존 시 false)

- [ ] **Step 1: ScriptHotReload에 복사본 로드 + 불변식 적용**

`ScriptHotReload.cpp`의 `LoadInternal`에서 DLL 경로 결정 후 `LoadLibraryW` **직전**에 복사본 생성 로직을 넣는다. 기존:

```cpp
			const wchar_t* loadedPath = dllPathInDir.c_str();
			HMODULE mod = ::LoadLibraryW(loadedPath);
			if (!mod)
			{
				loadedPath = dllPathExe.c_str();
				mod = ::LoadLibraryW(loadedPath);
			}
```

를 다음으로 교체:

```cpp
			// 원본을 직접 로드하면 파일이 잠겨 다음 빌드/복사가 실패한다.
			// → "_live" 복사본을 만들어 그 파일을 로드한다. (원본은 항상 잠금 없음)
			auto MakeLiveCopy = [](const std::filesystem::path& src) -> std::filesystem::path
			{
				std::error_code ec;
				if (!std::filesystem::exists(src, ec) || ec)
					return {};
				std::filesystem::path live = src;
				live.replace_filename(src.stem().wstring() + L"_live" + src.extension().wstring());
				std::filesystem::copy_file(src, live,
					std::filesystem::copy_options::overwrite_existing, ec);
				if (ec)
				{
					// 복사 실패(이전 live가 아직 로드 중 등) 시 원본 직접 로드로 폴백
					return src;
				}
				return live;
			};

			std::filesystem::path candidate = MakeLiveCopy(dllPathInDir);
			if (candidate.empty())
				candidate = MakeLiveCopy(dllPathExe);

			std::wstring loadedPathStr = candidate.wstring();
			HMODULE mod = loadedPathStr.empty() ? nullptr : ::LoadLibraryW(loadedPathStr.c_str());
			const wchar_t* loadedPath = loadedPathStr.c_str();
```

(이후 기존 코드의 `loadedPath` 사용부는 그대로 동작한다.)

`ScriptHotReload.h`에서 `void ScriptHotReload_Unload();` → `bool ScriptHotReload_Unload();` 로 변경하고, `ScriptHotReload.cpp`의 구현을 다음으로 교체:

```cpp
    bool ScriptHotReload_Unload()
    {
        if (g_ScriptModule)
        {
            // 불변식: 살아있는 스크립트 인스턴스가 있으면 언로드 금지.
            // (vtable이 사라진 뒤 파괴/호출되면 UB — 누수·크래시의 근원)
            const std::size_t alive = ScriptInstanceTracker::AliveCount();
            if (alive > 0)
            {
                ALICE_LOG_ERRORF("ScriptHotReload: unload BLOCKED. %zu script instance(s) still alive:", alive);
                for (const auto& name : ScriptInstanceTracker::AliveNames())
                    ALICE_LOG_ERRORF("  - %s", name.c_str());
                return false;
            }

            if (g_UnbindServices)
                g_UnbindServices();
            ::FreeLibrary(g_ScriptModule);
            g_ScriptModule = nullptr;
        }

        SetDynamicScriptFunctions(nullptr, nullptr, nullptr);
        g_BindServices = nullptr;
        g_UnbindServices = nullptr;

        ALICE_LOG_INFO("ScriptHotReload: unloaded script DLL");
        return true;
    }
```

`ScriptHotReload.cpp` 상단 include에 추가:

```cpp
#include "Runtime/Scripting/ScriptInstanceTracker.h"
```

주의: `LoadInternal` 첫머리의 기존 `ScriptHotReload_Unload();` 호출은 `if (!ScriptHotReload_Unload()) return false;` 로 교체한다.

- [ ] **Step 2: ScriptDomain.h 작성**

```cpp
#pragma once

namespace Alice
{
    class World;

    /// 스크립트 DLL 로드/언로드의 단일 관문.
    /// Unity 도메인 리로드 방식: 스냅샷(JSON) → 인스턴스 전부 파괴 →
    /// 콜백 클리어 → 언로드 → (복사본) 로드 → 복원.
    /// 다른 코드는 ScriptHotReload_*를 직접 호출하지 말 것.
    namespace ScriptDomain
    {
        /// 엔진 시작 시 최초 로드 (복원할 상태 없음)
        bool LoadInitial();

        /// 새 DLL로 교체. 스크립트 상태는 JSON으로 보존 후 복원된다.
        bool Reload(World& world);

        /// 게임 빌드 직전 등 DLL을 완전히 내려야 할 때. 복원하지 않는다.
        bool Unload(World& world);

        bool IsLoaded();
    }
}
```

- [ ] **Step 3: ScriptDomain.cpp 작성 — 스냅샷/복원을 ScriptReloadHelpers에서 이관**

`ScriptReloadHelpers.cpp`의 `ScriptReloadSnap`/`EntityReloadSnap`/`SnapshotAndDestroyScripts`/`RestoreScripts`를 그대로 가져와 다음 파일을 만든다:

```cpp
#include "Runtime/Scripting/ScriptDomain.h"

#include "Runtime/ECS/World.h"
#include "Runtime/Resources/Serialization/JsonRttr.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Scripting/ScriptHotReload.h"
#include "Runtime/Scripting/ScriptInstanceTracker.h"
#include "Runtime/Foundation/Logger.h"
#include "ThirdParty/json/json.hpp"

#include <string>
#include <vector>

namespace Alice::ScriptDomain
{
    namespace
    {
        struct ScriptReloadSnap
        {
            std::string name;
            bool enabled{};
            nlohmann::json props;
        };

        struct EntityReloadSnap
        {
            EntityId id{};
            std::vector<ScriptReloadSnap> scripts;
        };

        // (ScriptReloadHelpers.cpp의 SnapshotAndDestroyScripts 본문 그대로)
        void SnapshotAndDestroyScripts(World& world, std::vector<EntityReloadSnap>& out)
        {
            out.clear();
            auto& map = world.GetAllScriptsInWorld();
            out.reserve(map.size());
            for (auto& [id, list] : map)
            {
                EntityReloadSnap e{};
                e.id = id;
                e.scripts.reserve(list.size());
                for (auto& sc : list)
                {
                    ScriptReloadSnap s{};
                    s.name = sc.scriptName;
                    s.enabled = sc.enabled;
                    if (sc.instance && !sc.scriptName.empty())
                    {
                        rttr::type t = rttr::type::get_by_name(sc.scriptName);
                        s.props = JsonRttr::ToJsonObject(*sc.instance, t);
                        sc.instance->OnDisable();
                        sc.instance->OnDestroy();
                        sc.instance.reset();
                    }
                    sc.awoken = false;
                    sc.started = false;
                    sc.wasEnabled = sc.enabled;
                    sc.defaultsApplied = false;
                    e.scripts.push_back(std::move(s));
                }
                out.push_back(std::move(e));
            }
        }

        // (ScriptReloadHelpers.cpp의 RestoreScripts 본문 그대로)
        void RestoreScripts(World& world, const std::vector<EntityReloadSnap>& snaps)
        {
            auto& map = world.GetAllScriptsInWorld();
            for (const auto& e : snaps)
            {
                auto it = map.find(e.id);
                if (it == map.end())
                    continue;
                std::vector<ScriptComponent> rebuilt;
                rebuilt.reserve(e.scripts.size());
                for (const auto& s : e.scripts)
                {
                    if (s.name.empty())
                        continue;
                    ScriptComponent sc{};
                    sc.scriptName = s.name;
                    sc.enabled = s.enabled;
                    sc.instance = ScriptFactory::Create(s.name.c_str());
                    if (!sc.instance)
                    {
                        ALICE_LOG_WARN("ScriptDomain: script \"%s\" not found in new DLL. Component kept without instance.", s.name.c_str());
                        continue;
                    }
                    sc.instance->SetContext(&world, e.id);
                    rttr::instance inst = *sc.instance;
                    rttr::type t = rttr::type::get_by_name(sc.scriptName);
                    JsonRttr::FromJsonObject(inst, s.props, t);
                    sc.defaultsApplied = true;
                    rebuilt.push_back(std::move(sc));
                }
                it->second = std::move(rebuilt);
                if (it->second.empty())
                    map.erase(it);
            }
        }

        // Task 4에서 구현이 채워진다. 지금은 no-op.
        void ClearDllOriginatedCallbacks(World& world)
        {
            (void)world;
        }

        bool TeardownForUnload(World& world, std::vector<EntityReloadSnap>* outSnaps)
        {
            std::vector<EntityReloadSnap> localSnaps;
            SnapshotAndDestroyScripts(world, outSnaps ? *outSnaps : localSnaps);
            ClearDllOriginatedCallbacks(world);

            if (!ScriptHotReload_Unload())
            {
                ALICE_LOG_ERRORF("ScriptDomain: unload blocked by alive instances. See log above.");
                return false;
            }
            return true;
        }
    }

    bool LoadInitial()
    {
        return ScriptHotReload_Load();
    }

    bool Reload(World& world)
    {
        std::vector<EntityReloadSnap> snaps;
        if (!TeardownForUnload(world, &snaps))
            return false;

        if (!ScriptHotReload_Reload())
        {
            ALICE_LOG_ERRORF("ScriptDomain: reload failed after unload.");
            return false;
        }

        RestoreScripts(world, snaps);
        return true;
    }

    bool Unload(World& world)
    {
        return TeardownForUnload(world, nullptr);
    }

    bool IsLoaded()
    {
        return ScriptHotReload_IsLoaded();
    }
}
```

- [ ] **Step 4: 호출부 교체**

`ScriptReloadHelpers.cpp`:
- 이관한 `ScriptReloadSnap`/`EntityReloadSnap`/`SnapshotAndDestroyScripts`/`RestoreScripts` 정의를 **삭제**.
- include에 `#include "Runtime/Scripting/ScriptDomain.h"` 추가.
- 함수 끝부분의 기존 시퀀스:

```cpp
		std::vector<EntityReloadSnap> snaps;
		SnapshotAndDestroyScripts(world, snaps);
		ScriptHotReload_Unload();
		... (dll 복사) ...
		if (!ScriptHotReload_Reload()) { ... }
		RestoreScripts(world, snaps);
		return true;
```

를 다음으로 교체(복사를 먼저, 도메인 리로드를 마지막에 — 복사본 로드 덕분에 원본이 잠기지 않으므로 로드 중에도 복사 가능):

```cpp
		// 새 DLL을 배포 위치로 복사 (원본은 _live 복사본으로만 로드되므로 잠금 없음)
		path targetDll = dllDir / "AliceScripts.dll";
		std::error_code ecCopy;
		copy_file(builtDll, targetDll, copy_options::overwrite_existing, ecCopy);
		if (ecCopy)
		{
			ALICE_LOG_ERRORF("Reload Scripts: failed to copy DLL from \"%s\" to \"%s\" (%s)",
				builtDll.string().c_str(), targetDll.string().c_str(), ecCopy.message().c_str());
			return false;
		}

		ALICE_LOG_INFO("Reload Scripts: copied \"%s\" -> \"%s\"",
			builtDll.string().c_str(), targetDll.string().c_str());

		return ScriptDomain::Reload(world);
```

(rttr_core 복사 블록과 dllDir 생성 블록은 그대로 유지. 기존의 `ScriptHotReload_Unload()` / `ScriptHotReload_Reload()` 직접 호출은 모두 제거.)

`BuildGameWindow.cpp:782`의 `ScriptHotReload_Unload();` 를 다음으로 교체:

```cpp
					if (m_worldPtr) // BuildGameWindow가 World 접근자를 이미 가지고 있는지 확인
						ScriptDomain::Unload(*m_worldPtr);
					else
						ScriptHotReload_Unload();
```

주의: `BuildGameWindow`에 World 접근자가 없다면(`m_isPlayingPtr`만 있는 구조) 멤버 `World* m_worldPtr`를 추가하고 초기화 지점(에디터가 BuildGameWindow를 생성/설정하는 곳 — `grep -rn "BuildGameWindow\|m_isPlayingPtr" EngineSource/Engine/src/Editor`로 확인)에서 함께 주입한다. include에 `#include "Runtime/Scripting/ScriptDomain.h"` 추가.

`EngineInitialize.cpp`의 `InitializeCameraAndScriptHotReload()`에서 `ScriptHotReload_Load();` → `ScriptDomain::LoadInitial();` 로 교체하고 include 추가.

- [ ] **Step 5: 빌드 + 스모크 + 리로드 반복 검증**

```powershell
cmake -S D:\Github\EGOSIS_Refactoring -B D:\Github\EGOSIS_Refactoring\build -G "Visual Studio 17 2022"
cmake --build D:\Github\EGOSIS_Refactoring\build --config Release --target Launch -- /m
```

수동 검증:
1. 에디터 실행 → 로그에 `ScriptHotReload: loaded "...AliceScripts_live.dll"` 확인.
2. Reload Scripts 버튼 5회 → 매회 성공, 작업관리자에서 Working Set이 계단식으로 증가하지 않는지 확인(±수 MB 변동은 정상).
3. Play → Stop → Build Game 창 열기 → Build Game 눌러도 크래시 없음.

- [ ] **Step 6: Commit**

```powershell
git add EngineSource/Engine/src/Runtime/Scripting/ EngineSource/Engine/src/Editor/Scripting/ScriptReloadHelpers.cpp EngineSource/Engine/src/Editor/Tools/BuildGameWindow.cpp EngineSource/Engine/src/Runtime/Engine/EngineInitialize.cpp
git commit -m "[feat] ScriptDomain 핫리로드 단일 관문 + _live 복사본 로드 + 언로드 불변식"
```

---

### Task 4: ④c DLL-출처 콜백 클리어

**Files:**
- Modify: `EngineSource/Engine/src/Runtime/Scripting/ScriptDomain.cpp` (`ClearDllOriginatedCallbacks` 구현)

**Interfaces:**
- Consumes: `World::GetComponents<UIButtonComponent>()` (기존 API — `EngineWindow.cpp:136`의 `m_world.GetComponents<CameraFollowComponent>()` 사용 패턴과 동일), `UIButtonComponent::ClearDelegates()`

- [ ] **Step 1: std::function 보유처 전수 조사(기록용)**

```powershell
# 결과를 커밋 메시지에 요약해 남긴다
Select-String -Path EngineSource\Engine\src\Runtime\UI\*.h,EngineSource\Engine\src\Runtime\ECS\*.h,EngineSource\Engine\src\Runtime\Gameplay\Animation\*.h -Pattern "std::function"
```

판정 기준: **스크립트가 런타임에 등록할 수 있는 콜백**만 클리어 대상. (조사 시점 기준 대상: `UIButtonComponent::onPressed/onReleased/onHovered`. `EditorComponentRegistry`·`Delegate`·`AdvancedAnimationComponent`는 엔진 내부 등록이므로 제외 — 조사 결과가 다르면 대상에 추가.)

- [ ] **Step 2: ClearDllOriginatedCallbacks 구현**

`ScriptDomain.cpp`의 no-op을 교체:

```cpp
        // 스크립트(DLL 코드)가 캡처된 람다를 남길 수 있는 콜백을 일괄 해제한다.
        // 규약: 스크립트는 Awake/OnEnable에서 콜백을 다시 바인딩해야 한다.
        void ClearDllOriginatedCallbacks(World& world)
        {
            std::size_t cleared = 0;
            for (auto&& [id, button] : world.GetComponents<UIButtonComponent>())
            {
                (void)id;
                const std::size_t before =
                    button.onPressed.size() + button.onReleased.size() + button.onHovered.size();
                button.ClearDelegates();
                cleared += before;
            }
            if (cleared > 0)
                ALICE_LOG_INFO("ScriptDomain: cleared %zu UI button delegate(s) before DLL unload.", cleared);
        }
```

include 추가:

```cpp
#include "Runtime/UI/UIButtonComponent.h"
```

- [ ] **Step 3: 빌드 + 검증 + Commit**

빌드 후 수동 검증: UI 버튼이 있는 씬(예: TilteScene)에서 Play → 버튼 클릭 동작 확인 → Stop → Reload Scripts → 다시 Play → 버튼이 여전히 동작(스크립트 Awake 재바인딩)하는지 확인.

```powershell
git add EngineSource/Engine/src/Runtime/Scripting/ScriptDomain.cpp
git commit -m "[fix] 핫리로드 전 UI 버튼 델리게이트 일괄 해제 (댕글링 콜백 차단)"
```

---

### Task 5: ②a ResourceManager negative cache

**Files:**
- Modify: `EngineSource/Engine/src/Runtime/Resources/ResourceManager.h`
- Modify: `EngineSource/Engine/src/Runtime/Resources/ResourceManager.cpp`

**Interfaces:**
- Produces: `void ResourceManager::ClearNegativeCache();` (Task 11·12가 호출)

- [ ] **Step 1: 헤더에 멤버·API 추가**

`ResourceManager.h`의 `void Clear();` 아래에 추가:

```cpp
        /// 로드 실패 기록(negative cache)을 비웁니다.
        /// 에디터에서 애셋을 새로 임포트/복사한 뒤 호출하세요.
        void ClearNegativeCache();
```

private 멤버(`m_pathToHash` 아래)에 추가:

```cpp
        // 실패한 논리 경로 기록 — 재시도로 인한 매 프레임 디스크 접근을 차단.
        // 게임 모드에서는 파일이 불변이므로 영구, 에디터에서는 ClearNegativeCache로 무효화.
        mutable std::unordered_set<std::string> m_missingPaths;
```

`#include <unordered_set>` 를 헤더 include에 추가.

- [ ] **Step 2: LoadSharedBinaryAuto에 negative cache 적용**

`ResourceManager.cpp`의 `LoadSharedBinaryAuto`에서, `logicalKey` 계산 직후·기존 캐시 조회(`// 0)`) 블록 안에 실패 조회를 추가한다. 기존 블록:

```cpp
        // 0) logicalPath -> contentHash 캐시
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            if (auto it = m_pathToHash.find(logicalKey); it != m_pathToHash.end())
            ...
        }
```

블록 마지막(닫는 `}` 직전)에 추가:

```cpp
            if (m_missingPaths.count(logicalKey) != 0)
                return nullptr; // 이미 실패한 경로 — 디스크 접근 없이 즉시 반환
```

그리고 함수 내 **모든 `return nullptr;` 실패 지점**(gameMode Assets 청크 실패, Resource 청크 실패, Cooked LoadBinary 실패, 마지막 폴백 실패, editorMode LoadBinary 실패 — 총 5곳)을 다음 헬퍼 호출로 교체한다. 익명 네임스페이스가 아닌 클래스 내부 private 메서드로 추가(`ResourceManager.h` private에 선언):

```cpp
        std::shared_ptr<const std::vector<std::uint8_t>> MarkMissing(const std::string& logicalKey) const;
```

`ResourceManager.cpp` 구현:

```cpp
    std::shared_ptr<const std::vector<std::uint8_t>> ResourceManager::MarkMissing(const std::string& logicalKey) const
    {
        bool firstTime = false;
        {
            std::lock_guard<std::mutex> lock(m_cacheMutex);
            firstTime = m_missingPaths.insert(logicalKey).second;
        }
        if (firstTime)
            ALICE_LOG_WARN("ResourceManager: load failed (cached as missing, will not retry): \"%s\"", logicalKey.c_str());
        return nullptr;
    }

    void ResourceManager::ClearNegativeCache()
    {
        std::lock_guard<std::mutex> lock(m_cacheMutex);
        m_missingPaths.clear();
    }
```

교체 예 — 기존:

```cpp
                ALICE_LOG_ERRORF("ResourceManager: Chunk not found for \"%s\"", s.c_str());
                return nullptr;
```

교체 후:

```cpp
                ALICE_LOG_ERRORF("ResourceManager: Chunk not found for \"%s\"", s.c_str());
                return MarkMissing(logicalKey);
```

(나머지 4곳도 동일하게 `return nullptr;` → `return MarkMissing(logicalKey);`)

- [ ] **Step 3: 빌드 + 검증**

빌드 후 검증: 에디터 실행 → 존재하지 않는 텍스처를 참조하는 머티리얼이 있어도 같은 경로의 `load failed (cached as missing...)` 경고가 **로그에 1회만** 나타나는지 확인:

```powershell
$log = Get-ChildItem D:\Github\EGOSIS_Refactoring\build\bin\Release\Logs\*.log | Sort-Object LastWriteTime | Select-Object -Last 1
Select-String -Path $log -Pattern "cached as missing" | Group-Object Line | Where-Object Count -gt 1   # 기대값: 없음
```

- [ ] **Step 4: Commit**

```powershell
git add EngineSource/Engine/src/Runtime/Resources/ResourceManager.h EngineSource/Engine/src/Runtime/Resources/ResourceManager.cpp
git commit -m "[feat] ResourceManager negative cache - 실패 경로 재시도 차단"
```

---

### Task 6: ②b LRU 강참조 캐시

**Files:**
- Modify: `EngineSource/Engine/src/Runtime/Resources/ResourceManager.h`
- Modify: `EngineSource/Engine/src/Runtime/Resources/ResourceManager.cpp`

**Interfaces:**
- Produces: 동작 변화만(외부 API 불변). `m_blobCache`(weak) 적중 실패 시에도 LRU가 blob을 붙잡고 있으면 재로드가 발생하지 않는다.

- [ ] **Step 1: 헤더에 LRU 멤버 추가**

`ResourceManager.h` private(`m_missingPaths` 아래):

```cpp
        // 최근 사용 blob의 강참조 LRU — weak_ptr 캐시의 조기 해제로 인한
        // 반복 재로드를 방지한다. 기본 상한 256MB.
        mutable std::list<std::pair<std::uint64_t, std::shared_ptr<const std::vector<std::uint8_t>>>> m_lruList;
        mutable std::unordered_map<std::uint64_t, decltype(m_lruList)::iterator> m_lruIndex;
        mutable std::size_t m_lruBytes = 0;
        std::size_t m_lruCapacityBytes = 256ull * 1024 * 1024;

        void TouchLru(std::uint64_t hash,
                      const std::shared_ptr<const std::vector<std::uint8_t>>& blob) const; // m_cacheMutex 잠근 상태에서 호출
```

`#include <list>` 추가.

- [ ] **Step 2: TouchLru 구현 + 삽입 지점 연결**

`ResourceManager.cpp`:

```cpp
    void ResourceManager::TouchLru(std::uint64_t hash,
                                   const std::shared_ptr<const std::vector<std::uint8_t>>& blob) const
    {
        // 주의: 호출자가 m_cacheMutex를 이미 잠갔다고 가정한다.
        if (auto it = m_lruIndex.find(hash); it != m_lruIndex.end())
        {
            m_lruList.splice(m_lruList.begin(), m_lruList, it->second); // 앞으로 이동
            return;
        }

        m_lruList.emplace_front(hash, blob);
        m_lruIndex[hash] = m_lruList.begin();
        m_lruBytes += blob->size();

        while (m_lruBytes > m_lruCapacityBytes && !m_lruList.empty())
        {
            auto& back = m_lruList.back();
            m_lruBytes -= back.second->size();
            m_lruIndex.erase(back.first);
            m_lruList.pop_back();
        }
    }
```

`LoadSharedBinaryAuto`에서 blob을 캐시에 넣는 **모든 지점**(`m_blobCache[h] = sp; m_pathToHash[logicalKey] = h;` 패턴 — gameMode Assets/Resource/Cooked/폴백 + editorMode, 총 5곳)에 한 줄 추가:

```cpp
                    m_blobCache[h] = sp;
                    m_pathToHash[logicalKey] = h;
                    TouchLru(h, sp);
```

캐시 적중 지점(`// 0)` 블록의 `if (auto sp = it2->second.lock()) return sp;`)도 교체:

```cpp
                    if (auto sp = it2->second.lock())
                    {
                        TouchLru(h, sp);
                        return sp;
                    }
```

`Clear()`와 `ClearNegativeCache()`에는 LRU도 함께 비우는 코드 추가:

```cpp
        m_lruList.clear();
        m_lruIndex.clear();
        m_lruBytes = 0;
```

(`Clear()`가 현재 빈 구현이면 m_cacheMutex 잠금 후 m_blobCache/m_pathToHash/LRU/negative 전부 비우도록 채운다.)

- [ ] **Step 3: 게임 모드 청크 Resolve 프로브 캐시**

`ResourceManager.h` private에 추가(`m_lruCapacityBytes` 아래):

```cpp
        // gameMode에서 Resolve()의 청크 경로 프로브(exists 2회) 결과 캐시.
        // 게임 모드는 파일이 불변이므로 무효화가 필요 없다.
        mutable std::unordered_map<std::string, std::filesystem::path> m_chunkPathCache;
```

`ResourceManager.cpp`의 `Resolve()`에서 gameMode Assets 분기(기존):

```cpp
            if (m_gameMode)
            {
                const std::string rest = s.substr(std::string_view("Assets/").size());
                auto path = Chunk0PathForMetasRel(rest);
                std::error_code ec;
                if (!std::filesystem::exists(path, ec))
                {
                    const std::uint64_t legacyId = HashRelLegacy(rest);
                    auto legacyPath = Chunk0PathFromFileId(MetasDir(), legacyId);
                    ec.clear();
                    if (std::filesystem::exists(legacyPath, ec))
                        return legacyPath;
                }
                return path;
            }
```

교체:

```cpp
            if (m_gameMode)
            {
                {
                    std::lock_guard<std::mutex> lock(m_cacheMutex);
                    if (auto it = m_chunkPathCache.find(s); it != m_chunkPathCache.end())
                        return it->second;
                }

                const std::string rest = s.substr(std::string_view("Assets/").size());
                auto path = Chunk0PathForMetasRel(rest);
                std::error_code ec;
                if (!std::filesystem::exists(path, ec))
                {
                    const std::uint64_t legacyId = HashRelLegacy(rest);
                    auto legacyPath = Chunk0PathFromFileId(MetasDir(), legacyId);
                    ec.clear();
                    if (std::filesystem::exists(legacyPath, ec))
                        path = legacyPath;
                }

                std::lock_guard<std::mutex> lock(m_cacheMutex);
                m_chunkPathCache[s] = path;
                return path;
            }
```

gameMode Resource 분기(`Chunk0PathForResourceRel`/`CookedDir()` 사용, 구조 동일)도 같은 패턴으로 교체한다(캐시 키는 동일하게 `s` — "Assets/..."와 "Resource/..." 접두사가 달라 충돌 없음).

`Clear()`에 `m_chunkPathCache.clear();` 추가. (`ClearNegativeCache()`에는 넣지 않는다 — 이 캐시는 게임 모드 전용이고 에디터 경로는 캐시를 타지 않음)

- [ ] **Step 4: 빌드 + 스모크 + Commit**

빌드/스모크 통과 후:

```powershell
git add EngineSource/Engine/src/Runtime/Resources/ResourceManager.h EngineSource/Engine/src/Runtime/Resources/ResourceManager.cpp
git commit -m "[feat] ResourceManager 256MB LRU 강참조 캐시 + 청크 Resolve 프로브 캐시"
```

---

### Task 7: ②c 렌더 시스템 텍스처 실패 캐시

**Files:**
- Modify: `EngineSource/Engine/src/Runtime/Rendering/ForwardRenderSystem.cpp:1031-1058` (`GetOrCreateTexture`)
- Modify: `EngineSource/Engine/src/Runtime/Rendering/DeferredRenderSystem.cpp:5543-5572` (`GetOrCreateTexture`)

**Interfaces:**
- Consumes: 기존 `m_textureCache` (`unordered_map<string, ComPtr<ID3D11ShaderResourceView>>`)
- Produces: 실패도 캐시 항목(nullptr ComPtr)으로 기록되어 재시도가 발생하지 않음.
- 조사 결과 수정 불필요 확인: `UIRenderer::GetTexture`(UIRenderer.cpp:2604)는 이미 실패를 캐시하고, `SkinnedMeshSystem::BuildDrawList`의 emissive 경고(SkinnedMeshSystem.h:161)는 `s_matEmissivePathCache`로 머티리얼당 1회만 출력됨. `UnityVfxMeshRenderSystem`(2054행)은 머티리얼 로드 시점 1회 실행이라 per-frame 아님 — Task 5의 negative cache로 커버됨.

- [ ] **Step 1: ForwardRenderSystem::GetOrCreateTexture 수정**

실패 분기(기존):

```cpp
        if (!srv)
        {
            ALICE_LOG_WARN("[ForwardRenderSystem] Texture load FAILED: \"%s\"", path.c_str());
            return nullptr;
        }
```

교체:

```cpp
        if (!srv)
        {
            // 실패도 캐시해 매 드로우 재시도를 차단한다. (경고는 이 1회만)
            m_textureCache.emplace(path, nullptr);
            ALICE_LOG_WARN("[ForwardRenderSystem] Texture load FAILED (cached, will not retry): \"%s\"", path.c_str());
            return nullptr;
        }
```

- [ ] **Step 2: DeferredRenderSystem::GetOrCreateTexture 동일 수정**

같은 패턴으로 `DeferredRenderSystem.cpp:5562-5566`의 실패 분기를 교체(로그 태그만 `[DeferredRenderSystem]`).

- [ ] **Step 3: 빌드 + FPS 검증 + Commit**

검증: 씬 파일 하나를 열고 `.mat`이 가리키는 텍스처 파일을 임시로 이름 변경 → 에디터 실행 → FPS(메뉴바 표시)가 정상 씬과 동일 수준인지, `FAILED (cached` 로그가 경로당 1회인지 확인 → 파일명 원복.

```powershell
git add EngineSource/Engine/src/Runtime/Rendering/ForwardRenderSystem.cpp EngineSource/Engine/src/Runtime/Rendering/DeferredRenderSystem.cpp
git commit -m "[fix] 렌더 시스템 텍스처 로드 실패 캐시 - 매 프레임 fallback 재시도 제거"
```

---

### Task 8: ⑤a AsyncBlobLoader

**Files:**
- Create: `EngineSource/Engine/src/Runtime/Resources/AsyncBlobLoader.h`
- Create: `EngineSource/Engine/src/Runtime/Resources/AsyncBlobLoader.cpp`

**Interfaces:**
- Consumes: `ResourceManager::LoadSharedBinaryAuto(path)` (스레드 세이프 — m_cacheMutex 보호 확인됨)
- Produces (Task 9가 사용):
  - `AsyncBlobLoader(ResourceManager& rm, unsigned workerCount = 2)`
  - `void Request(std::string logicalPath)` — 워커가 LoadSharedBinaryAuto를 호출해 ResourceManager 캐시(LRU)에 적재
  - `bool TryPopCompleted(std::string& outPath, bool& outSuccess)` — 메인 스레드에서 완료 항목 수거
  - `std::size_t PendingCount() const`

- [ ] **Step 1: AsyncBlobLoader.h 작성**

```cpp
#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace Alice
{
    class ResourceManager;

    /// 백그라운드 워커에서 blob(파일 IO + 복호화)을 미리 로드해
    /// ResourceManager의 캐시(LRU)에 적재한다.
    /// GPU 리소스 생성은 하지 않는다 — 밉맵 생성이 immediate context를
    /// 요구하므로 GPU 생성은 메인 스레드 몫이다.
    class AsyncBlobLoader
    {
    public:
        explicit AsyncBlobLoader(ResourceManager& rm, unsigned workerCount = 2);
        ~AsyncBlobLoader();

        AsyncBlobLoader(const AsyncBlobLoader&) = delete;
        AsyncBlobLoader& operator=(const AsyncBlobLoader&) = delete;

        /// 로드 요청 (논리 경로). 완료 순서는 요청 순서와 다를 수 있다.
        void Request(std::string logicalPath);

        /// 완료 항목 하나를 수거. 없으면 false. (메인 스레드에서 폴링)
        bool TryPopCompleted(std::string& outPath, bool& outSuccess);

        /// 아직 완료 수거되지 않은 요청 수 (진행 중 + 완료 대기)
        std::size_t PendingCount() const;

    private:
        void WorkerMain();

        ResourceManager& m_rm;
        mutable std::mutex m_mutex;
        std::condition_variable m_cv;
        std::deque<std::string> m_requests;
        std::deque<std::pair<std::string, bool>> m_completed;
        std::size_t m_inFlight = 0;
        bool m_stop = false;
        std::vector<std::thread> m_workers;
    };
}
```

- [ ] **Step 2: AsyncBlobLoader.cpp 작성**

```cpp
#include "Runtime/Resources/AsyncBlobLoader.h"
#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Foundation/Logger.h"

namespace Alice
{
    AsyncBlobLoader::AsyncBlobLoader(ResourceManager& rm, unsigned workerCount)
        : m_rm(rm)
    {
        if (workerCount == 0) workerCount = 1;
        m_workers.reserve(workerCount);
        for (unsigned i = 0; i < workerCount; ++i)
            m_workers.emplace_back([this] { WorkerMain(); });
    }

    AsyncBlobLoader::~AsyncBlobLoader()
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_stop = true;
        }
        m_cv.notify_all();
        for (auto& t : m_workers)
            if (t.joinable()) t.join();
    }

    void AsyncBlobLoader::Request(std::string logicalPath)
    {
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_requests.push_back(std::move(logicalPath));
        }
        m_cv.notify_one();
    }

    bool AsyncBlobLoader::TryPopCompleted(std::string& outPath, bool& outSuccess)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_completed.empty())
            return false;
        outPath = std::move(m_completed.front().first);
        outSuccess = m_completed.front().second;
        m_completed.pop_front();
        return true;
    }

    std::size_t AsyncBlobLoader::PendingCount() const
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_requests.size() + m_inFlight + m_completed.size();
    }

    void AsyncBlobLoader::WorkerMain()
    {
        for (;;)
        {
            std::string path;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [this] { return m_stop || !m_requests.empty(); });
                if (m_stop)
                    return;
                path = std::move(m_requests.front());
                m_requests.pop_front();
                ++m_inFlight;
            }

            const bool ok = (m_rm.LoadSharedBinaryAuto(path) != nullptr);

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                --m_inFlight;
                m_completed.emplace_back(std::move(path), ok);
            }
        }
    }
}
```

- [ ] **Step 3: 빌드 확인 (아직 소비처 없음 — 컴파일만)**

```powershell
cmake -S D:\Github\EGOSIS_Refactoring -B D:\Github\EGOSIS_Refactoring\build -G "Visual Studio 17 2022"
cmake --build D:\Github\EGOSIS_Refactoring\build --config Release --target Launch -- /m
```

- [ ] **Step 4: Commit**

```powershell
git add EngineSource/Engine/src/Runtime/Resources/AsyncBlobLoader.h EngineSource/Engine/src/Runtime/Resources/AsyncBlobLoader.cpp
git commit -m "[feat] AsyncBlobLoader - 백그라운드 blob 프리페치 워커"
```

---

### Task 9: ⑥ 로딩 화면 — 인위적 대기 제거 + 실제 진행률 + 백그라운드 프리페치

**Files:**
- Modify: `EngineSource/Engine/src/Runtime/Engine/EngineInitialize.cpp:1637-1957` (로딩 루프 전체)

**Interfaces:**
- Consumes: Task 8의 `AsyncBlobLoader`

- [ ] **Step 1: AdvanceTo에서 인위적 대기 제거**

기존(1746행 부근):

```cpp
		auto AdvanceTo = [&](float nextTarget, float minSeconds) -> bool
		{
			targetProgress = std::clamp(nextTarget, 0.0f, 1.0f);
			const auto endTime = clock::now() + std::chrono::duration<float>(std::max(0.0f, minSeconds));
			while (clock::now() < endTime || displayedProgress + 0.001f < targetProgress)
			{
				float dt = CalcDelta();
				if (!RenderFrame(dt, true))
				{
					ALICE_LOG_WARN("[Loading] RenderFrame failed during progress update.");
					return false;
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(8));
			}
			return true;
		};
```

교체 — 목표만 갱신하고 프레임 1장만 그린다(따라잡기 대기 없음):

```cpp
		// 진행 목표만 갱신하고 UI 프레임을 1장 그린다. 인위적 대기 없음.
		auto AdvanceTo = [&](float nextTarget) -> bool
		{
			targetProgress = std::clamp(nextTarget, 0.0f, 1.0f);
			return RenderFrame(CalcDelta(), true);
		};
```

모든 호출부 `AdvanceTo(x, seconds)` → `AdvanceTo(x)` 로 수정 (1829, 1837, 1848, 1865, 1878, 1898, 1918, 1922행 부근 — 컴파일 에러가 전부 짚어준다).

- [ ] **Step 2: 상태 문구를 실제 단계명으로**

상태 문구 헬퍼를 `AdvanceTo` 정의 아래에 추가:

```cpp
		auto SetStatus = [&](const std::string& text)
		{
			if (auto* statusText = loadingWorld.GetComponent<UITextComponent>(statusId))
				statusText->text = text;
		};
```

`UpdateLoadingUI`의 고정 문구 갱신(1686-1687행)을 **삭제**한다:

```cpp
			if (auto* statusText = loadingWorld.GetComponent<UITextComponent>(statusId))
				statusText->text = std::string("쉐이더 컴파일중") + dotSeq[dotIndex];
```

(점 애니메이션은 SetStatus 시점 문자열에 포함하지 않고 제거 — dotTimer/dotIndex/dotSeq 변수도 삭제)

각 단계 직전에 실제 문구 삽입:

```cpp
		SetStatus("오디오 초기화");            // InitializeAudio() 직전
		SetStatus("렌더 시스템 초기화 (셰이더 컴파일)"); // InitializeRenderSystems() 직전
		SetStatus("이펙트 시스템 초기화");      // InitializeComputeEffectSystem() 직전
```

초기 상태 생성부(1600행 부근)와 얼리 로딩(1316, 1367, 1382행)의 `"쉐이더 컴파일중."` 문자열은 `"엔진 준비중."` 으로 교체. 완료 문구(1931행) `"쉐이더 컴파일 완료"` → `"로딩 완료"`.

무결성 실패 분기(1805행)의 `"쉐이더 컴파일중..."` → `"게임 데이터 오류"`.

- [ ] **Step 3: 프리로드를 백그라운드 프리페치 + 메인 스레드 GPU 생성으로 교체**

기존 프리로드 루프(1881-1902행):

```cpp
			ALICE_LOG_INFO("[Loading] Preloading %zu items...", total);
			float stepTime = 0.015f;
			if (total <= 6) stepTime = 0.08f;
			else if (total <= 24) stepTime = 0.03f;

			for (const auto& path : preloadList)
			{
				if (!preloadCtx.PreloadByType(path, true))
				{
					ALICE_LOG_WARN("Preload failed: %s", path.c_str());
				}
				++loaded;
				const float frac = static_cast<float>(loaded) / static_cast<float>(total);
				const float nextTarget = preloadBase + preloadWeight * frac;
				if (!AdvanceTo(nextTarget, stepTime))
					return false;
			}
```

교체:

```cpp
			ALICE_LOG_INFO("[Loading] Preloading %zu items (async prefetch)...", total);

			// 워커가 디스크 IO+복호화를 수행해 ResourceManager 캐시에 적재하고,
			// 메인 스레드는 완료된 항목만 GPU 리소스로 변환한다(캐시 적중 → 빠름).
			AsyncBlobLoader prefetcher(m_resourceManager, 2);
			for (const auto& path : preloadList)
				prefetcher.Request(path);

			while (loaded < total)
			{
				std::string donePath;
				bool ok = false;
				if (prefetcher.TryPopCompleted(donePath, ok))
				{
					if (!preloadCtx.PreloadByType(donePath, true))
					{
						ALICE_LOG_WARN("Preload failed: %s", donePath.c_str());
					}
					++loaded;
					const float frac = static_cast<float>(loaded) / static_cast<float>(total);
					SetStatus("애셋 로딩 (" + std::to_string(loaded) + "/" + std::to_string(total) + ")");
					if (!AdvanceTo(preloadBase + preloadWeight * frac))
						return false;
				}
				else
				{
					// 완료 항목이 없으면 UI만 갱신 (블로킹 없음)
					if (!RenderFrame(CalcDelta(), true))
						return false;
					std::this_thread::sleep_for(std::chrono::milliseconds(2));
				}
			}
```

파일 상단 include에 추가:

```cpp
#include "Runtime/Resources/AsyncBlobLoader.h"
```

- [ ] **Step 4: 마지막 게이지 따라잡기(유일하게 허용되는 대기)**

`AdvanceTo(1.0f, 0.10f)` 자리(1922행)에는 짧은 시각적 마무리만 남긴다:

```cpp
		targetProgress = 1.0f;
		while (displayedProgress + 0.005f < 1.0f)
		{
			if (!RenderFrame(CalcDelta(), true))
				return false;
			std::this_thread::sleep_for(std::chrono::milliseconds(4));
		}
```

- [ ] **Step 5: 빌드 + 검증**

에디터 스모크 통과 확인. 게임 빌드 검증은 에디터 Build Game으로 AlicePlayer 산출 후:
1. 로딩 화면 문구가 단계별로 바뀌는지("오디오 초기화" → "렌더 시스템 초기화..." → "애셋 로딩 (N/M)").
2. 로딩 총 시간을 개선 전/후 스톱워치 측정해 커밋 메시지에 기록.
3. Cooked/Chunks 폴더 하나 삭제 → "게임 데이터 오류" 화면 확인 → 원복.

- [ ] **Step 6: Commit**

```powershell
git add EngineSource/Engine/src/Runtime/Engine/EngineInitialize.cpp
git commit -m "[feat] 로딩 화면 실제 진행률화 - 인위적 대기 제거 + 비동기 프리페치"
```

---

### Task 10: ⑤b 증분 쿠킹 (CookManifest.json)

**Files:**
- Modify: `EngineSource/Engine/src/Runtime/Resources/ResourceManager.cpp:752-881` (`CookResourceToChunkStore`)

**Interfaces:**
- Produces: `<cookedDirAbs>/CookManifest.json` — `{ "<rel>": { "size": u64, "mtime": i64, "chunkCount": u32, "fileId": "<hex16>" } }`. 외부 API 시그니처 불변.

- [ ] **Step 1: 함수 시작부에 이전 매니페스트 로드 추가**

`if (chunkBytes < 4096) chunkBytes = 4096;` 아래에 추가:

```cpp
        // 증분 쿠킹: 이전 매니페스트를 읽어 변경 없는 파일은 재암호화를 건너뛴다.
        nlohmann::json prevManifest;
        const fs::path cookManifestPath = cookedDirAbs / "CookManifest.json";
        {
            std::ifstream mfs(cookManifestPath);
            if (mfs.is_open())
            {
                try { mfs >> prevManifest; }
                catch (...) { prevManifest = nlohmann::json::object(); }
            }
        }
        nlohmann::json newManifest = nlohmann::json::object();
        std::size_t skippedCount = 0;
```

- [ ] **Step 2: 파일 루프에 스킵 판정 삽입**

`const std::uint64_t fileId = HashRelNormalized(relStr);` 아래, `char hex[17]` 계산 **뒤**, `// 파일 읽기` **앞**에 삽입:

```cpp
            // 변경 판정: size+mtime이 매니페스트와 같고 c0000.alice가 존재하면 스킵
            std::uint64_t srcSize = 0;
            long long srcMtime = 0;
            {
                std::error_code sec;
                srcSize = static_cast<std::uint64_t>(fs::file_size(inPath, sec));
                if (sec) srcSize = 0;
                sec.clear();
                const auto t = fs::last_write_time(inPath, sec);
                if (!sec)
                    srcMtime = static_cast<long long>(t.time_since_epoch().count());
            }

            if (prevManifest.contains(relStr))
            {
                const auto& e = prevManifest[relStr];
                if (e.value("size", std::uint64_t(0)) == srcSize &&
                    e.value("mtime", 0ll) == srcMtime &&
                    fs::exists(outDir / "c0000.alice", ec))
                {
                    ec.clear();
                    const std::uint32_t prevChunks = e.value("chunkCount", 0u);
                    manifestList.push_back({ fileId, prevChunks });
                    newManifest[relStr] = e;
                    ++skippedCount;
                    ++fileCount;
                    continue;
                }
                ec.clear();
            }
```

- [ ] **Step 3: 쿠킹한 파일을 새 매니페스트에 기록**

파일 루프 끝의 `++fileCount;` 직전에 추가:

```cpp
            char hexBuf[17] = {};
            std::snprintf(hexBuf, sizeof(hexBuf), "%016llx", static_cast<unsigned long long>(fileId));
            newManifest[relStr] = {
                { "size", originalSize },
                { "mtime", srcMtime },
                { "chunkCount", chunkCount },
                { "fileId", std::string(hexBuf) }
            };
```

- [ ] **Step 4: 고아 청크 정리 + 매니페스트 저장**

함수 끝의 `ALICE_LOG_INFO("CookResourceToChunkStore: cooked %zu files...` 직전에 추가:

```cpp
        // 소스에서 삭제된 파일의 청크 디렉터리 제거 (이전 매니페스트에만 있는 항목)
        for (auto it = prevManifest.begin(); it != prevManifest.end(); ++it)
        {
            if (newManifest.contains(it.key()))
                continue;
            const std::string hexStr2 = it.value().value("fileId", "");
            if (hexStr2.size() != 16)
                continue;
            const fs::path orphanDir = cookedDirAbs / "Chunks" / hexStr2.substr(0, 2) / hexStr2;
            std::error_code rec;
            const auto removed = fs::remove_all(orphanDir, rec);
            if (!rec && removed > 0)
                ALICE_LOG_INFO("CookResourceToChunkStore: removed orphan chunks for \"%s\"", it.key().c_str());
        }

        // 새 매니페스트 저장 (평문 JSON — 쿠킹 메타데이터일 뿐 게임 데이터가 아님)
        {
            std::ofstream mfs(cookManifestPath, std::ios::trunc);
            if (mfs.is_open())
                mfs << newManifest.dump(1);
        }

        ALICE_LOG_INFO("CookResourceToChunkStore: incremental skip=%zu / total=%zu", skippedCount, fileCount);
```

파일 상단에 `#include <fstream>` 존재 확인(이미 있음), `#include "ThirdParty/json/json.hpp"` 존재 확인(이미 있음 — 없으면 추가).

주의: 스킵 시에도 `manifestList`(런타임 무결성 Manifest.alice용)에는 엔트리를 넣는다(Step 2에서 처리) — **Manifest.alice는 매 빌드 전체 재생성**되므로 무결성 검증은 그대로 동작한다.

- [ ] **Step 5: 검증 + Commit**

검증: 에디터 Build Game 2회 실행 → 2회차 로그에 `incremental skip=N / total=N`(전부 스킵) 확인, 빌드 산출물 실행 정상. 소스 텍스처 1개 touch 후 3회차 → skip=N-1.

```powershell
git add EngineSource/Engine/src/Runtime/Resources/ResourceManager.cpp
git commit -m "[feat] 증분 쿠킹 - CookManifest 기반 미변경 파일 스킵 + 고아 청크 정리"
```

---

### Task 11: ⑤c 탐색기 드래그&드롭 임포트

**Files:**
- Create: `EngineSource/Engine/src/Editor/Project/AssetDropImport.cpp`
- Create: `EngineSource/Engine/src/Editor/Project/AssetDropImport.h`
- Modify: `EngineSource/Engine/src/Runtime/Engine/EngineWindow.cpp` (WM_DROPFILES)
- Modify: `EngineSource/Engine/src/Runtime/Engine/EngineImpl.h` (드롭 핸들러 멤버)

**Interfaces:**
- Produces:
  - Engine::Impl 멤버 `std::function<void(const std::vector<std::filesystem::path>&)> m_dropFilesHandler;` — 에디터가 등록, 게임 모드에선 비어 있음
  - `Alice::AssetDropImport::Handle(const std::vector<std::filesystem::path>& files, const std::filesystem::path& projectRoot);`

- [ ] **Step 1: EngineImpl에 핸들러 멤버 + 창 드롭 허용**

`EngineImpl.h`의 Engine::Impl 멤버 선언부(다른 `std::function`/시스템 멤버 근처)에 추가:

```cpp
		// 에디터 모드에서 탐색기 드래그&드롭 파일을 처리할 핸들러 (에디터가 등록)
		std::function<void(const std::vector<std::filesystem::path>&)> m_dropFilesHandler;
```

(`#include <filesystem>`, `#include <functional>`, `#include <vector>` 가 없으면 추가)

창 생성 직후(`CreateWindowExW`/`CreateWindowW` 호출 지점 — `grep -n "CreateWindow" EngineSource/Engine/src/Runtime/Engine/*.cpp` 로 위치 확인) 에 추가:

```cpp
		if (m_editorMode)
			DragAcceptFiles(m_hWnd, TRUE);
```

해당 파일 상단에 `#include <shellapi.h>` 추가. (Shell32는 이미 링크됨 — Launch.exe가 SHELL32.dll을 import)

- [ ] **Step 2: WM_DROPFILES 처리**

`EngineWindow.cpp`의 `HandleMessage` switch에 `case WM_INPUT:` 앞에 추가:

```cpp
		case WM_DROPFILES:
		{
			HDROP drop = reinterpret_cast<HDROP>(wParam);
			const UINT count = DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
			std::vector<std::filesystem::path> files;
			files.reserve(count);
			for (UINT i = 0; i < count; ++i)
			{
				wchar_t buf[MAX_PATH] = {};
				if (DragQueryFileW(drop, i, buf, MAX_PATH) > 0)
					files.emplace_back(buf);
			}
			DragFinish(drop);

			if (m_dropFilesHandler && !files.empty())
				m_dropFilesHandler(files);
			return 0;
		}
```

`EngineWindow.cpp` 상단에 `#include <shellapi.h>` 추가.

- [ ] **Step 3: AssetDropImport 작성**

`AssetDropImport.h`:

```cpp
#pragma once

#include <filesystem>
#include <vector>

namespace Alice::AssetDropImport
{
    /// 탐색기에서 드롭된 파일들을 확장자 규칙에 따라 프로젝트로 복사한다.
    /// 폴더는 재귀 처리. 복사 후 ResourceManager negative cache를 비운다.
    void Handle(const std::vector<std::filesystem::path>& files,
                const std::filesystem::path& projectRoot);
}
```

`AssetDropImport.cpp`:

```cpp
#include "Editor/Project/AssetDropImport.h"

#include "Runtime/Foundation/Logger.h"
#include "Runtime/Resources/ResourceManager.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <system_error>

namespace Alice::AssetDropImport
{
    namespace
    {
        namespace fs = std::filesystem;

        std::string LowerExt(const fs::path& p)
        {
            std::string ext = p.extension().string();
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return ext;
        }

        // 확장자 → 프로젝트 내 대상 폴더 (스펙의 규칙 테이블)
        // 반환이 빈 경로면 미지원 확장자.
        fs::path TargetDirFor(const fs::path& file, const fs::path& projectRoot)
        {
            const std::string ext = LowerExt(file);
            const std::string stem = file.stem().string();

            if (ext == ".fbx")
                return projectRoot / "Resource" / "fbx" / stem;
            if (ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".dds" || ext == ".tga")
                return projectRoot / "Resource" / "Textures" / stem;
            if (ext == ".wav" || ext == ".mp3" || ext == ".ogg")
                return projectRoot / "Resource" / "Sound";
            if (ext == ".ttf" || ext == ".otf")
                return projectRoot / "Resource" / "Fonts";
            return {};
        }

        // 대상에 같은 이름이 있으면 "이름_1", "이름_2"... 로 회피
        fs::path UniqueDestPath(const fs::path& dir, const fs::path& filename)
        {
            fs::path dest = dir / filename;
            std::error_code ec;
            int suffix = 1;
            while (fs::exists(dest, ec) && !ec)
            {
                fs::path renamed = filename.stem();
                renamed += "_" + std::to_string(suffix++);
                renamed += filename.extension();
                dest = dir / renamed;
            }
            return dest;
        }

        void ImportOneFile(const fs::path& file, const fs::path& projectRoot,
                           std::size_t& imported, std::size_t& skipped)
        {
            const fs::path targetDir = TargetDirFor(file, projectRoot);
            if (targetDir.empty())
            {
                ALICE_LOG_WARN("[DropImport] unsupported extension, skipped: \"%s\"",
                               file.string().c_str());
                ++skipped;
                return;
            }

            std::error_code ec;
            fs::create_directories(targetDir, ec);
            ec.clear();

            const fs::path dest = UniqueDestPath(targetDir, file.filename());
            fs::copy_file(file, dest, ec);
            if (ec)
            {
                ALICE_LOG_ERRORF("[DropImport] copy failed: \"%s\" -> \"%s\" (%s)",
                                 file.string().c_str(), dest.string().c_str(), ec.message().c_str());
                ++skipped;
                return;
            }

            ALICE_LOG_INFO("[DropImport] imported: \"%s\" -> \"%s\"",
                           file.string().c_str(), dest.string().c_str());
            ++imported;
        }
    }

    void Handle(const std::vector<fs::path>& files, const fs::path& projectRoot)
    {
        std::size_t imported = 0, skipped = 0;
        std::error_code ec;

        for (const auto& item : files)
        {
            if (fs::is_directory(item, ec) && !ec)
            {
                for (fs::recursive_directory_iterator it(item, ec), end; it != end; it.increment(ec))
                {
                    if (ec) { ec.clear(); continue; }
                    if (!it->is_regular_file(ec) || ec) { ec.clear(); continue; }
                    ImportOneFile(it->path(), projectRoot, imported, skipped);
                }
            }
            else
            {
                ec.clear();
                ImportOneFile(item, projectRoot, imported, skipped);
            }
        }

        if (imported > 0)
            ResourceManager::Get().ClearNegativeCache();

        ALICE_LOG_INFO("[DropImport] done. imported=%zu skipped=%zu", imported, skipped);
    }
}
```

- [ ] **Step 4: 에디터에서 핸들러 등록**

에디터 초기화 지점을 찾는다: `grep -rn "m_editorCore\|EditorCore" EngineSource/Engine/src/Runtime/Engine/EngineInitialize.cpp | head`. 에디터 모드 초기화 코드(m_editorMode == true 분기)에 추가:

```cpp
		if (m_editorMode)
		{
			m_dropFilesHandler = [this](const std::vector<std::filesystem::path>& files)
			{
				// projectRoot = exeDir 3단계 상위 (ResourceManager::Configure와 동일 규칙)
				AssetDropImport::Handle(files, m_resourceManager.RootDir());
			};
		}
```

include 추가: `#include "Editor/Project/AssetDropImport.h"`.

주의: Engine 타깃이 Editor 소스를 포함하는지 확인(`grep -n "Editor" CMakeLists.txt | head` 로 소스 수집 범위 확인). Editor 소스가 Launch에만 포함되고 Engine lib에는 없다면, 이 등록 코드를 EngineInitialize가 아니라 **에디터 쪽 초기화(EditorCore 생성 직후)** 로 옮기고 Engine::Impl에 `SetDropFilesHandler(std::function<...>)` public 세터를 추가해 주입한다.

- [ ] **Step 5: 검증 + Commit**

검증(수동): 에디터 실행 → 탐색기에서 아무 .png를 창에 드롭 → 로그 `[DropImport] imported: ... -> ...Resource\Textures\<이름>\...` 확인 + 실제 파일 존재 확인. .xyz 같은 미지원 파일 드롭 → `unsupported extension` 경고만. 게임 모드(AlicePlayer)에서는 드롭이 무시되는지(핸들러 미등록) 확인.

```powershell
git add EngineSource/Engine/src/Editor/Project/AssetDropImport.h EngineSource/Engine/src/Editor/Project/AssetDropImport.cpp EngineSource/Engine/src/Runtime/Engine/EngineWindow.cpp EngineSource/Engine/src/Runtime/Engine/EngineImpl.h EngineSource/Engine/src/Runtime/Engine/EngineInitialize.cpp
git commit -m "[feat] 탐색기 드래그&드롭 애셋 임포트 (WM_DROPFILES + 확장자 규칙)"
```

---

### Task 12: ⑤d 프로젝트 패널 검색 + 새로고침

**Files:**
- Modify: `EngineSource/Engine/src/Editor/Panels/ProjectPanel.cpp` (`DrawProjectWindow`)

**Interfaces:**
- Consumes: Task 5의 `ResourceManager::ClearNegativeCache()`

- [ ] **Step 1: 검색창 + 새로고침 버튼 추가**

`DrawProjectWindow` 시작부(패널 위젯 그리기 직전, `DrawDirectoryNode(...)` 호출 전)에 추가:

```cpp
		static char s_searchBuf[128] = {};

		if (ImGui::Button("Refresh"))
		{
			ResourceManager::Get().ClearNegativeCache();
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputTextWithHint("##ProjectSearch", "Search assets...", s_searchBuf, sizeof(s_searchBuf));
```

include에 `#include "Runtime/Resources/ResourceManager.h"` 추가.

- [ ] **Step 2: 검색어가 있으면 평면 결과 목록 표시**

`DrawDirectoryNode(world, selectedEntity, assetsRoot);` 호출을 다음으로 교체:

```cpp
		const std::string search = s_searchBuf;
		if (search.empty())
		{
			DrawDirectoryNode(world, selectedEntity, assetsRoot);
		}
		else
		{
			// 부분일치(대소문자 무시) 평면 목록
			std::string needle = search;
			std::transform(needle.begin(), needle.end(), needle.begin(),
			               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

			std::error_code ec;
			int shown = 0;
			for (std::filesystem::recursive_directory_iterator it(assetsRoot, ec), end;
			     it != end && shown < 200; it.increment(ec))
			{
				if (ec) { ec.clear(); continue; }
				if (!it->is_regular_file(ec) || ec) { ec.clear(); continue; }

				std::string name = it->path().filename().string();
				std::string lower = name;
				std::transform(lower.begin(), lower.end(), lower.begin(),
				               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (lower.find(needle) == std::string::npos)
					continue;

				ImGui::PushID(shown);
				if (ImGui::Selectable(name.c_str()))
				{
					// 기존 트리의 파일 클릭과 동일한 동작이 필요하면
					// DrawDirectoryNode 내부의 클릭 처리 함수를 재사용한다.
					// 최소 구현: 선택만 표시.
				}
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("%s", it->path().string().c_str());
				ImGui::PopID();
				++shown;
			}
			if (shown == 0)
				ImGui::TextDisabled("No results.");
		}
```

`#include <algorithm>`, `#include <filesystem>`, `#include <system_error>` 확인/추가. `assetsRoot` 변수는 기존 코드(72행 부근)에서 이미 계산됨 — 이름이 다르면 그에 맞춘다.

- [ ] **Step 3: 빌드 + 검증 + Commit**

검증: 에디터 Project 패널에서 "boss" 검색 → boss 관련 파일 평면 목록, 지우면 트리 복귀. Refresh 클릭 → 크래시 없음.

```powershell
git add EngineSource/Engine/src/Editor/Panels/ProjectPanel.cpp
git commit -m "[feat] 프로젝트 패널 검색 필터 + 새로고침(negative cache 무효화)"
```

---

## 최종 검증 (모든 태스크 완료 후)

- [ ] Release 전체 재빌드(Launch + AliceScripts) 성공
- [ ] 에디터 스모크: `[Error]` 0건
- [ ] Play → 게임 진행 → Stop → Reload Scripts → Play 재확인
- [ ] Build Game으로 AlicePlayer 산출 → 로딩 화면·게임 실행 확인
- [ ] `Docs/superpowers/specs/2026-07-06-engine-refactor-2-6-design.md`의 각 절 요구사항 대조 체크
