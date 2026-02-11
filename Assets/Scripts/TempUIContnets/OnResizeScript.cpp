#include "OnResizeScript.h"

#include <algorithm>
#include <cmath>
#include <cwchar>

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "Runtime/UI/UITransformComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(OnResizeScript);

    namespace
    {
        constexpr wchar_t kWindowClassName[] = L"AliceRendererWindowClass";
        constexpr wchar_t kWindowTitle[] = L"AliceRenderer";

        EntityId FindWidgetByName(World& world, const std::string& name)
        {
            if (name.empty())
                return InvalidEntityId;

            for (auto [id, widget] : world.GetComponents<UIWidgetComponent>())
            {
                const std::string widgetName = widget.widgetName.empty()
                    ? world.GetEntityName(id)
                    : widget.widgetName;

                if (!widgetName.empty() && widgetName == name)
                    return id;
            }

            return InvalidEntityId;
        }

        bool IsEngineWindow(HWND hwnd)
        {
            if (!hwnd)
                return false;

            wchar_t cls[128] = {};
            const int len = GetClassNameW(hwnd, cls, static_cast<int>(sizeof(cls) / sizeof(cls[0])));
            if (len <= 0)
                return false;

            return std::wcscmp(cls, kWindowClassName) == 0;
        }

        HWND FindEngineWindow()
        {
            HWND hwnd = FindWindowW(kWindowClassName, kWindowTitle);
            if (!hwnd)
                hwnd = FindWindowW(kWindowClassName, nullptr);
            if (hwnd)
                return hwnd;

            hwnd = GetActiveWindow();
            if (IsEngineWindow(hwnd))
                return hwnd;

            hwnd = GetForegroundWindow();
            if (IsEngineWindow(hwnd))
                return hwnd;

            return nullptr;
        }

        bool GetClientSize(float& outW, float& outH)
        {
            HWND hwnd = FindEngineWindow();
            if (!hwnd)
                return false;

            RECT rc{};
            if (!GetClientRect(hwnd, &rc))
                return false;

            outW = static_cast<float>(std::max<LONG>(0, rc.right - rc.left));
            outH = static_cast<float>(std::max<LONG>(0, rc.bottom - rc.top));

            if (outW < 8.0f || outH < 8.0f)
                return false;

            return true;
        }

        float ComputeScale(float sx, float sy, int mode)
        {
            switch (mode)
            {
            case 1: // Fill
                return std::max(sx, sy);
            case 2: // MatchWidth
                return sx;
            case 3: // MatchHeight
                return sy;
            default: // Fit (min - 가장 무난)
                return std::min(sx, sy);
            }
        }
    }

    void OnResizeScript::Start()
    {
        World* world = GetWorld();
        if (!world)
            return;

        const EntityId resolved = ResolveTarget(*world);
        if (resolved == InvalidEntityId)
            return;

        auto* uiTransform = world->GetComponent<UITransformComponent>(resolved);
        if (!uiTransform)
            return;

        // baseSize 초기화: baseSizeX/Y가 0이면 현재 size로 설정 (처음 한 번만)
        const float eps = 0.001f;
        if (!m_baseSizeInitialized && 
            (std::fabs(Get_baseSizeX()) < eps && std::fabs(Get_baseSizeY()) < eps))
        {
            Set_baseSizeX(uiTransform->size.x);
            Set_baseSizeY(uiTransform->size.y);
            m_baseSizeInitialized = true;
            ALICE_LOG_INFO("[OnResizeScript] Initialized baseSize: %.2f x %.2f", 
                Get_baseSizeX(), Get_baseSizeY());
        }
        else if (!m_baseSizeInitialized)
        {
            // 인스펙터에서 이미 설정된 경우
            m_baseSizeInitialized = true;
        }

        // 첫 적용
        ApplyLayout(true);
    }

    void OnResizeScript::Update(float deltaTime)
    {
        (void)deltaTime;
        // 매 프레임 기준 해상도에 맞게 resize
        ApplyLayout(true);
    }

    EntityId OnResizeScript::ResolveTarget(World& world) const
    {
        const std::string name = Get_targetWidgetName();
        if (!name.empty())
            return FindWidgetByName(world, name);

        return GetOwnerId();
    }

    void OnResizeScript::ApplyLayout(bool force)
    {
        World* world = GetWorld();
        if (!world)
            return;

        const EntityId resolved = ResolveTarget(*world);
        if (resolved == InvalidEntityId)
        {
            if (force)
                ALICE_LOG_WARN("[OnResizeScript] Target widget not found: %s", Get_targetWidgetName().c_str());
            return;
        }

        if (resolved != m_targetId)
        {
            m_targetId = resolved;
            m_baseSizeInitialized = false; // 타겟이 바뀌면 재초기화
        }

        auto* uiTransform = world->GetComponent<UITransformComponent>(m_targetId);
        if (!uiTransform)
            return;

        // baseSize 초기화 (타겟이 바뀌었거나 아직 초기화되지 않은 경우)
        if (!m_baseSizeInitialized)
        {
            const float eps = 0.001f;
            if (std::fabs(Get_baseSizeX()) < eps && std::fabs(Get_baseSizeY()) < eps)
            {
                Set_baseSizeX(uiTransform->size.x);
                Set_baseSizeY(uiTransform->size.y);
            }
            m_baseSizeInitialized = true;
        }

        // 현재 화면 크기 읽기
        float screenW = 0.0f;
        float screenH = 0.0f;

        if (Get_overrideWidth() > 0.0f && Get_overrideHeight() > 0.0f)
        {
            screenW = Get_overrideWidth();
            screenH = Get_overrideHeight();
        }
        else if (!GetClientSize(screenW, screenH))
        {
            return;
        }

        // baseResolution (기준 해상도)
        const float baseW = std::max(1.0f, Get_referenceWidth());
        const float baseH = std::max(1.0f, Get_referenceHeight());

        // ratio 계산
        const float ratioX = screenW / baseW;
        const float ratioY = screenH / baseH;

        // currentScale: 실제 적용 스케일 (런타임 계산)
        float scaleX = 1.0f;
        float scaleY = 1.0f;
        if (Get_useNonUniformScale())
        {
            scaleX = std::clamp(ratioX, Get_minScale(), Get_maxScale());
            scaleY = std::clamp(ratioY, Get_minScale(), Get_maxScale());
        }
        else
        {
            // scaleMode에 따라 ratio 계산 (기본값 0 = Fit = min)
            const float scale = std::clamp(ComputeScale(ratioX, ratioY, Get_scaleMode()), Get_minScale(), Get_maxScale());
            scaleX = scale;
            scaleY = scale;
        }

        // Anchor 스트레치 체크
        const float anchorEps = 1e-4f;
        const bool stretchX = std::fabs(uiTransform->anchorMax.x - uiTransform->anchorMin.x) > anchorEps;
        const bool stretchY = std::fabs(uiTransform->anchorMax.y - uiTransform->anchorMin.y) > anchorEps;
        const bool fullStretchX =
            stretchX &&
            std::fabs(uiTransform->anchorMin.x) <= anchorEps &&
            std::fabs(uiTransform->anchorMax.x - 1.0f) <= anchorEps;
        const bool fullStretchY =
            stretchY &&
            std::fabs(uiTransform->anchorMin.y) <= anchorEps &&
            std::fabs(uiTransform->anchorMax.y - 1.0f) <= anchorEps;

        // currentSize = baseSize * currentScale (스크립트의 baseSizeX/Y 사용)
        DirectX::XMFLOAT2 newSize;
        newSize.x = fullStretchX ? 0.0f : (Get_baseSizeX() * scaleX);
        newSize.y = fullStretchY ? 0.0f : (Get_baseSizeY() * scaleY);

        if (Get_roundToInt())
        {
            newSize.x = std::round(newSize.x);
            newSize.y = std::round(newSize.y);
        }

        // 런타임에만 적용 (저장되지 않음)
        uiTransform->size = newSize;

        if (Get_forceScaleOne())
        {
            uiTransform->scale = { 1.0f, 1.0f };
        }

        m_lastScreenW = screenW;
        m_lastScreenH = screenH;
    }
}
