#include "OptionScript.h"

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/Input/Input.h"
#include "Runtime/Audio/SoundManager.h"
#include "Runtime/UI/BindWidget.h"
#include "Runtime/UI/UIButtonComponent.h"
#include "Runtime/UI/UITextComponent.h"
#include "Runtime/UI/UIImageComponent.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "../Tempsound/UISoundScript.h"

namespace Alice
{
    REGISTER_SCRIPT(OptionScript);

    namespace
    {
        EntityId SearchRootWidgetByName(World& world, const std::string& name)
        {
            if (name.empty())
                return InvalidEntityId;

            for (auto [id, widget] : world.GetComponents<UIWidgetComponent>())
            {
                const std::string widgetName = widget.widgetName.empty() ? world.GetEntityName(id) : widget.widgetName;
                if (!widgetName.empty() && widgetName == name)
                    return id;
            }
            return InvalidEntityId;
        }

        void SetChildrenVisibility(World& world, EntityId root, AliceUI::UIVisibility visibility)
        {
            auto children = world.GetChildren(root);
            ALICE_LOG_INFO("[OptionScript] Setting visibility for %zu children of entity %llu",
                children.size(), static_cast<unsigned long long>(root));

            for (EntityId child : children)
            {
                if (auto* widget = world.GetComponent<UIWidgetComponent>(child))
                {
                    widget->visibility = visibility;
                    ALICE_LOG_INFO("[OptionScript] Set child %llu visibility to %d",
                        static_cast<unsigned long long>(child), static_cast<int>(visibility));
                }
                SetChildrenVisibility(world, child, visibility);  // recursive
            }
        }

        void SetVisibilityRecursive(World& world, EntityId root, AliceUI::UIVisibility visibility)
        {
            if (auto* widget = world.GetComponent<UIWidgetComponent>(root))
            {
                widget->visibility = visibility;
            }
            SetChildrenVisibility(world, root, visibility);
        }

        UISoundScript* FindUISound(World& world, const std::string& name)
        {
            if (name.empty())
                return nullptr;
            GameObject go = world.FindGameObject(name);
            if (!go.IsValid())
                return nullptr;
            auto* scripts = world.GetScripts(go.id());
            if (!scripts)
                return nullptr;
            for (auto& sc : *scripts)
            {
                if (sc.scriptName == "UISoundScript" && sc.instance)
                    return static_cast<UISoundScript*>(sc.instance.get());
            }
            return nullptr;
        }
    }

    void OptionScript::Start()
    {
        World* w = GetWorld();
        if (!w)
            return;

        if (Get_enableToggleChildrenOnEsc())
        {
            if (!Get_rootWidgetName().empty())
            {
                rootEntity = SearchRootWidgetByName(*w, Get_rootWidgetName());
            }

            if (rootEntity == InvalidEntityId)
            {
                rootEntity = GetOwnerId();
            }

            if (rootEntity == InvalidEntityId)
            {
                ALICE_LOG_WARN("[OptionScript] Root entity not found (rootWidgetName=%s)", Get_rootWidgetName().c_str());
            }

            childrenVisible = false;
            if (rootEntity != InvalidEntityId)
            {
                SetChildrenVisibility(*w, rootEntity, AliceUI::UIVisibility::Collapsed);
            }
        }

        if (!Get_clickTargetWidgetName().empty())
        {
            m_clickTargetEntity = SearchRootWidgetByName(*w, Get_clickTargetWidgetName());
            if (m_clickTargetEntity == InvalidEntityId)
            {
                ALICE_LOG_WARN("[OptionScript] Click target not found (clickTargetWidgetName=%s)",
                    Get_clickTargetWidgetName().c_str());
            }
        }

        // Button visual/sound handling (no scene change)
        const std::string buttonName = Get_buttonWidgetName();
        const std::string buttonRootName = Get_buttonRootWidgetName();
        EntityId buttonRoot = InvalidEntityId;
        if (!buttonRootName.empty())
            buttonRoot = SearchRootWidgetByName(*w, buttonRootName);
        else if (!buttonName.empty())
            buttonRoot = SearchRootWidgetByName(*w, buttonName);

        if (buttonRoot == InvalidEntityId)
            buttonRoot = GetOwnerId();

        EntityId buttonEntity = InvalidEntityId;
        if (!buttonName.empty())
            buttonEntity = AliceUI::FindWidgetByName(*w, buttonRoot, buttonName);
        else
            buttonEntity = buttonRoot;

        m_buttonEntityId = buttonEntity;
        m_button = (buttonEntity != InvalidEntityId) ? w->GetComponent<UIButtonComponent>(buttonEntity) : nullptr;
        if (!m_button)
            return;

        m_uiSound = FindUISound(*w, Get_uiSoundEntityName());

        const std::string textName = Get_TextWidgetName();
        if (!textName.empty())
            m_textEntityId = AliceUI::FindWidgetByName(*w, buttonEntity, textName);

        const std::string lineName = Get_UnderLineWidgetName();
        if (!lineName.empty())
            m_underLineEntityId = AliceUI::FindWidgetByName(*w, buttonEntity, lineName);

        if (m_textEntityId != InvalidEntityId)
        {
            if (auto* textComp = w->GetComponent<UITextComponent>(m_textEntityId))
                m_textNormalColor = textComp->color;
        }
        if (m_underLineEntityId != InvalidEntityId)
        {
            if (auto* imgComp = w->GetComponent<UIImageComponent>(m_underLineEntityId))
                m_lineNormalColor = imgComp->color;
        }
        if (m_buttonEntityId != InvalidEntityId)
        {
            if (auto* imgComp = w->GetComponent<UIImageComponent>(m_buttonEntityId))
                m_buttonNormalColor = imgComp->color;
        }

        const EntityId ownerId = GetOwnerId();
        const std::uint32_t ownerGen = w->GetEntityGeneration(ownerId);
        const auto isValid = [w, ownerId, ownerGen, self = this]() -> bool
        {
            if (!w)
                return false;
            if (!w->IsEntityValid(ownerId, ownerGen))
                return false;
            const auto* scripts = w->GetScripts(ownerId);
            if (!scripts)
                return false;
            for (const auto& sc : *scripts)
            {
                if (sc.instance.get() == self)
                    return true;
            }
            return false;
        };

        m_button->AddOnHoveredSafe([this]()
        {
            if (m_uiSound)
                m_uiSound->PlayHover();
        }, isValid);

        m_button->AddOnReleasedSafe([this]()
        {
            if (m_uiSound)
                m_uiSound->PlayClick();
            HandleClickTarget();
        }, isValid);

        ApplyChildColors(AliceUI::UIButtonState::Normal);
    }

