#pragma once

#include <memory>
#include <string>
#include <vector>
#include <typeinfo>

#include <rttr/type>

#include "Core/Entity.h"
#include "Core/ScriptAPI.h"
#include "Components/TransformComponent.h"

namespace Alice
{
    class World;
    struct TransformComponent;
    class GameObject;

    /// 모든 스크립트가 상속해야 하는 기본 베이스 클래스입니다.
    /// - Unity 의 MonoBehaviour 와 비슷한 개념
    /// - World / Entity 에 접근해서 간단한 게임 로직을 작성할 수 있습니다.
    class IScript
    {
    public:
        RTTR_ENABLE()
    public:
        virtual ~IScript() = default;

        /// 스크립트 이름 (디버깅용, typeid로 자동 추출)
        /// - typeid를 사용하여 실제 클래스 이름을 자동으로 반환합니다.
        virtual const char* GetName() const { return typeid(*this).name(); }

        // ==== Component lifecycle ====
        virtual void Awake() {}
        virtual void OnEnable() {}
        virtual void Start() {}
        virtual void Update(float /*deltaTime*/) {}
        virtual void LateUpdate(float /*deltaTime*/) {}
        virtual void PostCombatUpdate(float /*deltaTime*/) {}
        virtual void FixedUpdate(float /*fixedDeltaTime*/) {}
        virtual void OnDisable() {}
        virtual void OnDestroy() {}
        virtual void OnApplicationQuit() {}

        template <typename T> T* GetComponent();
        template <typename T> const T* GetComponent() const;
        template <typename T> std::vector<T*> GetComponents();
        template <typename T, typename... Args> T& AddComponent(Args&&... args);
        template <typename T> void RemoveComponent();

        /// World / Entity 컨텍스트를 내부에 저장합니다.
        /// - World::AddScript 에서 자동으로 호출됩니다.
        void SetContext(World* world, EntityId entity) { m_world = world; m_entity = entity; }

        /// ScriptSystem 이 제공하는 서비스(입력/씬/레지스트리 등)
        void SetServices(ScriptServices* services) { m_services = services; }

    protected:
        /// 이 스크립트를 소유한 월드에 접근합니다.
        World* GetWorld() const { return m_world; }

        /// 소유 엔티티 ID (Unity 의 gameObject / this.Entity 느낌)
        EntityId GetOwnerId() const { return m_entity; }

        /// 소유 게임오브젝트 핸들을 안전하게 반환합니다.
        /// - 엔티티 또는 월드가 유효하지 않으면 nullptr 을 반환합니다.
        GameObject* GetOwner();

        /// 소유 엔티티의 Transform 컴포넌트를 가져옵니다. (없으면 nullptr)
        TransformComponent* GetTransform();

        /// Unity 스타일 짧은 별칭 (transform)
        TransformComponent* transform() { return GetTransform(); }

        /// Unity 느낌의 게임오브젝트 핸들
        GameObject gameObject() const;

        /// 입력/씬/리소스 서비스
        IScriptInput* Input() const { return m_services ? m_services->input : nullptr; }
        IScriptScene* Scenes() const { return m_services ? m_services->scene : nullptr; }
        ResourceManager* Resources() const { return m_services ? m_services->resources : nullptr; }

    private:
        World*   m_world  = nullptr;
        EntityId m_entity = InvalidEntityId;
        ScriptServices* m_services = nullptr;
    };
}
