#include "Editor/Scripting/ScriptReloadHelpers.h"

#include "Runtime/ECS/World.h"
#include "Runtime/Resources/Serialization/JsonRttr.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Scripting/ScriptHotReload.h"
#include "Runtime/Scripting/ScriptSystem.h"
#include "Runtime/Foundation/Logger.h"
#include "ThirdParty/json/json.hpp"

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#include <Windows.h>

namespace Alice
{
	namespace
	{
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

		// 명령어를 실행하고 Exit Code를 반환하는 함수임
		int ExecuteCommandWithConsole(const std::wstring& command)
		{
			STARTUPINFOW si;
			PROCESS_INFORMATION pi;

			ZeroMemory(&si, sizeof(si));
			si.cb = sizeof(si);
			ZeroMemory(&pi, sizeof(pi));

			// cmd.exe /C 를 앞에 붙여서 실행해야 쉘 명령어(cmake 등)가 인식됨
			// 전체 명령어를 " "로 감싸서 공백이나 특수문자 문제를 방지합니다.
			std::wstring finalCmd = L"cmd.exe /C \"" + command + L"\"";


			// 만약 알 수 없이 실패한다면 위의 finalCmd 쪽을 주석시키고 밑의 주석을 풀어보세요.
			// 전체 명령어를 " "로 감싸서 공백이나 특수문자 문제를 방지합니다.
			// 실패 시 콘솔이 바로 꺼지지 않도록 pause를 걸고, 원래 에러코드를 반환합니다.
			//std::wstring finalCmd = L"cmd.exe /C \"";
			//finalCmd += command;
			//finalCmd += L" & set _exit=%errorlevel% & if not %_exit%==0 (echo. & echo Press any key to close... & pause >nul) & exit /b %_exit%\"";

			// CreateProcess는 문자열 버퍼를 수정할 수 있어야 하므로 vector에 복사
			std::vector<wchar_t> cmdBuffer(finalCmd.begin(), finalCmd.end());
			cmdBuffer.push_back(0); // Null terminator

			// CreateProcess 실행
			// CREATE_NEW_CONSOLE: 부모가 GUI라도 무조건 새 콘솔창을 띄움
			BOOL result = CreateProcessW(
				NULL,                   // 어플리케이션 이름 (NULL이면 커맨드라인에서 파싱)
				cmdBuffer.data(),       // 커맨드 라인
				NULL,                   // 프로세스 보안 속성
				NULL,                   // 스레드 보안 속성
				FALSE,                  // 핸들 상속 여부
				CREATE_NEW_CONSOLE,     // 새 콘솔 창 생성 플래그
				NULL,                   // 환경 변수 (NULL이면 부모 상속)
				NULL,                   // 현재 디렉토리 (NULL이면 부모와 동일)
				&si,                    // 시작 정보
				&pi                     // 프로세스 정보 (핸들 등)
			);

			if (!result)
			{
				// 실행 자체 실패
				return -1;
			}

			// 프로세스가 끝날 때까지 대기
			WaitForSingleObject(pi.hProcess, INFINITE);

			// 종료 코드(Exit Code) 가져오기
			DWORD exitCode = 0;
			GetExitCodeProcess(pi.hProcess, &exitCode);

			// 핸들 닫기
			CloseHandle(pi.hProcess);
			CloseHandle(pi.hThread);

			return static_cast<int>(exitCode);
		}

		/// 에디터 Reload Scripts 버튼에서 호출하는 헬퍼입니다.
		/// - ScriptsBuild CMake 프로젝트를 configure/build 해서 AliceScripts.dll 을 만들고
		///   현재 실행 중인 exe 옆으로 복사한 뒤 ScriptHotReload_Reload 를 호출합니다.
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

		static void SnapshotAndDestroyScripts(World& world, std::vector<EntityReloadSnap>& out)
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

						// DLL이 살아있는 동안 가상함수 호출해서 정리
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

		static void RestoreScripts(World& world, const std::vector<EntityReloadSnap>& snaps)
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
						continue;

					// 컨텍스트 주입 (필수): World와 EntityId 설정
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
	}

