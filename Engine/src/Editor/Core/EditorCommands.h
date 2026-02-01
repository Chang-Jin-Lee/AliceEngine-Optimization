#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <set>
#include <string>
#include <vector>

#include <DirectXMath.h>

#include "Editor/Core/EditorCore.h"
#include "Runtime/ECS/Components/IDComponent.h"
#include "Runtime/Gameplay/Sockets/SocketComponent.h"
#include "Runtime/Gameplay/Sockets/SocketAttachmentComponent.h"
#include "Runtime/Resources/Serialization/SocketSerialization.h"

namespace Alice
{
	// 엔티티 생성 명령
	struct CreateEntityCommand : ICommand
	{
		EntityId entityId;
		std::string entityType;
		mutable std::string description; // GetDescription에서 사용

		CreateEntityCommand(EntityId id, const std::string& type)
			: entityId(id), entityType(type)
		{
			description = "Create " + entityType;
		}

		void Execute(World& world, EntityId& selectedEntity) override
		{
			// 엔티티는 이미 생성되어 있음
			selectedEntity = entityId;
		}

		void Undo(World& world, EntityId& selectedEntity) override
		{
			if (selectedEntity == entityId)
				selectedEntity = InvalidEntityId;
			world.DestroyEntity(entityId);
		}

		const char* GetDescription() const override
		{
			return description.c_str();
		}

		// Create/Destroy는 ID 변경 문제로 Redo 지원 안 함
		bool SupportsRedo() const override { return false; }
	};

	// 엔티티 삭제 명령 (씬 파일 직렬화 함수 사용)
	struct DestroyEntityCommand : ICommand
	{
		EntityId entityId;
		std::string entityName;
		JsonRttr::json serializedData; // 엔티티 전체를 JSON으로 저장
		JsonRttr::json childrenData; // 자식 엔티티들의 JSON 데이터 배열
		mutable std::string description;

		DestroyEntityCommand(EntityId id, const std::string& name, const World& world)
			: entityId(id), entityName(name)
		{
			description = "Delete Entity";
			// 엔티티 삭제 전에 전체 상태를 JSON으로 저장
			// SceneFile의 WriteEntity 함수와 동일한 방식 사용
			JsonRttr::json entityJson;
			if (WriteEntityToJson(entityJson, world, id))
			{
				serializedData = entityJson;
			}

			// 자식 엔티티들도 저장 (재귀적으로)
			childrenData = JsonRttr::json::array();
			SaveChildrenRecursive(world, id, childrenData);
		}

		// 재귀적으로 자식 엔티티들을 저장하는 헬퍼 함수
		static void SaveChildrenRecursive(const World& world, EntityId parentId, JsonRttr::json& outArray)
		{
			std::vector<EntityId> children = world.GetChildren(parentId);
			for (EntityId childId : children)
			{
				JsonRttr::json childJson;
				if (WriteEntityToJson(childJson, world, childId))
				{
					// 재귀적으로 손자들도 저장 (push_back 전에 완료)
					JsonRttr::json grandchildrenArray = JsonRttr::json::array();
					SaveChildrenRecursive(world, childId, grandchildrenArray);
					if (!grandchildrenArray.empty())
					{
						childJson["_children"] = std::move(grandchildrenArray);
					}

					// 손자 정보를 포함한 childJson을 push
					outArray.push_back(std::move(childJson));
				}
			}
		}

