#include "FadeInOutScript.h"

#include <algorithm>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "Runtime/UI/BindWidget.h"

namespace Alice
{
    REGISTER_SCRIPT(FadeInOutScript);

    namespace
    {
        EntityId SearchRootWidgetByName(World& world, const std::string& name)
        {
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

        EntityId ResolveTargetWidget(World& world,
                                     EntityId ownerId,
                                     const std::string& rootName,
                                     const std::string& targetName)
        {
            if (targetName.empty())
                return ownerId;

            if (rootName.empty())
            {
                for (auto [id, widget] : world.GetComponents<UIWidgetComponent>())
                {
                    const std::string widgetName = widget.widgetName.empty()
                        ? world.GetEntityName(id)
                        : widget.widgetName;
                    if (!widgetName.empty() && widgetName == targetName)
                        return id;
                }
                return InvalidEntityId;
            }

            const EntityId root = SearchRootWidgetByName(world, rootName);
            if (root == InvalidEntityId)
                return InvalidEntityId;

            return AliceUI::FindWidgetByName(world, root, targetName);
        }
    }

    bool FadeInOutScript::InitTarget()
    {
        World* w = GetWorld();
        if (!w)
            return false;

        if (m_targetId == InvalidEntityId)
        {
            m_targetId = ResolveTargetWidget(*w, GetOwnerId(), Get_rootWidgetName(), Get_targetWidgetName());
            m_baseAlphaInitialized = false;
        }

        if (m_targetId == InvalidEntityId)
        {
            if (!Get_targetWidgetName().empty())
                ALICE_LOG_WARN("[FadeInOutScript] Target widget not found: %s", Get_targetWidgetName().c_str());
            return false;
        }

        m_image = w->GetComponent<UIImageComponent>(m_targetId);
        m_text = w->GetComponent<UITextComponent>(m_targetId);

        if (!m_image && !m_text)
        {
            ALICE_LOG_WARN("[FadeInOutScript] Target has no UIImage/UIText: %s", Get_targetWidgetName().c_str());
            return false;
        }

        if (!m_baseAlphaInitialized)
        {
            if (m_image)
                m_baseImageAlpha = m_image->color.w;
            if (m_text)
                m_baseTextAlpha = m_text->color.w;
            m_baseAlphaInitialized = true;
        }

        return true;
    }

    void FadeInOutScript::ApplyAlpha()
    {
        // Apply current alpha directly (0..1).
        if (m_image)
        {
            auto c = m_image->color;
            c.w = m_currentAlpha;
            m_image->color = c;
        }
        if (m_text)
        {
            auto c = m_text->color;
            c.w = m_currentAlpha;
            m_text->color = c;
        }
    }

    void FadeInOutScript::Start()
    {
        if (!InitTarget())
            return;

        m_currentAlpha = std::clamp(Get_startAlpha(), 0.0f, 1.0f);
        ApplyAlpha();
        m_shouldFadeToBlack = Get_isFadeIn();
    }

    void FadeInOutScript::OnEnable()
    {
        if (!InitTarget())
            return;

        m_currentAlpha = std::clamp(Get_startAlpha(), 0.0f, 1.0f);
        ApplyAlpha();
        m_shouldFadeToBlack = Get_isFadeIn();
    }

    void FadeInOutScript::Update(float deltaTime)
    {
        if (!m_image && !m_text)
            return;

        const float targetAlpha = m_shouldFadeToBlack ? 1.0f : 0.0f;
        const float speed = std::max(0.0f, Get_fadeSpeed());
        const float dt = std::max(0.0f, deltaTime);

        if (m_currentAlpha < targetAlpha)
        {
            m_currentAlpha = std::min(m_currentAlpha + speed * dt, targetAlpha);
        }
        else if (m_currentAlpha > targetAlpha)
        {
            m_currentAlpha = std::max(m_currentAlpha - speed * dt, targetAlpha);
        }

        ApplyAlpha();
    }

    void FadeInOutScript::SetFadeToBlack(bool fadeToBlack)
    {
        m_shouldFadeToBlack = fadeToBlack;
    }

    void FadeInOutScript::StartFadeIn()
    {
        m_shouldFadeToBlack = true;
    }

    void FadeInOutScript::StartFadeOut()
    {
        m_shouldFadeToBlack = false;
    }

    bool FadeInOutScript::IsFadingToBlack() const
    {
        return m_shouldFadeToBlack;
    }

    bool FadeInOutScript::IsFadeComplete() const
    {
        if (m_shouldFadeToBlack)
            return m_currentAlpha >= 1.0f;
        return m_currentAlpha <= 0.0f;
    }
}
