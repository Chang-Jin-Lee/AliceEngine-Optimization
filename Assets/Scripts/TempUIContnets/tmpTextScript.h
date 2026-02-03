#pragma once

#include <string>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/UI/UITextComponent.h"

namespace Alice
{
    class TmpTextScript : public IScript
    {
        ALICE_BODY(TmpTextScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        ALICE_PROPERTY(std::string, rootWidgetName, "UI_Root");
        ALICE_PROPERTY(std::string, textWidgetName, "UI_Text");
        ALICE_PROPERTY(std::string, targetEntityName, "Cube_PoiseTest");
        ALICE_PROPERTY(std::string, targetScriptName, "PoiseGauge");

        ALICE_PROPERTY(int, decimalPlaces, 0);
        ALICE_PROPERTY(float, matchEpsilon, 0.01f);
        ALICE_PROPERTY(std::string, value1Text, "");       // newValue ~= 1
        ALICE_PROPERTY(std::string, value2Text, "");       // newValue ~= 2
        ALICE_PROPERTY(std::string, value3Text, "");       // newValue ~= 3
        ALICE_PROPERTY(std::string, defaultText, "");      // 
        ALICE_PROPERTY(float, fadeDuration, 1.0f);         // seconds to fade out (<=0 disables)
        ALICE_PROPERTY(float, holdDuration, 0.0f);         // seconds to hold before fading

        UITextComponent* TargetText = nullptr;

        void changeValue(float newValue);

        void ExampleFunction();
        ALICE_FUNC(ExampleFunction);

    private:
        float fadeElapsed = 0.0f;
        float holdElapsed = 0.0f;
        float baseAlpha = 1.0f;
    };
}
