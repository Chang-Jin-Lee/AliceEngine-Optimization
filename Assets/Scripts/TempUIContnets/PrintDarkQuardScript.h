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
    // ?諭??????낆젾 ??DieLine ?癒?뵠?????筌왖????뽯뻻??랁? 筌왖????볦퍢 ????ｍ돥??덈뼄.
    class PrintDarkQuardScript : public IScript
    {
        ALICE_BODY(PrintDarkQuardScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        /// ??????????筌왖????뽯뻻????(KeyCode 揶? 0=Alpha0, 3=D, 26=Space ??
        ALICE_PROPERTY(int, m_triggerKey, 3);
        /// ???筌왖????뽯뻻????볦퍢(??. ????볦퍢??筌왖??롢늺 ?癒?짗??곗쨮 ???
        ALICE_PROPERTY(float, m_totalCycle, 3.2f);
        /// 筌ｋ???0?????癒?짗??곗쨮 ??뽯뻻
        ALICE_PROPERTY(bool, triggerOnDeath, false);
        /// Death 筌ｋ똾寃???????酉?????已?
        ALICE_PROPERTY(std::string, healthEntityName, "");
        // Boss death trigger
        ALICE_PROPERTY(bool, triggerOnBossDeath, false);
        // Boss health entity name
        ALICE_PROPERTY(std::string, bossHealthEntityName, "");
        // DieText widget name
        ALICE_PROPERTY(std::string, dieTextWidgetName, "UI_DieText");
        // Player death text (leave empty to keep current)
        ALICE_PROPERTY(std::string, playerDeathText, "");
        // Boss death text (leave empty to keep current)
        ALICE_PROPERTY(std::string, bossDeathText, "");

    private:
        float m_elapsed{ 0.0f };
        float m_scriptElapsed{ 0.0f };  // ?袁⑹읅 ??볦퍢 (startTime ?袁⑤뼎??
        bool m_isShowing{ false };
        bool m_playerDeathTriggered{ false };
        bool m_bossDeathTriggered{ false };
        EntityId m_dieTextEntityId{ InvalidEntityId };  // UI_DieText ?酉???(揶쏆늿??癰귣똻?????λ???
    };
}
