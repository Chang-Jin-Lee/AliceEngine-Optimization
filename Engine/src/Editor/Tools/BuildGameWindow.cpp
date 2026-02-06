#include "Editor/Core/EditorCore.h"
#include "Editor/Core/EditorUIState.h"

#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Foundation/Logger.h"

#include "ThirdParty/json/json.hpp"

#include "imgui.h"

#include <ShlObj.h>
#include <algorithm>
#include <atomic>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

namespace Alice
{
	namespace
	{
		// Build Game 진행 상황 (간단한 멀티스레드 + atomic 사용)
		std::atomic<bool>        g_BuildInProgress{ false };
		std::atomic<float>       g_BuildProgress{ 0.0f };   // 0.0 ~ 1.0
		std::atomic<long>        g_BuildExitCode{ -1 };     // -1: 아직 없음

		struct ScopedHandle
		{
			HANDLE h = nullptr;
			ScopedHandle() = default;
			explicit ScopedHandle(HANDLE handle) : h(handle) {}
			ScopedHandle(const ScopedHandle&) = delete;
			ScopedHandle& operator=(const ScopedHandle&) = delete;
			ScopedHandle(ScopedHandle&& other) noexcept : h(other.h) { other.h = nullptr; }
			ScopedHandle& operator=(ScopedHandle&& other) noexcept
			{
				if (this != &other)
				{
					if (h) CloseHandle(h);
					h = other.h;
					other.h = nullptr;
				}
				return *this;
			}
			~ScopedHandle() { if (h) CloseHandle(h); }
		};

		bool RunProcessHidden(const std::wstring& cmd,
			const std::filesystem::path& workDir,
			DWORD& outExitCode)
		{
			STARTUPINFOW        si{};
			PROCESS_INFORMATION pi{};
			si.cb = sizeof(si);
			si.dwFlags = STARTF_USESHOWWINDOW;
			si.wShowWindow = SW_HIDE;

			std::vector<wchar_t> cmdBuffer(cmd.begin(), cmd.end());
			cmdBuffer.push_back(L'\0');

			BOOL ok = CreateProcessW(
				nullptr,
				cmdBuffer.data(),
				nullptr,
				nullptr,
				FALSE,
				CREATE_NO_WINDOW,
				nullptr,
				workDir.wstring().c_str(),
				&si,
				&pi);

			if (!ok)
				return false;

			ScopedHandle hProcess(pi.hProcess);
			ScopedHandle hThread(pi.hThread);

			WaitForSingleObject(hProcess.h, INFINITE);
			outExitCode = 0;
			GetExitCodeProcess(hProcess.h, &outExitCode);
			return true;
		}

		// 빌드/배포용 간단 파일 유틸 (에러는 로그로 남기고, 실패는 false 반환)
		bool CopyDirTree(const std::filesystem::path& src, const std::filesystem::path& dst)
		{
			namespace fs = std::filesystem;
			std::error_code ec;
			if (!fs::exists(src, ec) || ec) return true; // 없는 건 스킵
			fs::create_directories(dst, ec);
			if (ec)
			{
				ALICE_LOG_ERRORF("BuildGame: create_directories failed. dst=\"%s\" (%s)",
					dst.string().c_str(), ec.message().c_str());
				return false;
			}
			fs::copy(src, dst, fs::copy_options::recursive | fs::copy_options::overwrite_existing, ec);
			if (ec)
			{
				ALICE_LOG_ERRORF("BuildGame: copy dir failed. \"%s\" -> \"%s\" (%s)",
					src.string().c_str(), dst.string().c_str(), ec.message().c_str());
				return false;
			}
			return true;
		}

		bool CopyFileOver(const std::filesystem::path& src, const std::filesystem::path& dst)
		{
			namespace fs = std::filesystem;
			std::error_code ec;
			fs::create_directories(dst.parent_path(), ec);
			ec.clear();
			fs::copy_file(src, dst, fs::copy_options::overwrite_existing, ec);
			if (ec)
			{
				ALICE_LOG_ERRORF("BuildGame: copy file failed. \"%s\" -> \"%s\" (%s)",
					src.string().c_str(), dst.string().c_str(), ec.message().c_str());
				return false;
			}
			return true;
		}

