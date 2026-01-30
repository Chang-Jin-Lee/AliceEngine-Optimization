#include "AddGetRemoveComponentTest.h"
#include "Core/ScriptFactory.h"
#include "Core/Logger.h"
#include "Core/World.h"

namespace Alice
{
    // 이 스크립트를 리플렉션/팩토리 시스템에 등록합니다.
    REGISTER_SCRIPT(AddGetRemoveComponentTest);

    void AddGetRemoveComponentTest::Awake()
    {
        // 초기화 로직을 여기에 작성하세요.
		ALICE_LOG_INFO("[AddGetRemoveComponentTest] Awake called. m_exampleValue = %f", m_exampleValue);
	}

    void AddGetRemoveComponentTest::Start()
    {
        // 초기화 로직을 여기에 작성하세요.
    }

    void AddGetRemoveComponentTest::Update(float deltaTime)
    {
        // 매 프레임 호출되는 로직을 여기에 작성하세요.
    }

    void AddGetRemoveComponentTest::OnDestroy()
    {
		// 정리 로직을 여기에 작성하세요.
		ALICE_LOG_INFO("[AddGetRemoveComponentTest] OnDestroy called.");
    }

    void AddGetRemoveComponentTest::ExampleFunction()
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
