#pragma once

#include <unordered_map>
#include <vector>
#include <string>
#include <type_traits> // for std::is_same_v
#include <cstdint>
#include <memory>
#include <utility>
#include <typeindex>
#include <functional>

#include "Runtime/ECS/Entity.h"
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/Components/ScriptComponent.h"
#include "Runtime/ECS/Components/ComponentStorage.h"

// 컴포넌트 헤더들
#include "Runtime/ECS/Components/IDComponent.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Rendering/Components/MaterialComponent.h"
#include "Runtime/Rendering/Components/SkinnedMeshComponent.h"
#include "Runtime/Rendering/Components/SkinnedAnimationComponent.h"
#include "Runtime/Gameplay/Animation/AdvancedAnimationComponent.h"
#include "Runtime/Audio/Components/SoundBoxComponent.h"
#include "Runtime/Audio/Components/AudioListenerComponent.h"
#include "Runtime/Audio/Components/AudioSourceComponent.h"
#include "Runtime/Rendering/Components/CameraComponent.h"
#include "Runtime/Rendering/Components/CameraFollowComponent.h"
#include "Runtime/Rendering/Components/CameraSpringArmComponent.h"
#include "Runtime/Rendering/Components/CameraLookAtComponent.h"
#include "Runtime/Rendering/Components/CameraShakeComponent.h"
#include "Runtime/Rendering/Components/CameraBlendComponent.h"
#include "Runtime/Rendering/Components/CameraInputComponent.h"
#include "Runtime/Rendering/Components/PointLightComponent.h"
#include "Runtime/Rendering/Components/SpotLightComponent.h"
#include "Runtime/Rendering/Components/RectLightComponent.h"
#include "Runtime/Rendering/Components/PostProcessVolumeComponent.h"
#include "Runtime/Rendering/Components/EffectComponent.h"
#include "Runtime/Rendering/Components/TrailEffectComponent.h"
#include "Runtime/Rendering/Components/ComputeEffectComponent.h"
#include "Runtime/Gameplay/Sockets/SocketAttachmentComponent.h"
#include "Runtime/Gameplay/Combat/HurtboxComponent.h"
#include "Runtime/Gameplay/Combat/WeaponTraceComponent.h"
#include "Runtime/Gameplay/Combat/HealthComponent.h"
#include "Runtime/Gameplay/Combat/AttackDriverComponent.h"
#include "Runtime/Gameplay/Combat/CombatHitEvent.h"


// 물리 컴포넌트들
#include "Runtime/Physics/Components/Phy_RigidBodyComponent.h"
#include "Runtime/Physics/Components/Phy_ColliderComponent.h"
#include "Runtime/Physics/Components/Phy_MeshColliderComponent.h"
#include "Runtime/Physics/Components/Phy_SettingsComponent.h"
#include "Runtime/Physics/Components/Phy_TerrainHeightFieldComponent.h"
#include "Runtime/Physics/Components/Phy_CCTComponent.h"
#include "Runtime/Physics/Components/Phy_JointComponent.h"

class IPhysicsWorld; // 물리 인터페이스 전방선언

namespace Alice
{
    class GameObject;

    class World
    {
    public:
        World() = default;

        void Clear();
        EntityId CreateEntity();
        void DestroyEntity(EntityId id);

        GameObject CreateGameObject();
        void DestroyGameObject(GameObject gameObject);

        // ==== 유틸리티 ====
        GameObject FindGameObject(const std::string& name);
        EntityId FindEntityByGuid(std::uint64_t guid) const;
        void SetEntityName(EntityId id, const std::string& name);
        std::string GetEntityName(EntityId id) const;
        
        // ==== 부모-자식 관계 관리 ====
        /// 엔티티의 부모를 설정합니다. 순환 참조를 방지합니다.
        /// @param keepWorld true면 월드 위치를 유지하고 로컬을 재계산, false면 관계만 변경
        void SetParent(EntityId child, EntityId parent, bool keepWorld = false);
        /// 엔티티의 부모를 가져옵니다. InvalidEntityId면 부모 없음
        EntityId GetParent(EntityId child) const;
        /// 엔티티의 모든 자식을 가져옵니다.
        std::vector<EntityId> GetChildren(EntityId parent) const;
        /// 루트 엔티티들(부모가 없는 엔티티들)을 가져옵니다.
        std::vector<EntityId> GetRootEntities() const;

