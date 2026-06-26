#include "Editor/Core/EditorCore.h"
#include "Editor/Core/EditorCommands.h"
#include "Editor/Core/EditorUIState.h"

#include "Runtime/Foundation/ImGuiEx.h"
#include "Runtime/Resources/ResourceManager.h"
#include "Runtime/Resources/Prefab.h"
#include "Runtime/Rendering/Components/SkinnedMeshComponent.h"
#include "Runtime/Rendering/Data/Material.h"
#include "Runtime/Importing/FbxImporter.h"
#include "Runtime/Importing/FbxAsset.h"
#include "Runtime/Importing/FbxModel.h"

#include "imgui.h"

#include <algorithm>
#include <commdlg.h>
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
			DrawInspectorDecal(world, selectedEntity);
			ImGui::Separator();

			// 3-2. Lights
			DrawInspectorPointLight(world, selectedEntity);
			DrawInspectorSpotLight(world, selectedEntity);
			DrawInspectorRectLight(world, selectedEntity);

			// 3-3. Post Process Volume
			DrawInspectorPostProcessVolume(world, selectedEntity);

			// 3-3. Compute Effect
			DrawInspectorComputeEffect(world, selectedEntity);
			DrawInspectorUnityVfx(world, selectedEntity);

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
				if (ImGui::CollapsingHeader("Skinned Mesh", ImGuiTreeNodeFlags_DefaultOpen))
				{
					ImGui::Text("Skinned Mesh: %s", skinned->meshAssetPath.c_str());
					ImGui::Text("Instance Asset: %s", skinned->instanceAssetPath.empty() ? "None" : skinned->instanceAssetPath.c_str());

					ImGui::PushID("SkinnedMeshBrowse");
					if (ImGui::Button("Browse..."))
					{
					// 프로젝트 루트 기준 경로 계산
					wchar_t exePathW[MAX_PATH] = {};
					GetModuleFileNameW(nullptr, exePathW, MAX_PATH);
					std::filesystem::path exeDir = std::filesystem::path(exePathW).parent_path();
					std::filesystem::path projectRoot = exeDir.parent_path().parent_path().parent_path();
					std::filesystem::path resourceDir = projectRoot / "Resource";
					if (!std::filesystem::exists(resourceDir))
						resourceDir = projectRoot;

					std::wstring initialDirW = resourceDir.wstring();

					wchar_t fileBuffer[MAX_PATH] = {};
					OPENFILENAMEW ofn{};
					ofn.lStructSize = sizeof(ofn);
					ofn.hwndOwner = m_hwnd;
					ofn.lpstrFilter = L"FBX/FBXAsset\0*.fbx;*.fbxasset\0FBX\0*.fbx\0FBX Asset\0*.fbxasset\0All Files\0*.*\0";
					ofn.lpstrFile = fileBuffer;
					ofn.nMaxFile = MAX_PATH;
					ofn.lpstrInitialDir = initialDirW.c_str();
					ofn.Flags = OFN_EXPLORER | OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR;

					if (GetOpenFileNameW(&ofn))
					{
						std::filesystem::path pickedPath = fileBuffer;
						std::string ext = pickedPath.extension().string();
						std::transform(ext.begin(), ext.end(), ext.begin(),
							[](unsigned char c) { return static_cast<char>(std::tolower(c)); });

						// .fbxasset 선택: 인스턴스 경로로 연결
						if (ext == ".fbxasset")
						{
							std::string logicalPath = pickedPath.string();
							{
								std::filesystem::path logical = ResourceManager::NormalizeResourcePathAbsoluteToLogical(pickedPath);
								if (!logical.empty() && !logical.is_absolute())
									logicalPath = logical.generic_string();
								else
								{
									std::error_code ec;
									std::filesystem::path rel = std::filesystem::relative(pickedPath, projectRoot, ec);
									if (!ec && !rel.empty())
										logicalPath = rel.generic_string();
								}
							}

							FbxInstanceAsset asset{};
							if (LoadFbxInstanceAssetAuto(ResourceManager::Get(), logicalPath, asset))
							{
								skinned->instanceAssetPath = logicalPath;
								skinned->meshAssetPath = asset.meshAssetPath;

								static DirectX::XMFLOAT4X4 s_identityBone =
									DirectX::XMFLOAT4X4(1, 0, 0, 0,
										0, 1, 0, 0,
										0, 0, 1, 0,
										0, 0, 0, 1);
								skinned->boneMatrices = &s_identityBone;
								skinned->boneCount = 1;

								if (m_renderDevice && m_skinnedRegistry && !asset.sourceFbx.empty())
								{
									FbxImporter importer(ResourceManager::Get(), m_skinnedRegistry);
									importer.Import(m_renderDevice->GetDevice(),
										ResourceManager::Get().Resolve(asset.sourceFbx), {});
								}
								g_SceneDirty = true;
							}
						}
						// .fbx 선택: 임포트 후 컴포넌트에 연결
						else if (ext == ".fbx")
						{
							if (m_renderDevice)
							{
								std::filesystem::path fbxPath = pickedPath;
								std::error_code ec;
								std::filesystem::path rel = std::filesystem::relative(pickedPath, projectRoot, ec);
								if (!ec && !rel.empty())
									fbxPath = rel;

								FbxImportOptions opt{};
								FbxImporter importer(ResourceManager::Get(), m_skinnedRegistry);
								FbxImportResult result = importer.Import(m_renderDevice->GetDevice(), fbxPath, opt);

								if (!result.meshAssetPath.empty())
								{
									skinned->meshAssetPath = result.meshAssetPath;
									skinned->instanceAssetPath = result.instanceAssetPath;

									static DirectX::XMFLOAT4X4 s_identityBone =
										DirectX::XMFLOAT4X4(1, 0, 0, 0,
											0, 1, 0, 0,
											0, 0, 1, 0,
											0, 0, 0, 1);
									skinned->boneMatrices = &s_identityBone;
									skinned->boneCount = 1;

									if (!result.materialAssetPaths.empty())
									{
										MaterialComponent* mat = world.GetComponent<MaterialComponent>(selectedEntity);
										if (!mat)
										{
											DirectX::XMFLOAT3 defaultColor(0.7f, 0.7f, 0.7f);
											mat = &world.AddComponent<MaterialComponent>(selectedEntity, defaultColor);
										}
										mat->assetPath = result.materialAssetPaths.front();
										MaterialFile::Load(mat->assetPath, *mat, &ResourceManager::Get());
									}

									g_SceneDirty = true;
								}
							}
						}
					}
					}
					ImGui::PopID();

					ImGui::SameLine();
					ImGui::PushID("SkinnedMeshClear");
					if (ImGui::Button("Clear"))
					{
						skinned->meshAssetPath.clear();
						skinned->instanceAssetPath.clear();
						skinned->boneMatrices = nullptr;
						skinned->boneCount = 0;
						g_SceneDirty = true;
					}
					ImGui::PopID();

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
		}

		ImGui::End();
	}
}
