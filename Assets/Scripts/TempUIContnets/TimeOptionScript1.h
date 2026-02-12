#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/ECS/Entity.h"
#include "Runtime/UI/UICommon.h"

namespace Alice
{
    // 간단한 예제 스크립트입니다. 필요에 맞게 수정해서 사용하세요.
    class TimeOptionScript1 : public IScript
    {
        ALICE_BODY(TimeOptionScript1);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        // --- 변수 리플렉션 예시 (에디터에서 수정 가능) ---
        ALICE_PROPERTY(float, m_exampleValue, 1.0f);

        // --- ESC 키로 설정 창 열기/닫기 및 시간 정지 기능 ---
        ALICE_PROPERTY(std::string, m_rootWidgetName, "");
        ALICE_PROPERTY(std::string, m_settingBoardName, "");
        ALICE_PROPERTY(bool, m_enableEscToggle, true); // ESC 키 토글 활성화 여부

    private:
        void SetUIVisibility(EntityId entityId, AliceUI::UIVisibility visibility);

        EntityId m_settingBoardEntityId = InvalidEntityId;
        bool m_isPaused = false; // 현재 일시정지 상태
    };
}
