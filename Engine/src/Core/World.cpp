#include "Core/World.h"
#include "Core/GameObject.h"
#include "Core/ScriptFactory.h"
#include "Core/ThreadSafety.h"
#include "Components/IDComponent.h"
#include "Components/DebugDrawBoxComponent.h"
#include "Components/PostProcessVolumeComponent.h"
#include "Components/TransformComponent.h"
#include <random>
#include <algorithm>
#include <Game/FbxImporter.h>

namespace Alice {
	// GUID 생성 함수
	static std::uint64_t NewGuid()
	{
		static std::mt19937_64 rng{ std::random_device{}() };
		static std::uniform_int_distribution<std::uint64_t> dist;
		return dist(rng);
	}

	void World::Clear()
	{
		ThreadSafety::AssertMainThread();
		// 0. Clear 전 콜백 호출 (Engine에서 물리 시스템 정리)
		if (m_onBeforeClear)
		{
			m_onBeforeClear();
		}

		// 1. 스크립트 컴포넌트들의 정리(Cleanup) 함수 호출
		RemoveAllScript();
		// 2. 모든 컴포넌트 컨테이너 비우기 (메모리 해제)

		// 1.5 물리 월드 정리
		// (Clear 전 콜백에서 PhysicsSystem 정리가 이미 완료되었을 수 있음)
		m_physicsWorld.reset();

		// 2. 엔티티 이름 비우기
		m_names.clear();

		// 모든 엔진 컴포넌트 저장소 클리어
		for (auto& [typeIndex, storage] : m_engineStorages)
		{
			storage->Clear();
		}

		m_scripts.clear();
		m_delayedDestructions.clear();
		m_entityGenerations.clear();
		m_transformDirty.clear();
		m_worldMatrixCache.clear();
		InvalidateChildrenCache();
		m_frameCombatHits = nullptr;
		m_scriptCombatEnabled = false;

		// 3. World Epoch 증가 (씬 전환 시 이전 userData 무효화)
		//    EntityId는 재사용하지 않고 단조 증가하여 안전성 보장
		//    worldEpoch를 증가시켜 이전 씬의 PhysX userData를 무효화
		++m_worldEpoch;

		// 주의: m_nextEntityId는 리셋하지 않음 (EntityId 재사용 방지)
		//       대신 worldEpoch를 증가시켜 userData 매칭 안전성 보장
	}

	EntityId World::ExtractEntityIdFromUserData(void* userData) const
	{
		if (!userData) return InvalidEntityId;

		const uint64_t combined = static_cast<uint64_t>(reinterpret_cast<std::uintptr_t>(userData));
		const uint64_t userDataEpoch = (combined >> 32) & 0xFFFFFFFFull;

		// worldEpoch 검증: 이전 씬의 userData는 무시
		if (userDataEpoch != m_worldEpoch)
		{
			return InvalidEntityId;
		}

		// +1 오프셋 제거 (인코딩 시 +1을 했으므로)
		const uint64_t encodedEntityId = combined & 0xFFFFFFFFull;
		if (encodedEntityId == 0) return InvalidEntityId; // 오프셋 후 0이면 원래 InvalidEntityId
		return static_cast<EntityId>(encodedEntityId - 1u);
	}
	EntityId World::CreateEntity()
	{
		// 간단한 증가형 ID를 사용합니다.
		const EntityId newId = m_nextEntityId++;
		// SlotMap: 새로 생성된 엔티티의 generation을 0으로 초기화합니다.
		m_entityGenerations[newId] = 0;

		// IDComponent 자동 추가 (GUID 할당)
		auto& idComp = AddComponent<IDComponent>(newId);
		idComp.guid = NewGuid();

		return newId;
	}