        // ==== Transform 변경 API (스크립트/로직은 여기 경유 권장 — dirty 자동 반영) ====
        void SetLocalPosition(EntityId id, const DirectX::XMFLOAT3& position);
        void SetLocalRotation(EntityId id, const DirectX::XMFLOAT3& rotationRad);
        void SetLocalScale(EntityId id, const DirectX::XMFLOAT3& scale);
        void SetTransformEnabled(EntityId id, bool enabled);

        // ==== 게임 오브젝트 생성 헬퍼 ====
        /// 빈 게임 오브젝트를 생성합니다 (Transform만 가짐)
        EntityId CreateEmpty();
        
		/// 큐브 게임 오브젝트를 생성합니다 (Transform + Material)
		EntityId CreateCube();
        
        /// 카메라 게임 오브젝트를 생성합니다 (Transform + Camera)
        EntityId CreateCamera();

        /// 포인트 라이트 게임 오브젝트를 생성합니다 (Transform + PointLight)
        EntityId CreatePointLight();

        /// 스폿 라이트 게임 오브젝트를 생성합니다 (Transform + SpotLight)
        EntityId CreateSpotLight();

        /// 사각형 라이트 게임 오브젝트를 생성합니다 (Transform + RectLight)
        EntityId CreateRectLight();

        // ==== 제네릭 컴포넌트 관리 시스템 ====
        // 컴포넌트 타입 T에 따라 올바른 Map을 자동으로 찾아줍니다.

        /// 컴포넌트 추가 (기존 데이터가 있으면 덮어쓰거나 반환)
        /// 사용법: world.AddComponent<TransformComponent>(id).SetPosition(0,0,0);
        template <typename T, typename... Args>
        T& AddComponent(EntityId id, Args&&... args)
        {
            // 1. 유저 스크립트인 경우 (IScript 상속 여부 확인)
            if constexpr (std::is_base_of_v<IScript, T>)
            {
                // unique_ptr 생성
                auto instance = std::make_unique<T>(std::forward<Args>(args)...);

                // 반환값 저장을 위해 Raw Pointer 확보 (move 후에는 instance가 null이 됨)
                T* rawPtr = instance.get();

                // 컨테이너 생성 및 데이터 채우기
                ScriptComponent newScriptComp{};
                // RTTR 이름 사용. "Alice::" 접두사 제거하여 ScriptFactory 등록 키(REGISTER_SCRIPT)와 일치시킴.
                std::string scriptName = rttr::type::get<T>().get_name().to_string();
                if (scriptName.size() > 6 && scriptName.compare(0, 6, "Alice::") == 0)
                    scriptName = scriptName.substr(6);
                newScriptComp.scriptName = std::move(scriptName);
                newScriptComp.instance = std::move(instance); // 소유권 이전

                // 초기화 루틴
                newScriptComp.instance->SetContext(this, id);

                // 월드 데이터에 등록 (Move)
                m_scripts[id].push_back(std::move(newScriptComp));

                // 저장해둔 포인터 반환
                return *rawPtr;
            }
            else
            {
                auto& storage = GetStorage<T>();
                T* result = nullptr;
                if constexpr (std::is_default_constructible_v<T> && sizeof...(Args) == 0)
                {
                    // 기본 생성자만 호출
                    T defaultComp{};
                    result = &storage.Add(id, std::move(defaultComp));
                }
                else
                {
                    // 인자가 있는 경우 생성 후 추가
                    T newComp(std::forward<Args>(args)...);
                    result = &storage.Add(id, std::move(newComp));
                }
                
                // TransformComponent 추가/제거 시 children 캐시 무효화 및 Transform dirty 마킹
                if constexpr (std::is_same_v<T, TransformComponent>)
                {
                    InvalidateChildrenCache();
                    MarkTransformDirty(id);
                }
                
                // PostProcessVolumeComponent 추가 시 DebugDrawBoxComponent도 자동 추가
                if constexpr (std::is_same_v<T, PostProcessVolumeComponent>)
                {
                    UpdatePostProcessVolumeDebugBox(id, *result);
                }
                
                return *result;
            }
        }

