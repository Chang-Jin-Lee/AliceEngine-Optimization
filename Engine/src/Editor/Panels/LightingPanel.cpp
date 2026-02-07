#include "Editor/Core/EditorCore.h"

#include "Runtime/Foundation/ImGuiEx.h"
#include "Runtime/Rendering/ForwardRenderSystem.h"
#include "Runtime/Rendering/DeferredRenderSystem.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Resources/ResourceManager.h"

#include "imgui.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <DirectXMath.h>
#include <ShlObj.h>

namespace Alice
{
	namespace
	{
		int CALLBACK BrowseSkyboxFolderCallback(HWND hwnd, UINT uMsg, LPARAM /*lParam*/, LPARAM lpData)
		{
			if (uMsg == BFFM_INITIALIZED && lpData)
			{
				::SendMessage(hwnd, BFFM_SETSELECTIONW, TRUE, lpData);
			}
			return 0;
		}

		bool BrowseForSkyboxFolder(HWND owner, const std::filesystem::path& initialDir, std::filesystem::path& outDir)
		{
			BROWSEINFOW bi{};
			wchar_t displayName[MAX_PATH] = {};
			bi.hwndOwner = owner;
			bi.pszDisplayName = displayName;
			bi.lpszTitle = L"Select Skybox folder (Resource/Skybox/...)";
			bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_USENEWUI;
			const std::wstring initial = initialDir.wstring();
			bi.lpfn = BrowseSkyboxFolderCallback;
			bi.lParam = reinterpret_cast<LPARAM>(initial.c_str());

			PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
			if (!pidl) return false;

			wchar_t pathBuf[MAX_PATH] = {};
			const bool ok = (SHGetPathFromIDListW(pidl, pathBuf) != FALSE);
			CoTaskMemFree(pidl);
			if (!ok) return false;

			outDir = std::filesystem::path(pathBuf);
			return true;
		}

		std::string DetectSkyboxPrefixFromDir(const std::filesystem::path& dir)
		{
			static const char* kSuffixes[] = {
				"EnvHDR", "EnvMDR",
				"DiffuseHDR", "DiffuseMDR",
				"SpecularHDR", "SpecularMDR",
				"Brdf"
			};

			std::error_code ec;
			if (!std::filesystem::exists(dir, ec) || !std::filesystem::is_directory(dir, ec))
				return {};

			for (const auto& entry : std::filesystem::directory_iterator(dir, ec))
			{
				if (ec) break;
				if (!entry.is_regular_file(ec)) continue;

				const auto& path = entry.path();
				if (path.extension() != ".dds") continue;

				std::string stem = path.stem().string();
				for (const char* suffix : kSuffixes)
				{
					const size_t suffixLen = std::strlen(suffix);
					if (stem.size() >= suffixLen && stem.compare(stem.size() - suffixLen, suffixLen, suffix) == 0)
					{
						stem.erase(stem.size() - suffixLen);
						return stem;
					}
				}
			}

			return {};
		}

		constexpr float kPi = 3.14159265358979323846f;
		constexpr float kDegToRad = kPi / 180.0f;
		constexpr float kRadToDeg = 180.0f / kPi;

		DirectX::XMFLOAT3 NormalizeDirectionSafe(const DirectX::XMFLOAT3& dir)
		{
			using namespace DirectX;
			const XMVECTOR v = XMLoadFloat3(&dir);
			const float lenSq = XMVectorGetX(XMVector3LengthSq(v));
			if (lenSq <= 1e-8f)
			{
				return XMFLOAT3(0.0f, -1.0f, 0.0f);
			}
			XMFLOAT3 out;
			XMStoreFloat3(&out, XMVector3Normalize(v));
			return out;
		}

		bool NearlyEqualDirection(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, float eps = 1e-4f)
		{
			return (std::fabs(a.x - b.x) <= eps) &&
				(std::fabs(a.y - b.y) <= eps) &&
				(std::fabs(a.z - b.z) <= eps);
		}

		void DirectionToYawPitchDeg(const DirectX::XMFLOAT3& direction, float& outYawDeg, float& outPitchDeg)
		{
			const DirectX::XMFLOAT3 d = NormalizeDirectionSafe(direction);
			outYawDeg = std::atan2(d.x, -d.z) * kRadToDeg;
			const float y = std::clamp(-d.y, -1.0f, 1.0f);
			outPitchDeg = std::asin(y) * kRadToDeg;
		}

