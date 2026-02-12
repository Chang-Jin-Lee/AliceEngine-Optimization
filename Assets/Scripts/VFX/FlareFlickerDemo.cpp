#include "FlareFlickerDemo.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "Runtime/ECS/World.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/UI/UIImageComponent.h"
#include "Runtime/UI/UITransformComponent.h"
#include "Runtime/UI/UIWidgetComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(FlareFlickerDemo);

    namespace
    {
        float Saturate(float v)
        {
            return std::clamp(v, 0.0f, 1.0f);
        }

        DirectX::XMFLOAT3 ScaleColor(const DirectX::XMFLOAT3& c, float s)
        {
            return DirectX::XMFLOAT3(c.x * s, c.y * s, c.z * s);
        }
    }

    void FlareFlickerDemo::Start()
    {
        EnsureWorldUI();

        World* world = GetWorld();
        if (!world)
            return;

        if (auto* uiTr = world->GetComponent<UITransformComponent>(GetOwnerId()))
        {
            m_baseUiScale = uiTr->scale;
        }

        m_timeSec = 0.0f;
    }

    void FlareFlickerDemo::Update(float deltaTime)
    {
        EnsureWorldUI();

        const float dt = std::max(0.0f, deltaTime) * std::max(0.0f, Get_m_timeScale());
        m_timeSec += dt;

        const float primary = 0.5f + 0.5f * static_cast<float>(std::sin(m_timeSec * std::max(0.0f, Get_m_primarySpeed())));
        const float secondary = 0.5f + 0.5f * static_cast<float>(std::sin(0.73f + (m_timeSec * std::max(0.0f, Get_m_secondarySpeed()))));
        const float w = Saturate(Get_m_secondaryWeight());
        const float mixed = (primary * (1.0f - w)) + (secondary * w);
        const float shaped = static_cast<float>(std::pow(Saturate(mixed), std::max(0.01f, Get_m_responseCurve())));

        ApplyVisual(Saturate(shaped));
    }

    void FlareFlickerDemo::EnsureWorldUI()
    {
        World* world = GetWorld();
        if (!world)
            return;

        const EntityId owner = GetOwnerId();
        if (owner == InvalidEntityId)
            return;

        auto* widget = world->GetComponent<UIWidgetComponent>(owner);
        if (!widget)
        {
            world->AddComponent<UIWidgetComponent>(owner);
            widget = world->GetComponent<UIWidgetComponent>(owner);
        }

        auto* uiTr = world->GetComponent<UITransformComponent>(owner);
        if (!uiTr)
        {
            world->AddComponent<UITransformComponent>(owner);
            uiTr = world->GetComponent<UITransformComponent>(owner);
        }

        auto* image = world->GetComponent<UIImageComponent>(owner);
        if (!image)
        {
            world->AddComponent<UIImageComponent>(owner);
            image = world->GetComponent<UIImageComponent>(owner);
        }

        if (!widget || !uiTr || !image)
            return;

        widget->widgetName = "FlareUI";
        widget->space = AliceUI::UISpace::World;
        widget->visibility = AliceUI::UIVisibility::Visible;
        widget->raycastTarget = false;
        widget->interactable = false;
        widget->billboard = false;
        widget->shaderName = "Default";

        uiTr->useAlignment = false;
        uiTr->position = { 0.0f, 0.0f };
        uiTr->pivot = { 0.0f, 0.0f };
        uiTr->size = Get_m_baseSize();

        image->texturePath = ResolveTexturePath();
        image->preserveAspect = true;
    }

    void FlareFlickerDemo::ApplyVisual(float flicker01)
    {
        World* world = GetWorld();
        if (!world)
            return;

        const EntityId owner = GetOwnerId();
        if (owner == InvalidEntityId)
            return;

        auto* widget = world->GetComponent<UIWidgetComponent>(owner);
        auto* uiTr = world->GetComponent<UITransformComponent>(owner);
        auto* image = world->GetComponent<UIImageComponent>(owner);
        if (!widget || !uiTr || !image)
            return;

        const float colorMin = std::max(0.0f, Get_m_minColorScale());
        const float colorMax = std::max(colorMin, Get_m_maxColorScale());
        const float colorScale = colorMin + ((colorMax - colorMin) * flicker01);

        float alpha = std::clamp(Get_m_baseAlpha(), 0.0f, 1.0f);
        if (Get_m_useAlphaFlicker())
        {
            const float alphaCenter = std::clamp(Get_m_baseAlpha(), 0.0f, 1.0f);
            const float alphaAmp = std::max(0.0f, Get_m_alphaAmplitude());
            const float alphaRaw = alphaCenter + ((flicker01 * 2.0f - 1.0f) * alphaAmp);
            const float alphaMin = std::clamp(Get_m_minAlpha(), 0.0f, 1.0f);
            const float alphaMax = std::clamp(Get_m_maxAlpha(), alphaMin, 1.0f);
            alpha = std::clamp(alphaRaw, alphaMin, alphaMax);
        }

        const float scaleAmp = std::max(0.0f, Get_m_scaleAmplitude());
        const float pulse = 1.0f + ((flicker01 * 2.0f - 1.0f) * scaleAmp);
        const float safePulse = std::max(0.01f, pulse);

        const DirectX::XMFLOAT3 colorRgb = ScaleColor(Get_m_baseColor(), colorScale);

        widget->space = AliceUI::UISpace::World;
        widget->visibility = AliceUI::UIVisibility::Visible;
        widget->billboard = false;

        uiTr->size = Get_m_baseSize();
        uiTr->scale = { m_baseUiScale.x * safePulse, m_baseUiScale.y * safePulse };

        image->texturePath = ResolveTexturePath();
        image->preserveAspect = true;
        image->color = { colorRgb.x, colorRgb.y, colorRgb.z, alpha };
    }

    std::string FlareFlickerDemo::ResolveTexturePath() const
    {
        const std::array<std::string, 8> frames = {
            Get_m_framePath1(),
            Get_m_framePath2(),
            Get_m_framePath3(),
            Get_m_framePath4(),
            Get_m_framePath5(),
            Get_m_framePath6(),
            Get_m_framePath7(),
            Get_m_framePath8()
        };

        std::array<std::string, 8> active{};
        int activeCount = 0;
        for (const auto& path : frames)
        {
            if (!path.empty())
            {
                active[activeCount++] = path;
            }
        }

        if (!Get_m_useFrameAnimation() || activeCount <= 0)
        {
            return Get_m_texturePath();
        }

        const float fps = std::max(0.0f, Get_m_frameRate());
        if (fps <= 0.0f)
        {
            return active[0];
        }

        const int rawFrame = static_cast<int>(std::floor(m_timeSec * fps));
        int index = 0;
        if (Get_m_loopFrames())
        {
            index = (activeCount > 0) ? (rawFrame % activeCount) : 0;
            if (index < 0)
                index += activeCount;
        }
        else
        {
            index = std::clamp(rawFrame, 0, activeCount - 1);
        }

        return active[index];
    }
}