		// WriteEntity 함수를 복사한 헬퍼 함수 (SceneFile::WriteEntity와 동일한 방식)
		static bool WriteEntityToJson(JsonRttr::json& outEntity, const World& world, EntityId id)
		{
			outEntity = JsonRttr::json::object();

			const std::string name = world.GetEntityName(id);
			if (!name.empty())
				outEntity["name"] = name;

			// GUID 저장
			if (const auto* idComp = world.GetComponent<IDComponent>(id); idComp)
			{
				outEntity["guid"] = std::to_string(idComp->guid);
			}

			// Parent 관계 저장 (GUID 기반)
			EntityId parentId = world.GetParent(id);
			if (parentId != InvalidEntityId)
			{
				if (const auto* parentIdComp = world.GetComponent<IDComponent>(parentId); parentIdComp)
				{
					outEntity["_parentGuid"] = std::to_string(parentIdComp->guid);
				}
			}

			if (const auto* transform = world.GetComponent<TransformComponent>(id); transform)
			{
				rttr::instance inst = const_cast<TransformComponent&>(*transform);
				outEntity["Transform"] = JsonRttr::ToJsonObject(inst);
			}

			if (const auto* scripts = world.GetScripts(id); scripts && !scripts->empty())
			{
				JsonRttr::json arr = JsonRttr::json::array();
				for (const auto& sc : *scripts)
				{
					JsonRttr::json s = JsonRttr::json::object();
					s["name"] = sc.scriptName;
					s["enabled"] = sc.enabled;

					if (sc.instance)
					{
						rttr::instance inst = *sc.instance;
						const rttr::type t = rttr::type::get_by_name(sc.scriptName);
						s["props"] = JsonRttr::ToJsonObject(inst, t);
					}

					arr.push_back(s);
				}
				outEntity["Scripts"] = arr;
			}

			// Material
			if (const auto* mat = world.GetComponent<MaterialComponent>(id); mat)
			{
				MaterialComponent matCopy = *mat;
				rttr::instance inst = matCopy;
				outEntity["Material"] = JsonRttr::ToJsonObject(inst);
			}

			// SkinnedMesh
			if (const auto* skinned = world.GetComponent<SkinnedMeshComponent>(id); skinned)
			{
				SkinnedMeshComponent skinnedCopy = *skinned;
				rttr::instance inst = skinnedCopy;
				outEntity["SkinnedMesh"] = JsonRttr::ToJsonObject(inst);
			}

			// SkinnedAnimation
			if (const auto* anim = world.GetComponent<SkinnedAnimationComponent>(id); anim)
			{
				rttr::instance inst = const_cast<SkinnedAnimationComponent&>(*anim);
				outEntity["SkinnedAnimation"] = JsonRttr::ToJsonObject(inst);
			}

			// Socket
			if (const auto* socketComp = world.GetComponent<SocketComponent>(id); socketComp)
			{
				outEntity["Socket"] = SocketSerialization::SocketComponentToJson(*socketComp);
			}

			// SocketAttachment
			if (const auto* socketAttach = world.GetComponent<SocketAttachmentComponent>(id); socketAttach)
			{
				rttr::instance inst = const_cast<SocketAttachmentComponent&>(*socketAttach);
				JsonRttr::json obj = JsonRttr::ToJsonObject(inst);
				obj["ownerGuid"] = std::to_string(socketAttach->ownerGuid);
				outEntity["SocketAttachment"] = obj;
			}

			// Camera components
			if (const auto* cam = world.GetComponent<CameraComponent>(id); cam)
			{
				rttr::instance inst = const_cast<CameraComponent&>(*cam);
				outEntity["Camera"] = JsonRttr::ToJsonObject(inst);
			}
			if (const auto* follow = world.GetComponent<CameraFollowComponent>(id); follow)
			{
				rttr::instance inst = const_cast<CameraFollowComponent&>(*follow);
				outEntity["CameraFollow"] = JsonRttr::ToJsonObject(inst);
			}
			if (const auto* spring = world.GetComponent<CameraSpringArmComponent>(id); spring)
			{
				rttr::instance inst = const_cast<CameraSpringArmComponent&>(*spring);
				outEntity["CameraSpringArm"] = JsonRttr::ToJsonObject(inst);
			}
			if (const auto* lookAt = world.GetComponent<CameraLookAtComponent>(id); lookAt)
			{
				rttr::instance inst = const_cast<CameraLookAtComponent&>(*lookAt);
				outEntity["CameraLookAt"] = JsonRttr::ToJsonObject(inst);
			}
			if (const auto* shake = world.GetComponent<CameraShakeComponent>(id); shake)
			{
				rttr::instance inst = const_cast<CameraShakeComponent&>(*shake);
				outEntity["CameraShake"] = JsonRttr::ToJsonObject(inst);
			}
			if (const auto* blend = world.GetComponent<CameraBlendComponent>(id); blend)
			{
				rttr::instance inst = const_cast<CameraBlendComponent&>(*blend);
				outEntity["CameraBlend"] = JsonRttr::ToJsonObject(inst);
			}
			if (const auto* input = world.GetComponent<CameraInputComponent>(id); input)
			{
				rttr::instance inst = const_cast<CameraInputComponent&>(*input);
				outEntity["CameraInput"] = JsonRttr::ToJsonObject(inst);
			}

			// Lights
			if (const auto* pl = world.GetComponent<PointLightComponent>(id); pl)
			{
				rttr::instance inst = const_cast<PointLightComponent&>(*pl);
				outEntity["PointLight"] = JsonRttr::ToJsonObject(inst);
			}
			if (const auto* sl = world.GetComponent<SpotLightComponent>(id); sl)
			{
				rttr::instance inst = const_cast<SpotLightComponent&>(*sl);
				outEntity["SpotLight"] = JsonRttr::ToJsonObject(inst);
			}
			if (const auto* rl = world.GetComponent<RectLightComponent>(id); rl)
			{
				rttr::instance inst = const_cast<RectLightComponent&>(*rl);
				outEntity["RectLight"] = JsonRttr::ToJsonObject(inst);
			}

			// Physics Components
			if (const auto* rigid = world.GetComponent<Phy_RigidBodyComponent>(id); rigid)
			{
				rttr::instance inst = const_cast<Phy_RigidBodyComponent&>(*rigid);
				outEntity["RigidBody"] = JsonRttr::ToJsonObject(inst);
			}
			if (const auto* collider = world.GetComponent<Phy_ColliderComponent>(id); collider)
			{
				rttr::instance inst = const_cast<Phy_ColliderComponent&>(*collider);
				outEntity["Collider"] = JsonRttr::ToJsonObject(inst);
			}
			if (const auto* meshCollider = world.GetComponent<Phy_MeshColliderComponent>(id); meshCollider)
			{
				rttr::instance inst = const_cast<Phy_MeshColliderComponent&>(*meshCollider);
				outEntity["MeshCollider"] = JsonRttr::ToJsonObject(inst);
			}
			if (const auto* cct = world.GetComponent<Phy_CCTComponent>(id); cct)
			{
				rttr::instance inst = const_cast<Phy_CCTComponent&>(*cct);
				outEntity["CharacterController"] = JsonRttr::ToJsonObject(inst);
			}
			if (const auto* terrain = world.GetComponent<Phy_TerrainHeightFieldComponent>(id); terrain)
			{
				rttr::instance inst = const_cast<Phy_TerrainHeightFieldComponent&>(*terrain);
				outEntity["TerrainHeightField"] = JsonRttr::ToJsonObject(inst);
			}
			if (const auto* joint = world.GetComponent<Phy_JointComponent>(id); joint)
			{
				rttr::instance inst = const_cast<Phy_JointComponent&>(*joint);
				outEntity["Joint"] = JsonRttr::ToJsonObject(inst);
			}

			return true;
		}