        /// 컴포넌트 가져오기 (없으면 nullptr)
        /// 사용법: auto* tr = world.GetComponent<TransformComponent>(id);
        template <typename T>
        T* GetComponent(EntityId id)
        {
            // T가 유저 스크립트인 경우. IScript를 상속받았으면 유저가 만든 스크립트임
            if constexpr (std::is_base_of_v<IScript, T>)
            {
                auto it = m_scripts.find(id);
                if (it == m_scripts.end()) return nullptr;

                // 해당 엔티티에 붙은 모든 스크립트를 순회하며 타입 검사
                for (auto& scriptComp : it->second)
                {
                    // IScript* -> MyCustomScript* 로 변환 시도
                    // dynamic_cast는 실패 시 nullptr를 반환함
                    if (scriptComp.instance)
                    {
                        T* casted = dynamic_cast<T*>(scriptComp.instance.get());
                        if (casted) return casted;
                    }
                }
                return nullptr;
            }
            else
            {
                auto& storage = GetStorage<T>();
                return storage.Get(id);
            }
        }

        /// const 버전 가져오기
        template <typename T>
        const T* GetComponent(EntityId id) const
        {
            if constexpr (std::is_base_of_v<IScript, T>)
            {
                auto it = m_scripts.find(id);
                if (it == m_scripts.end()) return nullptr;

                for (const auto& scriptComp : it->second)
                {
                    if (scriptComp.instance)
                    {
                        const T* casted = dynamic_cast<const T*>(scriptComp.instance.get());
                        if (casted) return casted;
                    }
                }
                return nullptr;
            }
            else
            {
                const auto& storage = GetStorageConst<T>();
                return storage.Get(id);
            }
        }

        /// 사용법: std::vector<MonsterScript*> list = world.GetComponents<MonsterScript>(id);
        template <typename T>
        std::vector<T*> GetComponents(EntityId id)
        {
            std::vector<T*> results;

            // 스크립트인 경우: 벡터를 순회하며 dynamic_cast 성공하는 모든 객체 수집
            if constexpr (std::is_base_of_v<IScript, T>)
            {
                auto it = m_scripts.find(id);
                if (it != m_scripts.end())
                {
                    for (auto& scriptComp : it->second)
                    {
                        if (scriptComp.instance)
                        {
                            // 부모 타입으로 요청해도 자식들을 다 찾아줍니다.
                            T* casted = dynamic_cast<T*>(scriptComp.instance.get());
                            if (casted) results.push_back(casted);
                        }
                    }
                }
            }
            // 2. 일반 엔진 컴포넌트인 경우: 1개만 있으므로 있으면 담아서 리턴
            else
            {
                T* comp = GetComponent<T>(id);
                if (comp) results.push_back(comp);
            }

            return results;
        }

        /// const 버전 GetComponents
        template <typename T>
        std::vector<const T*> GetComponents(EntityId id) const
        {
            std::vector<const T*> results;

            if constexpr (std::is_base_of_v<IScript, T>)
            {
                auto it = m_scripts.find(id);
                if (it != m_scripts.end())
                {
                    for (const auto& scriptComp : it->second)
                    {
                        if (scriptComp.instance)
                        {
                            const T* casted = dynamic_cast<const T*>(scriptComp.instance.get());
                            if (casted) results.push_back(casted);
                        }
                    }
                }
            }
            else
            {
                const T* comp = GetComponent<T>(id);
                if (comp) results.push_back(comp);
            }
            return results;
        }

