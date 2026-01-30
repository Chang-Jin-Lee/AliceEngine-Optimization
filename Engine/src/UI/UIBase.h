#pragma once
#include <cstdint>
#include <vector>
#include <concepts>
#include <typeinfo>
#include <typeindex>
#include <tuple>
#include <functional>
#include <cassert>
#include <DirectXMath.h>
#include <d2d1.h>
#include "UIRenderStruct.h"
#include "UICompDelegate.h"          // ObjectID, Delegates
#include "IUIComponent.h"

class UITransform; // forward


enum class UIState
{
    Normal,
    Pressed,
    Release,
    Hold
};


class UIBase
{
    friend class UIWorld;       
    friend class UISceneManager; 
    friend class UIWorldManager;
    friend class UITransform;
    friend class UIRenderSystem;
    friend class UIHitTestSystem;
    friend class UILayoutSystem;
    friend class UIImageSystem;
    friend class UIScriptSystem;
    friend class UIInputSystem;
protected:
    UIBase() = default;

public:
    virtual ~UIBase() = default;

    // Transform은 엔티티 생성 직후 1회 AddComponent로 보장되고, 캐시로 접근한다.
    UITransform* Transform = nullptr;
    UITransform& GetTransform()
    {
        assert(Transform && "UIBase::GetTransform: Transform must be set");
        return *Transform;
    }

    // World 쪽에서 넘겨주는 CompDelegates 를 통해 컴포넌트 생성/조회/삭제를 위임
    virtual void Initalize(UIRenderStruct& UIRenderStruct, CompDelegates& worldDelegates);
    virtual void Update() = 0;
    virtual void Render() = 0;

    long unsigned int getID() { return ID; }
    UIState m_uiState{ UIState::Normal };
    
    // 직렬화를 위한 접근자 (ID 기반 저장/로드용)
    long unsigned int GetParentID() const { return parentID; }
    const std::vector<long unsigned int>& GetChildIDs() const { return childIDStorage; }


    //---------- 컴포넌트 헬퍼 ---------------
    template<class T, class... Args>
        requires std::derived_from<T, IUIComponent>
    T* AddComponent(Args&&... args)
    {
        if (!m_world) return nullptr;
        if (!m_world->AddComponent.IsBound()) return nullptr;

        // args... 를 값으로 안전하게 패킹
        auto packed = std::make_tuple(std::forward<Args>(args)...);

        std::function<void*()> factory =
            [packed = std::move(packed)]() mutable -> void*
        {
            return std::apply(
                [](auto&&... xs) -> void*
                {
                    return static_cast<void*>(
                        new T(std::forward<decltype(xs)>(xs)...)
                    );
                },
                std::move(packed)
            );
        };

        void* raw = m_world->AddComponent.Execute(
            static_cast<ObjectID>(ID),
            std::type_index(typeid(T)),
            std::move(factory)
        );

        return static_cast<T*>(raw);
    }

    template<class T>
    T* TryGetComponent()
    {
        if (!m_world) return nullptr;
        if (!m_world->FindComponent.IsBound()) return nullptr;

        void* raw = m_world->FindComponent.Execute(
            static_cast<ObjectID>(ID),
            std::type_index(typeid(T))
        );

        return static_cast<T*>(raw);
    }

    template<class T>
    T& GetComponent()
    {
        T* p = TryGetComponent<T>();
        assert(p && "Component not found");
        return *p;
    }

    template<class T>
    void RemoveComponent()
    {
        if (!m_world) return;
        if (!m_world->RemoveComponent.IsBound()) return;

        m_world->RemoveComponent.Execute(
            static_cast<ObjectID>(ID),
            std::type_index(typeid(T))
        );
    }

    // �ش� UI�� Rect
    D2D1_RECT_F m_rect = D2D1::RectF(-50, -50, 50, 50);

private:
    // World 에서 전달받은 델리게이트 묶음
    CompDelegates*  m_world{ nullptr };

protected:
    long unsigned ID{ 0 };

    // UI�� �θ� ������Ʈ ID
    long unsigned int parentID{ 0 };

    // UI�� �ڽ� ������Ʈ ID
    std::vector<long unsigned int> childIDStorage;

   


    UIRenderStruct* m_UIRenderStruct{nullptr};

    void SetID(long unsigned tmp) { ID = tmp; };


    // 마우스 충돌 함수
    virtual bool IsMouseOverUIAABB(DirectX::XMFLOAT2& tmpPoint, std::vector<long unsigned>& IDStorage);
    bool IsMouseOverUIRot(DirectX::XMFLOAT2& tmpPoint);
};

