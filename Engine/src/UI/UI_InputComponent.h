#pragma once
#include "IUIComponent.h"
#include <functional>
#include <DirectXMath.h>
#include <cassert>

namespace Alice
{
    class InputSystem;
}

class UIWorld;
class UITransform;

class UI_InputComponent : public IUIComponent
{
public:
    UI_InputComponent() = default;
    ~UI_InputComponent() = default;

    // 상태
    bool bIsHovered = false;
    bool bIsPressed = false;

    // 이벤트 콜백
    std::function<void()> OnHoverBegin;
    std::function<void()> OnHoverEnd;
    std::function<void()> OnPressed;
    std::function<void()> OnReleased;
    std::function<void()> OnClicked;

    // Update 호출 (UIWorld와 InputSystem을 받아서 처리)
    void Update(UIWorld& world, Alice::InputSystem& input);

    // 컴포넌트가 추가될 때 호출되는 훅
    void OnAdded() override
    {
        #ifdef _DEBUG
        assert(OwnerID != 0 && "UI_InputComponent::OnAdded: OwnerID must be set");
        #endif
    }

private:
    // 내부 상태 (클릭 판정용)
    bool bWasMouseDownPrev = false;
    bool bPressedInside = false; // 마우스를 누른 순간 inside였는지

    // 충돌 판정 헬퍼
    // 회전이 0이면서 root 노드인 경우만 AABB 사용, 그 외는 회전 판정 사용
    bool IsPointInside(const DirectX::XMFLOAT2& screenPoint, UITransform& transform, UIWorld& world) const;
    
    // AABB 판정 (회전 없음 + root 노드) - IsMouseOverUIAABB 기반
    bool IsPointInsideAABB(const DirectX::XMFLOAT2& screenPoint, UITransform& transform) const;
    
    // 회전된 UI 판정 (회전 있음 또는 자식 노드) - invWorldTrans 사용
    bool IsPointInsideRotated(const DirectX::XMFLOAT2& screenPoint, UITransform& transform) const;
};
