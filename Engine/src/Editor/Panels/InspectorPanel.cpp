#include "Editor/Core/EditorCore.h"
#include "Editor/Core/EditorCommands.h"
#include "Editor/Core/EditorUIState.h"

#include "Runtime/Foundation/ImGuiEx.h"
#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Resources/Prefab.h"
#include "Runtime/Rendering/Components/SkinnedMeshComponent.h"
#include "Runtime/Importing/FbxModel.h"

#include "imgui.h"

#include <algorithm>
#include <filesystem>

namespace Alice
{
	using DirectX::Keyboard;

	void EditorCore::DrawInspectorWindow(World& world, EntityId& selectedEntity)
	{
		// === Inspector ===
		if (!ImGui::Begin("Inspector"))
		{
			ImGui::End();
			return;
		}

		ImGuiIO& io = ImGui::GetIO();
		// Delete 키 입력 처리: Inspector 창이 포커스를 가지고 있고, 텍스트 입력 중이 아닐 때
		const bool inspectorTextInputActive = io.WantTextInput || ImGui::IsAnyItemActive();

		if (selectedEntity != InvalidEntityId &&
			ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) &&
			!inspectorTextInputActive &&
			m_inputSystem &&
			m_inputSystem->IsKeyPressed(Keyboard::Keys::Delete)) {
			// 선택된 엔티티 삭제
			const std::string entityName = world.GetEntityName(selectedEntity);
			PushCommand(std::make_unique<DestroyEntityCommand>(selectedEntity, entityName, world));
			world.DestroyEntity(selectedEntity);
			selectedEntity = InvalidEntityId;
			g_SceneDirty = true;
		}

		// 프리팹 드래그앤드롭: Inspector 창 전체에 드롭 타겟 추가
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE_PATH"))
			{
				const char* pathStr = static_cast<const char*>(payload->Data);
				std::filesystem::path droppedPath(pathStr);
				std::string ext = droppedPath.extension().string();
				std::transform(ext.begin(), ext.end(), ext.begin(),
					[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

				if (ext == ".prefab")
				{
					if (selectedEntity != InvalidEntityId)
					{
						// 선택된 엔티티의 자식으로 추가
						EntityId e = Alice::Prefab::InstantiateFromFile(world, droppedPath);
						if (e != InvalidEntityId)
						{
							EntityId oldParent = world.GetParent(e);
							world.SetParent(e, selectedEntity);

							// 성공 여부 확인 후에만 Undo 커맨드 추가
							if (world.GetParent(e) == selectedEntity)
							{
								PushCommand(std::make_unique<SetParentCommand>(e, oldParent, selectedEntity));
								g_SceneDirty = true;
							}

							selectedEntity = e; // 새로 생성된 엔티티를 선택
						}
					}
					else
					{
						// 선택된 엔티티가 없으면 루트에 추가
						EntityId e = Alice::Prefab::InstantiateFromFile(world, droppedPath);
						if (e != InvalidEntityId)
						{
							selectedEntity = e;
							g_SceneDirty = true;
						}
					}
				}
			}
			ImGui::EndDragDropTarget();
		}

		if (selectedEntity == InvalidEntityId) {
			Alice::ImGuiText(L"선택된 엔티티가 없습니다.");
		}
		else {
			// World 엔티티 Inspector 표시 (기존 로직)
			// 엔티티 ID와 이름 표시 (한 번만 가져와서 재사용)
			const std::string entityName = world.GetEntityName(selectedEntity);
			if (!entityName.empty()) {
				ImGui::Text("Entity %u - %s", static_cast<uint32_t>(selectedEntity), entityName.c_str());
			}
			else {
				ImGui::Text("Entity %u", static_cast<uint32_t>(selectedEntity));
			}
			ImGui::Separator();

			// 1. Transform
			DrawInspectorTransform(world, selectedEntity);
			ImGui::Separator();

			// 1-1. Animation Status
			DrawInspectorAnimationStatus(world, selectedEntity);
			ImGui::Separator();

			// 2. Scripts
			ImGui::Text("Scripts");
			DrawInspectorScripts(world, selectedEntity);
			ImGui::Separator();

			// 3. Material
			DrawInspectorMaterial(world, selectedEntity);
			ImGui::Separator();

			// 3-2. Lights
			DrawInspectorPointLight(world, selectedEntity);
			DrawInspectorSpotLight(world, selectedEntity);
			DrawInspectorRectLight(world, selectedEntity);

			// 3-3. Post Process Volume
			DrawInspectorPostProcessVolume(world, selectedEntity);

			// 3-3. Compute Effect
			DrawInspectorComputeEffect(world, selectedEntity);

			// 3-4. Camera 컴포넌트들
			DrawInspectorCameraSpringArm(world, selectedEntity);
			DrawInspectorCameraLookAt(world, selectedEntity);
			DrawInspectorCameraFollow(world, selectedEntity);
			DrawInspectorCameraShake(world, selectedEntity);
			DrawInspectorCameraInput(world, selectedEntity);
			DrawInspectorCameraBlend(world, selectedEntity);

			// 4. Skinned Mesh / 소켓 프리뷰 (간단 뷰)
			if (auto* skinned =
				world.GetComponent<SkinnedMeshComponent>(selectedEntity)) {
				ImGui::Separator();
				ImGui::Text("Skinned Mesh: %s", skinned->meshAssetPath.c_str());

				// 본 목록 미니 뷰 (이름 확인용)
				if (m_skinnedRegistry) {
					auto mesh = m_skinnedRegistry->Find(skinned->meshAssetPath);
					if (mesh && mesh->sourceModel) {
						const auto& bones = mesh->sourceModel->GetBoneNames();
						if (ImGui::TreeNode("Bones")) {
							for (size_t i = 0; i < bones.size(); ++i) {
								ImGui::Text("%zu: %s", i, bones[i].c_str());
							}
							ImGui::TreePop();
						}
					}
				}

				// 메시 경로 필드에 드롭 타겟 추가
				if (ImGui::BeginDragDropTarget())
				{
					if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE_PATH"))
					{
						const char* pathStr = static_cast<const char*>(payload->Data);
						std::filesystem::path droppedPath(pathStr);
						std::string ext = droppedPath.extension().string();

						// FBX 파일인지 확인
						std::transform(ext.begin(), ext.end(), ext.begin(),
							[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
						if (ext == ".fbx" || ext == ".fbxasset")
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
							skinned->meshAssetPath = logicalPath;
							g_SceneDirty = true;
						}
					}
					ImGui::EndDragDropTarget();
				}
			}
		}

		ImGui::End();
	}
}
