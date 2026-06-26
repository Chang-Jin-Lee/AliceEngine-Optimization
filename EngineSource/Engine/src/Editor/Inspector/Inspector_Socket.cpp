#include "Editor/Core/EditorCore.h"
#include "Editor/Core/EditorUIState.h"
#include "Runtime/Gameplay/Sockets/SocketAttachmentComponent.h"
#include "Runtime/Gameplay/Sockets/SocketComponent.h"
#include "Runtime/ECS/Components/IDComponent.h"
#include "Runtime/Importing/FbxModel.h"
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace Alice
{
	void EditorCore::DrawInspectorSocketAttachment(World& world, const EntityId& _selectedEntity)
	{
		if (auto* att = world.GetComponent<SocketAttachmentComponent>(_selectedEntity))
		{
			if (ImGui::CollapsingHeader("Socket Attachment", ImGuiTreeNodeFlags_DefaultOpen))
			{
				bool changed = false;

				if (ImGui::Button("Remove##SocketAttachmentRemove"))
				{
					world.RemoveComponent<SocketAttachmentComponent>(_selectedEntity);
					g_SceneDirty = true;
					return;
				}

				// Owner GUID
				std::uint64_t ownerGuid = att->ownerGuid;
				if (ImGui::InputScalar("Owner GUID", ImGuiDataType_U64, &ownerGuid))
				{
					att->ownerGuid = ownerGuid;
					att->ownerCached = InvalidEntityId;
					changed = true;
				}

				if (att->ownerGuid == 0)
					ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Owner GUID is 0 -> attachment will not run");

				// Set owner from list (dropdown of all entities with IDComponent)
				{
					std::string preview = att->ownerNameDebug.empty()
						? (att->ownerGuid != 0 ? std::to_string(att->ownerGuid) : "(none)")
						: att->ownerNameDebug;
					if (ImGui::BeginCombo("Owner (pick entity)", preview.c_str()))
					{
						for (auto&& [eid, idc] : world.GetComponents<IDComponent>())
						{
							std::string label = world.GetEntityName(eid);
							if (label.empty()) label = "Entity " + std::to_string(eid);
							label += " (";
							label += std::to_string(idc.guid);
							label += ")";
							const bool sel = (idc.guid == att->ownerGuid);
							if (ImGui::Selectable(label.c_str(), sel))
							{
								att->ownerGuid = idc.guid;
								att->ownerNameDebug = world.GetEntityName(eid);
								att->ownerCached = InvalidEntityId;
								changed = true;
							}
							if (sel)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
				}

				// Resolve owner and build socket option list (name + parentBone for fallback match)
				EntityId resolvedOwner = (att->ownerGuid != 0) ? world.FindEntityByGuid(att->ownerGuid) : InvalidEntityId;
				std::vector<std::string> socketOptions;
				if (resolvedOwner != InvalidEntityId)
				{
					auto addOption = [&socketOptions](const std::string& value) {
						if (value.empty()) return;
						if (std::find(socketOptions.begin(), socketOptions.end(), value) == socketOptions.end())
							socketOptions.push_back(value);
						};
					if (const auto* sc = world.GetComponent<SocketComponent>(resolvedOwner))
					{
						for (const auto& s : sc->sockets)
						{
							addOption(s.name);
							if (!s.parentBone.empty() && s.parentBone != s.name)
								addOption(s.parentBone);
						}
					}
					// Auto-select: if socket name is empty and owner has sockets, set to first option
					if (att->socketName.empty() && !socketOptions.empty())
					{
						att->socketName = socketOptions[0];
						changed = true;
					}
				}

				// Socket: 목록에서 선택만 (문자열 직접 입력 제거)
				if (!socketOptions.empty())
				{
					const char* preview = att->socketName.c_str();
					if (preview[0] == '\0') preview = "(선택)";
					if (ImGui::BeginCombo("Socket", preview))
					{
						for (const auto& opt : socketOptions)
						{
							const bool sel = (att->socketName == opt);
							if (ImGui::Selectable(opt.c_str(), sel))
							{
								att->socketName = opt;
								changed = true;
							}
							if (sel)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("오너 소켓 목록에서 선택 (이름 또는 parentBone)");
				}
				else if (resolvedOwner != InvalidEntityId)
				{
					ImGui::TextColored(ImVec4(1.0f, 0.3f, 0.3f, 1.0f), "Owner has no sockets (SocketComponent.sockets 추가)");
					// 목록 없을 때만 현재값 표시 (읽기 전용)
					ImGui::Text("Socket Name: %s", att->socketName.empty() ? "(none)" : att->socketName.c_str());
				}
				else
				{
					// Owner 미지정 시: 안내 + 현재값 표시
					ImGui::TextDisabled("Owner를 먼저 선택하면 Socket 목록에서 고를 수 있습니다.");
					ImGui::Text("Socket Name: %s", att->socketName.empty() ? "(none)" : att->socketName.c_str());
				}

				changed |= ImGui::Checkbox("Follow Scale", &att->followScale);
				changed |= ImGui::DragFloat3("Extra Pos", &att->extraPos.x, 0.01f);
				changed |= ImGui::DragFloat3("Extra Rot (rad)", &att->extraRotRad.x, 0.01f);
				changed |= ImGui::DragFloat3("Extra Scale", &att->extraScale.x, 0.01f);

				ImGui::Separator();
				ImGui::Text("Debug: Resolved Owner EntityId = %llu", static_cast<unsigned long long>(resolvedOwner == InvalidEntityId ? 0 : resolvedOwner));

				if (changed) g_SceneDirty = true;
			}
		}
	}


	void EditorCore::DrawInspectorSocketComponent(World& world, const EntityId& _selectedEntity)
	{
		auto* comp = world.GetComponent<SocketComponent>(_selectedEntity);
		if (!comp)
			return;

		if (!ImGui::CollapsingHeader("Sockets", ImGuiTreeNodeFlags_DefaultOpen))
			return;

		bool changed = false;

		// 본 이름 목록 (동일 엔티티의 SkinnedMesh에서)
		std::vector<std::string> boneNames;
		if (m_skinnedRegistry)
		{
			if (const auto* skinned = world.GetComponent<SkinnedMeshComponent>(_selectedEntity))
			{
				if (!skinned->meshAssetPath.empty())
				{
					std::shared_ptr<SkinnedMeshGPU> mesh = m_skinnedRegistry->Find(skinned->meshAssetPath);
					if (mesh && mesh->sourceModel)
						boneNames = mesh->sourceModel->GetBoneNames();
				}
			}
		}

		// 소켓 전용 UI: 본 드롭다운, 이름/위치/회전/스케일, + 추가 / 항목별 삭제
		const size_t size = comp->sockets.size();
		for (size_t i = 0; i < size; ++i)
		{
			SocketDef& s = comp->sockets[i];
			ImGui::PushID(static_cast<int>(i));

			bool open = ImGui::TreeNode("Socket", "%s [%s]", s.name.empty() ? "(unnamed)" : s.name.c_str(), s.parentBone.empty() ? "?" : s.parentBone.c_str());
			ImGui::SameLine();
			if (ImGui::SmallButton("-"))
			{
				comp->sockets.erase(comp->sockets.begin() + static_cast<ptrdiff_t>(i));
				changed = true;
				ImGui::PopID();
				if (open) ImGui::TreePop();
				break;
			}
			if (open)
			{
				// Name
				char nameBuf[256];
				std::snprintf(nameBuf, sizeof(nameBuf), "%.255s", s.name.c_str());
				if (ImGui::InputText("Name", nameBuf, sizeof(nameBuf)))
				{
					s.name = nameBuf;
					changed = true;
				}

				// Parent Bone: 드롭다운 (본 목록)
				int currentBoneIndex = -1;
				if (!boneNames.empty())
				{
					for (size_t k = 0; k < boneNames.size(); ++k)
						if (boneNames[k] == s.parentBone) { currentBoneIndex = static_cast<int>(k); break; }

					const char* preview = (currentBoneIndex >= 0 && currentBoneIndex < static_cast<int>(boneNames.size()))
						? boneNames[static_cast<size_t>(currentBoneIndex)].c_str()
						: (s.parentBone.empty() ? "(선택)" : s.parentBone.c_str());
					if (ImGui::BeginCombo("Parent Bone", preview))
					{
						for (size_t k = 0; k < boneNames.size(); ++k)
						{
							const bool sel = (currentBoneIndex == static_cast<int>(k));
							if (ImGui::Selectable(boneNames[k].c_str(), sel))
							{
								s.parentBone = boneNames[k];
								changed = true;
							}
							if (sel)
								ImGui::SetItemDefaultFocus();
						}
						ImGui::EndCombo();
					}
				}
				else
				{
					char boneBuf[256];
					std::snprintf(boneBuf, sizeof(boneBuf), "%.255s", s.parentBone.c_str());
					if (ImGui::InputText("Parent Bone", boneBuf, sizeof(boneBuf)))
					{
						s.parentBone = boneBuf;
						changed = true;
					}
					if (ImGui::IsItemHovered())
						ImGui::SetTooltip("SkinnedMesh가 없으면 본 이름을 직접 입력");
				}

				changed |= ImGui::DragFloat3("Position", &s.position.x, 0.01f);
				changed |= ImGui::DragFloat3("Rotation (deg)", &s.rotation.x, 1.0f);
				changed |= ImGui::DragFloat3("Scale", &s.scale.x, 0.01f);
				ImGui::TreePop();
			}
			ImGui::PopID();
		}
		if (ImGui::Button("+ Add Socket"))
		{
			comp->sockets.push_back(SocketDef{});
			changed = true;
		}
		if (ImGui::IsItemHovered())
			ImGui::SetTooltip("이 오브젝트에 소켓을 추가합니다.");

		if (changed)
			g_SceneDirty = true;
	}
}