		bool MakeCleanDir(const std::filesystem::path& dir)
		{
			namespace fs = std::filesystem;
			std::error_code ec;
			if (fs::exists(dir, ec))
			{
				ec.clear();
				fs::remove_all(dir, ec);
			}
			ec.clear();
			fs::create_directories(dir, ec);
			if (ec)
			{
				ALICE_LOG_ERRORF("BuildGame: create clean dir failed. \"%s\" (%s)",
					dir.string().c_str(), ec.message().c_str());
				return false;
			}
			return true;
		}

		// srcRoot의 모든 파일을 dstCookedRoot/<rel>.alice 로 "암호화 저장"합니다(폴더 구조 유지, 확장자는 .alice로 통일).
		// - 이미 암호화된 .alice 는 그대로 복사합니다(중복 암호화 방지).
		// - excludePrefixRel(예: "Resource/")로 시작하는 rel 경로는 스킵할 수 있습니다.
		bool CookAllIntoCookedRoot(const std::filesystem::path& srcRoot,
			const std::filesystem::path& dstCookedRoot,
			const std::string& excludePrefixRel = {})
		{
			namespace fs = std::filesystem;
			std::error_code ec;
			if (!fs::exists(srcRoot, ec) || ec) return true; // 없는 건 스킵
			if (!fs::is_directory(srcRoot, ec) || ec) return true;

			// 빌드할때 Cook으로 변환할때 쓸 멀티쓰레드 잡임
			// 모든 작업을 벡터에 수집
			struct Job
			{
				fs::path inPath;
				fs::path outPath;
				bool alreadyEncrypted;
			};
			std::vector<Job> jobs;

			for (fs::recursive_directory_iterator it(srcRoot, ec), end; it != end; it.increment(ec))
			{
				if (ec) { ec.clear(); continue; }
				if (!it->is_regular_file(ec) || ec) { ec.clear(); continue; }

				const fs::path inPath = it->path();
				fs::path rel = fs::relative(inPath, srcRoot, ec);
				if (ec) { ec.clear(); continue; }

				const std::string relStr = rel.generic_string();
				if (!excludePrefixRel.empty() && relStr.rfind(excludePrefixRel, 0) == 0)
					continue;

				fs::path outPath = dstCookedRoot / rel;
				outPath.replace_extension(".alice");
				const std::string ext = inPath.extension().string();
				const bool alreadyEncrypted = (_stricmp(ext.c_str(), ".alice") == 0);

				jobs.push_back({ inPath, outPath, alreadyEncrypted });
			}

			if (jobs.empty()) return true;

			// 2단계: 멀티스레드 병렬 처리
			std::atomic<size_t> nextIdx = 0;
			std::atomic<bool> success = true;
			std::mutex logMutex;

			const size_t numThreads = (std::max)(1u, std::thread::hardware_concurrency());
			std::vector<std::thread> workers;

			struct WorkerCtx
			{
				std::vector<Job>* jobs{};
				std::atomic<size_t>* nextIdx{};
				std::atomic<bool>* success{};
				std::mutex* logMutex{};
			};

			struct WorkerProc
			{
				static void Run(WorkerCtx ctx)
				{
					while (true)
					{
						const size_t idx = ctx.nextIdx->fetch_add(1);
						if (idx >= ctx.jobs->size())
							break;

						const Job& job = (*ctx.jobs)[idx];
						bool ok = false;

						if (job.alreadyEncrypted)
						{
							ok = CopyFileOver(job.inPath, job.outPath);
						}
						else
						{
							{
								std::lock_guard<std::mutex> lock(*ctx.logMutex);
								ALICE_LOG_INFO("CookFile: in=\"%s\" -> out=\"%s\"",
									job.inPath.string().c_str(),
									job.outPath.string().c_str());
							}
							ok = Alice::ResourceManager::Get().CookAndSave(job.inPath, job.outPath);
							if (!ok)
							{
								std::lock_guard<std::mutex> lock(*ctx.logMutex);
								ALICE_LOG_ERRORF("BuildGame: CookAndSave failed. in=\"%s\" out=\"%s\"",
									job.inPath.string().c_str(), job.outPath.string().c_str());
							}
						}

						if (!ok)
							ctx.success->store(false);
					}
				}
			};

			const WorkerCtx ctx{ &jobs, &nextIdx, &success, &logMutex };

			for (size_t i = 0; i < numThreads; ++i)
				workers.emplace_back(&WorkerProc::Run, ctx);

			for (auto& t : workers)
				t.join();

			return success.load();
		}

