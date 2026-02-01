#include "Editor/Core/EditorCore.h"
#include "Editor/Core/EditorUIState.h"

#include "Runtime/Foundation/ImGuiEx.h"
#include "Runtime/Resources/ResourceManager.h"

#include "imgui.h"

#include <filesystem>

namespace Alice
{
	void EditorCore::DrawProjectWindow(World& world, EntityId& selectedEntity)
	{
		// === Project ===
		if (!ImGui::Begin("Project"))
		{
			ImGui::End();
			return;
		}

		// 현재 씬 정보 및 저장 버튼
		ImGui::Text("Current Scene:");
		ImGui::SameLine();
		if (g_HasCurrentScenePath)
		{
			std::string sceneName = g_CurrentScenePath.filename().string();
			if (g_SceneDirty)
			{
				ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "%s *", sceneName.c_str());
			}
			else
			{
				ImGui::Text("%s", sceneName.c_str());
			}
		}
		else
		{
			ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Unsaved Scene");
		}

		ImGui::SameLine();
		if (ImGui::Button("Save"))
		{
			SaveScene(world);
		}
		if (ImGui::IsItemHovered())
		{
			if (g_HasCurrentScenePath)
			{
				ImGui::SetTooltip("Save to: %s", g_CurrentScenePath.string().c_str());
			}
			else
			{
				ImGui::SetTooltip("Save scene (will use AutoSaved.scene if no path set)");
			}
		}

		ImGui::Separator();

		Alice::ImGuiText(L"Assets 폴더");
		ImGui::Separator();

		// Assets 폴더는 논리 경로로만 다루고, 실제 위치는 ResourceManager 가 해석합니다.
		const std::filesystem::path assetsRoot = ResourceManager::Get().Resolve("Assets");
		if (!std::filesystem::exists(assetsRoot))
		{
			// 폴더가 없다면 한 번만 생성해 둡니다.
			std::filesystem::create_directories(assetsRoot);
		}

		DrawDirectoryNode(world, selectedEntity, assetsRoot);

		ImGui::End();
	}
}
