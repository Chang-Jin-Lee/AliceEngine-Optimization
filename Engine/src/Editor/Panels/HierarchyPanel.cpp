#include "Editor/Core/EditorCore.h"
#include "Editor/Core/EditorCommands.h"
#include "Editor/Core/EditorUIState.h"

#include "Runtime/Foundation/ImGuiEx.h"
#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Resources/Prefab.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "Runtime/ECS/Components/TransformComponent.h"

#include "imgui.h"

#include <algorithm>
#include <cstring>
#include <functional>
#include <set>

namespace Alice
{
	using DirectX::Keyboard;

	void EditorCore::DrawHierarchyWindow(World& world, EntityId& selectedEntity)
	{
		// === Hierarchy ===
		if (!ImGui::Begin("Hierarchy"))
		{
			ImGui::End();
			return;
		}

		ImGuiIO& io = ImGui::GetIO();

		// "엔티티 목록" 텍스트에 드롭 타겟 추가 (부모 관계 해제)
		Alice::ImGuiText(L"엔티티 목록");
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_HIERARCHY"))
			{
				IM_ASSERT(payload->DataSize == sizeof(EntityId));
				EntityId draggedId = *(const EntityId*)payload->Data;

				if (draggedId != InvalidEntityId)
				{
					EntityId oldParent = world.GetParent(draggedId);
					if (oldParent != InvalidEntityId)
					{
						// Transform 스냅샷 저장 (Undo용)
						TransformComponent oldTransform;
						if (auto* t = world.GetComponent<TransformComponent>(draggedId))
						{
							oldTransform = *t;
						}

						// keepWorld=true: 월드 위치 유지
						world.SetParent(draggedId, InvalidEntityId, true);

						// 성공 여부 확인 후에만 Undo 커맨드 추가
						if (world.GetParent(draggedId) == InvalidEntityId)
						{
							// 새 Transform 스냅샷 저장
							TransformComponent newTransform;
							if (auto* t = world.GetComponent<TransformComponent>(draggedId))
							{
								newTransform = *t;
							}
							PushCommand(std::make_unique<SetParentCommand>(draggedId, oldParent, InvalidEntityId, oldTransform, newTransform));
							g_SceneDirty = true;
						}
					}
				}
			}
			ImGui::EndDragDropTarget();
		}

		ImGui::Separator();

		EntityId entityToDelete = InvalidEntityId;
		static EntityId s_renameTarget = InvalidEntityId;
		static char s_renameBuf[128]{};
		bool openRenamePopup = false;
		static EntityId s_draggedEntity = InvalidEntityId;

