#include "Editor/Core/EditorCore.h"

#include "Runtime/Foundation/ImGuiEx.h"
#include "Runtime/Rendering/ForwardRenderSystem.h"
#include "Runtime/Rendering/DeferredRenderSystem.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/Components/TransformComponent.h"

#include "imgui.h"

#include <algorithm>
#include <cstring>
#include <DirectXMath.h>

namespace Alice
{
	void EditorCore::DrawLightingWindow(World& world,
		ForwardRenderSystem& forward,
		DeferredRenderSystem& deferred,
		int& shadingMode,
		bool& useFillLight,
		bool& useForwardRendering)
	{
		// === Lighting ===
		if (ImGui::Begin("Lighting"))
		{
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

			Alice::ImGuiCheckbox(L"Fill Light (보조광)", &useFillLight);

			// Forward/Deferred 모드에 따라 조명 파라미터를 각 렌더러에 반영합니다.
			//auto& lighting = useForwardRendering ? forward.GetLightingParameters() : deferred.GetLightingParameters();
			//auto& lighting = forward.GetLightingParameters();
			auto& lighting = deferred.GetLightingParameters();

			// PBR 모드일 때 PBR 파라미터 표시
			if (mode == 4 || mode == 5 || mode == 7)
			{
				ImGui::Separator();
				ImGui::Text("PBR Material Parameters");
				ImGui::ColorEdit3("Base Color", &lighting.baseColor.x);
				ImGui::SliderFloat("Metalness", &lighting.metalness, 0.0f, 1.0f);
				ImGui::SliderFloat("Roughness", &lighting.roughness, 0.0f, 1.0f);
				ImGui::SliderFloat("Ambient Occlusion", &lighting.ambientOcclusion, 0.0f, 1.0f);
				ImGui::Separator();
			}
			else
			{
				// 레거시 쉐이더 파라미터
				ImGui::SliderFloat("Shininess", &lighting.shininess, 2.0f, 128.0f);
				ImGui::ColorEdit3("Diffuse Color", &lighting.diffuseColor.x);
				ImGui::ColorEdit3("Specular Color", &lighting.specularColor.x);
			}

			// 공통 조명 파라미터
			Alice::ImGuiSliderFloat(L"Key Intensity (주광)",
				&lighting.keyIntensity,
				0.0f,
				3.0f);
			Alice::ImGuiSliderFloat3(L"Key Direction (주광)",
				&lighting.keyDirection.x,
				-1.0f,
				1.0f);

			// === Skybox ===
			ImGui::Separator();
			ImGui::TextUnformatted("Skybox");

			static int  skyboxChoice = 3; // 0 Off, 1 Bridge, 2 Indoor, 3 Baker, 4 darkenv
			static int  lastSkyboxChoice = -1;
			static bool lastForward = false;

			const char* skyboxItems[] = { "Off", "Bridge", "Indoor", "Baker", "darkenv" };

			auto ApplySkybox = [&](auto& renderer)
				{
					if (skyboxChoice == 0)
					{
						renderer.SetSkyboxEnabled(false);
						return;
					}

					renderer.SetSkyboxEnabled(true);
					switch (skyboxChoice)
					{
					case 1: renderer.SetIblSet("Bridge", "bridge");       break;
					case 2: renderer.SetIblSet("Indoor", "indoor");       break;
					case 3: renderer.SetIblSet("Sample", "BakerSample");  break;
					case 4: renderer.SetIblSet("darkenv", "darkenvDiffuseHDR");  break;
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

			bool skyboxChanged = ImGui::Combo("Skybox Choice", &skyboxChoice, skyboxItems, IM_ARRAYSIZE(skyboxItems));
			bool rendererChanged = (lastForward != useForwardRendering);

			// 선택 변경 or 렌더러 토글 변경 시 반영 (초기 1회 포함)
			if (skyboxChanged || rendererChanged || lastSkyboxChoice != skyboxChoice)
			{
				if (useForwardRendering) ApplySkybox(forward);
				else                     ApplySkybox(deferred);

				lastSkyboxChoice = skyboxChoice;
				lastForward = useForwardRendering;
			}

			if (useForwardRendering) EditBgIfOff(forward);
			else                     EditBgIfOff(deferred);

		}
		ImGui::End();
	}
}
