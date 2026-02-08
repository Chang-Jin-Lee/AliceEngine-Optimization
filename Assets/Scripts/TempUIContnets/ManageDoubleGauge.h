#pragma once
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/UI/UIGaugeComponent.h"

namespace Alice
{
    // Manage two gauge bars as a single value split.
    // Gauge A shows [0..splitRatio], Gauge B shows [splitRatio..1].
    class ManageDoubleGauge : public IScript
    {
        ALICE_BODY(ManageDoubleGauge);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        //  Bind targets set in editor
        ALICE_PROPERTY(std::string, rootWidgetName, "UI_Gauge");
        ALICE_PROPERTY(std::string, gaugeWidgetNameA, "HP_Gauge_1");
        ALICE_PROPERTY(std::string, gaugeWidgetNameB, "HP_Gauge_2");
        ALICE_PROPERTY(std::string, targetEntityName, "Cube_HPTest1");
        ALICE_PROPERTY(std::string, targetScriptName, "BoxDeligateScript");


        ALICE_PROPERTY(float, maxValue, 100.0f);

	
        ALICE_PROPERTY(float, splitRatio, 0.5f);

        // Force gauges to interpret value as normalized 
        ALICE_PROPERTY(bool, forceNormalized, true);

        ALICE_PROPERTY(float, nowValue, 0.0f);

        // Runtime cached gauge components
        UIGaugeComponent* TargetGaugeA = nullptr;
        UIGaugeComponent* TargetGaugeB = nullptr;

        // Bound to target script's OnValueChanged
        void changeValue(float newValue);

        void ExampleFunction();
        ALICE_FUNC(ExampleFunction);
    };
}
