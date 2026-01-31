#include "CameraManager.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/GameObject.h"
#include "CameraFollow.h"
#include "AddGetRemoveComponentTest.h"

namespace Alice
{
    REGISTER_SCRIPT(CameraManager);

    void CameraManager::Awake()
    {
        auto go = gameObject();
        if (!go.IsValid())
            return;

        // 현재 GameObject에 붙어 있는 CameraComponent를 가져옵니다.
        auto* cam = go.GetComponent<CameraComponent>();
        if (!cam)
        {
            // CameraComponent가 없으면 gameObject().AddComponent<>()로 추가 (Unity 스타일)
            cam = &go.AddComponent<CameraComponent>();
        }

        // 메인 카메라로 설정
        cam->primary = true;
        ALICE_LOG_INFO("[CameraManager] Ready. primary=1");
    }

    void CameraManager::Update(float /*deltaTime*/)
    {
        auto go = gameObject();
        if (!go.IsValid())
            return;

        auto* input = Input();
        if (!input)
            return;

        if (input->GetKeyDown(KeyCode::H))
        {
            // 현재 GameObject에 AddGetRemoveComponentTest 컴포넌트 추가
            go.AddComponent<AddGetRemoveComponentTest>();
        }
        if (input->GetKeyDown(KeyCode::J))
        {
            // 현재 GameObject에서 AddGetRemoveComponentTest 컴포넌트 제거
            go.RemoveComponent<AddGetRemoveComponentTest>();
        }
        if (input->GetMouseButtonDown(MouseCode::Right))
        {
            auto comps = go.GetComponents<AddGetRemoveComponentTest>();
            ALICE_LOG_INFO("[CameraManager] Found {%d} AddGetRemoveComponentTest components.", static_cast<int>(comps.size()));
        }
    }
}
