#include "MultiImageScript.h"

#include <algorithm>
#include <cctype>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"

namespace Alice
{
    REGISTER_SCRIPT(MultiImageScript);

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

    void MultiImageScript::Start()
    {
        m_elapsed = 0.0f;
        m_currentIndex = 0;
        m_playing = false;
        m_initialized = false;
        m_targetId = InvalidEntityId;
        m_targetImage = nullptr;
        m_images.clear();

        ResolveTarget();
        ParsePaths();

        if (!m_images.empty())
        {
            const int maxIndex = static_cast<int>(m_images.size()) - 1;
            const int idx = std::clamp(Get_startIndex(), 0, maxIndex);
            m_currentIndex = idx;
            ApplyImage();
        }

        m_playing = Get_playOnStart();
        m_initialized = true;
    }

    void MultiImageScript::Update(float deltaTime)
    {
        if (!m_initialized)
            return;

        if (!m_playing)
            return;

        if (!m_targetImage || m_images.empty())
            return;

        const float interval = Get_intervalSeconds();
        if (interval <= 0.0f)
            return;

        const float dt = std::max(0.0f, deltaTime);
        m_elapsed += dt;

        while (m_elapsed >= interval)
        {
            m_elapsed -= interval;
            Advance();
            if (!m_playing)
                break;
        }
    }

    void MultiImageScript::ResolveTarget()
    {
        World* w = GetWorld();
        if (!w)
            return;

        m_targetId = InvalidEntityId;

        if (Get_imageWidgetName().empty())
        {
            m_targetId = GetOwnerId();
        }
        else
        {
            for (auto [id, widget] : w->GetComponents<UIWidgetComponent>())
            {
                const std::string widgetName = widget.widgetName.empty()
                    ? w->GetEntityName(id)
                    : widget.widgetName;
                if (!widgetName.empty() && widgetName == Get_imageWidgetName())
                {
                    m_targetId = id;
                    break;
                }
            }
        }

        if (m_targetId != InvalidEntityId)
        {
            m_targetImage = w->GetComponent<UIImageComponent>(m_targetId);
        }

        if (!m_targetImage)
        {
            if (Get_imageWidgetName().empty())
                ALICE_LOG_WARN("[MultiImageScript] UIImageComponent not found on owner.");
            else
                ALICE_LOG_WARN("[MultiImageScript] UIImageComponent not found: %s", Get_imageWidgetName().c_str());
        }
    }

    void MultiImageScript::ParsePaths()
    {
        m_images.clear();

        const std::string& src = Get_imagePaths();
        if (src.empty())
            return;

        std::string token;
        token.reserve(src.size());

        for (char ch : src)
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

        if (m_images.empty())
        {
            ALICE_LOG_WARN("[MultiImageScript] imagePaths is set but no valid entries were parsed.");
        }
    }

    void MultiImageScript::ApplyImage()
    {
        if (!m_targetImage || m_images.empty())
            return;

        if (m_currentIndex < 0 || m_currentIndex >= static_cast<int>(m_images.size()))
            return;

        const std::string& path = m_images[static_cast<size_t>(m_currentIndex)];
        if (m_targetImage->texturePath != path)
            m_targetImage->texturePath = path;
    }

    void MultiImageScript::Advance()
    {
        if (m_images.empty())
            return;

        int nextIndex = m_currentIndex + 1;
        const int lastIndex = static_cast<int>(m_images.size()) - 1;

        if (nextIndex > lastIndex)
        {
            if (Get_loop())
            {
                nextIndex = 0;
            }
            else
            {
                nextIndex = lastIndex;
                m_playing = false;
            }
        }

        if (nextIndex != m_currentIndex)
        {
            m_currentIndex = nextIndex;
            ApplyImage();
        }
    }
}
