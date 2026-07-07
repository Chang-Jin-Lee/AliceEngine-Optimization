#include "Editor/Core/EditorCore.h"
#include "Editor/Core/EditorUIState.h"

#include "Runtime/Foundation/ImGuiEx.h"
#include "Runtime/Resources/ResourceManager.h"

#include "imgui.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <system_error>

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

		static char s_searchBuf[128] = {};

		if (ImGui::Button("Refresh"))
		{
			ResourceManager::Get().ClearNegativeCache();
		}
		ImGui::SameLine();
		ImGui::SetNextItemWidth(-1.0f);
		ImGui::InputTextWithHint("##ProjectSearch", "Search assets...", s_searchBuf, sizeof(s_searchBuf));

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

		ImGui::End();
	}
}