	bool ReloadScripts_FromButton(World& world)
	{
		using namespace std::filesystem;

		// 1) 실행 파일 위치 기준으로 프로젝트 루트 / ScriptsBuild 경로 계산
		wchar_t exePathW[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
		path exePath = exePathW;
		path exeDir = exePath.parent_path();
		path projectRoot = exeDir.parent_path().parent_path().parent_path(); // build/bin/Debug → 프로젝트 루트

		// 모노레포 구조는 EngineSource/ScriptsBuild, 구(원본) 구조는 루트/ScriptsBuild
		path scriptsRoot = projectRoot / "EngineSource" / "ScriptsBuild";
		if (!exists(scriptsRoot / "CMakeLists.txt"))
			scriptsRoot = projectRoot / "ScriptsBuild";

		path scriptsCMakePath = scriptsRoot / "CMakeLists.txt";
		path scriptsBuildDir = scriptsRoot / "build";

		if (!exists(scriptsCMakePath))
		{
			ALICE_LOG_ERRORF("Reload Scripts: ScriptsBuild/CMakeLists.txt not found. path=\"%s\"",
				(scriptsCMakePath).string().c_str());
			return false;
		}

#ifdef _DEBUG
		constexpr const wchar_t* kConfig = L"Debug";
#else
		constexpr const wchar_t* kConfig = L"Release";
#endif

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

		// ----------------------------------------------------------------------
		// 1: Configure (이미 구성된 빌드 트리가 있으면 건너뛰어 Play 시간을 줄인다.
		//    스크립트 파일 추가/삭제는 GLOB의 CONFIGURE_DEPENDS가 빌드 시점에 감지한다.)
		// ----------------------------------------------------------------------
		if (needBuild)
		{
			if (!exists(scriptsBuildDir / "CMakeCache.txt"))
			{
				std::wstring cmdConfig = L"cmake -S \"";
				cmdConfig += scriptsRoot.wstring();
				cmdConfig += L"\" -B \"";
				cmdConfig += scriptsBuildDir.wstring();
				cmdConfig += L"\"";

				int configResult = ExecuteCommandWithConsole(cmdConfig);
				if (configResult != 0)
				{
					ALICE_LOG_ERRORF("Reload Scripts: CMake Configure failed (exit code: %d).", configResult);
					// 실패 시에만 pause 실행 (사용자가 에러를 볼 수 있도록)
					ExecuteCommandWithConsole(L"pause");
					return false;
				}
			}

			// ----------------------------------------------------------------------
			// 2: Build 명령어 (cmd.exe /C는 ExecuteCommandWithConsole에서 처리)
			// ----------------------------------------------------------------------
			std::wstring cmdBuild = L"cmake --build \"";
			cmdBuild += scriptsBuildDir.wstring();
			cmdBuild += L"\" --config ";
			cmdBuild += kConfig;
			cmdBuild += L" --target AliceScripts";

			// Build 실행
			int buildResult = ExecuteCommandWithConsole(cmdBuild);
			if (buildResult != 0)
			{
				ALICE_LOG_ERRORF("Reload Scripts: CMake Build failed (exit code: %d).", buildResult);
				// 실패 시에만 pause 실행 (사용자가 에러를 볼 수 있도록)
				ExecuteCommandWithConsole(L"pause");
				return false;
			}
		}

		// 4) 중간 출력(bin/<Config>)의 AliceScripts.dll 을 실행 파일 옆으로 복사
		//    (구 구조는 build/<Config>/ 바로 아래에 출력했으므로 함께 탐색)
		path builtDll = intermediateDll;
		if (!exists(builtDll))
			builtDll = scriptsBuildDir / path(kConfig) / "AliceScripts.dll";
		if (!exists(builtDll))
		{
			ALICE_LOG_ERRORF("Reload Scripts: built DLL not found: \"%s\"",
				builtDll.string().c_str());
			return false;
		}

		// RTTR shared DLL은 dll 폴더에 하나만 존재해야 EXE/스크립트가 registry를 공유합니다.
		// 엔진 빌드가 이미 복사해 두므로, 없을 때만 ScriptsBuild 산출물에서 채워 넣습니다.
		// (이미 로드되어 잠긴 파일을 덮어쓰려다 매번 경고가 찍히는 것도 방지)
		{
			std::error_code ecMk;
			const path dllDir = exeDir / "dll";
			create_directories(dllDir, ecMk);

			const wchar_t* rttrName = (wcscmp(kConfig, L"Debug") == 0) ? L"rttr_core_d.dll" : L"rttr_core.dll";
			if (!exists(dllDir / rttrName))
			{
				path builtRttr = scriptsBuildDir / "Engine" / "ThirdParty" / "rttr" / "bin" / path(kConfig) / rttrName;
				if (!exists(builtRttr))
					builtRttr = scriptsBuildDir / path(kConfig) / rttrName;

				if (exists(builtRttr))
				{
					std::error_code ecRttr;
					copy_file(builtRttr, dllDir / rttrName,
						copy_options::overwrite_existing,
						ecRttr);
					if (ecRttr)
					{
						ALICE_LOG_WARN("Reload Scripts: failed to copy %ls (%s)",
							rttrName, ecRttr.message().c_str());
					}
				}
			}
		}

		// 기존 DLL을 언로드하기 전에, 기존 스크립트 인스턴스(가상 함수)가 남아있으면 크래시가 납니다.
		// - 값은 스냅샷 후 새 DLL 로드 뒤에 다시 주입합니다.
		std::vector<EntityReloadSnap> snaps;
		SnapshotAndDestroyScripts(world, snaps);

		ScriptHotReload_Unload();

		std::error_code ecMk;
		const path dllDir = exeDir / "dll";
		create_directories(dllDir, ecMk);

		path targetDll = dllDir / "AliceScripts.dll";
		std::error_code ecCopy;
		copy_file(builtDll, targetDll,
			copy_options::overwrite_existing,
			ecCopy);
		if (ecCopy)
		{
			ALICE_LOG_ERRORF("Reload Scripts: failed to copy DLL from \"%s\" to \"%s\" (%s)",
				builtDll.string().c_str(),
				targetDll.string().c_str(),
				ecCopy.message().c_str());
			return false;
		}

		ALICE_LOG_INFO("Reload Scripts: copied \"%s\" -> \"%s\"",
			builtDll.string().c_str(),
			targetDll.string().c_str());

		// 6) 새 DLL 로드
		if (!ScriptHotReload_Reload())
		{
			ALICE_LOG_ERRORF("Reload Scripts: ScriptHotReload_Reload() failed.");
			return false;
		}

		// 새 DLL의 vtable/RTTR이 준비된 뒤에 인스턴스를 다시 만듭니다.
		RestoreScripts(world, snaps);
		return true;
	}
}