		DirectX::XMFLOAT3 YawPitchRollDegToDirection(float yawDeg, float pitchDeg, float rollDeg)
		{
			using namespace DirectX;
			const float yaw = yawDeg * kDegToRad;
			const float pitch = pitchDeg * kDegToRad;
			const float roll = rollDeg * kDegToRad;
			const XMMATRIX rot = XMMatrixRotationRollPitchYaw(pitch, yaw, roll);
			const XMVECTOR forward = XMVectorSet(0.0f, 0.0f, -1.0f, 0.0f);
			XMFLOAT3 out;
			XMStoreFloat3(&out, XMVector3Normalize(XMVector3TransformNormal(forward, rot)));
			return out;
		}

		bool DragWithInputAngle(const char* label, float& value, float minValue, float maxValue, float dragSpeed = 0.25f)
		{
			bool changed = false;
			ImGui::PushID(label);
			ImGui::SetNextItemWidth(170.0f);
			changed |= ImGui::DragFloat("##drag", &value, dragSpeed, minValue, maxValue, "%.2f");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(100.0f);
			changed |= ImGui::InputFloat("##input", &value, 0.0f, 0.0f, "%.2f");
			ImGui::SameLine();
			ImGui::TextUnformatted(label);
			ImGui::PopID();
			value = std::clamp(value, minValue, maxValue);
			return changed;
		}
	}

