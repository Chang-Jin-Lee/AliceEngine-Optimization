#include "GuideImageSequenceScript.h"

#include <algorithm>
#include <cctype>

#include "Runtime/ECS/World.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Input/Input.h"
#include "Runtime/Input/InputTypes.h"
#include "Runtime/Scripting/ScriptFactory.h"

namespace Alice
{
    REGISTER_SCRIPT(GuideImageSequenceScript);

    namespace
    {
        void TrimInPlace(std::string& value)
        {
            size_t start = 0;
            while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])))
                ++start;

            size_t end = value.size();
            while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
                --end;

            if (start == 0 && end == value.size())
                return;

            value = value.substr(start, end - start);
        }
    }

    void GuideImageSequenceScript::Start()
    {
        m_targetId = InvalidEntityId;
        m_targetImage = nullptr;
        m_images.clear();
        m_sceneRequested = false;
        m_cooldownRemaining = 0.0f;
        m_currentIndex = 0;

        ResolveTarget();
        ParsePaths();

        if (m_images.empty())
        {
            ALICE_LOG_WARN("[GuideImageSequenceScript] imagePaths is empty.");
            return;
        }

        const int maxIndex = static_cast<int>(m_images.size()) - 1;
        m_currentIndex = std::clamp(Get_startIndex(), 0, maxIndex);
        ApplyCurrentImage();
    }

    void GuideImageSequenceScript::Update(float deltaTime)
    {
        if (m_sceneRequested || m_images.empty())
            return;

        m_cooldownRemaining = std::max(0.0f, m_cooldownRemaining - std::max(0.0f, deltaTime));
        if (m_cooldownRemaining > 0.0f)
            return;

        if (!ConsumeAdvanceInput())
            return;

        m_cooldownRemaining = std::max(0.0f, Get_inputCooldownSec());
        AdvanceOrSwitch();
    }

    void GuideImageSequenceScript::ResolveTarget()
    {
        World* world = GetWorld();
        if (!world)
            return;

        m_targetId = InvalidEntityId;
        const std::string targetName = Get_imageWidgetName();

        if (targetName.empty())
        {
            m_targetId = GetOwnerId();
        }
        else
        {
            for (auto [id, widget] : world->GetComponents<UIWidgetComponent>())
            {
                const std::string widgetName = widget.widgetName.empty() ? world->GetEntityName(id) : widget.widgetName;
                if (!widgetName.empty() && widgetName == targetName)
                {
                    m_targetId = id;
                    break;
                }
            }
        }

        if (m_targetId != InvalidEntityId)
            m_targetImage = world->GetComponent<UIImageComponent>(m_targetId);

        if (!m_targetImage)
        {
            if (targetName.empty())
                ALICE_LOG_WARN("[GuideImageSequenceScript] UIImageComponent not found on owner.");
            else
                ALICE_LOG_WARN("[GuideImageSequenceScript] UIImageComponent not found: %s", targetName.c_str());
        }
    }

    void GuideImageSequenceScript::ParsePaths()
    {
        m_images.clear();

        const std::string& source = Get_imagePaths();
        if (source.empty())
            return;

        std::string token;
        token.reserve(source.size());

        for (const char ch : source)
        {
            if (ch == ';' || ch == '|' || ch == ',' || ch == '\n' || ch == '\r')
            {
                TrimInPlace(token);
                if (!token.empty())
                    m_images.push_back(token);
                token.clear();
                continue;
            }
            token.push_back(ch);
        }

        TrimInPlace(token);
        if (!token.empty())
            m_images.push_back(token);
    }

    void GuideImageSequenceScript::ApplyCurrentImage()
    {
        if (!m_targetImage || m_images.empty())
            return;

        if (m_currentIndex < 0 || m_currentIndex >= static_cast<int>(m_images.size()))
            return;

        m_targetImage->texturePath = m_images[static_cast<size_t>(m_currentIndex)];
    }

    bool GuideImageSequenceScript::ConsumeAdvanceInput() const
    {
        auto* input = Input();
        if (!input)
            return false;

        if (Get_allowMouseLeftClick() && input->GetMouseButtonDown(MouseCode::Left))
            return true;
        if (Get_allowSpaceKey() && input->GetKeyDown(KeyCode::Space))
            return true;
        if (Get_allowEnterKey() && input->GetKeyDown(KeyCode::Enter))
            return true;
        if (Get_allowEscapeKey() && input->GetKeyDown(KeyCode::Escape))
            return true;
        if (Get_allowGamepadAButton())
        {
            const int playerIndex = std::clamp(Get_gamepadPlayerIndex(), 0, 3);
            if (input->GetGamepadButtonDown(GamepadButton::A, playerIndex))
                return true;
        }

        return false;
    }

    void GuideImageSequenceScript::AdvanceOrSwitch()
    {
        const int lastIndex = static_cast<int>(m_images.size()) - 1;
        if (m_currentIndex < lastIndex)
        {
            ++m_currentIndex;
            ApplyCurrentImage();
            return;
        }

        auto* scenes = Scenes();
        if (!scenes)
        {
            ALICE_LOG_WARN("[GuideImageSequenceScript] SceneManager not available.");
            return;
        }

        const std::string target = Get_targetScenePath();
        if (target.empty())
        {
            ALICE_LOG_WARN("[GuideImageSequenceScript] targetScenePath is empty.");
            return;
        }

        m_sceneRequested = true;
        scenes->LoadSceneFileRequest(target.c_str());
    }
}
