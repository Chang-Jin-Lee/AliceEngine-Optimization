#include "Editor/Core/EditorCore.h"

#include "imgui.h"
#include "imgui_internal.h"

namespace Alice
{
	void EditorCore::SetupDockSpaceAndDefaultLayout()
	{
		// 메인 뷰포트 전체를 도킹 스페이스로 사용합니다.
		ImGuiViewport* viewport = ImGui::GetMainViewport();
		ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(viewport->ID, viewport);

		// 첫 프레임에만 기본 도킹 레이아웃을 구성합니다.
		static bool s_dockInitialized = false;
		if (!s_dockInitialized)
		{
			s_dockInitialized = true;

			ImGui::DockBuilderRemoveNode(dockspaceId);
			ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
			ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->Size);

			ImGuiID dockMain = dockspaceId;
			ImGuiID dockLeft = 0;
			ImGuiID dockRight = 0;
			ImGuiID dockCenter = 0;
			ImGuiID dockRightCol = 0;

			ImGui::DockBuilderSplitNode(dockMain, ImGuiDir_Left, 0.22f, &dockLeft, &dockRight);
			ImGui::DockBuilderSplitNode(dockRight, ImGuiDir_Right, 0.26f, &dockRightCol, &dockCenter);

			ImGuiID dockCenterTop = 0;
			ImGuiID dockCenterBottom = 0;
			ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Down, 0.30f, &dockCenterBottom, &dockCenterTop);

			ImGuiID dockLeftTop = 0;
			ImGuiID dockLeftBottom = 0;
			ImGui::DockBuilderSplitNode(dockLeft, ImGuiDir_Down, 0.55f, &dockLeftBottom, &dockLeftTop);

			ImGui::DockBuilderDockWindow("Hierarchy", dockLeftTop);
			ImGui::DockBuilderDockWindow("Project", dockLeftBottom);
			ImGui::DockBuilderDockWindow("Game", dockCenterTop);
			ImGui::DockBuilderDockWindow("Camera", dockCenterBottom);
			// Inspector와 Lighting을 같은 도크에 배치해 탭으로 표시
			ImGui::DockBuilderDockWindow("Lighting", dockRightCol);
			ImGui::DockBuilderDockWindow("Inspector", dockRightCol);

			// 기본 포커스는 Inspector 탭
			if (ImGuiDockNode* rightNode = ImGui::DockBuilderGetNode(dockRightCol))
			{
				rightNode->SelectedTabId = ImGui::GetID("Inspector");
			}

			ImGui::DockBuilderFinish(dockspaceId);
		}
	}
}
