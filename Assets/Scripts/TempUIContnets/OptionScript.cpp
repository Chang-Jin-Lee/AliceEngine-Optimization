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
                const std::string entityName = world.GetEntityName(id);
                const std::string widgetName = widget.widgetName;
                if ((!widgetName.empty() && widgetName == name) ||
                    (!entityName.empty() && entityName == name))
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

        void SetInputRecursive(World& world, EntityId root, bool enabled)
        {
            if (auto* widget = world.GetComponent<UIWidgetComponent>(root))
            {
                widget->interactable = enabled;
                widget->raycastTarget = enabled;
            }
            if (auto* button = world.GetComponent<UIButtonComponent>(root))
            {
                button->enabled = enabled;
            }

            auto children = world.GetChildren(root);
            for (EntityId child : children)
            {
                SetInputRecursive(world, child, enabled);
            }
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
            {
                m_lineNormalColor = imgComp->color;
                if (!imgComp->texturePath.empty())
                    m_lineNormalTexturePath = imgComp->texturePath;
                if (Get_showUnderImageOnHoverOnly())
                {
                    auto c = imgComp->color;
                    c.w = 0.0f;
                    imgComp->color = c;
                }
            }
        }
        if (m_buttonEntityId != InvalidEntityId)
        {
            if (auto* imgComp = w->GetComponent<UIImageComponent>(m_buttonEntityId))
            {
                m_buttonNormalColor = imgComp->color;
                const std::string& fallbackButtonPath = Get_buttonImageNormalPath();
                if (!imgComp->texturePath.empty())
                {
                    m_buttonNormalTexturePath = imgComp->texturePath;
                    ALICE_LOG_INFO("[OptionScript] Texture backup success: %s", m_buttonNormalTexturePath.c_str());
                }
                else if (!fallbackButtonPath.empty())
                {
                    m_buttonNormalTexturePath = fallbackButtonPath;
                    ALICE_LOG_INFO("[OptionScript] Using buttonImageNormalPath fallback: %s", m_buttonNormalTexturePath.c_str());
                }
                else
                {
                    ALICE_LOG_WARN("[OptionScript] Button has no texture path in scene!");
                }

                // hover 전에는 이미지가 보이지 않도록 텍스처 비우기 (알파 사용 안 함)
                if (Get_showButtonImageOnHoverOnly())
                {
                    auto c = imgComp->color;
                    c.w = 0.0f;
                    imgComp->color = c;
                }
                else if (imgComp->texturePath.empty() && !m_buttonNormalTexturePath.empty())
                {
                    imgComp->texturePath = m_buttonNormalTexturePath;
                }
            }
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

        if (Get_enableClick())
        {
            m_button->AddOnReleasedSafe([this]()
            {
                if (m_uiSound)
                    m_uiSound->PlayClick();
                HandleClickTarget();
            }, isValid);
        }

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

        bool targetVisible = false;
        if (Get_clickToggleTarget())
        {
            const auto nextVis = (widget->visibility == AliceUI::UIVisibility::Visible)
                ? AliceUI::UIVisibility::Collapsed
                : AliceUI::UIVisibility::Visible;
            SetVisibilityRecursive(*w, m_clickTargetEntity, nextVis);
            targetVisible = (nextVis == AliceUI::UIVisibility::Visible);
        }
        else
        {
            const auto vis = Get_clickSetVisible()
                ? AliceUI::UIVisibility::Visible
                : AliceUI::UIVisibility::Collapsed;
            SetVisibilityRecursive(*w, m_clickTargetEntity, vis);
            targetVisible = (vis == AliceUI::UIVisibility::Visible);
        }

        if (!Get_blockInputRootWidgetName().empty())
        {
            const EntityId blockRoot = SearchRootWidgetByName(*w, Get_blockInputRootWidgetName());
            if (blockRoot != InvalidEntityId)
            {
                const bool block = Get_blockInputWhenTargetVisible() ? targetVisible : !targetVisible;
                SetInputRecursive(*w, blockRoot, !block);
            }
            else
            {
                ALICE_LOG_WARN("[OptionScript] blockInputRootWidgetName not found: %s",
                    Get_blockInputRootWidgetName().c_str());
            }
        }
    }

    void OptionScript::ApplyChildColors(AliceUI::UIButtonState state)
    {
        World* w = GetWorld();
        if (!w) return;

        const bool isHovered = (state == AliceUI::UIButtonState::Hovered || state == AliceUI::UIButtonState::Pressed);

        // 1. Text color by state
        const DirectX::XMFLOAT4* textColor = &m_textNormalColor;
        switch (state)
        {
        case AliceUI::UIButtonState::Hovered: textColor = &m_hoverColor; break;
        case AliceUI::UIButtonState::Pressed: textColor = &m_pressedColor; break;
        default: break;
        }

        if (m_textEntityId != InvalidEntityId)
        {
            if (auto* textComp = w->GetComponent<UITextComponent>(m_textEntityId))
                textComp->color = *textColor;
        }

        // 2. Button background image path control
        if (m_buttonEntityId != InvalidEntityId)
        {
            if (auto* imgComp = w->GetComponent<UIImageComponent>(m_buttonEntityId))
            {
                const std::string& fallbackButtonPath = Get_buttonImageNormalPath();
                const std::string& buttonPath = !m_buttonNormalTexturePath.empty()
                    ? m_buttonNormalTexturePath
                    : fallbackButtonPath;
                std::string desiredPath;
                if (Get_showButtonImageOnHoverOnly())
                {
                    if (state == AliceUI::UIButtonState::Hovered || state == AliceUI::UIButtonState::Pressed)
                        desiredPath = buttonPath;
                    else
                        desiredPath.clear();
                }
                else
                {
                    desiredPath = buttonPath;
                }

                if (buttonPath.empty() && !m_loggedEmptyButtonPath)
                {
                    ALICE_LOG_WARN("[OptionScript] buttonPath is empty. Check scene texturePath or buttonImageNormalPath.");
                    m_loggedEmptyButtonPath = true;
                }

                if (!buttonPath.empty() && imgComp->texturePath != buttonPath)
                    imgComp->texturePath = buttonPath;

                auto c = m_buttonNormalColor;
                if (Get_showButtonImageOnHoverOnly())
                    c.w = (state == AliceUI::UIButtonState::Hovered || state == AliceUI::UIButtonState::Pressed) ? m_buttonNormalColor.w : 0.0f;
                imgComp->color = c;
            }
        }

        // 3. Decoration (underline) image path control
        if (m_underLineEntityId != InvalidEntityId)
        {
            if (auto* lineImg = w->GetComponent<UIImageComponent>(m_underLineEntityId))
            {
                const std::string& normalPath = Get_underImageNormalPath();
                const std::string& hoverPath = Get_underImageHoverPath();
                const bool hasPaths = !normalPath.empty() || !hoverPath.empty();
                const bool useFallback = !hasPaths && !m_lineNormalTexturePath.empty();

                std::string desiredPath;
                if (hasPaths)
                {
                    if (state == AliceUI::UIButtonState::Hovered || state == AliceUI::UIButtonState::Pressed)
                        desiredPath = hoverPath.empty() ? normalPath : hoverPath;
                    else
                        desiredPath = normalPath.empty() ? hoverPath : normalPath;
                }
                else if (useFallback)
                {
                    desiredPath = m_lineNormalTexturePath;
                }

                if (!desiredPath.empty() && lineImg->texturePath != desiredPath)
                    lineImg->texturePath = desiredPath;

                auto c = m_lineNormalColor;
                if (Get_showUnderImageOnHoverOnly())
                    c.w = (state == AliceUI::UIButtonState::Hovered || state == AliceUI::UIButtonState::Pressed) ? m_lineNormalColor.w : 0.0f;
                lineImg->color = c;
            }
        }
    }
}
