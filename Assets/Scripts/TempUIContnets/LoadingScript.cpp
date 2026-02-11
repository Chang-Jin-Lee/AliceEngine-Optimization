#include "LoadingScript.h"

#include <algorithm>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/GameObject.h"
#include "FadeInOutScript.h"

namespace Alice
{
    REGISTER_SCRIPT(LoadingScript);

    namespace
    {
        FadeInOutScript* FindFadeScript(World& world, const std::string& name)
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
                if (sc.scriptName == "FadeInOutScript" && sc.instance)
                    return static_cast<FadeInOutScript*>(sc.instance.get());
            }
            return nullptr;
        }
    }

    void LoadingScript::Start()
    {
        m_timer = 0.0f;
        m_fadeTimer = 0.0f;
        m_fadeOutTriggered = false;
        m_pending = Get_autoStart();

        if (auto* world = GetWorld())
            m_fade = FindFadeScript(*world, Get_fadeEntityName());

        if (m_fade && Get_fadeInOnStart())
        {
            // Reset to startAlpha (typically 1 for black), then fade to clear.
            m_fade->OnEnable();
            m_fade->StartFadeOut();
        }
    }

    void LoadingScript::Update(float deltaTime)
    {
        const float dt = std::max(0.0f, deltaTime);

        if (m_fade && Get_enableFadeOut() && !m_fadeOutTriggered)
        {
            m_fadeTimer += dt;
            const float fadeDelay = std::max(0.0f, Get_fadeOutDelaySec());
            if (m_fadeTimer >= fadeDelay)
            {
                m_fade->StartFadeIn(); // fade to black
                m_fadeOutTriggered = true;
            }
        }

        if (!m_pending)
            return;

        const float delay = std::max(0.0f, Get_delaySec());
        m_timer += dt;
        if (m_timer < delay)
            return;

        auto* scenes = Scenes();
        if (!scenes)
        {
            ALICE_LOG_WARN("[LoadingScript] SceneManager not available");
            m_pending = false;
            return;
        }

        const std::string scenePath = Get_targetScenePath();
        if (scenePath.empty())
        {
            ALICE_LOG_WARN("[LoadingScript] Target scene path is empty");
            m_pending = false;
            return;
        }

        ALICE_LOG_INFO("[LoadingScript] Auto changing scene to: %s", scenePath.c_str());
        const bool success = scenes->LoadSceneFileRequest(scenePath.c_str());
        m_pending = false;
    }

    void LoadingScript::Trigger()
    {
        m_timer = 0.0f;
        m_pending = true;
    }
}