	void World::DestroyEntity(EntityId id)
	{
		if (id == InvalidEntityId)
			return;

		// 지연 파괴 예약이 있으면 제거
		m_delayedDestructions.erase(id);

		// 부모-자식 관계 정리: 자식들을 재귀적으로 삭제
		auto* transform = GetComponent<TransformComponent>(id);
		if (transform)
		{
			// 이 엔티티를 부모로 가지는 자식들을 찾아 재귀적으로 삭제
			std::vector<EntityId> children = GetChildren(id);
			for (EntityId child : children)
			{
				// 자식도 재귀적으로 삭제 (자식의 자식들도 함께 삭제됨)
				DestroyEntity(child);
			}
		}

		// SlotMap: 엔티티가 파괴될 때 generation을 증가시켜 이전 참조를 무효화합니다.
		auto genIt = m_entityGenerations.find(id);
		if (genIt != m_entityGenerations.end())
		{
			genIt->second++; // generation 증가
		}

		m_names.erase(id);

		// Transform 캐시 제거
		m_transformDirty.erase(id);
		m_worldMatrixCache.erase(id);

		// children 캐시 무효화
		InvalidateChildrenCache();

		// 스크립트 OnDisable/OnDestroy를 컴포넌트 제거 전에 호출
		// (스크립트가 OnDestroy에서 GetComponent 등을 호출할 수 있으므로)
		auto it = m_scripts.find(id);
		if (it != m_scripts.end()) {
			for (auto& sc : it->second)
			{
				if (!sc.instance)
					continue;
				sc.instance->OnDisable();
				sc.instance->OnDestroy();
			}
			m_scripts.erase(it);
		}

		// 모든 엔진 컴포넌트 저장소에서 해당 엔티티 제거
		for (auto& [typeIndex, storage] : m_engineStorages)
		{
			storage->Remove(id);
		}
	}

	GameObject World::CreateGameObject()
	{
		EntityId id = CreateEmpty();
		return GameObject(this, id, nullptr);
	}

	void World::DestroyGameObject(GameObject gameObject)
	{
		if (gameObject.IsValid())
			DestroyEntity(gameObject.id());
	}

	GameObject World::FindGameObject(const std::string& name)
	{
		// 이름으로 엔티티 검색 (선형 검색)
		// 엔티티가 많아지면 별도 Map<String, EntityId> 관리 권장
		for (const auto& [id, entityName] : m_names)
		{
			if (entityName == name)
			{
				// GameObject 생성 (ScriptServices는 nullptr로 전달)
				// 스크립트에서 사용할 때는 IScript::gameObject()를 통해 ScriptServices가 포함된 GameObject를 얻을 수 있음
				return GameObject(this, id, nullptr);
			}
		}
		// 찾지 못한 경우 빈 GameObject 반환 (IsValid() == false)
		return GameObject();
	}

	EntityId World::FindEntityByGuid(std::uint64_t guid) const
	{
		if (guid == 0)
			return InvalidEntityId;

		for (const auto& [eid, idc] : GetComponents<IDComponent>())
		{
			if (idc.guid == guid)
				return eid;
		}
		return InvalidEntityId;
	}

	void World::SetEntityName(EntityId id, const std::string& name)
	{
		if (id == InvalidEntityId)
			return;
		if (name.empty()) {
			m_names.erase(id);
			return;
		}
		m_names[id] = name;
	}

	std::string World::GetEntityName(EntityId id) const
	{
		auto it = m_names.find(id);
		if (it == m_names.end())
			return {};
		return it->second;
	}

	ScriptComponent& World::AddScript(EntityId id, const std::string& scriptName)
	{
		ScriptComponent comp{};
		comp.scriptName = scriptName;
		comp.instance = ScriptFactory::Create(scriptName.c_str());
		if (comp.instance) comp.instance->SetContext(this, id);

		m_scripts[id].push_back(std::move(comp));
		return m_scripts[id].back();
	}

	std::vector<ScriptComponent>* World::GetScripts(EntityId id)
	{
		auto it = m_scripts.find(id);
		if (it == m_scripts.end())
			return nullptr;
		return &it->second;
	}

	const std::vector<ScriptComponent>* World::GetScripts(EntityId id) const
	{
		auto it = m_scripts.find(id);
		if (it == m_scripts.end())
			return nullptr;
		return &it->second;
	}

	void World::RemoveScript(EntityId id, std::size_t index)
	{
		auto it = m_scripts.find(id);
		if (it == m_scripts.end())
			return;

		auto& list = it->second;
		if (index >= list.size())
			return;

		if (list[index].instance) {
			list[index].instance->OnDisable();
			list[index].instance->OnDestroy();
		}

		list.erase(list.begin() + (std::ptrdiff_t)index);
		if (list.empty())
			m_scripts.erase(it);
	}

