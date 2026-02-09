#include "BloodVignetteDemo.h"

#include <algorithm>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/UI/UICommon.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "Runtime/UI/UITransformComponent.h"
#include "Runtime/UI/UIImageComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(BloodVignetteDemo);

    void BloodVignetteDemo::Start()
    {
        EnsureOverlayUI();
        SetOverlayVisible(false);
        m_elapsedSec = 0.0f;
        m_showing = false;
    }

    void BloodVignetteDemo::Update(float deltaTime)
    {
        auto* input = Input();
        if (!input)
            return;

        const KeyCode triggerKey = static_cast<KeyCode>(Get_m_triggerKey());
        if (input->GetKeyDown(triggerKey))
        {
            EnsureOverlayUI();
            m_showing = true;
            m_elapsedSec = 0.0f;
            SetOverlayVisible(true);
        }

        if (!m_showing)
            return;

        m_elapsedSec += std::max(0.0f, deltaTime);
        if (m_elapsedSec >= std::max(0.0f, Get_m_showDurationSec()))
        {
            m_showing = false;
            SetOverlayVisible(false);
        }
    }

    void BloodVignetteDemo::EnsureOverlayUI()
    {
        World* world = GetWorld();
        if (!world)
            return;

        if (m_overlayEntity != InvalidEntityId)
        {
            auto* widget = world->GetComponent<UIWidgetComponent>(m_overlayEntity);
            auto* transform = world->GetComponent<UITransformComponent>(m_overlayEntity);
            auto* image = world->GetComponent<UIImageComponent>(m_overlayEntity);
            if (widget && transform && image)
            {
                transform->sortOrder = Get_m_sortOrder();
                image->texturePath = Get_m_texturePath();
                image->preserveAspect = false;
                image->color = { 1.0f, 1.0f, 1.0f, std::clamp(Get_m_overlayAlpha(), 0.0f, 1.0f) };
                return;
            }
        }

        const EntityId overlay = world->CreateEntity();
        world->SetEntityName(overlay, "BloodVignetteOverlay");

        auto& tr = world->AddComponent<TransformComponent>(overlay);
        tr.enabled = true;
        tr.position = { 0.0f, 0.0f, 0.0f };
        tr.rotation = { 0.0f, 0.0f, 0.0f };
        tr.scale = { 1.0f, 1.0f, 1.0f };
        tr.visible = true;

        auto& widget = world->AddComponent<UIWidgetComponent>(overlay);
        widget.widgetName = "BloodVignetteOverlay";
        widget.space = AliceUI::UISpace::Screen;
        widget.visibility = AliceUI::UIVisibility::Collapsed;
        widget.interactable = false;
        widget.raycastTarget = false;
        widget.shaderName = "Default";

        auto& uiTr = world->AddComponent<UITransformComponent>(overlay);
        uiTr.anchorMin = { 0.0f, 0.0f };
        uiTr.anchorMax = { 1.0f, 1.0f };
        uiTr.pivot = { -0.5f, 0.5f };
        uiTr.position = { 0.0f, 0.0f };
        uiTr.size = { 0.0f, 0.0f };
        uiTr.scale = { 1.0f, 1.0f };
        uiTr.sortOrder = Get_m_sortOrder();
        uiTr.useAlignment = false;

        auto& image = world->AddComponent<UIImageComponent>(overlay);
        image.texturePath = Get_m_texturePath();
        image.preserveAspect = false;
        image.color = { 1.0f, 1.0f, 1.0f, std::clamp(Get_m_overlayAlpha(), 0.0f, 1.0f) };

        m_overlayEntity = overlay;
    }

    void BloodVignetteDemo::SetOverlayVisible(bool visible)
    {
        World* world = GetWorld();
        if (!world || m_overlayEntity == InvalidEntityId)
            return;

        auto* widget = world->GetComponent<UIWidgetComponent>(m_overlayEntity);
        if (!widget)
            return;

        widget->visibility = visible ? AliceUI::UIVisibility::Visible : AliceUI::UIVisibility::Collapsed;
    }
}

