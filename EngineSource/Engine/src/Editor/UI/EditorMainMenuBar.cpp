#include "Editor/Core/EditorCore.h"
#include "Editor/Core/EditorCommands.h"
#include "Editor/Core/EditorUIState.h"
#include "Editor/Core/EditorUndoRedo.h"
#include "Editor/Scripting/ScriptReloadHelpers.h"

#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Resources/SceneFile.h"
#include "Runtime/Importing/FbxImporter.h"
#include "Runtime/Importing/FbxAsset.h"
#include "Runtime/Importing/FbxModel.h"
#include "Runtime/Rendering/Data/Material.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Resources/Serialization/JsonRttr.h"
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Scripting/ScriptHotReload.h"
#include "Runtime/Scripting/ScriptSystem.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Rendering/Components/MaterialComponent.h"
#include "Runtime/Rendering/Components/SkinnedMeshComponent.h"
#include "Runtime/Rendering/Components/PostProcessVolumeComponent.h"
#include "ThirdParty/json/json.hpp"

#include "imgui.h"

#include <algorithm>
#include <filesystem>
#include <string>
#include <vector>

#include <commdlg.h>

namespace Alice
{
	void EditorCore::DrawMainMenuBar(World& world,
		float deltaTime,
		float fps,
		bool& isPlaying,
		bool& isPaused,
		bool& stepOneFrame,
		std::string& playModeSnapshot,
		EntityId& selectedEntity,
		bool& useForwardRendering,
		bool& isDebugDraw)
	{
		if (!ImGui::BeginMainMenuBar())
			return;

		ImGui::Text("AliceRenderer");
		ImGui::Separator();

		// Play / Pause·Resume / Step / Stop 버튼 (Unity 스타일)
		if (!isPlaying)
		{
			if (ImGui::Button("Play"))
			{
				// 플레이 전에 스크립트 리로드 실행
				ALICE_LOG_INFO("Play button pressed: Starting script reload...");
				bool reloadSuccess = ReloadScripts_FromButton(world);
				if (!reloadSuccess)
				{
					// 스크립트 리로드 실패 시 경고 표시 및 게임 실행 중단
					ALICE_LOG_ERRORF("Play button: Script reload failed. Game will NOT start.");
					ImGui::OpenPopup("ScriptReloadFailed");
					// isPlaying은 설정하지 않음 (게임 실행 안 함)
				}
				else
				{
					ALICE_LOG_INFO("Play button: Script reload succeeded. Starting game...");

					// Unity처럼: Play 순간의 월드를 스냅샷 → Stop 시 이 상태로 복원한다.
					std::string snapshot;
					if (SceneFile::SaveToJsonString(world, snapshot))
						playModeSnapshot = std::move(snapshot);
					else
					{
						playModeSnapshot.clear();
						ALICE_LOG_WARN("Play snapshot failed. Stop will not restore the scene.");
					}

					isPaused = false;
					stepOneFrame = false;
					isPlaying = true;
				}
			}
		}
		else
		{
			if (ImGui::Button(isPaused ? "Resume" : "Pause"))
				isPaused = !isPaused;

			ImGui::SameLine();
			ImGui::BeginDisabled(!isPaused);
			if (ImGui::Button("Step"))
				stepOneFrame = true; // 다음 프레임 한 번만 UpdateShouldUpdateFromScene()을 통과시킴
			ImGui::EndDisabled();

			ImGui::SameLine();
			if (ImGui::Button("Stop"))
			{
				isPlaying = false;
				isPaused = false;
				stepOneFrame = false;

				// Play 스냅샷이 있으면 그 시점의 월드로 복원한다 (Unity의 Stop과 동일).
				if (!playModeSnapshot.empty())
				{
					if (SceneFile::LoadFromJsonString(world, playModeSnapshot))
					{
						selectedEntity = InvalidEntityId;
						EnsureSkinnedMeshesRegistered(world);
						ClearUndoStack(); // 월드 리로드로 기존 EntityId가 무효화되므로 Undo 스택도 초기화
					}
					else
					{
						ALICE_LOG_ERRORF("Stop: Play snapshot restore failed. Scene left in Play-end state.");
					}
					playModeSnapshot.clear();
				}
			}
		}

		// 스크립트 리로드 실패 경고 팝업
		if (ImGui::BeginPopupModal("ScriptReloadFailed", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::Text("Script Reload Failed!");
			ImGui::Separator();
			ImGui::Text("Failed to reload scripts before starting the game.");
			ImGui::Text("Please check the console for error details.");
			ImGui::Text("The game will not start until scripts are reloaded successfully.");
			ImGui::Separator();
			if (ImGui::Button("OK", ImVec2(120, 0)))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		// 오브젝트 생성 메뉴 버튼
		if (ImGui::Button("Create"))
		{
			ImGui::OpenPopup("CreateObjectPopup");
		}
		if (ImGui::BeginPopup("CreateObjectPopup"))
		{
			if (ImGui::MenuItem("Empty"))
			{
				EntityId e = world.CreateEmpty();
				PushCommand(std::make_unique<CreateEntityCommand>(e, "Empty"));
				selectedEntity = e;
				g_SceneDirty = true;
				ImGui::CloseCurrentPopup();
			}
			// FBX Primitives 메뉴
			if (ImGui::BeginMenu("FBX Primitives"))
			{
				// 프리미티브 폴더 스캔해서 자동으로 메뉴 채우기
				static std::vector<std::filesystem::path> cached;
				static bool cachedOnce = false;

				if (!cachedOnce)
				{
					cachedOnce = true;

					auto dirAbs = ResourceManager::Get().Resolve("Assets/Fbx");
					if (std::filesystem::exists(dirAbs))
					{
						for (auto& it : std::filesystem::directory_iterator(dirAbs))
						{
							if (!it.is_regular_file()) continue;
							auto p = it.path();
							auto ext = p.extension().string();
							std::transform(ext.begin(), ext.end(), ext.begin(),
								[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
							if (ext == ".fbxasset")
								cached.push_back(p);
						}
						std::sort(cached.begin(), cached.end());
					}
				}

				// 고정 프리미티브 목록 (폴더에 없어도 표시)
				struct Prim { const char* label; const char* path; };
				static Prim prims[] = {
					{"IcoSphere", "../Assets/Fbx/IcoSphere.fbxasset"},
					{"Torus",     "../Assets/Fbx/Torus.fbxasset"},
					{"Monkey",    "../Assets/Fbx/Monkey.fbxasset"},
					//{"Box",       "../Assets/Fbx/Box.fbxasset"},
					{"Cube(FBX)", "../Assets/Fbx/Cube.fbxasset"},
					{"Sphere(FBX)", "../Assets/Fbx/Sphere.fbxasset"},
					{"Quad(FBX)", "../Assets/Fbx/Quad.fbxasset"},
					{"Corn(FBX)", "../Assets/Fbx/Corn.fbxasset"}
				};

				// 고정 목록 표시
				for (auto& p : prims)
				{
					if (ImGui::MenuItem(p.label))
					{
						EntityId e = InstantiateFbxAssetToWorld(world, p.path, p.label);
						if (e != InvalidEntityId)
						{
							PushCommand(std::make_unique<CreateEntityCommand>(e, p.label));
							selectedEntity = e;
						}
						ImGui::CloseCurrentPopup();
					}
				}

				// 폴더에서 스캔한 추가 FBX들 표시
				if (!cached.empty())
				{
					ImGui::Separator();
					for (auto& abs : cached)
					{
						std::string label = abs.stem().string();

						// 이미 고정 목록에 있는 건 스킵
						bool skip = false;
						for (auto& p : prims)
						{
							if (label == p.label || label == "Cube" && std::string(p.label) == "Cube(FBX)")
							{
								skip = true;
								break;
							}
						}
						if (skip) continue;

						if (ImGui::MenuItem(label.c_str()))
						{
							EntityId e = InstantiateFbxAssetToWorld(world, abs, label);
							if (e != InvalidEntityId)
							{
								PushCommand(std::make_unique<CreateEntityCommand>(e, label));
								selectedEntity = e;
							}
							ImGui::CloseCurrentPopup();
						}
					}
				}

				ImGui::EndMenu();
			}

			if (ImGui::BeginMenu("Camera"))
			{

				if (ImGui::MenuItem("Camera"))
				{
					EntityId e = world.CreateCamera();
					PushCommand(std::make_unique<CreateEntityCommand>(e, "Camera"));
					selectedEntity = e;
					g_SceneDirty = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Light"))
			{
				if (ImGui::MenuItem("Point Light"))
				{
					EntityId e = world.CreatePointLight();
					PushCommand(std::make_unique<CreateEntityCommand>(e, "Point Light"));
					selectedEntity = e;
					g_SceneDirty = true;
					ImGui::CloseCurrentPopup();
				}
				if (ImGui::MenuItem("Spot Light"))
				{
					EntityId e = world.CreateSpotLight();
					PushCommand(std::make_unique<CreateEntityCommand>(e, "Spot Light"));
					selectedEntity = e;
					g_SceneDirty = true;
					ImGui::CloseCurrentPopup();
				}
				if (ImGui::MenuItem("Rect Light"))
				{
					EntityId e = world.CreateRectLight();
					PushCommand(std::make_unique<CreateEntityCommand>(e, "Rect Light"));
					selectedEntity = e;
					g_SceneDirty = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("Post-processing"))
			{
				if (ImGui::MenuItem("Post Process Volume"))
				{
					EntityId e = world.CreateEmpty();
					world.AddComponent<PostProcessVolumeComponent>(e);
					PushCommand(std::make_unique<CreateEntityCommand>(e, "Post Process Volume"));
					selectedEntity = e;
					g_SceneDirty = true;
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndMenu();
			}
			if (ImGui::BeginMenu("AliceUI"))
			{
				if (ImGui::MenuItem("Screen Image"))
				{
					EntityId e = CreateAliceUIImage(world);
					if (e != InvalidEntityId)
					{
						PushCommand(std::make_unique<CreateEntityCommand>(e, "UI Image"));
						selectedEntity = e;
						g_SceneDirty = true;
					}
					ImGui::CloseCurrentPopup();
				}
				if (ImGui::MenuItem("Screen Text"))
				{
					EntityId e = CreateAliceUIText(world);
					if (e != InvalidEntityId)
					{
						PushCommand(std::make_unique<CreateEntityCommand>(e, "UI Text"));
						selectedEntity = e;
						g_SceneDirty = true;
					}
					ImGui::CloseCurrentPopup();
				}
				if (ImGui::MenuItem("Screen Button"))
				{
					EntityId e = CreateAliceUIButton(world);
					if (e != InvalidEntityId)
					{
						PushCommand(std::make_unique<CreateEntityCommand>(e, "UI Button"));
						selectedEntity = e;
						g_SceneDirty = true;
					}
					ImGui::CloseCurrentPopup();
				}
				if (ImGui::MenuItem("Screen Gauge"))
				{
					EntityId e = CreateAliceUIGauge(world);
					if (e != InvalidEntityId)
					{
						PushCommand(std::make_unique<CreateEntityCommand>(e, "UI Gauge"));
						selectedEntity = e;
						g_SceneDirty = true;
					}
					ImGui::CloseCurrentPopup();
				}
				if (ImGui::MenuItem("Screen CheckBox"))
				{
					EntityId e = CreateAliceUICheckBox(world);
					if (e != InvalidEntityId)
					{
						PushCommand(std::make_unique<CreateEntityCommand>(e, "UI CheckBox"));
						selectedEntity = e;
						g_SceneDirty = true;
					}
					ImGui::CloseCurrentPopup();
				}
				if (ImGui::MenuItem("Screen Slider"))
				{
					EntityId e = CreateAliceUISlider(world);
					if (e != InvalidEntityId)
					{
						PushCommand(std::make_unique<CreateEntityCommand>(e, "UI Slider"));
						selectedEntity = e;
						g_SceneDirty = true;
					}
					ImGui::CloseCurrentPopup();
				}
				if (ImGui::MenuItem("World Image"))
				{
					EntityId e = CreateAliceUIWorldImage(world);
					if (e != InvalidEntityId)
					{
						PushCommand(std::make_unique<CreateEntityCommand>(e, "World UI Image"));
						selectedEntity = e;
						g_SceneDirty = true;
					}
					ImGui::CloseCurrentPopup();
				}
				ImGui::EndMenu();
			}
			ImGui::EndPopup();
		}

		ImGui::Separator();

		// 스크립트 핫 리로드 버튼 (C++ 스크립트 DLL 재빌드 + 재로드)
		if (ImGui::Button("Reload Scripts"))
		{
			// ImGui Begin/End 짝을 깨지 않기 위해,
			// 실제 빌드/복사/리로드 로직은 별도 헬퍼 함수에서 처리합니다.
			ReloadScripts_FromButton(world);
			m_scriptBuilded = true;
		}

		ImGui::Separator();
		// FBX 임포트 버튼
		if (ImGui::Button("Load FBX"))
		{
			wchar_t exePathW[MAX_PATH] = {};
			GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
			std::filesystem::path exePath = exePathW;
			std::filesystem::path exeDir = exePath.parent_path();
			std::filesystem::path projectRoot = exeDir.parent_path().parent_path().parent_path(); // build/bin/Debug → 프로젝트 루트
			std::filesystem::path resourceDir = projectRoot / "Resource";
			if (!std::filesystem::exists(resourceDir))
				resourceDir = projectRoot;

			std::wstring initialDirW = resourceDir.wstring();

			wchar_t fileBuffer[MAX_PATH] = {};
			OPENFILENAMEW ofn{};
			ofn.lStructSize = sizeof(ofn);
			ofn.hwndOwner = m_hwnd;
			ofn.lpstrFilter = L"FBX Files\0*.fbx\0All Files\0*.*\0";
			ofn.lpstrFile = fileBuffer;
			ofn.nMaxFile = MAX_PATH;
			ofn.lpstrInitialDir = initialDirW.c_str();
			ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

			if (GetOpenFileNameW(&ofn))
			{
				if (m_renderDevice)
				{
					std::filesystem::path fbxPath = fileBuffer;

					fbxPath = std::filesystem::relative(fbxPath, projectRoot);

					// 간단한 FBX 임포트 옵션
					FbxImportOptions opt{};
					FbxImporter importer(ResourceManager::Get(), m_skinnedRegistry);

					auto* d3dDevice = m_renderDevice->GetDevice();
					FbxImportResult result = importer.Import(d3dDevice, fbxPath, opt);

					// 1) 인스턴스 에셋(.fbxasset)이 생성되었으면, 프로젝트 뷰에서 활용할 수 있습니다.
					// 2) 월드에 기본 인스턴스 하나를 바로 생성해 줍니다. (언리얼의 "씬에 배치" 느낌)
					if (!result.meshAssetPath.empty())
					{
						EntityId e = world.CreateEntity();
						TransformComponent& t = world.AddComponent<TransformComponent>(e);
						t.position = { 0.0f, 0.0f, 0.0f };
						t.scale = { 1.0f, 1.0f, 1.0f };
						t.rotation = { 0.0f, 0.0f, 0.0f };

						// 스키닝 메시 컴포넌트 등록
						SkinnedMeshComponent& skinned = world.AddComponent<SkinnedMeshComponent>(e, result.meshAssetPath);
						skinned.instanceAssetPath = result.instanceAssetPath;

						// (임시) 본 행렬이 아직 없으므로, 1개짜리 항등 행렬 팔레트를 사용합니다.
						//  - 나중에 FbxModel/FbxAnimation 연동 시 실제 본 팔레트로 교체됩니다.
						static DirectX::XMFLOAT4X4 s_identityBone =
							DirectX::XMFLOAT4X4(1, 0, 0, 0,
								0, 1, 0, 0,
								0, 0, 1, 0,
								0, 0, 0, 1);
						skinned.boneMatrices = &s_identityBone;
						skinned.boneCount = 1;

						// 첫 번째 머티리얼이 있으면 기본 머티리얼로 할당
						// 원래 있는 경우 없는 경우 나눠서 있는 경우는 서브 메테리얼을 만들어야 하는데, 일단은 둘다 생기도록 함.
						// TODO : 여기서 서브 메테리얼을 각각 다르게 설정할 수 있게 해야함 
						if (!result.materialAssetPaths.empty())
						{
							DirectX::XMFLOAT3 defaultColor(0.7f, 0.7f, 0.7f);
							MaterialComponent& mat = world.AddComponent<MaterialComponent>(e, defaultColor);
							mat.assetPath = result.materialAssetPaths.front();
							MaterialFile::Load(mat.assetPath, mat, &ResourceManager::Get());
						}
						else
						{
							//DirectX::XMFLOAT3 defaultColor(0.7f, 0.7f, 0.7f);
							//MaterialComponent& mat = world.AddComponent<MaterialComponent>(e, defaultColor);
							//mat.assetPath = "fbx has no material. default material";
							//MaterialFile::Load(mat.assetPath, mat);
						}

						selectedEntity = e;
						g_SceneDirty = true;
					}
				}
			}
		}

		ImGui::Separator();

		// 게임 빌드 버튼 (간단한 1차 버전)
		if (ImGui::Button("Build"))
		{
			g_ShowBuildGameWindow = true;
		}

		ImGui::Separator();
		// PVD 설정 버튼
		if (ImGui::Button("PVD Settings"))
		{
			g_ShowPvdSettingsWindow = true;
		}

		ImGui::Separator();
		ImGui::Text("DeltaTime: %.3f  FPS: %.1f", deltaTime, fps);

		ImGui::Separator();
		// 렌더링 시스템 선택 체크박스
		ImGui::Checkbox("Show DebugDraw", &isDebugDraw);
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("체크: 디버그 라인 켜기\n해제: 디버그 라인 끄기");
		}

		ImGui::Separator();
		// 렌더링 시스템 선택 체크박스
		ImGui::Checkbox("Forward Rendering", &useForwardRendering);
		if (ImGui::IsItemHovered())
		{
			ImGui::SetTooltip("체크: Forward Rendering\n해제: Deferred Rendering");
		}

		ImGui::EndMainMenuBar();
	}
}
