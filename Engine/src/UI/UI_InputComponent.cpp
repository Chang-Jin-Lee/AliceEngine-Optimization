#include "UI_InputComponent.h"
#include "UISceneManager.h"  // UIWorld 정의 포함
#include "UITransform.h"
#include "Core/InputSystem.h"
#include "Core/Logger.h"
#include <d2d1_1.h>
#include <DirectXMath.h>

using namespace DirectX;

void UI_InputComponent::Update(UIWorld& world, Alice::InputSystem& input)
{
    if (OwnerID == 0) return;

    // Owner의 Transform 가져오기
    UITransform* transform = world.TryGetComponent<UITransform>(OwnerID);
    if (!transform) return;

    // 마우스 스크린 좌표 가져오기
    POINT mousePos = input.GetMousePosition();
    XMFLOAT2 screenPoint{ static_cast<float>(mousePos.x), static_cast<float>(mousePos.y) };

    // 충돌 판정 (회전이 0이면서 root 노드인 경우만 AABB 사용)
    bool inside = IsPointInside(screenPoint, *transform, world);

    // Hover 상태 전이 처리
    if (inside && !bIsHovered)
    {
        // false → true: Hover 시작
        bIsHovered = true;
        if (OnHoverBegin)
        {
            OnHoverBegin();
        }
        #ifdef _DEBUG
        ALICE_LOG_INFO("[UI_InputComponent] HoverBegin: OwnerID=%lu", OwnerID);
        #endif
    }
    else if (!inside && bIsHovered)
    {
        // true → false: Hover 종료
        bIsHovered = false;
        if (OnHoverEnd)
        {
            OnHoverEnd();
        }
        #ifdef _DEBUG
        ALICE_LOG_INFO("[UI_InputComponent] HoverEnd: OwnerID=%lu", OwnerID);
        #endif
    }

    // 마우스 버튼 상태 확인 (왼쪽 버튼, 인덱스 0)
    const int LEFT_MOUSE_BUTTON = 0;
    bool isMouseDown = input.IsLeftButtonDown();
    bool isMousePressed = input.IsMouseButtonPressed(LEFT_MOUSE_BUTTON);
    bool isMouseReleased = input.IsMouseButtonReleased(LEFT_MOUSE_BUTTON);

    // Press 처리 (Down 순간 1회)
    if (isMousePressed && inside)
    {
        bIsPressed = true;
        bPressedInside = true; // inside 상태에서 눌렀음을 기록
        if (OnPressed)
        {
            OnPressed();
        }
        //#ifdef _DEBUG
        //ALICE_LOG_INFO("[UI_InputComponent] Pressed: OwnerID=%lu", OwnerID);
        //#endif
    }

    // Release 처리
    if (isMouseReleased)
    {
        bool wasPressed = bIsPressed;
        bIsPressed = false;

        if (OnReleased)
        {
            OnReleased();
        }
        //#ifdef _DEBUG
        //ALICE_LOG_INFO("[UI_InputComponent] Released: OwnerID=%lu", OwnerID);
        //#endif

        // Click 처리: inside 상태에서 Down 후 Up이면 Click
        if (wasPressed && bPressedInside && inside)
        {
            if (OnClicked)
            {
                OnClicked();
            }
            #ifdef _DEBUG
            ALICE_LOG_INFO("[UI_InputComponent] Clicked: OwnerID=%lu", OwnerID);
            #endif
        }

        bPressedInside = false; // Reset
    }

    // 마우스가 밖으로 나가면 Press 상태 해제
    if (!inside && bIsPressed)
    {
        bIsPressed = false;
        bPressedInside = false;
    }

    // 이전 프레임 상태 저장
    bWasMouseDownPrev = isMouseDown;
}

bool UI_InputComponent::IsPointInside(const XMFLOAT2& screenPoint, UITransform& transform, UIWorld& world) const
{
    // 회전이 0이면서 root 노드인 경우만 AABB 사용
    bool isRoot = false;
    const auto& rootIDs = world.GetRootIDs();
    for (auto rootID : rootIDs)
    {
        if (rootID == OwnerID)
        {
            isRoot = true;
            break;
        }
    }
    
    if (transform.m_rotation == 0.0f && isRoot)
    {
        // 회전 없음 + root 노드: AABB 빠른 경로
        return IsPointInsideAABB(screenPoint, transform);
    }
    else
    {
        // 회전 있음 또는 자식 노드: invWorldTrans 사용
        return IsPointInsideRotated(screenPoint, transform);
    }
}

bool UI_InputComponent::IsPointInsideAABB(const XMFLOAT2& screenPoint, UITransform& transform) const
{
    // IsMouseOverUIAABB 기반 구현 (회전 없음)
    // Scale 적용
    float scaledW = transform.m_size.x * transform.m_scale.x;
    float scaledH = transform.m_size.y * transform.m_scale.y;

    // Pivot 고려 (scale 적용)
    float pivotPx = transform.m_size.x * transform.m_pivot.x * transform.m_scale.x;
    float pivotPy = transform.m_size.y * transform.m_pivot.y * transform.m_scale.y;

    // 월드 공간 AABB 계산
    const float minX = transform.m_translation.x - pivotPx;
    const float minY = transform.m_translation.y - pivotPy;
    const float maxX = minX + scaledW;
    const float maxY = minY + scaledH;

    // 스크린 좌표가 AABB 내부에 있는지 판정
    return (screenPoint.x >= minX && screenPoint.x <= maxX && 
            screenPoint.y >= minY && screenPoint.y <= maxY);
}

bool UI_InputComponent::IsPointInsideRotated(const XMFLOAT2& screenPoint, UITransform& transform) const
{
    // IsMouseOverUIRot 기반 구현 (회전 있음)
    // 스크린 좌표를 로컬 좌표로 변환 (invWorldTrans 직접 행렬 곱셈)
    // pos * invMatrix
    float inversePosX =
        screenPoint.x * transform.m_invWorldTrans._11 +
        screenPoint.y * transform.m_invWorldTrans._21 +
        transform.m_invWorldTrans._31;

    float inversePosY =
        screenPoint.x * transform.m_invWorldTrans._12 +
        screenPoint.y * transform.m_invWorldTrans._22 +
        transform.m_invWorldTrans._32;

    // Pivot 고려한 판정 범위 계산
    float px = transform.m_size.x * transform.m_pivot.x;
    float py = transform.m_size.y * transform.m_pivot.y;

    // 로컬 좌표 기준 Rect 범위 (pivot 기준)
    return (inversePosX >= -px && inversePosX <= transform.m_size.x - px &&
            inversePosY >= -py && inversePosY <= transform.m_size.y - py);
}