		// GUID 파싱 헬퍼 (SceneFile와 동일)
		static std::uint64_t ParseGuid(const JsonRttr::json& j)
		{
			if (j.is_string())
			{
				try
				{
					return std::stoull(j.get<std::string>());
				}
				catch (...)
				{
					// GUID 생성 함수는 World.cpp에 있으므로 여기서는 0 반환 (나중에 덮어쓰기)
					return 0;
				}
			}
			else if (j.is_number_unsigned())
			{
				return j.get<std::uint64_t>();
			}
			return 0;
		}

		// ApplyEntity 함수를 복사한 헬퍼 함수
		static bool RestoreEntityFromJson(World& world, const JsonRttr::json& e, EntityId& restoredId)
		{
			if (!e.is_object()) return false;

			const EntityId id = world.CreateEntity();
			restoredId = id;

			// RAII 가드: 복원 실패 시 엔티티가 찌꺼기로 남지 않게 자동 정리
			struct EntityGuard {
				World& world;
				EntityId id;
				bool committed = false;

				EntityGuard(World& w, EntityId i) : world(w), id(i) {}
				~EntityGuard() {
					if (!committed) {
						world.DestroyEntity(id);
					}
				}
			} guard(world, id);

			const std::string name = e.value("name", std::string{});
			if (!name.empty())
				world.SetEntityName(id, name);

			// IDComponent: GUID 복원 (저장된 값으로 덮어쓰기)
			auto* idComp = world.GetComponent<IDComponent>(id);
			if (idComp)
			{
				if (auto itGuid = e.find("guid"); itGuid != e.end())
				{
					auto parsed = ParseGuid(*itGuid);
					if (parsed != 0) idComp->guid = parsed; // 실패면 덮어쓰지 않기
				}
				// 없으면 CreateEntity에서 생성한 GUID 유지
			}

			// Transform
			TransformComponent& t = world.AddComponent<TransformComponent>(id);
			auto itT = e.find("Transform");
			if (itT != e.end())
			{
				rttr::instance inst = t;
				if (!JsonRttr::FromJsonObject(inst, *itT)) return false;
				if (itT->is_object() && itT->find("visible") == itT->end())
				{
					auto itLegacy = itT->find("renderEnabled");
					if (itLegacy != itT->end())
					{
						if (itLegacy->is_boolean())
							t.visible = itLegacy->get<bool>();
						else if (itLegacy->is_number())
							t.visible = (itLegacy->get<double>() != 0.0);
					}
				}
			}

			// Scripts
			auto itS = e.find("Scripts");
			if (itS != e.end() && itS->is_array())
			{
				for (const auto& s : *itS)
				{
					if (!s.is_object()) continue;
					const std::string scriptName = s.value("name", std::string{});
					if (scriptName.empty()) continue;

					ScriptComponent& sc = world.AddScript(id, scriptName);
					sc.enabled = s.value("enabled", true);

					auto itP = s.find("props");
					if (itP != s.end() && itP->is_object() && sc.instance)
					{
						rttr::instance inst = *sc.instance;
						const rttr::type ttype = rttr::type::get_by_name(sc.scriptName);
						if (!JsonRttr::FromJsonObject(inst, *itP, ttype)) return false;
					}
				}
			}

			// Material
			auto itM = e.find("Material");
			if (itM != e.end() && itM->is_object())
			{
				MaterialComponent& mc = world.AddComponent<MaterialComponent>(id, DirectX::XMFLOAT3(0.7f, 0.7f, 0.7f));
				rttr::instance inst = mc;
				if (!JsonRttr::FromJsonObject(inst, *itM)) return false;
			}

			// SkinnedMesh
			auto itSM = e.find("SkinnedMesh");
			if (itSM != e.end() && itSM->is_object())
			{
				SkinnedMeshComponent tmp;
				rttr::instance instTmp = tmp;
				if (!JsonRttr::FromJsonObject(instTmp, *itSM)) return false;

				if (!tmp.meshAssetPath.empty())
				{
					SkinnedMeshComponent& sm = world.AddComponent<SkinnedMeshComponent>(id, tmp.meshAssetPath);
					sm.instanceAssetPath = tmp.instanceAssetPath;
					static DirectX::XMFLOAT4X4 identityBone = DirectX::XMFLOAT4X4(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1);
					sm.boneMatrices = &identityBone;
					sm.boneCount = 1;
				}
			}

			// SkinnedAnimation
			auto itSA = e.find("SkinnedAnimation");
			if (itSA != e.end() && itSA->is_object())
			{
				SkinnedAnimationComponent& sa = world.AddComponent<SkinnedAnimationComponent>(id);
				rttr::instance inst = sa;
				if (!JsonRttr::FromJsonObject(inst, *itSA)) return false;
			}

			// Socket
			auto itSocket = e.find("Socket");
			if (itSocket != e.end() && itSocket->is_object())
			{
				SocketComponent& sc = world.AddComponent<SocketComponent>(id);
				if (!SocketSerialization::JsonToSocketComponent(*itSocket, sc)) return false;
			}

			// SocketAttachment
			auto itSAc = e.find("SocketAttachment");
			if (itSAc != e.end() && itSAc->is_object())
			{
				SocketAttachmentComponent& sa = world.AddComponent<SocketAttachmentComponent>(id);
				if (auto itGuid = itSAc->find("ownerGuid"); itGuid != itSAc->end())
					sa.ownerGuid = ParseGuid(*itGuid);

				JsonRttr::json copy = *itSAc;
				copy.erase("ownerGuid");
				rttr::instance inst = sa;
				if (!JsonRttr::FromJsonObject(inst, copy)) return false;
			}

			// Camera components
			auto itC = e.find("Camera");
			if (itC != e.end() && itC->is_object())
			{
				CameraComponent& cc = world.AddComponent<CameraComponent>(id);
				rttr::instance inst = cc;
				if (!JsonRttr::FromJsonObject(inst, *itC)) return false;
			}
			auto itCF = e.find("CameraFollow");
			if (itCF != e.end() && itCF->is_object())
			{
				CameraFollowComponent& cf = world.AddComponent<CameraFollowComponent>(id);
				rttr::instance inst = cf;
				if (!JsonRttr::FromJsonObject(inst, *itCF)) return false;
			}
			auto itSpring = e.find("CameraSpringArm");
			if (itSpring != e.end() && itSpring->is_object())
			{
				CameraSpringArmComponent& sa = world.AddComponent<CameraSpringArmComponent>(id);
				rttr::instance inst = sa;
				if (!JsonRttr::FromJsonObject(inst, *itSpring)) return false;
			}
			auto itLA = e.find("CameraLookAt");
			if (itLA != e.end() && itLA->is_object())
			{
				CameraLookAtComponent& la = world.AddComponent<CameraLookAtComponent>(id);
				rttr::instance inst = la;
				if (!JsonRttr::FromJsonObject(inst, *itLA)) return false;
			}
			auto itCS = e.find("CameraShake");
			if (itCS != e.end() && itCS->is_object())
			{
				CameraShakeComponent& cs = world.AddComponent<CameraShakeComponent>(id);
				rttr::instance inst = cs;
				if (!JsonRttr::FromJsonObject(inst, *itCS)) return false;
			}
			auto itCB = e.find("CameraBlend");
			if (itCB != e.end() && itCB->is_object())
			{
				CameraBlendComponent& cb = world.AddComponent<CameraBlendComponent>(id);
				rttr::instance inst = cb;
				if (!JsonRttr::FromJsonObject(inst, *itCB)) return false;
			}
			auto itCI = e.find("CameraInput");
			if (itCI != e.end() && itCI->is_object())
			{
				CameraInputComponent& ci = world.AddComponent<CameraInputComponent>(id);
				rttr::instance inst = ci;
				if (!JsonRttr::FromJsonObject(inst, *itCI)) return false;
			}

			// Lights
			auto itPL = e.find("PointLight");
			if (itPL != e.end() && itPL->is_object())
			{
				PointLightComponent& pl = world.AddComponent<PointLightComponent>(id);
				rttr::instance inst = pl;
				if (!JsonRttr::FromJsonObject(inst, *itPL)) return false;
			}
			auto itSL = e.find("SpotLight");
			if (itSL != e.end() && itSL->is_object())
			{
				SpotLightComponent& sl = world.AddComponent<SpotLightComponent>(id);
				rttr::instance inst = sl;
				if (!JsonRttr::FromJsonObject(inst, *itSL)) return false;
			}
			auto itRL = e.find("RectLight");
			if (itRL != e.end() && itRL->is_object())
			{
				RectLightComponent& rl = world.AddComponent<RectLightComponent>(id);
				rttr::instance inst = rl;
				if (!JsonRttr::FromJsonObject(inst, *itRL)) return false;
			}

			// Physics Components
			auto itRB = e.find("RigidBody");
			if (itRB != e.end() && itRB->is_object())
			{
				Phy_RigidBodyComponent& rb = world.AddComponent<Phy_RigidBodyComponent>(id);
				rttr::instance inst = rb;
				if (!JsonRttr::FromJsonObject(inst, *itRB)) return false;
			}
			auto itCollider = e.find("Collider");
			if (itCollider != e.end() && itCollider->is_object())
			{
				Phy_ColliderComponent& col = world.AddComponent<Phy_ColliderComponent>(id);
				rttr::instance inst = col;
				if (!JsonRttr::FromJsonObject(inst, *itCollider)) return false;
			}
			auto itMeshCollider = e.find("MeshCollider");
			if (itMeshCollider != e.end() && itMeshCollider->is_object())
			{
				Phy_MeshColliderComponent& mc = world.AddComponent<Phy_MeshColliderComponent>(id);
				rttr::instance inst = mc;
				if (!JsonRttr::FromJsonObject(inst, *itMeshCollider)) return false;
			}
			auto itCCT = e.find("CharacterController");
			if (itCCT != e.end() && itCCT->is_object())
			{
				Phy_CCTComponent& cct = world.AddComponent<Phy_CCTComponent>(id);
				rttr::instance inst = cct;
				if (!JsonRttr::FromJsonObject(inst, *itCCT)) return false;
			}
			auto itTerrain = e.find("TerrainHeightField");
			if (itTerrain != e.end() && itTerrain->is_object())
			{
				Phy_TerrainHeightFieldComponent& terrain = world.AddComponent<Phy_TerrainHeightFieldComponent>(id);
				rttr::instance inst = terrain;
				if (!JsonRttr::FromJsonObject(inst, *itTerrain)) return false;
			}
			auto itJoint = e.find("Joint");
			if (itJoint != e.end() && itJoint->is_object())
			{
				Phy_JointComponent& joint = world.AddComponent<Phy_JointComponent>(id);
				rttr::instance inst = joint;
				if (!JsonRttr::FromJsonObject(inst, *itJoint)) return false;
			}

			// Parent 관계는 RestoreEntityFromJson 내부에서 처리하지 않음
			// Undo에서 GUID로 찾아서 연결 (바깥에서 처리)

			// 모든 컴포넌트 복원이 성공했으므로 가드 커밋
			guard.committed = true;
			return true;
		}

