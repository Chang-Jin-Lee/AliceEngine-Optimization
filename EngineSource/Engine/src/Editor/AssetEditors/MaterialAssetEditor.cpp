#include "Editor/Core/EditorCore.h"
#include "Editor/Core/EditorUIState.h"

#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Rendering/Data/Material.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/UI/UIRenderer.h"

#include "imgui.h"

#include <algorithm>
#include <commdlg.h>
#include <filesystem>
#include <string>

namespace Alice
{
	namespace
	{
		inline bool MaterialAssetEditorFilter(const std::string& propName)
		{
			// assetPath/*TexturePath/shadingMode는 특별 UI 처리
			// shadow/toon/emissive 관련은 커스텀 UI 처리
			return propName != "assetPath" && propName != "albedoTexturePath" && propName != "emissiveTexturePath" && propName != "shadingMode" &&
			       propName != "shadowStrength" && propName != "toonPbrRampIntensity" &&
			       propName != "toonSelfShadowStrength" &&
			       propName != "emissiveIntensity" && propName != "emissiveBloom";
		}

		inline std::string NormalizePathKey(const std::filesystem::path& p)
		{
			std::string s = p.lexically_normal().generic_string();
			std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
			return s;
		}
	}

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

			struct ShadingItem
			{
				const char* label;
				int value;
			};
			const ShadingItem shadingItems[] = {
				{ "Global", -1 },
				{ "Lambert", 0 },
				{ "Phong", 1 },
				{ "Blinn-Phong", 2 },
				{ "Toon", 3 },
				{ "PBR", 4 },
				{ "ToonPBR", 5 },
				{ "ToonPBREditable", 7 },
				{ "OnlyTextureWithOutline", 6 }
			};
			int shadingIndex = 0;
			for (int i = 0; i < (int)std::size(shadingItems); ++i)
			{
				if (shadingItems[i].value == g_MaterialEditorData.shadingMode)
				{
					shadingIndex = i;
					break;
				}
			}
			if (ImGui::Combo("Shading", &shadingIndex, [](void* data, int idx, const char** out_text) {
				auto* items = static_cast<const ShadingItem*>(data);
				*out_text = items[idx].label;
				return true;
				}, (void*)shadingItems, (int)std::size(shadingItems)))
			{
				g_MaterialEditorData.shadingMode = shadingItems[shadingIndex].value;
				changed = true;
			}

			float shadowIntensity = g_MaterialEditorData.shadowStrength;
			if (ImGui::SliderFloat("Shadow Intensity", &shadowIntensity, 0.0f, 1.0f, "%.3f"))
			{
				g_MaterialEditorData.shadowStrength = std::clamp(shadowIntensity, 0.0f, 1.0f);
				changed = true;
			}

			float toonRamp = g_MaterialEditorData.toonPbrRampIntensity;
			if (ImGui::SliderFloat("Toon Ramp Intensity", &toonRamp, 0.0f, 1.0f, "%.3f"))
			{
				g_MaterialEditorData.toonPbrRampIntensity = std::clamp(toonRamp, 0.0f, 1.0f);
				changed = true;
			}

			float selfShadow = g_MaterialEditorData.toonSelfShadowStrength;
			if (ImGui::SliderFloat("Self Shadow", &selfShadow, 0.0f, 1.0f, "%.3f"))
			{
				g_MaterialEditorData.toonSelfShadowStrength = std::clamp(selfShadow, 0.0f, 1.0f);
				changed = true;
			}

			float emissiveIntensity = g_MaterialEditorData.emissiveIntensity;
			if (ImGui::SliderFloat("Emissive Intensity", &emissiveIntensity, 0.0f, 100.0f, "%.3f"))
			{
				g_MaterialEditorData.emissiveIntensity = std::max(emissiveIntensity, 0.0f);
				changed = true;
			}

			float emissiveBloom = g_MaterialEditorData.emissiveBloom;
			if (ImGui::SliderFloat("Emissive Bloom", &emissiveBloom, 0.0f, 10.0f, "%.3f"))
			{
				g_MaterialEditorData.emissiveBloom = std::max(emissiveBloom, 0.0f);
				changed = true;
			}

			changed |= ReflectionUI::RenderInspector(g_MaterialEditorData, MaterialAssetEditorFilter).changed;

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

			ImGui::Separator();
			ImGui::Text("Emissive Texture");
			if (!g_MaterialEditorData.emissiveTexturePath.empty())
			{
				ImGui::TextWrapped("%s", g_MaterialEditorData.emissiveTexturePath.c_str());
			}
			else
			{
				ImGui::TextDisabled("None");
			}
			if (ImGui::Button("Browse Emissive##Mat"))
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

					g_MaterialEditorData.emissiveTexturePath = logicalPath;
					changed = true;

					ALICE_LOG_INFO("[Editor] Material emissive set from MatEditor: \"%s\"\n",
						g_MaterialEditorData.emissiveTexturePath.c_str());
				}
			}

			if (changed)
			{
				// 1) 에셋 파일에 저장
				MaterialFile::Save(g_MaterialEditorPath, g_MaterialEditorData);

				// 2) 이 에셋을 참조하는 모든 엔티티의 MaterialComponent 를 갱신
				//    - 부분 복사 대신 파일 재로드로 모든 필드를 동기화
				const std::filesystem::path targetAbsPath = std::filesystem::absolute(g_MaterialEditorPath).lexically_normal();
				const std::string targetAbsKey = NormalizePathKey(targetAbsPath);
				std::filesystem::path targetLogicalPath = ResourceManager::NormalizeResourcePathAbsoluteToLogical(targetAbsPath);
				const std::string targetLogicalKey = NormalizePathKey(targetLogicalPath);

				const auto& allMats = world.GetComponents<MaterialComponent>();
				for (const auto& [id, matConst] : allMats)
				{
					MaterialComponent* mat = world.GetComponent<MaterialComponent>(id);
					if (!mat) continue;

					bool match = false;
					if (!mat->assetPath.empty())
					{
						const std::filesystem::path matAssetPath = std::filesystem::path(mat->assetPath).lexically_normal();
						const std::string matAssetKey = NormalizePathKey(matAssetPath);

						if (matAssetKey == targetAbsKey || matAssetKey == targetLogicalKey)
						{
							match = true;
						}
						else
						{
							// 논리 경로/상대 경로로 참조 중인 경우 절대 경로로 한 번 더 비교
							const std::filesystem::path resolved = ResourceManager::Get().Resolve(matAssetPath).lexically_normal();
							const std::string resolvedKey = NormalizePathKey(resolved);
							if (resolvedKey == targetAbsKey)
								match = true;
						}
					}

					if (match)
					{
						MaterialFile::Load(targetAbsPath, *mat, &ResourceManager::Get());
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