		void CopyAllDlls(const std::filesystem::path& fromDir, const std::filesystem::path& toDir)
		{
			namespace fs = std::filesystem;
			std::error_code ec;
			if (!fs::exists(fromDir, ec) || ec) return;
			fs::create_directories(toDir, ec);
			ec.clear();

			// 1단계: 모든 DLL 파일 경로 수집
			std::vector<fs::path> dllFiles;
			for (fs::directory_iterator it(fromDir, ec), end; it != end; it.increment(ec))
			{
				if (ec) { ec.clear(); continue; }
				if (!it->is_regular_file(ec) || ec) { ec.clear(); continue; }
				const fs::path p = it->path();
				if (p.extension() == ".dll")
				{
					const std::string filename = p.filename().string();
					// Release 패키징에 debug 전용 DLL이 섞이지 않도록 필터링
					if (filename == "zlibd1.dll" ||
						filename == "assimp-vc143-mtd.dll" ||
						filename == "fmodL.dll")
					{
						continue;
					}
					dllFiles.push_back(p);
				}
			}

			if (dllFiles.empty()) return;

			// 2단계: 멀티스레드 병렬 복사
			std::atomic<size_t> nextIdx = 0;
			const size_t numThreads = (std::max)(1u, std::thread::hardware_concurrency());
			std::vector<std::thread> workers;

			struct CopyCtx
			{
				std::vector<fs::path>* dllFiles{};
				std::atomic<size_t>* nextIdx{};
				fs::path toDir{};
			};

			struct CopyProc
			{
				static void Run(CopyCtx ctx)
				{
					while (true)
					{
						const size_t idx = ctx.nextIdx->fetch_add(1);
						if (idx >= ctx.dllFiles->size())
							break;
						CopyFileOver((*ctx.dllFiles)[idx], ctx.toDir / (*ctx.dllFiles)[idx].filename());
					}
				}
			};

			const CopyCtx ctx{ &dllFiles, &nextIdx, toDir };

			for (size_t i = 0; i < numThreads; ++i)
				workers.emplace_back(&CopyProc::Run, ctx);

			for (auto& t : workers)
				t.join();
		}

		struct BuildGameTaskArgs
		{
			std::filesystem::path projectRoot;
			std::filesystem::path cfgPath;
			std::string           exportPathStr;
		};