		void Execute(World& world, EntityId& selectedEntity) override
		{
			if (selectedEntity == entityId)
				selectedEntity = InvalidEntityId;
			world.DestroyEntity(entityId);
		}

		// GUID로 엔티티 찾기 헬퍼
		static EntityId FindEntityByGuid(World& world, std::uint64_t guid)
		{
			for (auto&& [eid, idc] : world.GetComponents<IDComponent>())
			{
				if (idc.guid == guid)
					return eid;
			}
			return InvalidEntityId;
		}

		void Undo(World& world, EntityId& selectedEntity) override
		{
			// 엔티티 복원
			if (!serializedData.is_null())
			{
				EntityId restoredId = InvalidEntityId;
				if (RestoreEntityFromJson(world, serializedData, restoredId))
				{
					// 복원된 엔티티를 선택
					selectedEntity = restoredId;

					// 루트의 외부 부모 복원 (GUID 기반)
					if (serializedData.contains("_parentGuid"))
					{
						std::uint64_t parentGuid = ParseGuid(serializedData["_parentGuid"]);
						EntityId parent = FindEntityByGuid(world, parentGuid);
						if (parent != InvalidEntityId)
						{
							world.SetParent(restoredId, parent, false);
						}
					}

					// 자식 엔티티들도 재귀적으로 복원하고 부모 관계 설정
					if (childrenData.is_array())
					{
						RestoreChildrenRecursive(world, restoredId, childrenData);
					}
				}
			}
		}

