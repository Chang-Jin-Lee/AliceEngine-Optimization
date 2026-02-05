#include "Editor/Core/EditorCore.h"
#include "Editor/Core/EditorUIState.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include <algorithm>
#include <cctype>
#include <vector>

namespace Alice
{
	namespace
	{
		ReflectionUI::UIEditEvent RenderInspectorInstance(rttr::instance inst, World* world)
		{
			ReflectionUI::UIEditEvent result{};
			if (!inst.is_valid()) return result;

			rttr::type t = inst.get_type();
			for (auto& prop : t.get_properties())
			{
				const std::string propName = prop.get_name().to_string();

				ReflectionUI::UIEditEvent ev{};
				if (propName == "roughness" || propName == "metalness")
				{
					ev = ReflectionUI::Detail::RenderPropertyWithRange(prop, inst, 0.0f, 1.0f, "", world);
				}
				else
				{
					ev = ReflectionUI::Detail::RenderProperty(prop, inst, "", world);
				}

				result.changed |= ev.changed;
				result.activated |= ev.activated;
				result.deactivatedAfterEdit |= ev.deactivatedAfterEdit;
			}
			return result;
		}
	}

	void EditorCore::DrawInspectorScripts(World& world, const EntityId& _selectedEntity)
	{
		static std::vector<std::string> scriptNames;
		if (ImGui::BeginCombo("Add Script", "Select Script...")) {
			if (scriptNames.empty() || m_scriptBuilded) {
				m_scriptBuilded = false;
				scriptNames = ScriptFactory::GetRegisteredScriptNames();
				std::sort(scriptNames.begin(), scriptNames.end());
				scriptNames.erase(std::unique(scriptNames.begin(), scriptNames.end()),
					scriptNames.end());
			}

			for (const auto& name : scriptNames) {
				if (ImGui::Selectable(name.c_str())) {
					world.AddScript(_selectedEntity, name);
					g_SceneDirty = true;
				}
			}
			ImGui::EndCombo();
		}

		// Script 추가 필드에 드롭 타겟 추가
		if (ImGui::BeginDragDropTarget())
		{
			if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ASSET_FILE_PATH"))
			{
				const char* pathStr = static_cast<const char*>(payload->Data);
				std::filesystem::path droppedPath(pathStr);
				std::string ext = droppedPath.extension().string();

				// 스크립트 파일인지 확인 (.h, .cpp)
				std::transform(ext.begin(), ext.end(), ext.begin(),
					[](unsigned char c) { return static_cast<char>(std::tolower(c)); });
				if (ext == ".h" || ext == ".hpp" || ext == ".cpp" || ext == ".cxx")
				{
					// 파일명에서 스크립트 이름 추출 (확장자 제외)
					std::string scriptName = droppedPath.stem().string();

					// 등록된 스크립트 목록 확인
					if (scriptNames.empty() || m_scriptBuilded) {
						m_scriptBuilded = false;
						scriptNames = ScriptFactory::GetRegisteredScriptNames();
						std::sort(scriptNames.begin(), scriptNames.end());
						scriptNames.erase(std::unique(scriptNames.begin(), scriptNames.end()),
							scriptNames.end());
					}

					// 등록된 스크립트 목록에 있는지 확인
					bool found = std::find(scriptNames.begin(), scriptNames.end(), scriptName) != scriptNames.end();
					if (found) {
						world.AddScript(_selectedEntity, scriptName);
						g_SceneDirty = true;
					}
					// 등록되지 않은 경우에도 시도 (나중에 빌드되면 사용 가능할 수 있음)
					else {
						world.AddScript(_selectedEntity, scriptName);
						g_SceneDirty = true;
					}
				}
			}
			ImGui::EndDragDropTarget();
		}

		// 엔진 컴포넌트 추가 UI - 레지스트리 기반
		if (ImGui::BeginCombo("Add Engine Component", "Select Component..."))
		{
			auto& reg = EditorComponentRegistry::Get();
			const auto& list = reg.All();

			std::string currentCat;
			for (auto& d : list)
			{
				if (!d.addable) continue;

				if (d.category != currentCat)
				{
					currentCat = d.category;
					ImGui::Separator();
					ImGui::TextDisabled("%s", currentCat.c_str());
				}

				const bool has = d.has(world, _selectedEntity);

				if (has) ImGui::BeginDisabled();
				if (ImGui::Selectable(d.displayName.c_str(), false) && !has)
				{
					d.add(world, _selectedEntity);
					g_SceneDirty = true;
				}
				if (has) ImGui::EndDisabled();

				if (has && ImGui::IsItemHovered())
					ImGui::SetTooltip("이 컴포넌트는 이미 추가되어 있습니다.");
			}
			ImGui::EndCombo();
		}



		// 엔진 컴포넌트 표시 - 레지스트리 기반
		// 레지스트리 순회로 컴포넌트 표시
		auto& reg = EditorComponentRegistry::Get();
		for (auto& d : reg.All())
		{
			// 고정 레이아웃에서 처리되는 컴포넌트들은 제외 (중복 방지)
			std::string typeName = d.type.get_name().to_string();
			if (typeName == "TransformComponent" ||
				typeName == "MaterialComponent" ||
				typeName == "DecalComponent" ||
				typeName == "ComputeEffectComponent" ||
				typeName == "UnityVfxComponent" ||
				typeName == "PointLightComponent" ||
				typeName == "SpotLightComponent" ||
				typeName == "RectLightComponent" ||
				typeName == "PostProcessVolumeComponent" ||
				typeName == "SkinnedMeshComponent" ||
				typeName == "SkinnedAnimationComponent")  // Animation Status 섹션에서 처리됨
				continue;

			// 특수 처리 필요한 컴포넌트들 (물리 컴포넌트 등)
			if (typeName == "Phy_ColliderComponent")
			{
				DrawInspectorCollider(world, _selectedEntity);
				continue;
			}
			else if (typeName == "Phy_MeshColliderComponent")
			{
				DrawInspectorMeshCollider(world, _selectedEntity);
				continue;
			}
			else if (typeName == "Phy_CCTComponent")
			{
				DrawInspectorCharacterController(world, _selectedEntity);
				continue;
			}
			else if (typeName == "Phy_TerrainHeightFieldComponent")
			{
				DrawInspectorTerrainHeightField(world, _selectedEntity);
				continue;
			}
			else if (typeName == "Phy_SettingsComponent")
			{
				DrawInspectorPhysicsSceneSettings(world, _selectedEntity);
				continue;
			}
			else if (typeName == "Phy_JointComponent")
			{
				DrawInspectorJoint(world, _selectedEntity);
				continue;
			}
			else if (typeName == "AttackDriverComponent")
			{
				DrawInspectorAttackDriver(world, _selectedEntity);
				continue;
			}
			else if (typeName == "HurtboxComponent")
			{
				DrawInspectorHurtbox(world, _selectedEntity);
				continue;
			}
			else if (typeName == "WeaponTraceComponent")
			{
				DrawInspectorWeaponTrace(world, _selectedEntity);
				continue;
			}
			else if (typeName == "SocketAttachmentComponent")
			{
				DrawInspectorSocketAttachment(world, _selectedEntity);
				continue;
			}
			else if (typeName == "SocketComponent")
			{
				DrawInspectorSocketComponent(world, _selectedEntity);
				continue;
			}
			// 일반 컴포넌트: 레지스트리 기반 렌더링
			if (!d.has(world, _selectedEntity)) continue;

			if (ImGui::CollapsingHeader(d.displayName.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
			{
				if (d.removable)
				{
					std::string btn = "Remove##" + d.displayName;
					if (ImGui::Button(btn.c_str()))
					{
						d.remove(world, _selectedEntity);
						g_SceneDirty = true;
						continue;
					}
				}

				// 편집 시작/종료 감지 및 Undo 스냅샷
				static EntityId lastEditedEntity = InvalidEntityId;
				static std::string lastEditedComponentType;
				static JsonRttr::json editStartJson;
				static const EditorComponentDesc* lastEditedDesc = nullptr;

				rttr::instance inst = d.getInstance(world, _selectedEntity);
				ReflectionUI::UIEditEvent ev = RenderInspectorInstance(inst, &world);

				// 편집 시작: oldJson 스냅샷 저장
				if (ev.activated && (_selectedEntity != lastEditedEntity || lastEditedComponentType != typeName))
				{
					editStartJson = JsonRttr::ToJsonObject(inst);
					lastEditedEntity = _selectedEntity;
					lastEditedComponentType = typeName;
					lastEditedDesc = &d;
				}

				// 편집 종료: newJson 저장하고 커맨드 푸시
				if (ev.deactivatedAfterEdit && _selectedEntity == lastEditedEntity && lastEditedComponentType == typeName)
				{
					JsonRttr::json editEndJson = JsonRttr::ToJsonObject(inst);

					// 변경사항이 있으면 커맨드 푸시
					if (editStartJson != editEndJson && lastEditedDesc)
					{
						PushCommand(std::make_unique<ComponentEditCommandRTTR>(
							_selectedEntity, lastEditedDesc, editStartJson, editEndJson));
						g_SceneDirty = true;
					}

					lastEditedEntity = InvalidEntityId;
					lastEditedComponentType.clear();
					lastEditedDesc = nullptr;
				}

				if (ev.changed)
				{
					g_SceneDirty = true;
				}
			}
		}

		// List Scripts
		if (auto* scripts = world.GetScripts(_selectedEntity);
			scripts && !scripts->empty()) {
			for (size_t i = 0; i < scripts->size();) {
				auto& sc = (*scripts)[i];
				bool removed = false;

				ImGui::PushID(static_cast<int>(i));
				std::string header = sc.scriptName.empty() ? "Script" : sc.scriptName;
				if (ImGui::CollapsingHeader(header.c_str(),
					ImGuiTreeNodeFlags_DefaultOpen)) {
					ImGui::Checkbox("Enabled", &sc.enabled);
					ImGui::SameLine();
					if (ImGui::Button("Remove##ScriptRemove"))
						removed = true;

					// Save/Load Defaults (.meta)
					ImGui::SameLine();
					if (sc.instance && ImGui::Button("SaveDefaults")) {
						auto path = std::filesystem::path("Assets/Scripts") /
							(sc.scriptName + ".meta");
						JsonRttr::json root;
						root["version"] = 1;
						root["props"] = JsonRttr::ToJsonObject(
							*sc.instance, rttr::type::get_by_name(sc.scriptName));
						JsonRttr::SaveJsonFile(path, root, 4);
						ALICE_LOG_INFO("[Editor] Saved script defaults: %s",
							path.string().c_str());
					}
					ImGui::SameLine();
					if (sc.instance && ImGui::Button("LoadDefaults")) {
						auto path = std::filesystem::path("Assets/Scripts") /
							(sc.scriptName + ".meta");
						JsonRttr::json root;
						if (JsonRttr::LoadJsonFile(path, root)) {
							JsonRttr::FromJsonObject(
								*sc.instance, root["props"],
								rttr::type::get_by_name(sc.scriptName));
							g_SceneDirty = true;
						}
					}

					// Properties
					if (sc.instance) {
						rttr::instance inst = *sc.instance;
						rttr::type type = rttr::type::get_by_name(sc.scriptName);
						if (!type.is_valid()) type = inst.get_type();

						//rttr::instance inst = sc.instance;
						//rttr::type type = inst.get_derived_type(); // 이제 정확한 자식 타입이 나옴
						//if (!type.is_valid()) return;

						for (auto prop : type.get_properties()) {
							// Entity Reference Check
							// 1. Type is EntityId
							// 2. Metadata "EntityRef" is present
							rttr::type pType = prop.get_type();
							std::string pTypeName = pType.get_name().to_string();
							bool isEntityRef = (pType == rttr::type::get<EntityId>()) ||
								prop.get_metadata("EntityRef") ||
								(pTypeName == "EntityId") ||
								(pTypeName == "Alice::EntityId");

							if (isEntityRef) {
								EntityId currentRef = InvalidEntityId;
								rttr::variant val = prop.get_value(inst);
								if (val.can_convert<EntityId>())
									currentRef = val.get_value<EntityId>();

								std::string currentName = "None";
								if (currentRef != InvalidEntityId) {
									currentName = world.GetEntityName(currentRef);
									if (currentName.empty())
										currentName =
										"Entity " + std::to_string((uint32_t)currentRef);
								}

								if (ImGui::BeginCombo(prop.get_name().to_string().c_str(),
									currentName.c_str())) {
									if (ImGui::Selectable("None", currentRef == InvalidEntityId)) {
										prop.set_value(inst, InvalidEntityId);
										g_SceneDirty = true;
									}

									for (auto [eid, t] :
										world.GetComponents<TransformComponent>()) {
										std::string name = world.GetEntityName(eid);
										if (name.empty()) name = "Entity " + std::to_string((uint32_t)eid);
										if (ImGui::Selectable(name.c_str(), eid == currentRef)) {
											prop.set_value(inst, eid);
											g_SceneDirty = true;
										}
									}
									ImGui::EndCombo();
								}

								// EntityId 참조 필드에 드롭 타겟 추가 (Hierarchy에서 드래그한 엔티티)
								if (ImGui::BeginDragDropTarget())
								{
									if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload("ENTITY_HIERARCHY"))
									{
										IM_ASSERT(payload->DataSize == sizeof(EntityId));
										EntityId draggedId = *(const EntityId*)payload->Data;

										if (draggedId != InvalidEntityId)
										{
											prop.set_value(inst, draggedId);
											g_SceneDirty = true;
										}
									}
									ImGui::EndDragDropTarget();
								}
							}
							else {
								// Generic - string 타입의 경우 world를 전달하여 드래그 앤 드롭 지원
								// ReflectionUI::Detail::RenderProperty가 자동으로 엔티티 참조 필드를 감지하고 처리함
								ReflectionUI::UIEditEvent propEvent = ReflectionUI::Detail::RenderProperty(prop, inst, "", &world);
								if (propEvent.changed)
									g_SceneDirty = true;
							}
						}
					}
				}
				ImGui::PopID();

				if (removed) {
					world.RemoveScript(_selectedEntity, i);
					g_SceneDirty = true;
				}
				else
					i++;
			}
		}
	}
}