	void EditorCore::DrawLightingWindow(World& world,
		ForwardRenderSystem& forward,
		DeferredRenderSystem& deferred,
		int& shadingMode,
		bool& useFillLight,
		bool& useForwardRendering,
		LightingParameters& lightingParams,
		int& skyboxChoice,
		std::string& skyboxCustomDir,
		std::string& skyboxCustomPrefix,
		int& skyboxResolution)
	{
		// === Lighting ===
		if (ImGui::Begin("Lighting"))
		{
			bool lightingChanged = false;
			int mode = shadingMode;
			if (ImGui::RadioButton("Lambert", mode == 0))   mode = 0;
			ImGui::SameLine();
			if (ImGui::RadioButton("Phong", mode == 1))     mode = 1;
			ImGui::SameLine();
			if (ImGui::RadioButton("Blinn-Phong", mode == 2)) mode = 2;
			ImGui::SameLine();
			if (ImGui::RadioButton("Toon", mode == 3))      mode = 3;
			ImGui::SameLine();
			if (ImGui::RadioButton("PBR", mode == 4))       mode = 4;
			ImGui::SameLine();
			if (ImGui::RadioButton("ToonPBR", mode == 5))   mode = 5;
			ImGui::SameLine();
			if (ImGui::RadioButton("ToonPBREditable", mode == 7)) mode = 7;
			shadingMode = mode;

			lightingChanged |= Alice::ImGuiCheckbox(L"Fill Light (보조광)", &useFillLight);

			// Forward/Deferred 모드에 따라 조명 파라미터를 각 렌더러에 반영합니다.
			//auto& lighting = useForwardRendering ? forward.GetLightingParameters() : deferred.GetLightingParameters();
			//auto& lighting = forward.GetLightingParameters();
			auto& lighting = lightingParams;

			// PBR 모드일 때 PBR 파라미터 표시
			if (mode == 4 || mode == 5 || mode == 7)
			{
				ImGui::Separator();
				ImGui::Text("PBR Material Parameters");
				lightingChanged |= ImGui::ColorEdit3("Base Color", &lighting.baseColor.x);
				lightingChanged |= ImGui::ColorEdit3("Key Light Color", &lighting.diffuseColor.x);
				lightingChanged |= ImGui::SliderFloat("Metalness", &lighting.metalness, 0.0f, 1.0f);
				lightingChanged |= ImGui::SliderFloat("Roughness", &lighting.roughness, 0.0f, 1.0f);
				lightingChanged |= ImGui::SliderFloat("Ambient Occlusion", &lighting.ambientOcclusion, 0.0f, 1.0f);
				ImGui::Separator();
			}
			else
			{
				// 레거시 쉐이더 파라미터
				lightingChanged |= ImGui::SliderFloat("Shininess", &lighting.shininess, 2.0f, 128.0f);
				lightingChanged |= ImGui::ColorEdit3("Diffuse Color", &lighting.diffuseColor.x);
				lightingChanged |= ImGui::ColorEdit3("Specular Color", &lighting.specularColor.x);
			}

			// 공통 조명 파라미터
			lightingChanged |= Alice::ImGuiSliderFloat(L"Key Intensity (주광)",
				&lighting.keyIntensity,
				0.0f,
				3.0f);
			ImGui::TextUnformatted("Key Rotation (Euler)");
			static bool keyEulerInit = false;
			static DirectX::XMFLOAT3 lastKeyDirection = { 0.0f, -1.0f, 0.0f };
			static float keyYawDeg = 0.0f;
			static float keyPitchDeg = 0.0f;
			static float keyRollDeg = 0.0f;

			const DirectX::XMFLOAT3 normalizedKeyDir = NormalizeDirectionSafe(lighting.keyDirection);
			if (!keyEulerInit || !NearlyEqualDirection(normalizedKeyDir, lastKeyDirection))
			{
				DirectionToYawPitchDeg(normalizedKeyDir, keyYawDeg, keyPitchDeg);
				lastKeyDirection = normalizedKeyDir;
				keyEulerInit = true;
			}

			bool keyDirectionChanged = false;
			keyDirectionChanged |= DragWithInputAngle("Yaw (deg)", keyYawDeg, -180.0f, 180.0f);
			keyDirectionChanged |= DragWithInputAngle("Pitch (deg)", keyPitchDeg, -89.9f, 89.9f);
			keyDirectionChanged |= DragWithInputAngle("Roll (deg)", keyRollDeg, -180.0f, 180.0f);

			if (keyDirectionChanged)
			{
				lighting.keyDirection = YawPitchRollDegToDirection(keyYawDeg, keyPitchDeg, keyRollDeg);
				lastKeyDirection = lighting.keyDirection;
				lightingChanged = true;
			}

			ImGui::Text("Key Direction Vector: %.3f, %.3f, %.3f",
				lighting.keyDirection.x, lighting.keyDirection.y, lighting.keyDirection.z);
			
			ImGui::Separator();
			ImGui::TextUnformatted("Shadow");
			lightingChanged |= Alice::ImGuiSliderFloat(L"Shadow Strength",
				&lighting.shadowStrength,
				0.0f,
				1.0f);

			bool shadowQualityChanged = false;
			ShadowSettings shadowSettings = forward.GetShadowSettings();
			int shadowMapSize = static_cast<int>(shadowSettings.mapSizePx);
			float shadowPcfRadius = shadowSettings.pcfRadius;

			shadowQualityChanged |= ImGui::SliderInt("Shadow Map Size", &shadowMapSize, 512, 8192);
			shadowQualityChanged |= ImGui::SliderFloat("Shadow PCF Radius", &shadowPcfRadius, 0.0f, 3.0f, "%.2f");

			if (shadowQualityChanged)
			{
				// Snap to reasonable increments for stability
				shadowMapSize = (shadowMapSize / 256) * 256;
				shadowMapSize = (std::max)(512, shadowMapSize);

				shadowSettings.mapSizePx = static_cast<std::uint32_t>(shadowMapSize);
				shadowSettings.pcfRadius = std::clamp(shadowPcfRadius, 0.0f, 3.0f);

				forward.ApplyShadowSettings(shadowSettings);
				deferred.ApplyShadowSettings(shadowSettings);
			}

			ImGui::Separator();
			ImGui::TextUnformatted("Tone Mapping");
			float tmExposure = 0.0f;
			float tmMaxNits = 1000.0f;
			float tmSaturation = 1.0f;
			float tmContrast = 1.0f;
			float tmGamma = 1.0f;
			forward.GetPostProcessParams(tmExposure, tmMaxNits, tmSaturation, tmContrast, tmGamma);

			bool toneMappingChanged = false;
			toneMappingChanged |= ImGui::SliderFloat("Exposure", &tmExposure, -5.0f, 5.0f, "%.2f");
			toneMappingChanged |= ImGui::SliderFloat("Saturation", &tmSaturation, 0.0f, 3.0f, "%.2f");
			toneMappingChanged |= ImGui::SliderFloat("Contrast", &tmContrast, 0.0f, 2.0f, "%.2f");
			toneMappingChanged |= ImGui::SliderFloat("Gamma", &tmGamma, 0.1f, 3.0f, "%.2f");

			if (toneMappingChanged)
			{
				forward.SetPostProcessParams(tmExposure, tmMaxNits, tmSaturation, tmContrast, tmGamma);
				deferred.SetPostProcessParams(tmExposure, tmMaxNits, tmSaturation, tmContrast, tmGamma);
			}

			if (lightingChanged)
			{
				forward.GetLightingParameters() = lighting;
				deferred.GetLightingParameters() = lighting;
			}

			// === Skybox ===
			ImGui::Separator();
			ImGui::TextUnformatted("Skybox");

			static int  lastSkyboxChoice = -1;
			static int  lastSkyboxResolution = -1;
			static bool lastForward = false;

			const char* skyboxItems[] = { "Off", "Bridge", "Indoor", "Baker", "darkenv", "Custom" };

			auto ApplySkybox = [&](auto& renderer)
				{
					const std::string iblSuffix = (skyboxResolution == 1) ? "MDR" : "HDR";
					if (skyboxChoice == 0)
					{
						renderer.SetSkyboxEnabled(false);
						return;
					}

					renderer.SetSkyboxEnabled(true);
					switch (skyboxChoice)
					{
					case 1: renderer.SetIblSet("Bridge", "bridge", iblSuffix);       break;
					case 2: renderer.SetIblSet("Indoor", "indoor", iblSuffix);       break;
					case 3: renderer.SetIblSet("Sample", "BakerSample", iblSuffix);  break;
					case 4: renderer.SetIblSet("darkenv", "darkenvDiffuseHDR", iblSuffix);  break;
					case 5:
						if (!skyboxCustomDir.empty() && !skyboxCustomPrefix.empty())
							renderer.SetIblSet(skyboxCustomDir, skyboxCustomPrefix, iblSuffix);
						else
							renderer.SetSkyboxEnabled(false);
						break;
					default: break;
					}
				};

			auto EditBgIfOff = [&](auto& renderer)
				{
					if (skyboxChoice != 0) return;

					DirectX::XMFLOAT4 bgColor = renderer.GetBackgroundColor();
					if (ImGui::ColorEdit4("Background Color", &bgColor.x))
						renderer.SetBackgroundColor(bgColor);
				};

			if (skyboxChoice < 0 || skyboxChoice > 5) skyboxChoice = 0;
			bool skyboxChanged = ImGui::Combo("Skybox Choice", &skyboxChoice, skyboxItems, IM_ARRAYSIZE(skyboxItems));
			bool rendererChanged = (lastForward != useForwardRendering);

			const char* skyboxResItems[] = { "HDR", "MDR" };
			if (skyboxResolution < 0 || skyboxResolution > 1) skyboxResolution = 0;
			bool skyboxResChanged = ImGui::Combo("Skybox Resolution", &skyboxResolution, skyboxResItems, IM_ARRAYSIZE(skyboxResItems));
			bool customChanged = false;
			if (skyboxChoice == 5)
			{
				char dirBuf[256] = {};

				strncpy_s(dirBuf, sizeof(dirBuf), skyboxCustomDir.c_str(), _TRUNCATE);

				ImGui::Text("Custom Dir: %s", dirBuf[0] ? dirBuf : "-");
				ImGui::SameLine();
				if (ImGui::Button("Browse...##SkyboxCustom"))
				{
					const auto& rm = ResourceManager::Get();
					std::filesystem::path initial = rm.Resolve("Resource/Skybox");
					if (!skyboxCustomDir.empty())
						initial = rm.Resolve(std::filesystem::path("Resource/Skybox") / skyboxCustomDir);
					if (!std::filesystem::exists(initial))
						initial = rm.Resolve("Resource/Skybox");

					std::filesystem::path selectedDir;
					if (BrowseForSkyboxFolder(m_hwnd, initial, selectedDir))
					{
						const std::filesystem::path selectedAbs = std::filesystem::absolute(selectedDir);
						const std::filesystem::path baseAbs = std::filesystem::absolute(rm.Resolve("Resource/Skybox"));

						std::filesystem::path relativeDir;
						if (selectedAbs.wstring().rfind(baseAbs.wstring(), 0) == 0)
							relativeDir = selectedAbs.lexically_relative(baseAbs);
						else
							relativeDir = selectedAbs;

						skyboxCustomDir = relativeDir.generic_string();
						skyboxCustomPrefix = DetectSkyboxPrefixFromDir(selectedAbs);
						if (skyboxCustomPrefix.empty())
							skyboxCustomPrefix = DetectSkyboxPrefixFromDir(selectedAbs / ".");

						customChanged = true;
					}
				}
			}

			// 선택 변경 or 렌더러 토글 변경 시 반영 (초기 1회 포함)
			if (skyboxChanged || skyboxResChanged || customChanged || rendererChanged ||
				lastSkyboxChoice != skyboxChoice || lastSkyboxResolution != skyboxResolution)
			{
				ApplySkybox(forward);
				ApplySkybox(deferred);

				lastSkyboxChoice = skyboxChoice;
				lastSkyboxResolution = skyboxResolution;
				lastForward = useForwardRendering;
			}

			if (useForwardRendering) EditBgIfOff(forward);
			else                     EditBgIfOff(deferred);

		}
		ImGui::End();
	}
}