		struct BuildGameTask
		{
			static void Run(BuildGameTaskArgs args)
			{
				namespace fs2 = std::filesystem;

				const std::wstring cmd = L"cmake --build build --config Release --target AlicePlayer";
				const fs2::path releaseBinDir = args.projectRoot / "build/bin/Release";

				STARTUPINFOW        si{};
				PROCESS_INFORMATION pi{};
				si.cb = sizeof(si);
				si.dwFlags = STARTF_USESHOWWINDOW;
				si.wShowWindow = SW_HIDE;

				// CreateProcessW는 커맨드라인 버퍼를 수정할 수 있어야 하므로 writable buffer 사용
				std::vector<wchar_t> cmdBuffer(cmd.begin(), cmd.end());
				cmdBuffer.push_back(L'\0');

				BOOL ok = CreateProcessW(
					nullptr,
					cmdBuffer.data(),  // writable buffer
					nullptr,
					nullptr,
					FALSE,
					CREATE_NO_WINDOW,
					nullptr,
					args.projectRoot.wstring().c_str(),
					&si,
					&pi);

				if (!ok)
				{
					ALICE_LOG_ERRORF("Build Game: failed to start CMake process.");
					g_BuildInProgress.store(false);
					g_BuildProgress.store(0.0f);
					g_BuildExitCode.store(1);
					return;
				}

				ScopedHandle hProcess(pi.hProcess);
				ScopedHandle hThread(pi.hThread);

				float p = 0.0f;
				for (;;)
				{
					DWORD wait = WaitForSingleObject(hProcess.h, 50);
					if (wait == WAIT_TIMEOUT)
					{
						p += 0.005f;
						if (p > 0.9f) p = 0.9f;
						g_BuildProgress.store(p);
						continue;
					}
					break;
				}

				DWORD exitCode = 0;
				GetExitCodeProcess(hProcess.h, &exitCode);
				ALICE_LOG_INFO("Build Game: CMake build finished with exitCode=%lu",
					static_cast<unsigned long>(exitCode));

				if (exitCode != 0)
				{
					g_BuildProgress.store(1.0f);
					g_BuildExitCode.store(static_cast<long>(exitCode));
					g_BuildInProgress.store(false);
					return;
				}

				// (0) ScriptsBuild: Release AliceScripts.dll 빌드/복사
				const fs2::path scriptsRoot = args.projectRoot / "ScriptsBuild";
				const fs2::path scriptsCMake = scriptsRoot / "CMakeLists.txt";
				const fs2::path scriptsBuildDir = scriptsRoot / "build";
				const fs2::path releaseDllDir = releaseBinDir / "dll";
				std::error_code dllEc;
				fs2::create_directories(releaseDllDir, dllEc);
				if (fs2::exists(scriptsCMake))
				{
					DWORD scExit = 0;
					const std::wstring cmdConfig =
						L"cmake -S \"" + scriptsRoot.wstring() + L"\" -B \"" + scriptsBuildDir.wstring() + L"\"";
					if (!RunProcessHidden(cmdConfig, args.projectRoot, scExit) || scExit != 0)
					{
						ALICE_LOG_ERRORF("Build Game: ScriptsBuild configure failed (exitCode=%lu).",
							static_cast<unsigned long>(scExit));
						g_BuildExitCode.store(12);
						g_BuildInProgress.store(false);
						return;
					}

					const std::wstring cmdBuild =
						L"cmake --build \"" + scriptsBuildDir.wstring() + L"\" --config Release --target AliceScripts";
					if (!RunProcessHidden(cmdBuild, args.projectRoot, scExit) || scExit != 0)
					{
						ALICE_LOG_ERRORF("Build Game: ScriptsBuild build failed (exitCode=%lu).",
							static_cast<unsigned long>(scExit));
						g_BuildExitCode.store(13);
						g_BuildInProgress.store(false);
						return;
					}

					const fs2::path builtDll = scriptsBuildDir / "Release" / "AliceScripts.dll";
					if (!fs2::exists(builtDll))
					{
						ALICE_LOG_ERRORF("Build Game: AliceScripts.dll not found: \"%s\"",
							builtDll.string().c_str());
						g_BuildExitCode.store(14);
						g_BuildInProgress.store(false);
						return;
					}

					if (!CopyFileOver(builtDll, releaseDllDir / "AliceScripts.dll"))
					{
						g_BuildExitCode.store(15);
						g_BuildInProgress.store(false);
						return;
					}

					// RTTR 공유 DLL도 가능하면 스크립트 빌드 결과로 덮어씁니다.
					const fs2::path builtRttr = scriptsBuildDir / "Release" / "rttr_core.dll";
					if (fs2::exists(builtRttr))
					{
						CopyFileOver(builtRttr, releaseDllDir / "rttr_core.dll");
					}
				}

				// (1) Metas: Assets를 청크로 패킹 (폴더구조 숨김, 256KB)
				const fs2::path stageMetas = releaseBinDir / "Metas";
				if (!MakeCleanDir(stageMetas))
				{
					g_BuildExitCode.store(2);
					g_BuildInProgress.store(false);
					return;
				}
				{
					if (!Alice::ResourceManager::Get().CookResourceToChunkStore(args.projectRoot / "Assets", stageMetas, 256 * 1024))
					{
						ALICE_LOG_ERRORF("Build Game: failed to cook Assets -> Metas/Chunks.");
						g_BuildExitCode.store(3);
						g_BuildInProgress.store(false);
						return;
					}
				}

				// (2) Cooked: 항상 새로 생성 + 전부 .alice 암호화
				const fs2::path stageCooked = releaseBinDir / "Cooked";
				if (!MakeCleanDir(stageCooked))
				{
					g_BuildExitCode.store(4);
					g_BuildInProgress.store(false);
					return;
				}
				if (!CookAllIntoCookedRoot(args.projectRoot / "Cooked", stageCooked, "Resource/"))
				{
					g_BuildExitCode.store(5);
					g_BuildInProgress.store(false);
					return;
				}

				// (3) Resource: 원본 폴더를 넣지 않고 Cooked/Chunks로 패킹
				{
					if (!Alice::ResourceManager::Get().CookResourceToChunkStore(args.projectRoot / "Resource", stageCooked))
					{
						ALICE_LOG_ERRORF("Build Game: failed to cook Resource -> Cooked/Chunks (stage).");
						g_BuildExitCode.store(6);
						g_BuildInProgress.store(false);
						return;
					}
				}

				// (4) BuildSettings 복사 (exe 옆)
				if (!CopyFileOver(args.cfgPath, releaseBinDir / "BuildSettings.json"))
				{
					g_BuildExitCode.store(7);
					g_BuildInProgress.store(false);
					return;
				}

				// (4-1) EngineSettings 복사 (Lighting/Skybox)
				const fs2::path engineSettingsPath = args.projectRoot / "EngineSettings.json";
				if (!CopyFileOver(engineSettingsPath, releaseBinDir / "EngineSettings.json"))
				{
					g_BuildExitCode.store(7);
					g_BuildInProgress.store(false);
					return;
				}

				// (5) Export: Bin 아래로 정리 (exe/dll/buildsettings/cooked/metas)
				fs2::path exportRoot = args.exportPathStr;
				if (!exportRoot.is_absolute())
					exportRoot = args.projectRoot / exportRoot;

				const fs2::path exportBin = exportRoot / "Bin";
				if (!MakeCleanDir(exportBin))
				{
					g_BuildExitCode.store(8);
					g_BuildInProgress.store(false);
					return;
				}

				if (!CopyFileOver(releaseBinDir / "AlicePlayer.exe", exportBin / "AlicePlayer.exe"))
				{
					g_BuildExitCode.store(9);
					g_BuildInProgress.store(false);
					return;
				}

				if (!CopyDirTree(releaseBinDir / "dll", exportBin / "dll"))
				{
					g_BuildExitCode.store(9);
					g_BuildInProgress.store(false);
					return;
				}

				if (!CopyDirTree(releaseBinDir / "Cooked", exportBin / "Cooked") ||
					!CopyDirTree(releaseBinDir / "Metas", exportBin / "Metas"))
				{
					g_BuildExitCode.store(10);
					g_BuildInProgress.store(false);
					return;
				}

				if (!CopyFileOver(releaseBinDir / "BuildSettings.json", exportBin / "BuildSettings.json"))
				{
					g_BuildExitCode.store(11);
					g_BuildInProgress.store(false);
					return;
				}

				if (!CopyFileOver(releaseBinDir / "EngineSettings.json", exportBin / "EngineSettings.json"))
				{
					g_BuildExitCode.store(11);
					g_BuildInProgress.store(false);
					return;
				}

				ALICE_LOG_INFO("Build Game: exported to \"%s\" (run: Bin/AlicePlayer.exe)",
					exportRoot.string().c_str());

				g_BuildProgress.store(1.0f);
				g_BuildExitCode.store(static_cast<long>(exitCode));
				g_BuildInProgress.store(false);
			}
		};
	}