        /// 컴포넌트 제거
        template <typename T>
        void RemoveComponent(EntityId id)
        {
            if constexpr (std::is_base_of_v<IScript, T>)
            {
                auto it = m_scripts.find(id);
                if (it == m_scripts.end()) return;

                auto& vec = it->second;
                for (auto iter = vec.begin(); iter != vec.end(); ++iter)
                {
                    // 타입 일치 확인
                    if (iter->instance && dynamic_cast<T*>(iter->instance.get()))
                    {
                        iter->instance->OnDisable();
                        iter->instance->OnDestroy();

                        vec.erase(iter); // 벡터에서 해당 요소 하나만 제거

                        // 비었으면 맵에서도 엔티티 키 제거
                        if (vec.empty()) m_scripts.erase(it);
                        return;
                    }
                }
            }
            else
            {
                auto& storage = GetStorage<T>();
                
                // TransformComponent 제거 시 children 캐시 무효화 및 Transform dirty 마킹
                if constexpr (std::is_same_v<T, TransformComponent>)
                {
                    InvalidateChildrenCache();
                    MarkTransformDirty(id);
                }
                
                storage.Remove(id);
            }
        }

        // ==== 전체 컴포넌트 순회 (시스템/에디터용) ====
        // 
        // 사용 예시 (읽기 전용):
        //   for (auto&& [entityId, transform] : world.GetComponents<TransformComponent>())
        //   {
        //       // transform은 TransformComponent& (auto&& 사용으로 참조 보장)
        //       // 연속 메모리에서 효율적으로 순회됨 (캐시 친화적)
        //   }
        //
        // 사용 예시 (수정 가능):
        //   for (auto&& [entityId, transform] : world.GetComponents<TransformComponent>())
        //   {
        //       // transform은 TransformComponent& (auto&& 사용으로 참조 보장)
        //       transform.position.x += 1.0f; // 수정 가능
        //   }
        //
        // 성능 최적화:
        //   - 모든 TransformComponent가 연속 메모리에 저장되어 캐시 효율 극대화
        //   - 순회 시 해시맵 조회 없이 직접 접근
        //   - O(1) 삭제로 인한 순회 중 삭제 안전성 보장
        template <typename T>
        auto GetComponents() const
        {
            if constexpr (std::is_base_of_v<IScript, T>)
            {
                // 스크립트는 별도 처리 필요 (현재 구조 유지)
                static_assert(std::is_same_v<T, void>, "스크립트는 GetComponents()로 전체 순회할 수 없습니다.");
            }
            else
            {
                const auto& storage = GetStorageConst<T>();
                return storage.GetView();
            }
        }

        // 비상수 버전 (수정 가능한 순회)
        template <typename T>
        auto GetComponents()
        {
            if constexpr (std::is_base_of_v<IScript, T>)
            {
                static_assert(std::is_same_v<T, void>, "스크립트는 GetComponents()로 전체 순회할 수 없습니다.");
            }
            else
            {
                auto& storage = GetStorage<T>();
                return storage.GetView(); // const 오버로딩으로 자동 판단
            }
        }

        // ==== 스크립트 (특수 케이스) ====
        // 스크립트는 1개 엔티티에 여러 개가 붙을 수 있어 별도 관리 추천
        ScriptComponent& AddScript(EntityId id, const std::string& scriptName);

        /// 전체 Script 컨테이너 ScriptSystem에서 사용
        const std::unordered_map<EntityId, std::vector<ScriptComponent>>& GetAllScriptsInWorld() const { return m_scripts;  }
        std::unordered_map<EntityId, std::vector<ScriptComponent>>& GetAllScriptsInWorld() { return m_scripts; }

        std::vector<ScriptComponent>* GetScripts(EntityId id);
        const std::vector<ScriptComponent>* GetScripts(EntityId id) const;
        void RemoveScript(EntityId id, std::size_t index);
        void RemoveAllScript(); // Clear용

        // ==== 카메라 (특수 케이스 - 메인 카메라 등) ====
        // 필요하다면 별도 헬퍼 함수 유지
        EntityId GetMainCameraEntityId();

        // ==== Transform 행렬 계산 (공용 API) ====
        /// 엔티티의 월드 행렬을 계산합니다 (부모-자식 계층 포함)
        /// 에디터/런타임 모두 이 함수를 사용하여 일관성 보장
        /// 캐시를 사용하므로 UpdateTransformMatrices()를 먼저 호출해야 최신 값이 보장됩니다.
        DirectX::XMMATRIX ComputeWorldMatrix(EntityId entityId) const;
        
        /// Transform 월드행렬 캐시를 갱신합니다. (매 프레임 호출 권장)
        /// dirty 플래그가 있는 엔티티들의 월드행렬을 재계산합니다.
        void UpdateTransformMatrices();
        
