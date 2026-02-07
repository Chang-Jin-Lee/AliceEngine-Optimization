#include "Editor/Core/EditorCore.h"
#include "Editor/Core/EditorUIState.h"
#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Rendering/Data/Material.h"
#include "Runtime/Rendering/Components/PostProcessVolumeComponent.h"
#include "Runtime/Rendering/Components/UnityVfxComponent.h"
#include "Runtime/Rendering/Components/DecalComponent.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Foundation/Logger.h"
#include "ThirdParty/json/json.hpp"
#include <algorithm>
#include <commdlg.h>
#include <fstream>

namespace Alice
{
	namespace
	{
		inline bool MaterialInspectorFilter(const std::string& propName)
		{
			// assetPath/albedoTexturePath/shadingMode는 특별 UI 처리하므로 제외
			return propName != "assetPath" && propName != "albedoTexturePath" && propName != "shadingMode" &&
			       propName != "shadowStrength" && propName != "toonPbrRampIntensity" &&
			       propName != "toonSelfShadowStrength";
		}
	}

	void EditorCore::DrawInspectorMaterial(World& world, const EntityId& _selectedEntity)
	{
		if (auto* mat = world.GetComponent<MaterialComponent>(_selectedEntity)) {
			ImGui::Text("Material");
			if (!mat->assetPath.empty())
				ImGui::Text("Asset: %s", mat->assetPath.c_str());

			bool changed = false;

			// Material assetPath 필드에 드롭 타겟 추가
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE_PATH"))
				{
					const char* pathStr = static_cast<const char*>(payload->Data);
					std::filesystem::path droppedPath(pathStr);
					std::string ext = droppedPath.extension().string();

					// Material 파일인지 확인
					std::transform(ext.begin(), ext.end(), ext.begin(),
						[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					if (ext == ".mat")
					{
						// 논리 경로로 변환
						std::string logicalPath = droppedPath.string();
						{
							std::filesystem::path logical = ResourceManager::NormalizeResourcePathAbsoluteToLogical(droppedPath);
							if (!logical.empty())
							{
								logicalPath = logical.string();
							}
						}
						mat->assetPath = logicalPath;
						// Material 파일에서 속성 로드
						MaterialFile::Load(droppedPath, *mat, &ResourceManager::Get());
						changed = true;
						g_SceneDirty = true;
					}
				}
				ImGui::EndDragDropTarget();
			}
			// Shadow Intensity (커스텀 UI)
			{
				float shadowIntensity = mat->shadowStrength;
				if (ImGui::SliderFloat("Shadow Intensity", &shadowIntensity, 0.0f, 1.0f, "%.3f"))
				{
					mat->shadowStrength = std::clamp(shadowIntensity, 0.0f, 1.0f);
					changed = true;
				}
			}
			// Toon Ramp Intensity (커스텀 UI)
			{
				float toonRamp = mat->toonPbrRampIntensity;
				if (ImGui::SliderFloat("Toon Ramp Intensity", &toonRamp, 0.0f, 1.0f, "%.3f"))
				{
					mat->toonPbrRampIntensity = std::clamp(toonRamp, 0.0f, 1.0f);
					changed = true;
				}
			}
			// Toon Self Shadow (커스텀 UI)
			{
				float toonSelfShadow = mat->toonSelfShadowStrength;
				if (ImGui::SliderFloat("Toon Self Shadow", &toonSelfShadow, 0.0f, 1.0f, "%.3f"))
				{
					mat->toonSelfShadowStrength = std::clamp(toonSelfShadow, 0.0f, 1.0f);
					changed = true;
				}
			}

			changed |= ReflectionUI::RenderInspector(*mat, MaterialInspectorFilter).changed;

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
				if (shadingItems[i].value == mat->shadingMode)
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
				mat->shadingMode = shadingItems[shadingIndex].value;
				changed = true;
			}

			auto IsImageExt = [](std::string ext)
				{
					std::transform(ext.begin(), ext.end(), ext.begin(),
						[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
					return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".dds" || ext == ".tga" || ext == ".bmp";
				};

			auto NormalizeToLogicalIfPossible = [&](const std::filesystem::path& p) -> std::string
				{
					// 기본은 입력 경로 문자열
					std::string out = p.string();

					// 가능하면 "논리 경로"로 변환
					{
						std::filesystem::path logical = ResourceManager::NormalizeResourcePathAbsoluteToLogical(p);
						if (!logical.empty() && !logical.is_absolute())
							out = logical.string();
					}
					return out;
				};

			auto ApplyAlbedoPath = [&](const std::filesystem::path& anyPath)
				{
					if (!IsImageExt(anyPath.extension().string()))
						return;

					mat->albedoTexturePath = NormalizeToLogicalIfPossible(anyPath);
					changed = true;
					g_SceneDirty = true;
				};

			// ---- UI
			ImGui::Text("Albedo: %s", mat->albedoTexturePath.empty() ? "None" : mat->albedoTexturePath.c_str());

			// 드롭 타겟
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE_PATH"))
				{
					// payload가 널 종결 문자열이라는 전제(너희 쪽에서 보장해야 함)
					const char* pathStr = static_cast<const char*>(payload->Data);
					if (pathStr && pathStr[0] != '\0')
					{
						ApplyAlbedoPath(std::filesystem::path(pathStr));
					}
				}
				ImGui::EndDragDropTarget();
			}

			// Browse
			if (ImGui::Button("Browse..."))
			{
				wchar_t buf[MAX_PATH] = {};
				OPENFILENAMEW ofn = { sizeof(ofn) };
				ofn.hwndOwner = m_hwnd;
				ofn.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.dds;*.tga;*.bmp\0All\0*.*\0";
				ofn.lpstrFile = buf;
				ofn.nMaxFile = MAX_PATH;
				ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

				if (GetOpenFileNameW(&ofn))
				{
					ApplyAlbedoPath(std::filesystem::path(buf));
				}
			}


			if (changed) {
				g_SceneDirty = true;
			}

			if (ImGui::Button("Remove Material")) {
				world.RemoveComponent<MaterialComponent>(_selectedEntity);
				g_SceneDirty = true;
			}
		}
	}

	void EditorCore::DrawInspectorDecal(World& world, const EntityId& _selectedEntity)
	{
		if (auto* decal = world.GetComponent<DecalComponent>(_selectedEntity))
		{
			ImGui::PushID("DecalComponent");
			if (ImGui::CollapsingHeader("Decal##DecalComponent", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool changed = false;

				changed |= ImGui::Checkbox("Enabled##Decal", &decal->enabled);

				auto IsImageExt = [](std::string ext)
					{
						std::transform(ext.begin(), ext.end(), ext.begin(),
							[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
						return ext == ".png" || ext == ".jpg" || ext == ".jpeg" || ext == ".dds" || ext == ".tga" || ext == ".bmp";
					};

				auto NormalizeToLogicalIfPossible = [&](const std::filesystem::path& p) -> std::string
					{
						std::string out = p.string();
						std::filesystem::path logical = ResourceManager::NormalizeResourcePathAbsoluteToLogical(p);
						if (!logical.empty() && !logical.is_absolute())
							out = logical.string();
						return out;
					};

				auto ApplyAlbedoPath = [&](const std::filesystem::path& anyPath)
					{
						if (!IsImageExt(anyPath.extension().string()))
							return;
						decal->albedoTexturePath = NormalizeToLogicalIfPossible(anyPath);
						changed = true;
						g_SceneDirty = true;
					};

				ImGui::Text("Albedo: %s", decal->albedoTexturePath.empty() ? "None" : decal->albedoTexturePath.c_str());

				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE_PATH"))
					{
						const char* pathStr = static_cast<const char*>(payload->Data);
						if (pathStr && pathStr[0] != '\0')
						{
							ApplyAlbedoPath(std::filesystem::path(pathStr));
						}
					}
					ImGui::EndDragDropTarget();
				}

				if (ImGui::Button("Browse...##DecalAlbedo"))
				{
					wchar_t buf[MAX_PATH] = {};
					OPENFILENAMEW ofn = { sizeof(ofn) };
					ofn.hwndOwner = m_hwnd;
					ofn.lpstrFilter = L"Images\0*.png;*.jpg;*.jpeg;*.dds;*.tga;*.bmp\0All\0*.*\0";
					ofn.lpstrFile = buf;
					ofn.nMaxFile = MAX_PATH;
					ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

					if (GetOpenFileNameW(&ofn))
					{
						ApplyAlbedoPath(std::filesystem::path(buf));
					}
				}

				auto filter = [](const std::string& propName)
					{
						return propName != "albedoTexturePath" && propName != "enabled";
					};
				changed |= ReflectionUI::RenderInspector(*decal, filter).changed;

				if (ImGui::Button("Remove Decal"))
				{
					world.RemoveComponent<DecalComponent>(_selectedEntity);
					g_SceneDirty = true;
					return;
				}

				if (changed) g_SceneDirty = true;
			}
			ImGui::PopID();
		}
	}


	void EditorCore::DrawInspectorPointLight(World& world, const EntityId& _selectedEntity)
	{
		if (auto* light = world.GetComponent<PointLightComponent>(_selectedEntity)) {
			if (ImGui::CollapsingHeader("Point Light", ImGuiTreeNodeFlags_DefaultOpen)) {
				bool changed = false;
				changed |= ImGui::Checkbox("Enabled##PointLight", &light->enabled);
				changed |= ImGui::ColorEdit3("Color##PointLight", &light->color.x);
				changed |= ImGui::SliderFloat("Intensity##PointLight", &light->intensity, 0.0f, 50.0f);
				changed |= ImGui::SliderFloat("Range##PointLight", &light->range, 0.1f, 200.0f);

				if (ImGui::Button("Remove Point Light")) {
					world.RemoveComponent<PointLightComponent>(_selectedEntity);
					g_SceneDirty = true;
					return;
				}

				if (changed) g_SceneDirty = true;
			}
		}
	}


	void EditorCore::DrawInspectorSpotLight(World& world, const EntityId& _selectedEntity)
	{
		if (auto* light = world.GetComponent<SpotLightComponent>(_selectedEntity)) {
			if (ImGui::CollapsingHeader("Spot Light", ImGuiTreeNodeFlags_DefaultOpen)) {
				bool changed = false;
				changed |= ImGui::Checkbox("Enabled##SpotLight", &light->enabled);
				changed |= ImGui::ColorEdit3("Color##SpotLight", &light->color.x);
				changed |= ImGui::SliderFloat("Intensity##SpotLight", &light->intensity, 0.0f, 50.0f);
				changed |= ImGui::SliderFloat("Range##SpotLight", &light->range, 0.1f, 200.0f);
				changed |= ImGui::SliderFloat("Inner Angle (deg)##SpotLight", &light->innerAngleDeg, 0.0f, 89.0f);
				changed |= ImGui::SliderFloat("Outer Angle (deg)##SpotLight", &light->outerAngleDeg, 0.0f, 89.0f);

				if (light->innerAngleDeg > light->outerAngleDeg)
					light->innerAngleDeg = light->outerAngleDeg;

				if (ImGui::Button("Remove Spot Light")) {
					world.RemoveComponent<SpotLightComponent>(_selectedEntity);
					g_SceneDirty = true;
					return;
				}

				if (changed) g_SceneDirty = true;
			}
		}
	}


	void EditorCore::DrawInspectorRectLight(World& world, const EntityId& _selectedEntity)
	{
		if (auto* light = world.GetComponent<RectLightComponent>(_selectedEntity)) {
			if (ImGui::CollapsingHeader("Rect Light", ImGuiTreeNodeFlags_DefaultOpen)) {
				bool changed = false;
				changed |= ImGui::Checkbox("Enabled##RectLight", &light->enabled);
				changed |= ImGui::ColorEdit3("Color##RectLight", &light->color.x);
				changed |= ImGui::SliderFloat("Intensity##RectLight", &light->intensity, 0.0f, 50.0f);
				changed |= ImGui::SliderFloat("Width##RectLight", &light->width, 0.1f, 50.0f);
				changed |= ImGui::SliderFloat("Height##RectLight", &light->height, 0.1f, 50.0f);
				changed |= ImGui::SliderFloat("Range##RectLight", &light->range, 0.1f, 200.0f);

				if (ImGui::Button("Remove Rect Light")) {
					world.RemoveComponent<RectLightComponent>(_selectedEntity);
					g_SceneDirty = true;
					return;
				}

				if (changed) g_SceneDirty = true;
			}
		}
	}


	void EditorCore::SaveDefaultPostProcessSettings()
	{
		namespace fs = std::filesystem;

		// 프로젝트 루트 경로 계산
		wchar_t exePathW[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
		fs::path exePath = exePathW;
		fs::path exeDir = exePath.parent_path();
		fs::path projectRoot = exeDir.parent_path().parent_path().parent_path(); // build/bin/Debug → 프로젝트 루트
		fs::path settingsPath = projectRoot / "EngineSettings.json";

		try
		{
			nlohmann::json j;

			// 기존 파일이 있으면 읽기
			if (fs::exists(settingsPath))
			{
				std::ifstream ifs(settingsPath);
				if (ifs.is_open())
				{
					ifs >> j;
					ifs.close();
				}
			}

			// Default PostProcess Settings 저장
			nlohmann::json ppSettings;
			ppSettings["exposure"] = m_defaultPostProcessSettings.exposure;
			ppSettings["maxHDRNits"] = m_defaultPostProcessSettings.maxHDRNits;
			ppSettings["saturation"] = { m_defaultPostProcessSettings.saturation.x, m_defaultPostProcessSettings.saturation.y, m_defaultPostProcessSettings.saturation.z };
			ppSettings["contrast"] = { m_defaultPostProcessSettings.contrast.x, m_defaultPostProcessSettings.contrast.y, m_defaultPostProcessSettings.contrast.z };
			ppSettings["gamma"] = { m_defaultPostProcessSettings.gamma.x, m_defaultPostProcessSettings.gamma.y, m_defaultPostProcessSettings.gamma.z };
			ppSettings["gain"] = { m_defaultPostProcessSettings.gain.x, m_defaultPostProcessSettings.gain.y, m_defaultPostProcessSettings.gain.z };
			ppSettings["bloomThreshold"] = m_defaultPostProcessSettings.bloomThreshold;
			ppSettings["bloomKnee"] = m_defaultPostProcessSettings.bloomKnee;
			ppSettings["bloomIntensity"] = m_defaultPostProcessSettings.bloomIntensity;
			ppSettings["bloomGaussianIntensity"] = m_defaultPostProcessSettings.bloomGaussianIntensity;
			ppSettings["bloomRadius"] = m_defaultPostProcessSettings.bloomRadius;
			ppSettings["bloomDownsample"] = m_defaultPostProcessSettings.bloomDownsample;

			j["defaultPostProcess"] = ppSettings;

			// 파일 저장
			std::ofstream ofs(settingsPath);
			if (ofs.is_open())
			{
				ofs << j.dump(4);
				ofs.close();
				ALICE_LOG_INFO("Default PostProcess Settings saved to EngineSettings.json");
			}
			else
			{
				ALICE_LOG_ERRORF("Failed to save Default PostProcess Settings to %s", settingsPath.string().c_str());
			}
		}
		catch (const std::exception& e)
		{
			ALICE_LOG_ERRORF("Exception while saving Default PostProcess Settings: %s", e.what());
		}
	}


	void EditorCore::LoadDefaultPostProcessSettings()
	{
		namespace fs = std::filesystem;

		// 프로젝트 루트 경로 계산
		wchar_t exePathW[MAX_PATH] = {};
		GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
		fs::path exePath = exePathW;
		fs::path exeDir = exePath.parent_path();
		fs::path projectRoot = exeDir.parent_path().parent_path().parent_path(); // build/bin/Debug → 프로젝트 루트
		fs::path settingsPath = projectRoot / "EngineSettings.json";

		try
		{
			if (!fs::exists(settingsPath))
			{
				// 파일이 없으면 기본값 유지
				return;
			}

			std::ifstream ifs(settingsPath);
			if (!ifs.is_open())
			{
				return;
			}

			nlohmann::json j;
			ifs >> j;
			ifs.close();

			// Default PostProcess Settings 로드
			if (j.contains("defaultPostProcess"))
			{
				const auto& ppSettings = j["defaultPostProcess"];

				if (ppSettings.contains("exposure"))
					m_defaultPostProcessSettings.exposure = ppSettings["exposure"].get<float>();
				if (ppSettings.contains("maxHDRNits"))
					m_defaultPostProcessSettings.maxHDRNits = ppSettings["maxHDRNits"].get<float>();

				if (ppSettings.contains("saturation") && ppSettings["saturation"].is_array() && ppSettings["saturation"].size() >= 3)
				{
					m_defaultPostProcessSettings.saturation.x = ppSettings["saturation"][0].get<float>();
					m_defaultPostProcessSettings.saturation.y = ppSettings["saturation"][1].get<float>();
					m_defaultPostProcessSettings.saturation.z = ppSettings["saturation"][2].get<float>();
				}

				if (ppSettings.contains("contrast") && ppSettings["contrast"].is_array() && ppSettings["contrast"].size() >= 3)
				{
					m_defaultPostProcessSettings.contrast.x = ppSettings["contrast"][0].get<float>();
					m_defaultPostProcessSettings.contrast.y = ppSettings["contrast"][1].get<float>();
					m_defaultPostProcessSettings.contrast.z = ppSettings["contrast"][2].get<float>();
				}

				if (ppSettings.contains("gamma") && ppSettings["gamma"].is_array() && ppSettings["gamma"].size() >= 3)
				{
					m_defaultPostProcessSettings.gamma.x = ppSettings["gamma"][0].get<float>();
					m_defaultPostProcessSettings.gamma.y = ppSettings["gamma"][1].get<float>();
					m_defaultPostProcessSettings.gamma.z = ppSettings["gamma"][2].get<float>();
				}

				if (ppSettings.contains("gain") && ppSettings["gain"].is_array() && ppSettings["gain"].size() >= 3)
				{
					m_defaultPostProcessSettings.gain.x = ppSettings["gain"][0].get<float>();
					m_defaultPostProcessSettings.gain.y = ppSettings["gain"][1].get<float>();
					m_defaultPostProcessSettings.gain.z = ppSettings["gain"][2].get<float>();
				}

				if (ppSettings.contains("bloomThreshold"))
					m_defaultPostProcessSettings.bloomThreshold = ppSettings["bloomThreshold"].get<float>();
				if (ppSettings.contains("bloomKnee"))
					m_defaultPostProcessSettings.bloomKnee = ppSettings["bloomKnee"].get<float>();
				if (ppSettings.contains("bloomIntensity"))
					m_defaultPostProcessSettings.bloomIntensity = ppSettings["bloomIntensity"].get<float>();
				if (ppSettings.contains("bloomGaussianIntensity"))
					m_defaultPostProcessSettings.bloomGaussianIntensity = ppSettings["bloomGaussianIntensity"].get<float>();
				if (ppSettings.contains("bloomRadius"))
					m_defaultPostProcessSettings.bloomRadius = ppSettings["bloomRadius"].get<float>();
				if (ppSettings.contains("bloomDownsample"))
					m_defaultPostProcessSettings.bloomDownsample = ppSettings["bloomDownsample"].get<int>();

				ALICE_LOG_INFO("Default PostProcess Settings loaded from EngineSettings.json");
			}
		}
		catch (const std::exception& e)
		{
			ALICE_LOG_ERRORF("Exception while loading Default PostProcess Settings: %s", e.what());
		}
	}


	void EditorCore::DrawInspectorPostProcessVolume(World& world, const EntityId& _selectedEntity)
	{
		if (auto* volume = world.GetComponent<PostProcessVolumeComponent>(_selectedEntity))
		{
			if (ImGui::CollapsingHeader("Post Process Volume", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool changed = false;

				// ==== Unbound 설정 (최상단) ====
				ImGui::Text("Volume Type");
				bool unboundBefore = volume->unbound;
				changed |= ImGui::Checkbox("Unbound (전역 적용)##PostProcessVolume", &volume->unbound);
				if (ImGui::IsItemHovered())
					ImGui::SetTooltip("Unbound: ON이면 항상 전역 적용 (무한 범위)\nOFF이면 Shape + BlendRadius 기반 공간 적용");

				if (volume->unbound)
				{
					ImGui::SameLine();
					ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f), "[전역 적용 중]");
				}
				else
				{
					ImGui::SameLine();
					ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f), "[공간 기반 적용]");
				}

				// Unbound 변경 시 DebugDrawBoxComponent 업데이트
				if (unboundBefore != volume->unbound)
				{
					world.UpdatePostProcessVolumeDebugBox(_selectedEntity, *volume);
				}

				ImGui::Separator();

				// ==== Bound 설정 (Unbound OFF일 때만 의미 있음) ====
				if (volume->unbound)
				{
					// Unbound ON: Bound 비활성화
					ImGui::BeginDisabled();
				}

				float blendRadius = volume->GetBlendRadius();
				if (ImGui::SliderFloat("Bound##PostProcessVolume", &blendRadius, 0.0f, 50.0f))
				{
					volume->SetBlendRadius(blendRadius);
					// DebugDrawBoxComponent bounds 업데이트
					world.UpdatePostProcessVolumeDebugBox(_selectedEntity, *volume);
					changed = true;
				}
				if (ImGui::IsItemHovered())
				{
					if (volume->unbound)
						ImGui::SetTooltip("Unbound가 켜져 있어 Bound는 적용되지 않습니다.");
					else
						ImGui::SetTooltip("보간이 적용되는 범위 (0이면 내부에서만 적용)");
				}

				if (volume->unbound)
				{
					ImGui::EndDisabled();
				}

				ImGui::Separator();

				// ==== Post Process Settings ====
				ImGui::Text("Post Process Settings");
				PostProcessSettings& settings = volume->settings;

				// Exposure
				if (ImGui::TreeNode("Exposure##PostProcessVolume"))
				{
					changed |= ImGui::Checkbox("Override Exposure##PostProcessVolume", &settings.bOverride_Exposure);
					if (settings.bOverride_Exposure)
					{
						ImGui::Indent();
						changed |= ImGui::SliderFloat("Exposure##PostProcessVolume", &settings.exposure, -3.0f, 3.0f, "%.2f");
						ImGui::Unindent();
					}
					ImGui::TreePop();
				}

				// Max HDR Nits
				if (ImGui::TreeNode("Max HDR Nits##PostProcessVolume"))
				{
					changed |= ImGui::Checkbox("Override Max HDR Nits##PostProcessVolume", &settings.bOverride_MaxHDRNits);
					if (settings.bOverride_MaxHDRNits)
					{
						ImGui::Indent();
						changed |= ImGui::SliderFloat("Max HDR Nits##PostProcessVolume", &settings.maxHDRNits, 100.0f, 10000.0f, "%.0f nits");
						ImGui::Unindent();
					}
					ImGui::TreePop();
				}

				// Color Grading
				if (ImGui::TreeNode("Color Grading##PostProcessVolume"))
				{
					// Saturation
					changed |= ImGui::Checkbox("Override Saturation##PostProcessVolume", &settings.bOverride_ColorGradingSaturation);
					if (settings.bOverride_ColorGradingSaturation)
					{
						ImGui::Indent();
						changed |= ImGui::ColorEdit3("Saturation (RGB)##PostProcessVolume", &settings.saturation.x,
							ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_Float);
						ImGui::Unindent();
					}

					// Contrast
					changed |= ImGui::Checkbox("Override Contrast##PostProcessVolume", &settings.bOverride_ColorGradingContrast);
					if (settings.bOverride_ColorGradingContrast)
					{
						ImGui::Indent();
						changed |= ImGui::ColorEdit3("Contrast (RGB)##PostProcessVolume", &settings.contrast.x,
							ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_Float);
						ImGui::Unindent();
					}

					// Gamma
					changed |= ImGui::Checkbox("Override Gamma##PostProcessVolume", &settings.bOverride_ColorGradingGamma);
					if (settings.bOverride_ColorGradingGamma)
					{
						ImGui::Indent();
						changed |= ImGui::ColorEdit3("Gamma (RGB)##PostProcessVolume", &settings.gamma.x,
							ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_Float);
						ImGui::Unindent();
					}

					// Gain
					changed |= ImGui::Checkbox("Override Gain##PostProcessVolume", &settings.bOverride_ColorGradingGain);
					if (settings.bOverride_ColorGradingGain)
					{
						ImGui::Indent();
						changed |= ImGui::ColorEdit3("Gain (RGB)##PostProcessVolume", &settings.gain.x,
							ImGuiColorEditFlags_NoAlpha | ImGuiColorEditFlags_InputRGB | ImGuiColorEditFlags_Float);
						ImGui::Unindent();
					}

					ImGui::TreePop();
				}

				// Bloom
				if (ImGui::TreeNode("Bloom##PostProcessVolume"))
				{
					changed |= ImGui::Checkbox("Override Threshold##PostProcessVolume", &settings.bOverride_BloomThreshold);
					if (settings.bOverride_BloomThreshold)
					{
						ImGui::Indent();
						changed |= ImGui::SliderFloat("Threshold##PostProcessVolume", &settings.bloomThreshold, 0.0f, 5.0f);
						ImGui::Unindent();
					}

					changed |= ImGui::Checkbox("Override Knee##PostProcessVolume", &settings.bOverride_BloomKnee);
					if (settings.bOverride_BloomKnee)
					{
						ImGui::Indent();
						changed |= ImGui::SliderFloat("Knee##PostProcessVolume", &settings.bloomKnee, 0.0f, 1.0f);
						ImGui::Unindent();
					}

					changed |= ImGui::Checkbox("Override Intensity##PostProcessVolume", &settings.bOverride_BloomIntensity);
					if (settings.bOverride_BloomIntensity)
					{
						ImGui::Indent();
						changed |= ImGui::SliderFloat("Intensity##PostProcessVolume", &settings.bloomIntensity, 0.0f, 5.0f);
						ImGui::Unindent();
					}

					changed |= ImGui::Checkbox("Override Gaussian Intensity##PostProcessVolume", &settings.bOverride_BloomGaussianIntensity);
					if (settings.bOverride_BloomGaussianIntensity)
					{
						ImGui::Indent();
						changed |= ImGui::SliderFloat("Gaussian Intensity##PostProcessVolume", &settings.bloomGaussianIntensity, 0.0f, 5.0f);
						ImGui::Unindent();
					}

					changed |= ImGui::Checkbox("Override Radius##PostProcessVolume", &settings.bOverride_BloomRadius);
					if (settings.bOverride_BloomRadius)
					{
						ImGui::Indent();
						changed |= ImGui::SliderFloat("Radius##PostProcessVolume", &settings.bloomRadius, 0.1f, 10.0f);
						ImGui::Unindent();
					}

					changed |= ImGui::Checkbox("Override Downsample##PostProcessVolume", &settings.bOverride_BloomDownsample);
					if (settings.bOverride_BloomDownsample)
					{
						ImGui::Indent();
						const char* downsampleNames[] = { "1x", "2x", "4x" };
						int downsampleValues[] = { 1, 2, 4 };
						int currentDownsample = settings.bloomDownsample;
						int currentIndex = 0;
						for (int i = 0; i < IM_ARRAYSIZE(downsampleValues); ++i)
						{
							if (downsampleValues[i] == currentDownsample)
							{
								currentIndex = i;
								break;
							}
						}
						if (ImGui::Combo("Downsample##PostProcessVolume", &currentIndex, downsampleNames, IM_ARRAYSIZE(downsampleNames)))
						{
							settings.bloomDownsample = downsampleValues[currentIndex];
							changed = true;
						}
						ImGui::Unindent();
					}

					ImGui::TreePop();
				}

				ImGui::Separator();

				// ==== 참조 오브젝트 설정 ====
				if (ImGui::TreeNode("Reference Object##PostProcessVolume"))
				{
					changed |= ImGui::Checkbox("Use Reference Object##PostProcessVolume", &volume->useReferenceObject);
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("PostProcessVolume 보간 기준을 참조 오브젝트로 사용할지 여부\n비활성화하면 카메라 위치 사용");

					if (volume->useReferenceObject)
					{
						ImGui::Indent();
						char nameBuf[256] = {};
						strncpy_s(nameBuf, volume->referenceObjectName.c_str(), sizeof(nameBuf) - 1);
						if (ImGui::InputText("Reference GameObject Name##PostProcessVolume", nameBuf, sizeof(nameBuf)))
						{
							volume->SetReferenceObjectName(nameBuf);
							changed = true;
						}
						if (ImGui::IsItemHovered())
							ImGui::SetTooltip("PostProcessVolume 보간 기준이 될 GameObject 이름\n비어있으면 카메라 위치 사용");

						if (!volume->referenceObjectName.empty())
						{
							GameObject refObj = world.FindGameObject(volume->referenceObjectName);
							if (refObj.IsValid())
							{
								auto* transform = world.GetComponent<TransformComponent>(refObj.id());
								if (transform && transform->enabled)
								{
									ImGui::TextColored(ImVec4(0.3f, 0.9f, 0.3f, 1.0f),
										"Bound to: %s (Position: %.2f, %.2f, %.2f)",
										volume->referenceObjectName.c_str(),
										transform->position.x, transform->position.y, transform->position.z);
								}
								else
								{
									ImGui::TextColored(ImVec4(0.9f, 0.3f, 0.3f, 1.0f),
										"Bound to: %s (Transform not found or disabled)", volume->referenceObjectName.c_str());
								}
							}
							else
							{
								ImGui::TextColored(ImVec4(0.9f, 0.7f, 0.3f, 1.0f),
									"Object not found: %s (using camera position)", volume->referenceObjectName.c_str());
							}
						}
						else
						{
							ImGui::TextDisabled("No reference object set (using camera position)");
						}
						ImGui::Unindent();
					}
					ImGui::TreePop();
				}

				if (ImGui::Button("Remove Post Process Volume"))
				{
					world.RemoveComponent<PostProcessVolumeComponent>(_selectedEntity);
					g_SceneDirty = true;
					return;
				}

				if (changed) g_SceneDirty = true;
			}
		}
	}


	void EditorCore::DrawInspectorComputeEffect(World& world, const EntityId& _selectedEntity)
	{
		if (auto* effect = world.GetComponent<ComputeEffectComponent>(_selectedEntity)) {
			if (ImGui::CollapsingHeader("Particle System (Compute)", ImGuiTreeNodeFlags_DefaultOpen)) {
				bool changed = false;
				changed |= ImGui::Checkbox("Enabled##ComputeEffect", &effect->enabled);

				// 파티클 타입 콤보박스
				const char* particleTypes[] = { "Particle", "Sparks", "Smoke", "Vortex", "Snow", "Explosion" };
				int currentIndex = 0;
				for (int i = 0; i < IM_ARRAYSIZE(particleTypes); ++i) {
					if (effect->shaderName == particleTypes[i]) {
						currentIndex = i;
						break;
					}
				}
				if (ImGui::Combo("Particle Type##ComputeEffect", &currentIndex, particleTypes, IM_ARRAYSIZE(particleTypes))) {
					effect->shaderName = particleTypes[currentIndex];
					changed = true;
				}

				ImGui::TextDisabled("Emitter uses Transform. Use Local Offset for placement.");

				// Main Module
				if (ImGui::TreeNode("Main##ComputeEffect")) {
					const char* simSpaces[] = { "World", "Local" };
					int simIndex = (effect->simulationSpace == ParticleSimulationSpace::Local) ? 1 : 0;
					if (ImGui::Combo("Simulation Space##ComputeEffect", &simIndex, simSpaces, IM_ARRAYSIZE(simSpaces))) {
						effect->simulationSpace = (simIndex == 1) ? ParticleSimulationSpace::Local : ParticleSimulationSpace::World;
						changed = true;
					}
					changed |= ImGui::SliderFloat3("Local Offset##ComputeEffect", &effect->localOffset.x, -10.0f, 10.0f);
					changed |= ImGui::SliderFloat("Start Lifetime Min##ComputeEffect", &effect->lifeMin, 0.01f, 10.0f);
					changed |= ImGui::SliderFloat("Start Lifetime Max##ComputeEffect", &effect->lifeMax, 0.01f, 10.0f);
					if (effect->lifeMax < effect->lifeMin) effect->lifeMax = effect->lifeMin;
					changed |= ImGui::SliderFloat("Start Speed##ComputeEffect", &effect->startSpeed, 0.0f, 10.0f);
					changed |= ImGui::SliderFloat("Start Size (px)##ComputeEffect", &effect->sizePx, 0.5f, 50.0f);
					changed |= ImGui::ColorEdit3("Start Color##ComputeEffect", &effect->color.x);
					changed |= ImGui::SliderFloat("Intensity##ComputeEffect", &effect->intensity, 0.0f, 10.0f);
					ImGui::TreePop();
				}

				// Emission Module
				if (ImGui::TreeNode("Emission##ComputeEffect")) {
					changed |= ImGui::SliderFloat("Spawn Rate##ComputeEffect", &effect->spawnRate, 0.0f, 1.0f);
					ImGui::TreePop();
				}

				// Shape Module
				if (ImGui::TreeNode("Shape##ComputeEffect")) {
					changed |= ImGui::SliderFloat("Radius##ComputeEffect", &effect->radius, 0.01f, 5.0f);
					ImGui::TreePop();
				}

				// Forces Module
				if (ImGui::TreeNode("Forces##ComputeEffect")) {
					changed |= ImGui::SliderFloat3("Gravity##ComputeEffect", &effect->gravity.x, -10.0f, 10.0f);
					changed |= ImGui::SliderFloat("Drag##ComputeEffect", &effect->drag, 0.0f, 1.0f);
					ImGui::TreePop();
				}

				// Renderer Module
				if (ImGui::TreeNode("Renderer##ComputeEffect")) {
					changed |= ImGui::Checkbox("Depth Test##ComputeEffect", &effect->depthTest);
					if (effect->depthTest) {
						changed |= ImGui::SliderFloat("Depth Bias (m)##ComputeEffect", &effect->depthBiasMeters, 0.0f, 0.5f);
					}
					ImGui::TreePop();
				}

				if (ImGui::Button("Remove Compute Effect")) {
					world.RemoveComponent<ComputeEffectComponent>(_selectedEntity);
					g_SceneDirty = true;
					return;
				}

				if (changed) g_SceneDirty = true;
			}
		}
	}

	void EditorCore::DrawInspectorUnityVfx(World& world, const EntityId& _selectedEntity)
	{
		if (auto* vfx = world.GetComponent<UnityVfxComponent>(_selectedEntity)) {
			if (ImGui::CollapsingHeader("Unity VFX (Particle)", ImGuiTreeNodeFlags_DefaultOpen)) {
				bool changed = false;
				auto DragFloatWithInput = [&](const char* label, float* value, float minValue, float maxValue, const char* fmt = "%.3f") -> bool {
					float speed = (maxValue - minValue) / 200.0f;
					if (speed <= 0.0f) speed = 0.01f;

					ImGui::PushID(label);
					bool localChanged = ImGui::DragFloat(label, value, speed, minValue, maxValue, fmt);

					static float s_editValue = 0.0f;
					if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
					{
						s_editValue = *value;
						ImGui::OpenPopup("EditValue");
					}

					if (ImGui::BeginPopup("EditValue"))
					{
						ImGui::SetNextItemWidth(140.0f);
						bool edited = ImGui::InputFloat("Value", &s_editValue, 0.0f, 0.0f, fmt);
						if (edited && ImGui::IsKeyPressed(ImGuiKey_Enter))
						{
							const float clamped = std::clamp(s_editValue, minValue, maxValue);
							if (*value != clamped)
							{
								*value = clamped;
								localChanged = true;
							}
							ImGui::CloseCurrentPopup();
						}
						if (ImGui::Button("OK"))
						{
							const float clamped = std::clamp(s_editValue, minValue, maxValue);
							if (*value != clamped)
							{
								*value = clamped;
								localChanged = true;
							}
							ImGui::CloseCurrentPopup();
						}
						ImGui::SameLine();
						if (ImGui::Button("Cancel"))
						{
							ImGui::CloseCurrentPopup();
						}
						ImGui::EndPopup();
					}

					ImGui::PopID();
					return localChanged;
				};

				changed |= ImGui::Checkbox("Enabled##UnityVfx", &vfx->enabled);
				changed |= ImGui::Checkbox("Use Mesh Renderer (v2)##UnityVfx", &vfx->useMeshRenderer);
				changed |= ImGui::Checkbox("Use Compute Effect (v1)##UnityVfx", &vfx->useComputeEffect);
				ImGui::Text("Effect JSON: %s", vfx->effectPath.empty() ? "None" : vfx->effectPath.c_str());

				// Drag & drop
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE_PATH"))
					{
						const char* pathStr = static_cast<const char*>(payload->Data);
						std::filesystem::path droppedPath(pathStr);
						std::string ext = droppedPath.extension().string();
						std::transform(ext.begin(), ext.end(), ext.begin(),
							[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
						if (ext == ".json")
						{
							std::string logicalPath = droppedPath.string();
							{
								std::filesystem::path logical = ResourceManager::NormalizeResourcePathAbsoluteToLogical(droppedPath);
								if (!logical.empty())
									logicalPath = logical.string();
							}
							vfx->effectPath = logicalPath;
							changed = true;
							g_SceneDirty = true;
						}
					}
					ImGui::EndDragDropTarget();
				}

				if (ImGui::Button("Browse...##UnityVfx"))
				{
					wchar_t buf[MAX_PATH] = {};
					OPENFILENAMEW ofn = { sizeof(ofn) };
					ofn.hwndOwner = m_hwnd;
					ofn.lpstrFilter = L"JSON\0*.json\0All\0*.*\0";
					ofn.lpstrFile = buf;
					ofn.nMaxFile = MAX_PATH;
					ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

					if (GetOpenFileNameW(&ofn))
					{
						std::filesystem::path p(buf);
						std::string logicalPath = p.string();
						{
							std::filesystem::path logical = ResourceManager::NormalizeResourcePathAbsoluteToLogical(p);
							if (!logical.empty())
								logicalPath = logical.string();
						}
						vfx->effectPath = logicalPath;
						changed = true;
					}
				}

				if (ImGui::TreeNode("Playback##UnityVfx"))
				{
					changed |= DragFloatWithInput("Time Scale##UnityVfx", &vfx->timeScale, 0.0f, 4.0f);
					changed |= DragFloatWithInput("Lifetime Scale##UnityVfx", &vfx->lifetimeScale, 0.1f, 5.0f);
					changed |= DragFloatWithInput("Spawn Rate Scale##UnityVfx", &vfx->spawnRateScale, 0.0f, 2.0f);
					changed |= ImGui::Checkbox("Override Loop##UnityVfx", &vfx->overrideLoop);
					if (vfx->overrideLoop)
					{
						ImGui::SameLine();
						changed |= ImGui::Checkbox("Loop##UnityVfx", &vfx->loop);
					}
					ImGui::TreePop();
				}

				if (ImGui::TreeNode("Transform/Movement##UnityVfx"))
				{
					changed |= DragFloatWithInput("Size Scale##UnityVfx", &vfx->sizeScale, 0.1f, 50.0f);
					changed |= DragFloatWithInput("Speed Scale##UnityVfx", &vfx->speedScale, 0.1f, 10.0f);
					ImGui::TreePop();
				}

				if (ImGui::TreeNode("Material Overrides##UnityVfx"))
				{
					changed |= DragFloatWithInput("Intensity Scale##UnityVfx", &vfx->intensityScale, 0.0f, 20.0f);
					changed |= ImGui::ColorEdit3("Color Tint##UnityVfx", &vfx->colorTint.x);
					changed |= DragFloatWithInput("Color Scale##UnityVfx", &vfx->colorScale, 0.0f, 10.0f);
					changed |= DragFloatWithInput("Alpha Scale##UnityVfx", &vfx->alphaScale, 0.0f, 5.0f);
					changed |= DragFloatWithInput("HDR Color Clamp##UnityVfx", &vfx->hdrColorClamp, 0.0f, 20.0f);
					changed |= DragFloatWithInput("UV Scroll Scale##UnityVfx", &vfx->uvScrollScale, 0.0f, 10.0f);
					changed |= DragFloatWithInput("Dissolve Offset##UnityVfx", &vfx->dissolveOffset, -1.0f, 1.0f);
					changed |= DragFloatWithInput("Noise Scale##UnityVfx", &vfx->noiseScale, 0.0f, 5.0f);
					changed |= DragFloatWithInput("Ramp Scale##UnityVfx", &vfx->rampScale, 0.0f, 5.0f);
					ImGui::TreePop();
				}

				if (ImGui::TreeNode("Trails##UnityVfx"))
				{
					changed |= ImGui::Checkbox("Enable Trails##UnityVfx", &vfx->enableTrails);
					changed |= DragFloatWithInput("Trail Width Scale##UnityVfx", &vfx->trailWidthScale, 0.1f, 5.0f);
					changed |= DragFloatWithInput("Trail Life Scale##UnityVfx", &vfx->trailLifeScale, 0.1f, 5.0f);
					ImGui::TreePop();
				}

				if (ImGui::Button("Remove Unity VFX"))
				{
					world.RemoveComponent<UnityVfxComponent>(_selectedEntity);
					g_SceneDirty = true;
					return;
				}

				if (changed) g_SceneDirty = true;
			}
		}
	}
}