	void World::RemoveAllScript() {
		for (auto& [id, list] : m_scripts) {
			for (auto& scriptComp : list) {
				if (!scriptComp.instance)
					continue;
				scriptComp.instance->OnDisable();
				scriptComp.instance->OnDestroy();
			}
		}
		m_scripts.clear();
	}

	EntityId World::GetMainCameraEntityId() {
		// Sparse Set 기반: 카메라 컴포넌트 뷰에서 첫 번째 EntityId를 반환
		auto cameras = GetComponents<CameraComponent>();
		if (cameras.empty())
			return InvalidEntityId;
		return cameras.begin()->first;
	}

	void World::ScheduleDelayedDestruction(EntityId id, float delay)
	{
		if (id == InvalidEntityId || delay <= 0.0f)
			return;

		// 이미 예약된 파괴가 있으면 더 짧은 시간으로 업데이트
		auto it = m_delayedDestructions.find(id);
		if (it != m_delayedDestructions.end())
		{
			it->second = std::min(it->second, delay);
		}
		else
		{
			m_delayedDestructions[id] = delay;
		}
	}

	void World::UpdateDelayedDestruction(float deltaTime)
	{
		// 역순으로 순회하여 삭제 시 iterator 무효화 방지
		std::vector<EntityId> toDestroy;
		toDestroy.reserve(m_delayedDestructions.size());

		for (auto& [id, remainingTime] : m_delayedDestructions)
		{
			remainingTime -= deltaTime;
			if (remainingTime <= 0.0f)
			{
				toDestroy.push_back(id);
			}
		}

		// 시간이 지난 엔티티들을 파괴
		for (EntityId id : toDestroy)
		{
			m_delayedDestructions.erase(id);
			DestroyEntity(id);
		}
	}

	std::uint32_t World::GetEntityGeneration(EntityId id) const
	{
		auto it = m_entityGenerations.find(id);
		if (it == m_entityGenerations.end())
			return 0; // 존재하지 않는 엔티티는 generation 0
		return it->second;
	}

	bool World::IsEntityValid(EntityId id, std::uint32_t generation) const
	{
		if (id == InvalidEntityId)
			return false;

		auto it = m_entityGenerations.find(id);
		if (it == m_entityGenerations.end())
			return false; // 엔티티가 존재하지 않음

		// generation이 일치하면 유효, 다르면 무효 (파괴 후 재사용된 경우)
		return it->second == generation;
	}

	EntityId World::CreateEmpty()
	{
		EntityId e = CreateEntity();
		auto& t = AddComponent<TransformComponent>(e);
		t.SetPosition(0.0f, 0.0f, 0.0f)
			.SetScale(1.0f, 1.0f, 1.0f);
		SetEntityName(e, "GameObject" + std::to_string((std::uint32_t)e));
		return e;
	}

	EntityId World::CreateCube()
	{
		EntityId e = CreateEntity();
		auto& t = AddComponent<TransformComponent>(e);
		t.SetPosition(0.0f, 0.0f, 0.0f)
			.SetScale(1.0f, 1.0f, 1.0f);

		// 기본 회색 머티리얼을 함께 추가합니다.
		DirectX::XMFLOAT3 defaultColor(0.7f, 0.7f, 0.7f);
		AddComponent<MaterialComponent>(e, defaultColor);

		SetEntityName(e, "Entity" + std::to_string((std::uint32_t)e));
		return e;
	}

	EntityId World::CreateCamera()
	{
		const bool hasCamera = GetComponents<CameraComponent>().empty();
		const std::uint32_t camIndex = static_cast<std::uint32_t>(GetComponents<CameraComponent>().size() + 1);

		EntityId e = CreateEntity();
		auto& t = AddComponent<TransformComponent>(e);
		t.position = { 0.0f, 2.0f, -5.0f };
		t.rotation = { 0.0f, 0.0f, 0.0f };
		t.scale = { 1.0f, 1.0f, 1.0f };

		auto& c = AddComponent<CameraComponent>(e);
		c.primary = !hasCamera;

		SetEntityName(e, "Camera" + std::to_string(camIndex));
		return e;	
	}

	EntityId World::CreatePointLight()
	{
		EntityId e = CreateEntity();
		AddComponent<TransformComponent>(e);
		AddComponent<PointLightComponent>(e);
		SetEntityName(e, "Point Light");
		return e;
	}