        /// 특정 엔티티와 모든 자식의 Transform을 dirty로 표시합니다.
        /// Transform 변경 시 자동으로 호출되지만, 수동 호출도 가능합니다.
        void MarkTransformDirty(EntityId entityId);

        // ==== PostProcessVolume 헬퍼 ====
        /// PostProcessVolumeComponent의 DebugDrawBoxComponent를 업데이트합니다.
        /// boxSize 변경 시 호출하여 bounds를 동기화합니다.
        void UpdatePostProcessVolumeDebugBox(EntityId id, const PostProcessVolumeComponent& volume);

        // ==== 지연 파괴 시스템 ====
        /// 지연 파괴를 예약합니다. (delay 초 후에 파괴)
        void ScheduleDelayedDestruction(EntityId id, float delay);
        
        /// 지연 파괴 시스템을 업데이트합니다. (매 프레임 호출 필요)
        void UpdateDelayedDestruction(float deltaTime);

        // ==== SlotMap 기반 유효성 검사 ====
        /// 엔티티의 현재 generation을 가져옵니다. (없으면 0)
        std::uint32_t GetEntityGeneration(EntityId id) const;
        
        /// 엔티티가 유효한지 확인합니다. (generation 비교)
        bool IsEntityValid(EntityId id, std::uint32_t generation) const;

        // ==== World Epoch (씬 전환 시 증가하여 이전 userData 무효화) ====
        /// 현재 World의 Epoch를 가져옵니다. (씬 전환 시 증가)
        uint64_t GetWorldEpoch() const { return m_worldEpoch; }
        
        /// userData에서 EntityId를 추출합니다. (worldEpoch 검증 포함)
        /// 이전 씬의 userData인 경우 InvalidEntityId를 반환합니다.
        EntityId ExtractEntityIdFromUserData(void* userData) const;

        //==============================================================
        // ==== Combat (script bridge) ====
        void SetFrameCombatHits(const std::vector<CombatHitEvent>* hits) { m_frameCombatHits = hits; }
        bool HasFrameCombatHits() const { return m_frameCombatHits != nullptr; }
        const std::vector<CombatHitEvent>& GetFrameCombatHits() const
        {
            static const std::vector<CombatHitEvent> kEmpty;
            return m_frameCombatHits ? *m_frameCombatHits : kEmpty;
        }

        void SetScriptCombatEnabled(bool enabled) { m_scriptCombatEnabled = enabled; }
        bool IsScriptCombatEnabled() const { return m_scriptCombatEnabled; }

        // 물리
        void SetPhysicsWorld(std::shared_ptr<IPhysicsWorld> physicsWorld);
        IPhysicsWorld* GetPhysicsWorld();
        const IPhysicsWorld* GetPhysicsWorld() const;        
        std::shared_ptr<IPhysicsWorld> GetPhysicsWorldShared() const { return m_physicsWorld; }
        
        /// World::Clear() 호출 전에 호출될 콜백 설정
        /// Engine에서 물리 시스템 정리를 위해 사용
        void SetOnBeforeClearCallback(std::function<void()> callback) { m_onBeforeClear = std::move(callback); }
    private:
        std::shared_ptr<IPhysicsWorld> m_physicsWorld;
        std::function<void()> m_onBeforeClear; // Clear() 호출 전 실행될 콜백
        //==============================================================

    private:
        // 동적 저장소 관리 - 타입 인덱스로 저장소를 찾거나 생성
        template <typename T>
        ComponentStorage<T>& GetStorage()
        {
            static_assert(!std::is_base_of_v<IScript, T>, "스크립트는 GetStorage를 사용할 수 없습니다.");
            
            std::type_index key(typeid(T));
            auto it = m_engineStorages.find(key);
            
            // 저장소가 없으면 생성
            if (it == m_engineStorages.end())
            {
                auto storage = std::make_unique<ComponentStorage<T>>();
                ComponentStorage<T>* ptr = storage.get();
                m_engineStorages[key] = std::move(storage);
                return *ptr;
            }
            
            // 기존 저장소 반환
            return *static_cast<ComponentStorage<T>*>(it->second.get());
        }