	void EditorCore::DrawBuildGameWindow()
	{
		// === Build Game 창 (씬 선택 + 간단한 해상도 옵션) ===
		if (!g_ShowBuildGameWindow)
			return;

		if (ImGui::Begin("Build Game", &g_ShowBuildGameWindow))
		{
			namespace fs = std::filesystem;

			static int   s_Width = 1280;
			static int   s_Height = 720;
			static bool  s_ScanScenesOnce = true;
			static std::vector<fs::path> s_ScenePaths;
			static std::vector<bool>     s_SceneSelected;
			static int   s_DefaultScene = -1;       // 기본으로 실행될 씬 인덱스
			static char  s_ExportPath[260] = "../Build/Export"; // 배포용 출력 경로

			ImGui::Text("Output Resolution");
			ImGui::InputInt("Width (min : 320)", &s_Width);
			ImGui::InputInt("Height (min : 240)", &s_Height);
			if (s_Width < 320)  s_Width = 320;
			if (s_Height < 240) s_Height = 240;

			ImGui::Separator();
			ImGui::Text("Scenes to Build");

			if (s_ScanScenesOnce)
			{
				s_ScanScenesOnce = false;
				s_ScenePaths.clear();
				s_SceneSelected.clear();
				s_DefaultScene = -1;

				const fs::path assetsRoot = ResourceManager::Get().Resolve("Assets");
				if (fs::exists(assetsRoot))
				{
					for (const auto& entry : fs::recursive_directory_iterator(assetsRoot))
					{
						if (!entry.is_regular_file())
							continue;
						if (entry.path().extension() != ".scene")
							continue;

						s_ScenePaths.push_back(entry.path());
						s_SceneSelected.push_back(true);
					}
				}
			}

			// Refresh 버튼 추가
			if (ImGui::Button("Refresh Scenes"))
			{
				s_ScanScenesOnce = true; // 다음 프레임에 다시 스캔
			}
			ImGui::SameLine();
			if (ImGui::Button("Select All"))
			{
				for (std::size_t i = 0; i < s_SceneSelected.size(); ++i)
					s_SceneSelected[i] = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Deselect All"))
			{
				for (std::size_t i = 0; i < s_SceneSelected.size(); ++i)
				s_SceneSelected[i] = false;
				s_DefaultScene = -1;
			}
			ImGui::SameLine();
			ImGui::TextDisabled("(Click to rescan Assets folder)");

			if (s_ScenePaths.empty())
			{
				ImGui::TextDisabled("No .scene files found under Assets.");
			}
			else
			{
				for (std::size_t i = 0; i < s_ScenePaths.size(); ++i)
				{
					bool selected = s_SceneSelected[i];
					ImGui::Checkbox(s_ScenePaths[i].filename().string().c_str(), &selected);
					s_SceneSelected[i] = selected;

					ImGui::SameLine();
					bool isDefault = (static_cast<int>(i) == s_DefaultScene);
					std::string label = "Default##" + std::to_string(i);
					if (ImGui::RadioButton(label.c_str(), isDefault))
					{
						s_DefaultScene = static_cast<int>(i);
					}
				}
			}

			ImGui::Separator();

			// 배포용 출력 경로 입력 + 폴더 선택 버튼
			ImGui::Text("Export Path (relative to project root or absolute)");
			ImGui::InputText("##ExportPath", s_ExportPath, IM_ARRAYSIZE(s_ExportPath));

			// Export Path 입력 필드에 드래그 앤 드롭 추가
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE_PATH"))
				{
					const char* pathStr = static_cast<const char*>(payload->Data);
					std::filesystem::path droppedPath(pathStr);
					std::string ext = droppedPath.extension().string();
					std::transform(ext.begin(), ext.end(), ext.begin(),
						[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

					// 씬 파일(.scene)을 드래그하면 해당 씬 파일의 디렉토리 경로를 Export Path로 설정
					if (ext == ".scene")
					{
						std::filesystem::path sceneDir = droppedPath.parent_path();
						std::string dirStr = sceneDir.string();
						strncpy_s(s_ExportPath, dirStr.c_str(), IM_ARRAYSIZE(s_ExportPath) - 1);
						s_ExportPath[IM_ARRAYSIZE(s_ExportPath) - 1] = '\0';
					}
				}
				ImGui::EndDragDropTarget();
			}

			ImGui::SameLine();
			if (ImGui::Button("Browse..."))
			{
				BROWSEINFOW bi{};
				bi.hwndOwner = m_hwnd;
				bi.lpszTitle = L"Select export folder";
				bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;

				PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
				if (pidl)
				{
					wchar_t folderW[MAX_PATH] = {};
					if (SHGetPathFromIDListW(pidl, folderW))
					{
						std::filesystem::path p = folderW;
						std::string utf8 = p.string();
						// 선택한 경로를 그대로 ExportPath 로 사용 (필요하면 나중에 상대 경로로 변환 가능)
						strncpy_s(s_ExportPath, utf8.c_str(), _TRUNCATE);
					}
					CoTaskMemFree(pidl);
				}
			}

			// 빌드 진행 상황 표시
			if (g_BuildInProgress.load())
			{
				ImGui::Text("Building AliceGame (Release)...");
				float p = g_BuildProgress.load();
				ImGui::ProgressBar(p, ImVec2(-1.0f, 0.0f));
			}
			else
			{
				long exitCode = g_BuildExitCode.load();
				if (exitCode == 0)
				{
					ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "Last build: Success");
				}
				else if (exitCode > 0)
				{
					ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f), "Last build: Failed (code=%ld)", exitCode);
				}
			}

			if (!g_BuildInProgress.load())
			{
				if (ImGui::Button("Build Game"))
				{
					// 1) 빌드 설정 파일 저장 (JSON)
					wchar_t exePathW[MAX_PATH] = {};
					GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
					fs::path exePath = exePathW;
					fs::path exeDir = exePath.parent_path();
					fs::path projectRoot = exeDir.parent_path().parent_path().parent_path(); // build/bin/<Config> → 프로젝트 루트

					fs::path buildDir = projectRoot / "Build";
					std::error_code fec;
					fs::create_directories(buildDir, fec);

					fs::path cfgPath = buildDir / "BuildSettings.json";
					{
						std::ofstream ofs(cfgPath);
						if (ofs.is_open())
						{
							nlohmann::json j;
							j["width"] = s_Width;
							j["height"] = s_Height;

							std::vector<fs::path> includedScenes;
							includedScenes.reserve(s_ScenePaths.size());
							for (std::size_t i = 0; i < s_ScenePaths.size(); ++i)
							{
								if (i >= s_SceneSelected.size()) continue;
								if (!s_SceneSelected[i]) continue;

								fs::path relScene = fs::relative(s_ScenePaths[i], projectRoot);  // 프로젝트 루트 기준으로 상대 경로 (예: "Assets/Stage1/Stage1.scene")
								includedScenes.push_back(relScene);
							}

							// 기본(default) 씬 선택
							fs::path defaultScenePath;
							bool validIndex =
								s_DefaultScene >= 0 &&
								static_cast<size_t>(s_DefaultScene) < s_ScenePaths.size() &&
								s_DefaultScene < static_cast<int>(s_SceneSelected.size()) &&
								s_SceneSelected[s_DefaultScene];

							if (validIndex)
							{
								defaultScenePath = fs::relative(s_ScenePaths[s_DefaultScene], projectRoot); // 상대 경로로 가져오자. ../Assts를 Assets로 바꾸는 것
							}
							else if (!includedScenes.empty())
							{
								defaultScenePath = includedScenes.front();
							}

							if (!defaultScenePath.empty())
							{
								j["default"] = defaultScenePath.string();
							}

							std::vector<std::string> sceneStrings;
							sceneStrings.reserve(includedScenes.size());
							for (const auto& p : includedScenes)
								sceneStrings.push_back(p.string());
							j["scenes"] = sceneStrings;

							ofs << j.dump(4);
						}
					}

					ALICE_LOG_INFO("BuildSettings saved to \"%s\"", cfgPath.string().c_str());

					// 1-1) EngineSettings.json 갱신 (Lighting/Skybox 포함)
					if (!SaveLightingSettingsForBuild(projectRoot))
					{
						ALICE_LOG_WARN("Build Game: EngineSettings.json update failed or skipped.");
					}

					// 2) 별도 스레드에서 CMake 빌드 + 리소스 복사 실행
					g_BuildInProgress.store(true);
					g_BuildProgress.store(0.0f);
					g_BuildExitCode.store(-1);

					// Export 경로 문자열은 스레드 시작 시점에 복사해 둡니다.
					std::string exportPathStr = s_ExportPath;

					const BuildGameTaskArgs args{ projectRoot, cfgPath, exportPathStr };
					std::thread(&BuildGameTask::Run, args).detach();
				}
			}
		}
		ImGui::End();
	}
}