	EntityId World::CreateSpotLight()
	{
		EntityId e = CreateEntity();
		AddComponent<TransformComponent>(e);
		AddComponent<SpotLightComponent>(e);
		SetEntityName(e, "Spot Light");
		return e;
	}

	EntityId World::CreateRectLight()
	{
		EntityId e = CreateEntity();
		AddComponent<TransformComponent>(e);
		AddComponent<RectLightComponent>(e);
		SetEntityName(e, "Rect Light");
		return e;
	}




	//========================================================
	// ���� ���� �Լ�
	void World::SetPhysicsWorld(std::shared_ptr<IPhysicsWorld> physicsWorld) { m_physicsWorld = std::move(physicsWorld); }
	IPhysicsWorld* World::GetPhysicsWorld() { return m_physicsWorld.get(); }
	const IPhysicsWorld* World::GetPhysicsWorld() const { return m_physicsWorld.get(); }
	//========================================================

	// Transform 행렬 계산 헬퍼 (EditorCore와 동일한 로직)
	namespace
	{
		inline DirectX::XMMATRIX BuildRotYPR_Rad(const DirectX::XMFLOAT3& rotation)
		{
			return DirectX::XMMatrixRotationRollPitchYaw(rotation.x, rotation.y, rotation.z);
		}

		inline DirectX::XMMATRIX BuildLocalMatrix(const TransformComponent& transform)
		{
			DirectX::XMMATRIX S = DirectX::XMMatrixScaling(transform.scale.x, transform.scale.y, transform.scale.z);
			DirectX::XMMATRIX R = BuildRotYPR_Rad(transform.rotation);
			DirectX::XMMATRIX T = DirectX::XMMatrixTranslation(transform.position.x, transform.position.y, transform.position.z);
			return S * R * T;
		}

		// 내부 헬퍼 함수 (SetParent 등에서 사용)
		inline DirectX::XMMATRIX ComputeWorldMatrix_Internal(const World& world, EntityId entityId)
		{
			std::vector<DirectX::XMMATRIX> matrixStack;
			EntityId currentId = entityId;

			while (currentId != InvalidEntityId)
			{
				const TransformComponent* t = world.GetComponent<TransformComponent>(currentId);
				if (t)
				{
					DirectX::XMMATRIX localMatrix = BuildLocalMatrix(*t);
					matrixStack.push_back(localMatrix);
					currentId = t->parent;
				}
				else
				{
					break;
				}
			}

			DirectX::XMMATRIX worldMatrix = DirectX::XMMatrixIdentity();
			for (const auto& m : matrixStack)
			{
				worldMatrix = worldMatrix * m;
			}

			return worldMatrix;
		}
	}

	// Transform 행렬 계산 (공용 API)
	DirectX::XMMATRIX World::ComputeWorldMatrix(EntityId entityId) const
	{
		if (entityId == InvalidEntityId)
			return DirectX::XMMatrixIdentity();
		
		// 캐시 확인
		auto cacheIt = m_worldMatrixCache.find(entityId);
		if (cacheIt != m_worldMatrixCache.end())
		{
			// dirty 플래그 확인
			auto dirtyIt = m_transformDirty.find(entityId);
			if (dirtyIt == m_transformDirty.end() || !dirtyIt->second)
			{
				// 캐시가 유효하면 반환
				return DirectX::XMLoadFloat4x4(&cacheIt->second);
			}
		}
		
		// 캐시 미스 또는 dirty: 재계산
		DirectX::XMMATRIX worldMatrix = ComputeWorldMatrix_Internal(*this, entityId);
		
		// 캐시에 저장 (const 함수이지만 mutable 멤버이므로 가능)
		DirectX::XMFLOAT4X4 cachedMatrix;
		DirectX::XMStoreFloat4x4(&cachedMatrix, worldMatrix);
		m_worldMatrixCache[entityId] = cachedMatrix;		
		m_transformDirty[entityId] = false; // dirty 플래그 클리어
		
		return worldMatrix;
	}
	
