#include "PauseDeltaTimeDemo.h"

#include <algorithm>
#include <iomanip>
#include <sstream>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/UI/UICommon.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "Runtime/UI/UITransformComponent.h"
#include "Runtime/UI/UIImageComponent.h"
#include "Runtime/UI/UITextComponent.h"
#include "Runtime/UI/UIEmptyGaugeEffectComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(PauseDeltaTimeDemo);

    namespace
    {
        void SetSubtreeVisibility(World& world, EntityId id, AliceUI::UIVisibility visibility)
        {
            if (id == InvalidEntityId)
                return;

            if (auto* widget = world.GetComponent<UIWidgetComponent>(id))
                widget->visibility = visibility;

            for (EntityId child : world.GetChildren(id))
                SetSubtreeVisibility(world, child, visibility);
        }
    }

    void PauseDeltaTimeDemo::Start()
    {
        auto* t = GetTransform();
        if (t)
            t->position.x = Get_m_minX();

        EnsureOverlayUI();
        SetOverlayVisible(false);
    }

    void PauseDeltaTimeDemo::Update(float deltaTime)
    {
        auto* input = Input();
        auto* t = GetTransform();
        if (!input || !t)
            return;

        if (input->GetKeyDown(KeyCode::Escape))
        {
            const bool nextPaused = !input->IsDeltaTimeStopped();
            input->StopDeltaTime(nextPaused);

            if (nextPaused)
            {
                m_pausedUnscaledAccum = 0.0f;
                input->SetCursorVisible(true);
                input->SetCursorLocked(false);
            }

            SetOverlayVisible(nextPaused);
        }

        const float minX = std::min(Get_m_minX(), Get_m_maxX());
        const float maxX = std::max(Get_m_minX(), Get_m_maxX());
        const float speed = std::max(0.0f, Get_m_moveSpeed());

        if (m_moveToPositive)
        {
            t->position.x += speed * deltaTime;
            if (t->position.x >= maxX)
            {
                t->position.x = maxX;
                m_moveToPositive = false;
            }
        }
        else
        {
            t->position.x -= speed * deltaTime;
            if (t->position.x <= minX)
            {
                t->position.x = minX;
                m_moveToPositive = true;
            }
        }

        if (input->IsDeltaTimeStopped())
            m_pausedUnscaledAccum += std::max(0.0f, input->GetUnscaledDeltaTime());

        UpdateOverlayText(input->IsDeltaTimeStopped());
    }

    void PauseDeltaTimeDemo::EnsureOverlayUI()
    {
        World* w = GetWorld();
        if (!w)
            return;

        if (m_overlayRoot != InvalidEntityId && w->GetComponent<UIWidgetComponent>(m_overlayRoot))
            return;

        const EntityId root = w->CreateEntity();
        w->SetEntityName(root, "PauseOverlayRoot");

        auto& rootTr = w->AddComponent<TransformComponent>(root);
        rootTr.enabled = true;
        rootTr.position = { 0.0f, 0.0f, 0.0f };
        rootTr.rotation = { 0.0f, 0.0f, 0.0f };
        rootTr.scale = { 1.0f, 1.0f, 1.0f };

        auto& rootWidget = w->AddComponent<UIWidgetComponent>(root);
        rootWidget.widgetName = "PauseOverlayRoot";
        rootWidget.space = AliceUI::UISpace::Screen;
        rootWidget.visibility = AliceUI::UIVisibility::Collapsed;
        rootWidget.interactable = false;
        rootWidget.raycastTarget = false;

        auto& rootUiTr = w->AddComponent<UITransformComponent>(root);
        rootUiTr.anchorMin = { 0.5f, 0.5f };
        rootUiTr.anchorMax = { 0.5f, 0.5f };
        rootUiTr.pivot = { 0.0f, 0.0f };
        rootUiTr.position = { 0.0f, 0.0f };
        rootUiTr.size = { 1.0f, 1.0f };
        rootUiTr.scale = { 1.0f, 1.0f };
        rootUiTr.sortOrder = 100;

        const EntityId panel = w->CreateEntity();
        w->SetEntityName(panel, "PauseOverlayPanel");
        w->SetParent(panel, root, false);

        auto& panelTr = w->AddComponent<TransformComponent>(panel);
        panelTr.enabled = true;
        panelTr.position = { 0.0f, 0.0f, 0.0f };
        panelTr.rotation = { 0.0f, 0.0f, 0.0f };
        panelTr.scale = { 1.0f, 1.0f, 1.0f };

        auto& panelWidget = w->AddComponent<UIWidgetComponent>(panel);
        panelWidget.widgetName = "PauseOverlayPanel";
        panelWidget.space = AliceUI::UISpace::Screen;
        panelWidget.visibility = AliceUI::UIVisibility::Visible;
        panelWidget.interactable = false;
        panelWidget.raycastTarget = false;
        panelWidget.shaderName = "DieLine";

        auto& panelUiTr = w->AddComponent<UITransformComponent>(panel);
        panelUiTr.anchorMin = { 0.5f, 0.5f };
        panelUiTr.anchorMax = { 0.5f, 0.5f };
        panelUiTr.pivot = { 0.0f, 0.0f };
        panelUiTr.position = { 0.0f, 0.0f };
        panelUiTr.size = { 760.0f, 260.0f };
        panelUiTr.scale = { 1.0f, 1.0f };
        panelUiTr.sortOrder = 101;

        auto& panelImage = w->AddComponent<UIImageComponent>(panel);
        panelImage.texturePath = Get_m_panelTexturePath();
        panelImage.color = { 1.0f, 1.0f, 1.0f, 0.75f };
        panelImage.preserveAspect = false;

        auto& dieLine = w->AddComponent<UIDieLineParamsComponent>(panel);
        dieLine.totalCycle = 2.0f;
        dieLine.phase1Duration = 0.65f;
        dieLine.phase2End = 1.35f;
        dieLine.phase3Duration = 0.65f;
        dieLine.startTime = -1.0f;

        const EntityId text = w->CreateEntity();
        w->SetEntityName(text, "PauseOverlayText");
        w->SetParent(text, root, false);

        auto& textTr = w->AddComponent<TransformComponent>(text);
        textTr.enabled = true;
        textTr.position = { 0.0f, 0.0f, 0.0f };
        textTr.rotation = { 0.0f, 0.0f, 0.0f };
        textTr.scale = { 1.0f, 1.0f, 1.0f };

        auto& textWidget = w->AddComponent<UIWidgetComponent>(text);
        textWidget.widgetName = "PauseOverlayText";
        textWidget.space = AliceUI::UISpace::Screen;
        textWidget.visibility = AliceUI::UIVisibility::Visible;
        textWidget.interactable = false;
        textWidget.raycastTarget = false;
        textWidget.shaderName = "Default";

        auto& textUiTr = w->AddComponent<UITransformComponent>(text);
        textUiTr.anchorMin = { 0.5f, 0.5f };
        textUiTr.anchorMax = { 0.5f, 0.5f };
        textUiTr.pivot = { 0.0f, 0.0f };
        textUiTr.position = { 0.0f, 0.0f };
        textUiTr.size = { 700.0f, 220.0f };
        textUiTr.scale = { 1.0f, 1.0f };
        textUiTr.sortOrder = 102;

        auto& textComp = w->AddComponent<UITextComponent>(text);
        textComp.fontPath = Get_m_fontPath();
        textComp.fontSize = 34.0f;
        textComp.text = "ESC";
        textComp.alignH = AliceUI::UIAlignH::Center;
        textComp.alignV = AliceUI::UIAlignV::Center;
        textComp.wrap = false;
        textComp.maxWidth = 0.0f;
        textComp.lineSpacing = 6.0f;
        textComp.color = { 1.0f, 1.0f, 1.0f, 1.0f };

        m_overlayRoot = root;
        m_overlayText = text;
    }

    void PauseDeltaTimeDemo::SetOverlayVisible(bool visible)
    {
        World* w = GetWorld();
        if (!w || m_overlayRoot == InvalidEntityId || !w->GetComponent<UIWidgetComponent>(m_overlayRoot))
            return;

        SetSubtreeVisibility(*w, m_overlayRoot, visible ? AliceUI::UIVisibility::Visible : AliceUI::UIVisibility::Collapsed);
    }

    void PauseDeltaTimeDemo::UpdateOverlayText(bool paused)
    {
        World* w = GetWorld();
        if (!w || m_overlayText == InvalidEntityId || !w->GetComponent<UITextComponent>(m_overlayText))
            return;

        auto* text = w->GetComponent<UITextComponent>(m_overlayText);
        if (!text)
            return;

        if (!paused)
        {
            text->text = "ESC";
            return;
        }

        std::ostringstream oss;
        oss << Get_m_pauseMessage() << "\nUI UnscaledTime: "
            << std::fixed << std::setprecision(2) << m_pausedUnscaledAccum << "s";
        text->text = oss.str();
    }
}
