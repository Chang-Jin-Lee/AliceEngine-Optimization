#include "SceneSwitchExample.h"
#include "Core/ScriptFactory.h"
#include "Core/Logger.h"
namespace Alice
{
    REGISTER_SCRIPT(SceneSwitchExample);

    void SceneSwitchExample::Update(float /*deltaTime*/)
    {
        auto* input = Input();
        auto* scenes = Scenes();
        if (!input || !scenes)
            return;

        if (input->GetKeyDown(KeyCode::F1))
        {
            // .scene 파일 로드 요청 (SceneManager를 통한 지연 처리)
            scenes->LoadSceneFileRequest(targetSceneA.c_str());
        }

        if (input->GetKeyDown(KeyCode::F2))
        {            
            scenes->LoadSceneFileRequest(targetSceneB.c_str());
        }
    }
}