	void World::MarkTransformDirty(EntityId entityId)
	{
		if (entityId == InvalidEntityId)
			return;
		
		m_transformDirty[entityId] = true;
		
		// 자식은 children 캐시 없이 Transform 스토리지 스캔 (SetParent 시 캐시 타이밍 이슈 방지)
		const auto& transforms = GetComponents<TransformComponent>();
		for (const auto& [eid, tr] : transforms)
		{
			if (tr.parent == entityId)
				MarkTransformDirty(eid);
		}
	}
	
	void World::UpdateTransformMatrices()
	{
		// dirty 플래그가 있는 모든 엔티티의 월드행렬을 재계산
		std::vector<EntityId> dirtyEntities;
		dirtyEntities.reserve(m_transformDirty.size());
		
		for (const auto& [entityId, isDirty] : m_transformDirty)
		{
			if (isDirty)
			{
				dirtyEntities.push_back(entityId);
			}
		}
		
		// 각 dirty 엔티티의 월드행렬 재계산
		for (EntityId entityId : dirtyEntities)
		{
			DirectX::XMMATRIX worldMatrix = ComputeWorldMatrix_Internal(*this, entityId);
			DirectX::XMFLOAT4X4 cachedMatrix;
			DirectX::XMStoreFloat4x4(&cachedMatrix, worldMatrix);
			m_worldMatrixCache[entityId] = cachedMatrix;
			m_transformDirty[entityId] = false;
		}
	}

	inline DirectX::XMFLOAT3 QuaternionToYPR_Rad(DirectX::FXMVECTOR q)
	{
		DirectX::XMFLOAT4 qq;
		DirectX::XMStoreFloat4(&qq, q);
		const float x = qq.x, y = qq.y, z = qq.z, w = qq.w;

		float sinp = 2.0f * (w * x - y * z);
		float pitch = (std::abs(sinp) >= 1.0f)
			? std::copysign(DirectX::XM_PIDIV2, sinp)
			: std::asin(sinp);

		float siny_cosp = 2.0f * (w * y + x * z);
		float cosy_cosp = 1.0f - 2.0f * (x * x + y * y);
		float yaw = std::atan2(siny_cosp, cosy_cosp);

		float sinr_cosp = 2.0f * (w * z + x * y);
		float cosr_cosp = 1.0f - 2.0f * (x * x + z * z);
		float roll = std::atan2(sinr_cosp, cosr_cosp);

		return DirectX::XMFLOAT3(pitch, yaw, roll);
	}

	inline bool DecomposeLocalMatrix(const DirectX::XMMATRIX& localMatrix, DirectX::XMFLOAT3& position, DirectX::XMFLOAT3& rotation, DirectX::XMFLOAT3& scale)
	{
		DirectX::XMVECTOR s, q, t;
		if (!DirectX::XMMatrixDecompose(&s, &q, &t, localMatrix))
			return false;

		DirectX::XMStoreFloat3(&position, t);
		DirectX::XMStoreFloat3(&scale, s);
		rotation = QuaternionToYPR_Rad(q);
		return true;
	}


	// 부모-자식 관계 관리
	void World::SetParent(EntityId child, EntityId parent, bool keepWorld)
	{
		if (child == InvalidEntityId)
			return;

		auto* childTransform = GetComponent<TransformComponent>(child);
		if (!childTransform)
			return;

		EntityId oldParent = childTransform->parent;
		if (oldParent == parent)
			return;

		// 순환 참조 방지: parent가 child의 자식인지 확인
		if (parent != InvalidEntityId)
		{
			EntityId checkParent = parent;
			while (checkParent != InvalidEntityId)
			{
				if (checkParent == child)
					return; // 순환 참조 감지, 무시

				auto* checkTransform = GetComponent<TransformComponent>(checkParent);
				if (!checkTransform)
					break;
				checkParent = checkTransform->parent;
			}
		}

		DirectX::XMMATRIX childWorld = DirectX::XMMatrixIdentity();
		DirectX::XMMATRIX newParentWorldInv = DirectX::XMMatrixIdentity();

		if (keepWorld)
		{
			// 현재 월드 위치 계산
			childWorld = ComputeWorldMatrix_Internal(*this, child);

			// 새 부모의 월드 행렬 계산
			DirectX::XMMATRIX newParentWorld = (parent != InvalidEntityId)
				? ComputeWorldMatrix_Internal(*this, parent)
				: DirectX::XMMatrixIdentity();

			// 역행렬 계산
			DirectX::XMVECTOR det;
			newParentWorldInv = DirectX::XMMatrixInverse(&det, newParentWorld);
		}

		// children 캐시 무효화
		InvalidateChildrenCache();

		// 새 부모 설정
		childTransform->parent = parent;
		
		// Transform 변경: child와 모든 자식을 dirty로 표시
		MarkTransformDirty(child);

		if (keepWorld)
		{
			// 새 로컬 행렬 계산: NewLocal = OldWorld * inverse(NewParentWorld)
			DirectX::XMMATRIX newLocal = childWorld * newParentWorldInv;
			DirectX::XMFLOAT3 p, r, s;
			if (DecomposeLocalMatrix(newLocal, p, r, s))
			{
				childTransform->position = p;
				childTransform->rotation = r;
				childTransform->scale = s;
			}
		}
	}

