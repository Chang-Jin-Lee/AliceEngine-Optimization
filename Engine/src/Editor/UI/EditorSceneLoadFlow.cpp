#include "Editor/Core/EditorCore.h"
#include "Editor/Core/EditorUIState.h"
#include "Editor/Core/EditorUndoRedo.h"

#include "Runtime/Foundation/ImGuiEx.h"
#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Resources/SceneFile.h"
#include "Runtime/Foundation/Logger.h"

#include "imgui.h"

#include <sstream>

namespace Alice
{
	void EditorCore::HandleSceneLoadFlow(World& world, SceneManager* sceneManager, bool& isPlaying, EntityId& selectedEntity)
	{
		// === 씬 변경사항 저장 확인 모달 ===
		if (g_RequestSceneLoad)
		{
			// 현재 씬이 존재하고 변경사항이 있을 때만 확인 모달을 띄웁니다.
			if (g_HasCurrentScenePath && g_SceneDirty)
			{
				ImGui::OpenPopup("SaveSceneBeforeLoad");
			}
			else
			{
				// 저장할 필요가 없으면 바로 로드
				const std::filesystem::path loadAbs =
					ResourceManager::Get().Resolve(g_NextScenePath);

				if (isPlaying)
				{
					// 실행 중: 지연 처리
					ALICE_LOG_INFO("[Editor] LoadSceneFileRequest (no-save, playing): \"%s\"\n",
						g_NextScenePath.string().c_str());
					if (sceneManager)
					{
						if (!sceneManager->LoadSceneFileRequest(loadAbs))
						{
							const std::string errorMsg = "씬 로드 요청 실패: " + g_NextScenePath.string() + "\n\n경로가 잘못되었거나 SceneManager가 초기화되지 않았습니다.";
							ALICE_LOG_ERRORF("[Editor] Scene load request failed: %s", g_NextScenePath.string().c_str());
							g_SceneLoadErrorMsg = errorMsg;
							g_ShowSceneLoadError = true;
						}
						else
						{
							g_CurrentScenePath = g_NextScenePath;
							g_HasCurrentScenePath = true;
							g_SceneDirty = false;
						}
					}
				}
				else
				{
					// 실행 안 함: 즉시 로드
					ALICE_LOG_INFO("[Editor] SceneFile::Load (no-save, not playing): \"%s\"\n",
						g_NextScenePath.string().c_str());
					const bool loadSuccess = SceneFile::LoadAuto(world, Alice::ResourceManager::Get(), g_NextScenePath);

					if (!loadSuccess)
					{
						const std::string errorMsg = "씬 로드 실패: " + g_NextScenePath.string() + "\n\n파일을 읽거나 역직렬화하는 중 오류가 발생했습니다.";
						ALICE_LOG_ERRORF("[Editor] Scene load failed: %s", g_NextScenePath.string().c_str());
						g_SceneLoadErrorMsg = errorMsg;
						g_ShowSceneLoadError = true;
					}
					else
					{
						EnsureSkinnedMeshesRegistered(world);
						selectedEntity = InvalidEntityId;
						g_CurrentScenePath = g_NextScenePath;
						g_HasCurrentScenePath = true;
						g_SceneDirty = false;
						ClearUndoStack(); // 씬 로드 시 Undo 스택 초기화
					}
				}
				g_RequestSceneLoad = false;
			}
		}

		// === 씬 로드 에러 모달 ===
		if (g_ShowSceneLoadError)
		{
			ImGui::OpenPopup("SceneLoadError");
			g_ShowSceneLoadError = false;
		}

		if (ImGui::BeginPopupModal("SceneLoadError", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "씬 로드 실패");
			ImGui::Separator();

			// 에러 메시지 표시 (여러 줄 지원)
			std::istringstream iss(g_SceneLoadErrorMsg);
			std::string line;
			while (std::getline(iss, line))
			{
				ImGui::TextWrapped("%s", line.c_str());
			}

			ImGui::Separator();
			if (ImGui::Button("확인"))
			{
				g_SceneLoadErrorMsg.clear();
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		if (ImGui::BeginPopupModal("SaveSceneBeforeLoad", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			Alice::ImGuiText(L"현재 씬의 변경 내용을 저장하시겠습니까?");
			ImGui::Separator();

			if (ImGui::Button("Save"))
			{
				SaveScene(world);
				// 씬 로드
				const std::filesystem::path loadAbs =
					ResourceManager::Get().Resolve(g_NextScenePath);

				if (isPlaying)
				{
					// 실행 중: 지연 처리
					if (sceneManager)
					{
						if (!sceneManager->LoadSceneFileRequest(loadAbs))
						{
							const std::string errorMsg = "씬 로드 요청 실패: " + g_NextScenePath.string() + "\n\n경로가 잘못되었거나 SceneManager가 초기화되지 않았습니다.";
							ALICE_LOG_ERRORF("[Editor] Scene load request failed: %s", g_NextScenePath.string().c_str());
							g_SceneLoadErrorMsg = errorMsg;
							g_ShowSceneLoadError = true;
							g_RequestSceneLoad = false;
							ImGui::CloseCurrentPopup();
							return;
						}
						g_CurrentScenePath = g_NextScenePath;
						g_HasCurrentScenePath = true;
						g_SceneDirty = false;
					}
					else
					{
						ALICE_LOG_ERRORF("[Editor] SceneManager is null, cannot load scene");
					}
				}
				else
				{
					// 실행 안 함: 즉시 로드
					const bool loadSuccess = SceneFile::LoadAuto(world, Alice::ResourceManager::Get(), g_NextScenePath);

					if (!loadSuccess)
					{
						const std::string errorMsg = "씬 로드 실패: " + g_NextScenePath.string() + "\n\n파일을 읽거나 역직렬화하는 중 오류가 발생했습니다.";
						ALICE_LOG_ERRORF("[Editor] Scene load failed: %s", g_NextScenePath.string().c_str());
						g_SceneLoadErrorMsg = errorMsg;
						g_ShowSceneLoadError = true;
						g_RequestSceneLoad = false;
						ImGui::CloseCurrentPopup();
						return;
					}
					EnsureSkinnedMeshesRegistered(world);
					selectedEntity = InvalidEntityId;
					g_CurrentScenePath = g_NextScenePath;
					g_HasCurrentScenePath = true;
					g_SceneDirty = false;
				}
				selectedEntity = InvalidEntityId;
				g_RequestSceneLoad = false;
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();
			if (ImGui::Button("Don't Save"))
			{
				// 씬 로드
				const std::filesystem::path loadAbs =
					ResourceManager::Get().Resolve(g_NextScenePath);

				if (isPlaying)
				{
					// 실행 중: 지연 처리
					if (sceneManager)
					{
						ALICE_LOG_INFO("[Editor] LoadSceneFileRequest (dont-save, playing): \"%s\"\n",
							g_NextScenePath.string().c_str());
						if (!sceneManager->LoadSceneFileRequest(loadAbs))
						{
							const std::string errorMsg = "씬 로드 요청 실패: " + g_NextScenePath.string() + "\n\n경로가 잘못되었거나 SceneManager가 초기화되지 않았습니다.";
							ALICE_LOG_ERRORF("[Editor] Scene load request failed: %s", g_NextScenePath.string().c_str());
							g_SceneLoadErrorMsg = errorMsg;
							g_ShowSceneLoadError = true;
							g_RequestSceneLoad = false;
							ImGui::CloseCurrentPopup();
							return;
						}
						g_CurrentScenePath = g_NextScenePath;
						g_HasCurrentScenePath = true;
						g_SceneDirty = false;
					}
					else
					{
						ALICE_LOG_ERRORF("[Editor] SceneManager is null, cannot load scene");
					}
				}
				else
				{
					// 실행 안 함: 즉시 로드
					ALICE_LOG_INFO("[Editor] SceneFile::Load (dont-save, not playing): \"%s\"\n",
						g_NextScenePath.string().c_str());

					const bool loadSuccess = SceneFile::LoadAuto(world, Alice::ResourceManager::Get(), g_NextScenePath);

					if (!loadSuccess)
					{
						const std::string errorMsg = "씬 로드 실패: " + g_NextScenePath.string() + "\n\n파일을 읽거나 역직렬화하는 중 오류가 발생했습니다.";
						ALICE_LOG_ERRORF("[Editor] Scene load failed: %s", g_NextScenePath.string().c_str());
						g_SceneLoadErrorMsg = errorMsg;
						g_ShowSceneLoadError = true;
						g_RequestSceneLoad = false;
						ImGui::CloseCurrentPopup();
						return;
					}
					EnsureSkinnedMeshesRegistered(world);
					selectedEntity = InvalidEntityId;
					g_CurrentScenePath = g_NextScenePath;
					g_HasCurrentScenePath = true;
					g_SceneDirty = false;
				}
				selectedEntity = InvalidEntityId;
				g_RequestSceneLoad = false;
				ImGui::CloseCurrentPopup();
			}

			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				// 아무것도 하지 않고 씬 로드를 취소합니다.
				g_RequestSceneLoad = false;
				ImGui::CloseCurrentPopup();
			}

			ImGui::EndPopup();
		}
	}
}
