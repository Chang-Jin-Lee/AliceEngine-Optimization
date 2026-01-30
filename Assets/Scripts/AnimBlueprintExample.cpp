#include "AnimBlueprintExample.h"

#include "Core/ScriptFactory.h"
#include "Core/Logger.h"
#include "Core/GameObject.h"

namespace Alice
{
    REGISTER_SCRIPT(AnimBlueprintExample);

    void AnimBlueprintExample::Start()
    {
        // AnimBlueprint 경로 세팅
        auto go = gameObject();
        auto graph = go.GetAnimGraph();
        graph.SetBlueprint(blueprintPath.c_str());
    }

    void AnimBlueprintExample::Update(float /*deltaTime*/)
    {
        // 입력 확인
        auto* input = Input();
        if (!input)
            return;

        const bool move =
            input->GetKey(KeyCode::W) || input->GetKey(KeyCode::A) ||
            input->GetKey(KeyCode::S) || input->GetKey(KeyCode::D);

        auto go = gameObject();
        auto graph = go.GetAnimGraph();
        if (!graph.IsValid())
            return;

        // 파라미터 업데이트
        graph.SetFloat(speedParam.c_str(), move ? moveSpeed : 0.0f);
        graph.SetBool(moveParam.c_str(), move);

        if (input->GetKeyDown(KeyCode::Space))
            graph.SetTrigger(triggerParam.c_str());
    }
}

