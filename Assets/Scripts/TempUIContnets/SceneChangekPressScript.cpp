#include "SceneChangekPressScript.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Input/Input.h"

namespace Alice
{
    REGISTER_SCRIPT(SceneChangekPressScript);

    void SceneChangekPressScript::Start()
    {
        // 초기화 시 특별한 작업 없음
    }

    void SceneChangekPressScript::Update(float deltaTime)
    {
        auto* input = Input();
        if (!input)
            return;

        // 키 입력 처리
        KeyCode changeKeyCode = static_cast<KeyCode>(Get_changeKey());
        
        // K 키 (또는 설정된 키)를 누르면 씬 변경
        if (input->GetKeyDown(changeKeyCode))
        {
            auto* scenes = Scenes();
            if (!scenes)
            {
                ALICE_LOG_WARN("[SceneChangekPressScript] SceneManager not available");
                return;
            }

            const std::string scenePath = Get_targetScenePath();
            if (scenePath.empty())
            {
                ALICE_LOG_WARN("[SceneChangekPressScript] Target scene path is empty");
                return;
            }

            ALICE_LOG_INFO("[SceneChangekPressScript] Key pressed! Changing scene to: %s", scenePath.c_str());
            scenes->LoadSceneFileRequest(scenePath.c_str());
        }

        (void)deltaTime;
    }
}