		// 재귀적으로 트리 노드를 그리는 람다 함수
		std::function<void(EntityId)> DrawEntityNode = [&](EntityId entityId) {
			if (entityId == InvalidEntityId)
				return;

			const bool isSelected = (selectedEntity == entityId);
			const std::string name = world.GetEntityName(entityId);
			const std::string label = !name.empty()
				? name
				: ("Entity " + std::to_string(static_cast<std::uint32_t>(entityId)));

			ImGui::PushID((int)entityId);

			// 자식들 가져오기
			std::vector<EntityId> children = world.GetChildren(entityId);

			// TreeNode 플래그
			ImGuiTreeNodeFlags nodeFlags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick;
			if (isSelected)
				nodeFlags |= ImGuiTreeNodeFlags_Selected;
			if (children.empty())
				nodeFlags |= ImGuiTreeNodeFlags_Leaf;

			const bool isAliceUI = (world.GetComponent<UIWidgetComponent>(entityId) != nullptr);
			if (isAliceUI)
			{
				ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.2f, 0.7f, 1.0f));
			}

			// 트리 노드 열기
			bool nodeOpen = ImGui::TreeNodeEx(label.c_str(), nodeFlags);

			if (isAliceUI)
			{
				ImGui::PopStyleColor();
			}

			// 선택 처리 (더블클릭으로만 인스펙터 변경 - 드래그앤드롭을 위해)
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
			{
				selectedEntity = entityId;
			}

			// 드래그 시작
			if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None))
			{
				ImGui::SetDragDropPayload("ENTITY_HIERARCHY", &entityId, sizeof(EntityId));
				ImGui::TextUnformatted(label.c_str());
				s_draggedEntity = entityId;
				ImGui::EndDragDropSource();
			}

			// 드롭 대상 처리
			if (ImGui::BeginDragDropTarget())
			{
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_HIERARCHY"))
				{
					IM_ASSERT(payload->DataSize == sizeof(EntityId));
					EntityId draggedId = *(const EntityId*)payload->Data;

					if (draggedId != entityId && draggedId != InvalidEntityId)
					{
						// 기존 부모 가져오기
						EntityId oldParent = world.GetParent(draggedId);

						// Transform 스냅샷 저장 (Undo용)
						TransformComponent oldTransform;
						if (auto* t = world.GetComponent<TransformComponent>(draggedId))
						{
							oldTransform = *t;
						}

						// 새 부모 설정 (keepWorld=true: 월드 위치 유지)
						world.SetParent(draggedId, entityId, true);

						// 성공 여부 확인 후에만 Undo 커맨드 추가
						if (world.GetParent(draggedId) == entityId)
						{
							// 새 Transform 스냅샷 저장
							TransformComponent newTransform;
							if (auto* t = world.GetComponent<TransformComponent>(draggedId))
							{
								newTransform = *t;
							}
							PushCommand(std::make_unique<SetParentCommand>(draggedId, oldParent, entityId, oldTransform, newTransform));
							g_SceneDirty = true;
						}
					}
				}
				ImGui::EndDragDropTarget();
			}

			// 컨텍스트 메뉴
			if (ImGui::BeginPopupContextItem())
			{
				if (ImGui::MenuItem("Change Name"))
				{
					s_renameTarget = entityId;
					openRenamePopup = true;

					const std::string cur = world.GetEntityName(entityId);
					const std::string init = cur.empty()
						? ("Entity " + std::to_string((std::uint32_t)entityId))
						: cur;
					std::memset(s_renameBuf, 0, sizeof(s_renameBuf));
					strncpy_s(s_renameBuf, init.c_str(), sizeof(s_renameBuf) - 1);
				}

				if (ImGui::MenuItem("Delete"))
				{
					entityToDelete = entityId;
				}

				if (ImGui::MenuItem("Unparent"))
				{
					EntityId oldParent = world.GetParent(entityId);
					if (oldParent != InvalidEntityId)
					{
						world.SetParent(entityId, InvalidEntityId);

						// 성공 여부 확인 후에만 Undo 커맨드 추가
						if (world.GetParent(entityId) == InvalidEntityId)
						{
							PushCommand(std::make_unique<SetParentCommand>(entityId, oldParent, InvalidEntityId));
							g_SceneDirty = true;
						}
					}
				}

				// 현재 게임 오브젝트를 프리팹으로 저장하는 기능
				if (ImGui::MenuItem("Save as Prefab"))
				{
					namespace fs = std::filesystem;
					const fs::path prefabDir = ResourceManager::Get().Resolve("Assets/Prefabs");
					if (!fs::exists(prefabDir))
					{
						fs::create_directories(prefabDir);
					}

					std::string baseName = "Entity_" + std::to_string(static_cast<std::uint32_t>(entityId)) + ".prefab";
					fs::path prefabPath = prefabDir / baseName;

					int index = 1;
					while (fs::exists(prefabPath))
					{
						baseName = "Entity_" + std::to_string(static_cast<std::uint32_t>(entityId)) + "_" + std::to_string(index) + ".prefab";
						prefabPath = prefabDir / baseName;
						++index;
					}

					Prefab::SaveToFile(world, entityId, prefabPath);
				}

				ImGui::EndPopup();
			}

			// 자식 노드들 재귀적으로 그리기
			if (nodeOpen)
			{
				// 자식들을 ID 순으로 정렬
				std::sort(children.begin(), children.end());
				for (EntityId child : children)
				{
					DrawEntityNode(child);
				}
			}

			// TreeNodeEx를 호출했으면 항상 TreePop을 호출해야 함
			if (nodeOpen)
			{
				ImGui::TreePop();
			}

			ImGui::PopID();
			};

		// 루트 엔티티들 가져오기
		std::vector<EntityId> rootEntities = world.GetRootEntities();

		// AliceUI 엔티티들도 Hierarchy에 포함 (TransformComponent 없는 경우 대비)
		// 단, 부모가 있는 UI 위젯은 루트 목록에 다시 넣지 않는다.
		std::set<EntityId> rootSet(rootEntities.begin(), rootEntities.end());
		for (auto [id, widget] : world.GetComponents<UIWidgetComponent>())
		{
			if (world.GetParent(id) != InvalidEntityId)
				continue;
			if (rootSet.insert(id).second)
				rootEntities.push_back(id);
		}

		if (rootEntities.empty())
		{
			Alice::ImGuiText(L"생성된 엔티티가 없습니다.");
		}
		else
		{
			// 루트 엔티티들을 ID 순으로 정렬
			std::sort(rootEntities.begin(), rootEntities.end());

			// 루트 노드들 그리기
			for (EntityId rootId : rootEntities)
			{
				DrawEntityNode(rootId);
			}
		}

		// 빈 공간에 드롭하여 부모 관계 해제 또는 프리팹 인스턴스화
		// 빈 공간을 감지하기 위해 InvisibleButton 사용
		ImVec2 availSize = ImGui::GetContentRegionAvail();
		if (availSize.y > 0)
		{
			ImGui::InvisibleButton("##HierarchyEmptySpace", availSize);
			if (ImGui::BeginDragDropTarget())
			{
				// 엔티티 드래그앤드롭: 부모 관계 해제
				if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_HIERARCHY"))
				{
					IM_ASSERT(payload->DataSize == sizeof(EntityId));
					EntityId draggedId = *(const EntityId*)payload->Data;

					if (draggedId != InvalidEntityId)
					{
						EntityId oldParent = world.GetParent(draggedId);
						if (oldParent != InvalidEntityId)
						{
							// Transform 스냅샷 저장 (Undo용)
							TransformComponent oldTransform;
							if (auto* t = world.GetComponent<TransformComponent>(draggedId))
							{
								oldTransform = *t;
							}

							// keepWorld=true: 월드 위치 유지
							world.SetParent(draggedId, InvalidEntityId, true);

							// 성공 여부 확인 후에만 Undo 커맨드 추가
							if (world.GetParent(draggedId) == InvalidEntityId)
							{
								// 새 Transform 스냅샷 저장
								TransformComponent newTransform;
								if (auto* t = world.GetComponent<TransformComponent>(draggedId))
								{
									newTransform = *t;
								}
								PushCommand(std::make_unique<SetParentCommand>(draggedId, oldParent, InvalidEntityId, oldTransform, newTransform));
								g_SceneDirty = true;
							}
						}
					}
				}
				// 프리팹 파일 드래그앤드롭: 인스턴스화
				else if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE_PATH"))
				{
					const char* pathStr = static_cast<const char*>(payload->Data);
					std::filesystem::path droppedPath(pathStr);
					std::string ext = droppedPath.extension().string();
					std::transform(ext.begin(), ext.end(), ext.begin(),
						[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

					if (ext == ".prefab")
					{
						EntityId e = Alice::Prefab::InstantiateFromFile(world, droppedPath);
						if (e != InvalidEntityId)
						{
							selectedEntity = e;
							g_SceneDirty = true;
						}
					}
				}
				ImGui::EndDragDropTarget();
			}
		}

		// Delete 키 입력 처리: Hierarchy 창이 포커스를 가지고 있고, 텍스트 입력 중이 아닐 때
		const bool hierarchyTextInputActive = io.WantTextInput || ImGui::IsAnyItemActive();
		if (selectedEntity != InvalidEntityId &&
			ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
			!hierarchyTextInputActive &&
			m_inputSystem &&
			m_inputSystem->IsKeyPressed(Keyboard::Keys::Delete))
		{
			// 선택된 엔티티 삭제
			const std::string entityName = world.GetEntityName(selectedEntity);
			PushCommand(std::make_unique<DestroyEntityCommand>(selectedEntity, entityName, world));
			world.DestroyEntity(selectedEntity);
			selectedEntity = InvalidEntityId;
			g_SceneDirty = true;
		}

		if (openRenamePopup)
			ImGui::OpenPopup("Change Name");

		if (ImGui::BeginPopupModal("Change Name", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
		{
			ImGui::InputText("Name", s_renameBuf, sizeof(s_renameBuf));
			if (ImGui::Button("OK"))
			{
				const std::string oldName = world.GetEntityName(s_renameTarget);
				const std::string newName = s_renameBuf;
				world.SetEntityName(s_renameTarget, newName);
				PushCommand(std::make_unique<SetEntityNameCommand>(s_renameTarget, oldName, newName));
				g_SceneDirty = true;
				ImGui::CloseCurrentPopup();
			}
			ImGui::SameLine();
			if (ImGui::Button("Cancel"))
			{
				ImGui::CloseCurrentPopup();
			}
			ImGui::EndPopup();
		}

		// 루프가 끝난 뒤에 실제 삭제를 수행합니다. (반복 중 컨테이너 수정 방지)
		if (entityToDelete != InvalidEntityId)
		{
			const std::string entityName = world.GetEntityName(entityToDelete);
			PushCommand(std::make_unique<DestroyEntityCommand>(entityToDelete, entityName, world));
			world.DestroyEntity(entityToDelete);
			if (selectedEntity == entityToDelete)
			{
				selectedEntity = InvalidEntityId;
			}
			g_SceneDirty = true;
		}

		ImGui::End();
	}
}
