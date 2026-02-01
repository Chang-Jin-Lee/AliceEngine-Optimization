#include "Editor/Core/EditorCore.h"
#include "Editor/Core/EditorUIState.h"

#include "imgui.h"

#include <cstring>

namespace Alice
{
	void EditorCore::DrawPvdSettingsWindow(bool& pvdEnabled, std::string& pvdHost, int& pvdPort)
	{
		// === PVD Settings 창 ===
		if (!g_ShowPvdSettingsWindow)
			return;

		static bool s_wasOpen = false;
		bool isOpen = g_ShowPvdSettingsWindow;

		if (ImGui::Begin("PVD Settings", &g_ShowPvdSettingsWindow))
		{
			ImGui::Text("PhysX Visual Debugger Settings");
			ImGui::Separator();

			// PVD 활성화 체크박스
			ImGui::Checkbox("Enable PVD", &pvdEnabled);
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("Enable PhysX Visual Debugger.\n"
					"Note: Requires restart to apply changes.\n"
					"Make sure PVD is running on the target host/port.");
			}

			// PVD 설정 (비활성화 상태에서도 표시)
			ImGui::BeginDisabled(!pvdEnabled);

			// PVD Host 입력
			static char pvdHostBuf[256] = {};
			static bool s_hostBufInitialized = false;
			if (!s_hostBufInitialized || !s_wasOpen)
			{
				strncpy_s(pvdHostBuf, pvdHost.c_str(), 255);
				pvdHostBuf[255] = '\0';
				s_hostBufInitialized = true;
			}
			ImGui::Text("Host:");
			ImGui::SameLine();
			if (ImGui::InputText("##PvdHost", pvdHostBuf, sizeof(pvdHostBuf)))
			{
				pvdHost = pvdHostBuf;
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("PVD server host (default: 127.0.0.1)");
			}

			// PVD Port 입력
			ImGui::Text("Port:");
			ImGui::SameLine();
			if (ImGui::InputInt("##PvdPort", &pvdPort))
			{
				if (pvdPort < 1) pvdPort = 1;
				if (pvdPort > 65535) pvdPort = 65535;
			}
			if (ImGui::IsItemHovered())
			{
				ImGui::SetTooltip("PVD server port (default: 5425)");
			}

			ImGui::EndDisabled();

			ImGui::Separator();

			// 상태 표시
			ImGui::Text("Status:");
			ImGui::SameLine();
			if (pvdEnabled)
			{
				ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "Enabled");
				ImGui::Text("PVD will be enabled on next restart.");
				ImGui::Text("Connection: %s:%d", pvdHost.c_str(), pvdPort);
			}
			else
			{
				ImGui::TextColored(ImVec4(0.7f, 0.7f, 0.7f, 1.0f), "Disabled");
			}

			ImGui::Separator();
			ImGui::TextWrapped("Note: PVD settings are saved automatically when the engine shuts down.\n"
				"Restart the engine to apply changes.");
		}

		// 창이 닫힐 때 설정 저장 (이전에 열려있었고 지금 닫힌 경우)
		if (s_wasOpen && !g_ShowPvdSettingsWindow)
		{
			// 엔진 종료 시 자동 저장되므로 여기서는 선택적
			// 필요시 여기서도 저장 가능
		}
		s_wasOpen = isOpen;

		ImGui::End();
	}
}
