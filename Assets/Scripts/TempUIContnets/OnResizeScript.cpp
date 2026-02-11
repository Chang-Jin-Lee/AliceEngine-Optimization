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
            default: // Fit
                return std::min(sx, sy);
            }
        }
    }

    void OnResizeScript::Start()
    {
        ApplyLayout(true);
    }

    void OnResizeScript::Update(float deltaTime)
    {
        (void)deltaTime;
        // 항상 기준 화면 크기(1600x900)로 resize하도록 강제 적용
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
            m_baseSizeCached = false;
            m_initialApplied = false;
            m_baseScreenCached = false;
        }

        auto* uiTransform = world->GetComponent<UITransformComponent>(m_targetId);
        if (!uiTransform)
            return;

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

        // 기준 화면 크기를 항상 사용하므로 매 프레임 resize
        // (force가 false여도 항상 처리)

        if (Get_applyInitialSize() && !m_initialApplied)
        {
            uiTransform->size.x = Get_initialSizeX();
            uiTransform->size.y = Get_initialSizeY();
            m_initialApplied = true;
        }

        if (!m_baseSizeCached)
        {
            m_baseSizeX = uiTransform->size.x;
            m_baseSizeY = uiTransform->size.y;
            m_baseSizeCached = true;
        }
        if (!m_baseScreenCached)
        {
            m_baseScreenW = screenW;
            m_baseScreenH = screenH;
            m_baseScreenCached = true;
        }

        // 항상 기준 화면 크기(1600x900)를 사용하여 resize
        const float refW = std::max(1.0f, Get_referenceWidth());
        const float refH = std::max(1.0f, Get_referenceHeight());

        const float sx = screenW / refW;
        const float sy = screenH / refH;

        float scaleX = 1.0f;
        float scaleY = 1.0f;
        if (Get_useNonUniformScale())
        {
            scaleX = std::clamp(sx, Get_minScale(), Get_maxScale());
            scaleY = std::clamp(sy, Get_minScale(), Get_maxScale());
        }
        else
        {
            const float scale = std::clamp(ComputeScale(sx, sy, Get_scaleMode()), Get_minScale(), Get_maxScale());
            scaleX = scale;
            scaleY = scale;
        }

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

        DirectX::XMFLOAT2 newSize = uiTransform->size;

        // Only zero size for full-stretch anchors. For partial stretch, keep size as padding.
        newSize.x = fullStretchX ? 0.0f : (m_baseSizeX * scaleX);
        newSize.y = fullStretchY ? 0.0f : (m_baseSizeY * scaleY);

        if (Get_roundToInt())
        {
            newSize.x = std::round(newSize.x);
            newSize.y = std::round(newSize.y);
        }

        uiTransform->size = newSize;

        if (Get_forceScaleOne())
        {
            uiTransform->scale = { 1.0f, 1.0f };
        }

        m_lastScreenW = screenW;
        m_lastScreenH = screenH;
    }
}