    void OptionScript::Update(float /*deltaTime*/)
    {
        World* w = GetWorld();
        if (!w)
            return;

        if (Get_enableToggleChildrenOnEsc() && rootEntity != InvalidEntityId)
        {
            auto* input = Input();
            if (input && input->GetKeyDown(KeyCode::Escape))
            {
                childrenVisible = !childrenVisible;
                const auto vis = childrenVisible ? AliceUI::UIVisibility::Visible : AliceUI::UIVisibility::Collapsed;

                ALICE_LOG_INFO("[OptionScript] ESC pressed. Setting visibility to %s",
                    childrenVisible ? "Visible" : "Collapsed");

                SetChildrenVisibility(*w, rootEntity, vis);

                // Pause/resume all sounds when option menu toggles
                Sound::PauseAll(childrenVisible);

                auto children = w->GetChildren(rootEntity);
                ALICE_LOG_INFO("[OptionScript] Found %zu children", children.size());
            }
        }

        if (m_button)
            ApplyChildColors(m_button->state);
    }

    void OptionScript::OnDestroy()
    {
        if (m_button)
            m_button->ClearDelegates();
    }

    void OptionScript::HandleClickTarget()
    {
        if (m_clickTargetEntity == InvalidEntityId)
            return;

        World* w = GetWorld();
        if (!w)
            return;

        auto* widget = w->GetComponent<UIWidgetComponent>(m_clickTargetEntity);
        if (!widget)
            return;

        if (Get_clickToggleTarget())
        {
            const auto nextVis = (widget->visibility == AliceUI::UIVisibility::Visible)
                ? AliceUI::UIVisibility::Collapsed
                : AliceUI::UIVisibility::Visible;
            SetVisibilityRecursive(*w, m_clickTargetEntity, nextVis);
        }
        else
        {
            SetVisibilityRecursive(*w, m_clickTargetEntity, AliceUI::UIVisibility::Collapsed);
        }
    }

    void OptionScript::ApplyChildColors(AliceUI::UIButtonState state)
    {
        World* w = GetWorld();
        if (!w)
            return;

        const bool isHovered = (state == AliceUI::UIButtonState::Hovered || state == AliceUI::UIButtonState::Pressed);
        const DirectX::XMFLOAT4* textColor = &m_textNormalColor;
        const DirectX::XMFLOAT4* lineColor = &m_lineNormalColor;
        switch (state)
        {
        case AliceUI::UIButtonState::Hovered:
            textColor = &m_hoverColor;
            lineColor = &m_hoverColor;
            break;
        case AliceUI::UIButtonState::Pressed:
            textColor = &m_pressedColor;
            lineColor = &m_pressedColor;
            break;
        default:
            break;
        }

        if (m_textEntityId != InvalidEntityId)
        {
            if (auto* textComp = w->GetComponent<UITextComponent>(m_textEntityId))
                textComp->color = *textColor;
        }
        if (m_underLineEntityId != InvalidEntityId)
        {
            if (auto* imgComp = w->GetComponent<UIImageComponent>(m_underLineEntityId))
            {
                const std::string& normalPath = Get_underImageNormalPath();
                const std::string& hoverPath = Get_underImageHoverPath();
                if (!normalPath.empty() || !hoverPath.empty())
                {
                    const std::string& desired = isHovered
                        ? (hoverPath.empty() ? normalPath : hoverPath)
                        : normalPath;
                    if (!desired.empty() && imgComp->texturePath != desired)
                        imgComp->texturePath = desired;
                }

                DirectX::XMFLOAT4 color = *lineColor;
                if (Get_showUnderImageOnHoverOnly() && !isHovered)
                    color.w = 0.0f;
                imgComp->color = color;
            }
        }

        if (m_buttonEntityId != InvalidEntityId && Get_showButtonImageOnHoverOnly())
        {
            if (auto* imgComp = w->GetComponent<UIImageComponent>(m_buttonEntityId))
            {
                DirectX::XMFLOAT4 color = m_buttonNormalColor;
                if (!isHovered)
                    color.w = 0.0f;
                imgComp->color = color;
            }
        }
    }
}