		// 재귀적으로 자식 엔티티들을 복원하는 헬퍼 함수
		static void RestoreChildrenRecursive(World& world, EntityId parentId, const JsonRttr::json& childrenArray)
		{
			if (!childrenArray.is_array()) return;

			for (const auto& childJson : childrenArray)
			{
				EntityId restoredChildId = InvalidEntityId;
				if (RestoreEntityFromJson(world, childJson, restoredChildId))
				{
					// 부모 관계 복원 (keepWorld=false: 로드이므로)
					world.SetParent(restoredChildId, parentId, false);

					// 손자들도 재귀적으로 복원
					auto it = childJson.find("_children");
					if (it != childJson.end() && it->is_array())
					{
						RestoreChildrenRecursive(world, restoredChildId, *it);
					}
				}
			}
		}

		const char* GetDescription() const override
		{
			return description.c_str();
		}

		// Create/Destroy는 ID 변경 문제로 Redo 지원 안 함
		bool SupportsRedo() const override { return false; }
	};

	// 엔티티 이름 변경 명령
	struct SetEntityNameCommand : ICommand
	{
		EntityId entityId;
		std::string oldName;
		std::string newName;
		mutable std::string description;

		SetEntityNameCommand(EntityId id, const std::string& old, const std::string& nnew)
			: entityId(id), oldName(old), newName(nnew)
		{
			description = "Rename Entity";
		}

