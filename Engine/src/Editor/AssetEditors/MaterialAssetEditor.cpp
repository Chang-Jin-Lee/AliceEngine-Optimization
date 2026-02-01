#include "Editor/Core/EditorCore.h"
#include "Editor/Core/EditorUIState.h"

#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Rendering/Data/Material.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/UI/UIRenderer.h"

#include "imgui.h"

#include <commdlg.h>

namespace Alice
{
	void EditorCore::DrawMaterialAssetEditorWindow(World& world)
	{
		// === Material Asset Editor (.mat 더블클릭 시) ===
		if (!g_MaterialEditorOpen)
			return;

		if (ImGui::Begin("Material Asset Editor", &g_MaterialEditorOpen))
		{
			ImGui::Text("Asset: %s", g_MaterialEditorPath.string().c_str());
			ImGui::Separator();

			bool changed = false;
			changed |= ImGui::ColorEdit3("Base Color", &g_MaterialEditorData.color.x);
			changed |= ImGui::SliderFloat("Alpha", &g_MaterialEditorData.alpha, 0.0f, 1.0f);
			changed |= ImGui::SliderFloat("Roughness", &g_MaterialEditorData.roughness, 0.0f, 1.0f);
			changed |= ImGui::SliderFloat("Metalness", &g_MaterialEditorData.metalness, 0.0f, 1.0f);

			ImGui::Separator();
			ImGui::Text("Albedo Texture");
			if (!g_MaterialEditorData.albedoTexturePath.empty())
			{
				ImGui::TextWrapped("%s", g_MaterialEditorData.albedoTexturePath.c_str());
			}
			else
			{
				ImGui::TextDisabled("None");
			}
			if (ImGui::Button("Browse Texture##Mat"))
			{
				wchar_t fileBuffer[MAX_PATH] = {};
				OPENFILENAMEW ofn{};
				ofn.lStructSize = sizeof(ofn);
				ofn.hwndOwner = m_hwnd;
				ofn.lpstrFilter = L"Image Files\0*.png;*.jpg;*.jpeg;*.tga;*.bmp;*.dds\0All Files\0*.*\0";
				ofn.lpstrFile = fileBuffer;
				ofn.nMaxFile = MAX_PATH;
				ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

				if (GetOpenFileNameW(&ofn))
				{
					std::filesystem::path absolutePath = fileBuffer;

					// 절대 경로를 논리 경로로 변환
					std::string logicalPath = absolutePath.string();
					{
						std::filesystem::path logical = ResourceManager::NormalizeResourcePathAbsoluteToLogical(absolutePath);
						if (!logical.empty() && !logical.is_absolute())
						{
							logicalPath = logical.string();
						}
					}

					g_MaterialEditorData.albedoTexturePath = logicalPath;
					changed = true;

					ALICE_LOG_INFO("[Editor] Material albedo set from MatEditor: \"%s\"\n",
						g_MaterialEditorData.albedoTexturePath.c_str());
				}
			}

			if (changed)
			{
				// 1) 에셋 파일에 저장
				MaterialFile::Save(g_MaterialEditorPath, g_MaterialEditorData);

				// 2) 이 에셋을 참조하는 모든 엔티티의 MaterialComponent 를 갱신
				const std::string targetPath = g_MaterialEditorPath.string();
				const auto& allMats = world.GetComponents<MaterialComponent>();
				for (const auto& [id, matConst] : allMats)
				{
					MaterialComponent* mat = world.GetComponent<MaterialComponent>(id);
					if (!mat) continue;
					if (mat->assetPath == targetPath)
					{
						mat->color = g_MaterialEditorData.color;
						mat->alpha = g_MaterialEditorData.alpha;
						mat->roughness = g_MaterialEditorData.roughness;
						mat->metalness = g_MaterialEditorData.metalness;
						mat->albedoTexturePath = g_MaterialEditorData.albedoTexturePath;
					}
				}

				g_SceneDirty = true;
			}
		}
		else
		{
			if (m_aliceUIRenderer)
			{
				m_aliceUIRenderer->ClearScreenInputRect();
				m_aliceUIRenderer->ClearScreenMouseOverride();
			}
		}
		ImGui::End();
	}
}
