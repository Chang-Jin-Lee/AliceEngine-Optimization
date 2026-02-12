#include "ClearSceneReturnInputScript.h"

#include <algorithm>

#include "Runtime/Foundation/Logger.h"
#include "Runtime/Input/Input.h"
#include "Runtime/Input/InputTypes.h"
#include "Runtime/Scripting/ScriptFactory.h"

namespace Alice
{
    REGISTER_SCRIPT(ClearSceneReturnInputScript);

    void ClearSceneReturnInputScript::Start()
    {
        m_elapsedSec = 0.0f;
        m_cooldownRemaining = 0.0f;
        m_sceneRequested = false;
    }

    void ClearSceneReturnInputScript::Update(float deltaTime)
    {
        if (m_sceneRequested)
            return;

        const float dt = std::max(0.0f, deltaTime);
        m_elapsedSec += dt;
        m_cooldownRemaining = std::max(0.0f, m_cooldownRemaining - dt);

        if (m_elapsedSec < std::max(0.0f, Get_inputEnableDelaySec()))
            return;
        if (m_cooldownRemaining > 0.0f)
            return;
        if (!ConsumeAdvanceInput())
            return;

        auto* scenes = Scenes();
        if (!scenes)
        {
            ALICE_LOG_WARN("[ClearSceneReturnInputScript] SceneManager not available.");
            return;
        }

        const std::string target = Get_targetScenePath();
        if (target.empty())
        {
            ALICE_LOG_WARN("[ClearSceneReturnInputScript] targetScenePath is empty.");
            return;
        }

        m_sceneRequested = true;
        m_cooldownRemaining = std::max(0.0f, Get_inputCooldownSec());
        scenes->LoadSceneFileRequest(target.c_str());
    }

    bool ClearSceneReturnInputScript::ConsumeAdvanceInput() const
    {
        auto* input = Input();
        if (!input)
            return false;

        if (Get_allowSpaceKey() && input->GetKeyDown(KeyCode::Space))
            return true;
        if (Get_allowEnterKey() && input->GetKeyDown(KeyCode::Enter))
            return true;
        if (Get_allowMouseLeftClick() && input->GetMouseButtonDown(MouseCode::Left))
            return true;
        if (Get_allowGamepadAButton())
        {
            const int playerIndex = std::clamp(Get_gamepadPlayerIndex(), 0, 3);
            if (input->GetGamepadButtonDown(GamepadButton::A, playerIndex))
                return true;
        }

        return false;
    }
}
