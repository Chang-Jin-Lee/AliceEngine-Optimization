#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/Input/InputTypes.h"
#include "Runtime/ECS/Entity.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "Runtime/UI/UICommon.h"
#include <string>

namespace Alice
{
    // 특정 키 입력 시 DieLine 쉐이더 이미지를 표시하고, 지정 시간 후 숨깁니다.
    class PrintDarkQuardScript : public IScript
    {
        ALICE_BODY(PrintDarkQuardScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        /// 눌렀을 때 이미지를 표시할 키 (KeyCode 값: 0=Alpha0, 3=D, 26=Space 등)
        ALICE_PROPERTY(int, m_triggerKey, 3);
        /// 이미지를 표시할 시간(초). 이 시간이 지나면 자동으로 숨김
        ALICE_PROPERTY(float, m_totalCycle, 3.2f);
        /// 체력 0일 때 자동으로 표시
        ALICE_PROPERTY(bool, triggerOnDeath, false);
        /// Death 체크할 대상 엔티티 이름
        ALICE_PROPERTY(std::string, healthEntityName, "");

    private:
        float m_elapsed{ 0.0f };
        float m_scriptElapsed{ 0.0f };  // 누적 시간 (startTime 전달용)
        bool m_isShowing{ false };
        bool m_deathTriggered{ false };
        EntityId m_dieTextEntityId{ InvalidEntityId };  // UI_DieText 엔티티 (같이 보였다 숨었다)
    };
}