		void Execute(World& world, EntityId& selectedEntity) override
		{
			world.SetEntityName(entityId, newName);
		}

		void Undo(World& world, EntityId& selectedEntity) override
		{
			world.SetEntityName(entityId, oldName);
		}

		const char* GetDescription() const override
		{
			return description.c_str();
		}
	};

	// Transform 변경 명령
	struct TransformCommand : ICommand
	{
		EntityId entityId;
		struct TransformData
		{
			DirectX::XMFLOAT3 position;
			DirectX::XMFLOAT3 rotation;
			DirectX::XMFLOAT3 scale;
			bool enabled;
			bool visible;
		};
		TransformData oldData;
		TransformData newData;
		mutable std::string description;

		TransformCommand(EntityId id, const TransformData& old, const TransformData& nnew)
			: entityId(id), oldData(old), newData(nnew)
		{
			description = "Transform Change";
		}

		void Execute(World& world, EntityId& selectedEntity) override
		{
			if (auto* transform = world.GetComponent<TransformComponent>(entityId))
			{
				transform->position = newData.position;
				transform->rotation = newData.rotation;
				transform->scale = newData.scale;
				transform->enabled = newData.enabled;
				transform->visible = newData.visible;
				world.MarkTransformDirty(entityId);
			}
		}

		void Undo(World& world, EntityId& selectedEntity) override
		{
			if (auto* transform = world.GetComponent<TransformComponent>(entityId))
			{
				transform->position = oldData.position;
				transform->rotation = oldData.rotation;
				transform->scale = oldData.scale;
				transform->enabled = oldData.enabled;
				transform->visible = oldData.visible;
				world.MarkTransformDirty(entityId);
			}
		}