	EntityId World::GetParent(EntityId child) const
	{
		const auto* transform = GetComponent<TransformComponent>(child);
		if (!transform)
			return InvalidEntityId;
		return transform->parent;
	}

	std::vector<EntityId> World::GetChildren(EntityId parent) const
	{
		// 캐시 확인
		auto it = m_children.find(parent);
		if (it != m_children.end())
		{
			return it->second;
		}

		// 캐시 미스: 전체 스캔하여 캐시 구축
		std::vector<EntityId> children;
		const auto& transforms = GetComponents<TransformComponent>();
		for (const auto& [entityId, transform] : transforms)
		{
			if (transform.parent == parent)
			{
				children.push_back(entityId);
			}
		}

		// 캐시에 저장
		m_children[parent] = children;
		return children;
	}

	std::vector<EntityId> World::GetRootEntities() const
	{
		return GetChildren(InvalidEntityId);
	}

	void World::SetLocalPosition(EntityId id, const DirectX::XMFLOAT3& position)
	{
		if (auto* t = GetComponent<TransformComponent>(id))
		{
			t->position = position;
			MarkTransformDirty(id);
		}
	}

	void World::SetLocalRotation(EntityId id, const DirectX::XMFLOAT3& rotationRad)
	{
		if (auto* t = GetComponent<TransformComponent>(id))
		{
			t->rotation = rotationRad;
			MarkTransformDirty(id);
		}
	}

	void World::SetLocalScale(EntityId id, const DirectX::XMFLOAT3& scale)
	{
		if (auto* t = GetComponent<TransformComponent>(id))
		{
			t->scale = scale;
			MarkTransformDirty(id);
		}
	}

	void World::SetTransformEnabled(EntityId id, bool enabled)
	{
		if (auto* t = GetComponent<TransformComponent>(id))
		{
			t->enabled = enabled;
			MarkTransformDirty(id);
		}
	}

	void World::UpdatePostProcessVolumeDebugBox(EntityId id, const PostProcessVolumeComponent& volume)
	{
		// DebugDrawBoxComponent가 없으면 추가
		auto* debugBox = GetComponent<DebugDrawBoxComponent>(id);
		if (!debugBox)
		{
			debugBox = &AddComponent<DebugDrawBoxComponent>(id);
		}

		if (volume.unbound)
		{
			// Unbound 볼륨은 그리지 않음
			debugBox->enabled = false;
		}
		else
		{
			// 로컬 공간 bounds 설정 (DebugDrawComponentSystem에서 Transform을 자동 적용)
			// 보간이 적용되는 전체 범위를 그리기: bound + blendRadius
			// bound는 보간 시작 기준점(박스 표면), bound + blendRadius까지 보간이 적용됨
			float blendRadius = volume.blendRadius;
			float halfSize = (volume.bound * 0.5f) + blendRadius;
			
			debugBox->boundsMin = DirectX::XMFLOAT3(-halfSize, -halfSize, -halfSize);
			debugBox->boundsMax = DirectX::XMFLOAT3(halfSize, halfSize, halfSize);
			debugBox->enabled = true;
		}

		// 색상 설정
		float alpha = 0.5f + volume.blendWeight * 0.5f; // 0.5 ~ 1.0
		debugBox->color = DirectX::XMFLOAT4(0.0f, 1.0f, 0.0f, alpha);
		debugBox->depthTest = false;
	}

} // namespace Alice
