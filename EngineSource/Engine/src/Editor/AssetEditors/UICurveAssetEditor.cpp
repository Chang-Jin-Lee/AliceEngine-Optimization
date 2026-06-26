#include "Editor/Core/EditorCore.h"
#include "Editor/Core/EditorUIState.h"

#include "Runtime/UI/UICurveAsset.h"

#include "imgui.h"

#include <algorithm>

namespace Alice
{
	void EditorCore::DrawUICurveAssetEditorWindow()
	{
		// === UI Curve Asset Editor (.uicurve double-click) ===
		if (!g_UICurveEditorOpen)
			return;

		if (ImGui::Begin("UI Curve Asset Editor", &g_UICurveEditorOpen))
		{
			ImGui::Text("Asset: %s", g_UICurveEditorPath.string().c_str());
			ImGui::Separator();

			bool changed = false;

			ImVec2 graphSize = ImVec2(ImGui::GetContentRegionAvail().x, 180.0f);
			ImVec2 graphPos = ImGui::GetCursorScreenPos();
			ImGui::InvisibleButton("UICurveGraph", graphSize);
			ImDrawList* drawList = ImGui::GetWindowDrawList();
			ImVec2 graphMin = graphPos;
			ImVec2 graphMax = ImVec2(graphPos.x + graphSize.x, graphPos.y + graphSize.y);
			drawList->AddRect(graphMin, graphMax, IM_COL32(100, 100, 100, 255));

			float tMin = 0.0f;
			float tMax = 1.0f;
			float vMin = 0.0f;
			float vMax = 1.0f;
			for (const auto& key : g_UICurveEditorData.keys)
			{
				tMin = std::min(tMin, key.time);
				tMax = std::max(tMax, key.time);
				vMin = std::min(vMin, key.value);
				vMax = std::max(vMax, key.value);
			}
			const float tRange = std::max(0.0001f, tMax - tMin);
			const float vRange = std::max(0.0001f, vMax - vMin);
			auto ToScreen = [&](float t, float v)
			{
				const float x = (t - tMin) / tRange;
				const float y = (v - vMin) / vRange;
				return ImVec2(graphMin.x + x * graphSize.x, graphMax.y - y * graphSize.y);
			};

			for (int i = 1; i < 4; ++i)
			{
				const float tx = graphMin.x + (graphSize.x * i / 4.0f);
				const float ty = graphMin.y + (graphSize.y * i / 4.0f);
				drawList->AddLine(ImVec2(tx, graphMin.y), ImVec2(tx, graphMax.y), IM_COL32(60, 60, 60, 255));
				drawList->AddLine(ImVec2(graphMin.x, ty), ImVec2(graphMax.x, ty), IM_COL32(60, 60, 60, 255));
			}

			if (!g_UICurveEditorData.keys.empty())
			{
				const int steps = 120;
				ImVec2 prev = ToScreen(tMin, g_UICurveEditorData.Evaluate(tMin));
				for (int i = 1; i < steps; ++i)
				{
					const float t = tMin + (tRange * (static_cast<float>(i) / (steps - 1)));
					ImVec2 cur = ToScreen(t, g_UICurveEditorData.Evaluate(t));
					drawList->AddLine(prev, cur, IM_COL32(120, 200, 255, 255), 2.0f);
					prev = cur;
				}
			}

			for (std::size_t i = 0; i < g_UICurveEditorData.keys.size(); ++i)
			{
				const auto& key = g_UICurveEditorData.keys[i];
				ImVec2 p = ToScreen(key.time, key.value);
				drawList->AddCircleFilled(p, 4.0f, IM_COL32(255, 200, 80, 255));
				if (static_cast<int>(i) == g_UICurveEditorSelected)
					drawList->AddCircle(p, 6.0f, IM_COL32(255, 255, 255, 200));
			}

			if (ImGui::IsItemHovered())
			{
				const ImVec2 mouse = ImGui::GetIO().MousePos;
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
				{
					float bestDist = 999999.0f;
					int bestIdx = -1;
					for (std::size_t i = 0; i < g_UICurveEditorData.keys.size(); ++i)
					{
						ImVec2 p = ToScreen(g_UICurveEditorData.keys[i].time, g_UICurveEditorData.keys[i].value);
						const float dx = mouse.x - p.x;
						const float dy = mouse.y - p.y;
						const float dist = dx * dx + dy * dy;
						if (dist < bestDist)
						{
							bestDist = dist;
							bestIdx = static_cast<int>(i);
						}
					}
					if (bestIdx >= 0 && bestDist < 144.0f)
						g_UICurveEditorSelected = bestIdx;
				}
				if (ImGui::IsMouseClicked(ImGuiMouseButton_Right))
				{
					float u = (mouse.x - graphMin.x) / std::max(1.0f, graphSize.x);
					float v = 1.0f - (mouse.y - graphMin.y) / std::max(1.0f, graphSize.y);
					u = std::clamp(u, 0.0f, 1.0f);
					v = std::clamp(v, 0.0f, 1.0f);
					UICurveKey key{};
					key.time = tMin + u * tRange;
					key.value = vMin + v * vRange;
					key.interp = UICurveInterp::Cubic;
					key.tangentMode = UICurveTangentMode::Auto;
					g_UICurveEditorData.keys.push_back(key);
					g_UICurveEditorSelected = static_cast<int>(g_UICurveEditorData.keys.size()) - 1;
					changed = true;
				}
			}

			ImGui::Separator();
			if (ImGui::Button("Add Key"))
			{
				UICurveKey key{};
				key.time = tMax;
				key.value = 1.0f;
				key.interp = UICurveInterp::Cubic;
				key.tangentMode = UICurveTangentMode::Auto;
				g_UICurveEditorData.keys.push_back(key);
				g_UICurveEditorSelected = static_cast<int>(g_UICurveEditorData.keys.size()) - 1;
				changed = true;
			}
			ImGui::SameLine();
			if (ImGui::Button("Delete Key") && g_UICurveEditorSelected >= 0 && g_UICurveEditorSelected < static_cast<int>(g_UICurveEditorData.keys.size()))
			{
				g_UICurveEditorData.keys.erase(g_UICurveEditorData.keys.begin() + g_UICurveEditorSelected);
				g_UICurveEditorSelected = -1;
				changed = true;
			}

			const char* interpItems[] = { "Constant", "Linear", "Cubic" };
			const char* tangentItems[] = { "Auto", "User", "Break" };

			if (ImGui::BeginTable("UICurveKeys", 7, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
			{
				ImGui::TableSetupColumn("Idx");
				ImGui::TableSetupColumn("Time");
				ImGui::TableSetupColumn("Value");
				ImGui::TableSetupColumn("Interp");
				ImGui::TableSetupColumn("Tangent");
				ImGui::TableSetupColumn("In");
				ImGui::TableSetupColumn("Out");
				ImGui::TableHeadersRow();
				for (std::size_t i = 0; i < g_UICurveEditorData.keys.size(); ++i)
				{
					auto& key = g_UICurveEditorData.keys[i];
					ImGui::TableNextRow();
					ImGui::TableSetColumnIndex(0);
					ImGui::PushID(static_cast<int>(i));
					if (ImGui::Selectable(std::to_string(i).c_str(), g_UICurveEditorSelected == static_cast<int>(i)))
						g_UICurveEditorSelected = static_cast<int>(i);
					ImGui::TableSetColumnIndex(1);
					changed |= ImGui::DragFloat("##time", &key.time, 0.01f);
					ImGui::TableSetColumnIndex(2);
					changed |= ImGui::DragFloat("##value", &key.value, 0.01f);
					ImGui::TableSetColumnIndex(3);
					int interpIdx = static_cast<int>(key.interp);
					if (ImGui::Combo("##interp", &interpIdx, interpItems, IM_ARRAYSIZE(interpItems)))
					{
						key.interp = static_cast<UICurveInterp>(interpIdx);
						changed = true;
					}
					ImGui::TableSetColumnIndex(4);
					int tangentIdx = static_cast<int>(key.tangentMode);
					if (ImGui::Combo("##tangent", &tangentIdx, tangentItems, IM_ARRAYSIZE(tangentItems)))
					{
						key.tangentMode = static_cast<UICurveTangentMode>(tangentIdx);
						changed = true;
					}
					ImGui::TableSetColumnIndex(5);
					changed |= ImGui::DragFloat("##in", &key.inTangent, 0.01f);
					ImGui::TableSetColumnIndex(6);
					changed |= ImGui::DragFloat("##out", &key.outTangent, 0.01f);
					ImGui::PopID();
				}
				ImGui::EndTable();
			}

			if (changed)
			{
				g_UICurveEditorData.Sort();
				g_UICurveEditorData.RecalcAutoTangents();
				SaveUICurveAsset(g_UICurveEditorPath, g_UICurveEditorData);
			}
		}
		ImGui::End();
	}
}
