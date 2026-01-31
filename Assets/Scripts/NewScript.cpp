#include "NewScript.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"

namespace Alice
{
    // 이 스크립트를 리플렉션/팩토리 시스템에 등록합니다.
    REGISTER_SCRIPT(NewScript);

    void NewScript::Start()
    {
		ALICE_LOG_INFO("NewScript Start 호출됨");
        // 초기화 로직을 여기에 작성하세요.
    }

    void NewScript::Update(float deltaTime)
    {
        // 매 프레임 호출되는 로직을 여기에 작성하세요.
    }

    void NewScript::ExampleFunction()
    {
        // 리플렉션으로 등록된 함수 예시입니다.
        // 이 함수는 에디터에서 호출할 수 있습니다.
        
        // 예시: Transform 컴포넌트 가져오기
        if (auto* transform = GetComponent<TransformComponent>())
        {
            // 위치를 (0, 0, 0)으로 리셋하는 예시
            transform->position = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
        }
    }
}