        // const 버전 저장소 반환
        template <typename T>
        const ComponentStorage<T>& GetStorageConst() const
        {
            static_assert(!std::is_base_of_v<IScript, T>, "스크립트는 GetStorage를 사용할 수 없습니다.");
            
            std::type_index key(typeid(T));
            auto it = m_engineStorages.find(key);
            
            // 저장소가 없으면 빈 저장소 반환 (이론상 발생하지 않아야 함)
            if (it == m_engineStorages.end())
            {
                static ComponentStorage<T> emptyStorage;
                return emptyStorage;
            }
            
            // 기존 저장소 반환
            return *static_cast<ComponentStorage<T>*>(it->second.get());
        }

    private:
        EntityId m_nextEntityId{ 1 };
        uint64_t m_worldEpoch{ 1 }; // 씬 전환 시 증가하여 이전 userData 무효화

        std::unordered_map<EntityId, std::string> m_names;

        // 엔진 컴포넌트 저장소 관리 (Type Erasure 적용)
        // 컴포넌트 타입별로 동적으로 저장소를 관리합니다.
        // 새로운 컴포넌트 추가 시 World.h 수정 없이 자동으로 지원됩니다.
        std::unordered_map<std::type_index, std::unique_ptr<IStorageBase>> m_engineStorages;

        // ��ũ��Ʈ�� vector�� ������ �����Ƿ� �Ϲ� T�� ������ �޶� ���� ��
        std::unordered_map<EntityId, std::vector<ScriptComponent>> m_scripts;

        // 지연 파괴 시스템 (EntityId -> 남은 시간)
        std::unordered_map<EntityId, float> m_delayedDestructions;

        // SlotMap 기반 유효성 검사 (EntityId -> Generation)
        // 엔티티가 생성될 때 0으로 시작하고, 파괴될 때마다 증가합니다.
        std::unordered_map<EntityId, std::uint32_t> m_entityGenerations;

        // children 캐시 (parent -> children vector)
        // InvalidEntityId는 루트 엔티티들을 의미
        mutable std::unordered_map<EntityId, std::vector<EntityId>> m_children;
        
        // children 캐시 무효화 (SetParent, DestroyEntity, Clear에서 호출)
        void InvalidateChildrenCache() const { m_children.clear(); }
        
        // Transform 월드행렬 캐싱 시스템
        // dirty 플래그: 엔티티의 Transform이 변경되어 월드행렬 재계산이 필요한지 표시
        mutable std::unordered_map<EntityId, bool> m_transformDirty;
        
        // 월드행렬 캐시: EntityId -> 월드행렬 (XMMATRIX는 값 타입이므로 직접 저장)
        // XMMATRIX는 16개 float이므로 XMFLOAT4X4로 저장
        mutable std::unordered_map<EntityId, DirectX::XMFLOAT4X4> m_worldMatrixCache;

        // Combat frame hit buffer (owned by engine)
        const std::vector<CombatHitEvent>* m_frameCombatHits = nullptr;
        bool m_scriptCombatEnabled = false;
    };

    template <typename T>
    T* IScript::GetComponent()
    {
        if (!m_world || m_entity == InvalidEntityId) return nullptr;
        return m_world->GetComponent<T>(m_entity);
    }

    template <typename T>
    const T* IScript::GetComponent() const
    {
        if (!m_world || m_entity == InvalidEntityId) return nullptr;
        return m_world->GetComponent<T>(m_entity);
    }

    template <typename T>
    std::vector<T*> IScript::GetComponents()
    {
        if (!m_world || m_entity == InvalidEntityId) return {};
        return m_world->GetComponents<T>(m_entity);
    }

    template <typename T, typename... Args>
    T& IScript::AddComponent(Args&&... args)
    {
        // World가 없으면 크래시가 나겠지만, 스크립트가 실행 중이라면 World는 반드시 존재해야 합니다.
        return m_world->AddComponent<T>(m_entity, std::forward<Args>(args)...);
    }

    template <typename T>
    void IScript::RemoveComponent()
    {
        if (m_world && m_entity != InvalidEntityId)
            m_world->RemoveComponent<T>(m_entity);
    }
}
