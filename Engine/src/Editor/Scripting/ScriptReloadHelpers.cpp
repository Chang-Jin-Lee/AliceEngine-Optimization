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
		path scriptsRoot = projectRoot / "ScriptsBuild";
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
		// 1: Configure 명령어 (cmd.exe /C는 ExecuteCommandWithConsole에서 처리)
		// ----------------------------------------------------------------------
		std::wstring cmdConfig = L"cmake -S \"";
		cmdConfig += scriptsRoot.wstring();
		cmdConfig += L"\" -B \"";
		cmdConfig += scriptsBuildDir.wstring();
		cmdConfig += L"\"";

		// Configure 실행
		int configResult = ExecuteCommandWithConsole(cmdConfig);
		if (configResult != 0)
		{
			ALICE_LOG_ERRORF("Reload Scripts: CMake Configure failed (exit code: %d).", configResult);
			// 실패 시에만 pause 실행 (사용자가 에러를 볼 수 있도록)
			ExecuteCommandWithConsole(L"pause");
			return false;
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

		// 4) ScriptsBuild/build/<Config>/AliceScripts.dll 을 실행 파일 옆으로 복사
		path builtDll = scriptsBuildDir / path(kConfig) / "AliceScripts.dll";
		if (!exists(builtDll))
		{
			ALICE_LOG_ERRORF("Reload Scripts: built DLL not found: \"%s\"",
				builtDll.string().c_str());
			return false;
		}

		// RTTR shared DLL도 같이 복사해 둡니다. (스크립트 RTTR 등록이 엔진에서 보이려면 필수)
		// - ScriptsBuild는 자체적으로 rttr_core.dll을 빌드합니다.
		// - dll 폴더에 하나만 존재하면, EXE/DLL이 같은 registry를 공유합니다.
		{
			std::error_code ecMk;
			const path dllDir = exeDir / "dll";
			create_directories(dllDir, ecMk);

			path builtRttr = scriptsBuildDir / path(kConfig) / "rttr_core.dll";
			if (exists(builtRttr))
			{
				std::error_code ecRttr;
				copy_file(builtRttr, dllDir / "rttr_core.dll",
					copy_options::overwrite_existing,
					ecRttr);
				if (ecRttr)
				{
					ALICE_LOG_WARN("Reload Scripts: failed to copy rttr_core.dll (%s)",
						ecRttr.message().c_str());
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