		const char* GetDescription() const override
		{
			return description.c_str();
		}
	};

	// 부모 설정 명령
	struct SetParentCommand : ICommand
	{
		EntityId childId;
		EntityId oldParent;
		EntityId newParent;
		TransformComponent oldLocal;
		TransformComponent newLocal;
		bool hasLocalSnapshots;
		mutable std::string description;

		// 하이라키 드래그용: Transform 스냅샷 포함
		SetParentCommand(EntityId child, EntityId oldP, EntityId newP,
			const TransformComponent& oldT, const TransformComponent& newT)
			: childId(child), oldParent(oldP), newParent(newP),
			oldLocal(oldT), newLocal(newT), hasLocalSnapshots(true)
		{
			description = "Set Parent";
		}

		// 레거시 호환용: Transform 스냅샷 없음 (keepWorld=false로 동작)
		SetParentCommand(EntityId child, EntityId oldP, EntityId newP)
			: childId(child), oldParent(oldP), newParent(newP), hasLocalSnapshots(false)
		{
			description = "Set Parent";
		}

		void Execute(World& world, EntityId& selectedEntity) override
		{
			if (hasLocalSnapshots)
			{
				// 저장된 로컬 값 사용
				world.SetParent(childId, newParent, false);
				if (auto* t = world.GetComponent<TransformComponent>(childId))
				{
					*t = newLocal;
				}
			}
			else
			{
				// 레거시: keepWorld=false
				world.SetParent(childId, newParent, false);
			}
		}

		void Undo(World& world, EntityId& selectedEntity) override
		{
			if (hasLocalSnapshots)
			{
				// 저장된 로컬 값 사용
				world.SetParent(childId, oldParent, false);
				if (auto* t = world.GetComponent<TransformComponent>(childId))
				{
					*t = oldLocal;
				}
			}
			else
			{
				// 레거시: keepWorld=false
				world.SetParent(childId, oldParent, false);
			}
		}

		const char* GetDescription() const override
		{
			return description.c_str();
		}
	};
}
