#include "ManageDoubleGauge.h"
#include <algorithm>
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "Runtime/UI/BindWidget.h"
#include "BoxDeligateScript.h"
#include "Runtime/ECS/GameObject.h"

namespace Alice
{
    REGISTER_SCRIPT(ManageDoubleGauge);

    namespace
    {
        EntityId SearchRootWidgetByName(World& world, const std::string& name)
        {
            for (auto [id, widget] : world.GetComponents<UIWidgetComponent>())
            {
                const std::string widgetName = widget.widgetName.empty() ? world.GetEntityName(id) : widget.widgetName;
                if (!widgetName.empty() && widgetName == name)
                    return id;
            }
            return InvalidEntityId;
        }
    }

    void ManageDoubleGauge::Start()
    {
        World* w = GetWorld();
        if (!w) return;

        // Find UI root
        const EntityId root = SearchRootWidgetByName(*w, Get_rootWidgetName());
        if (root == InvalidEntityId)
        {
            ALICE_LOG_WARN("[ManageDoubleGauge] Root widget not found: %s", Get_rootWidgetName().c_str());
            return;
        }

        // Find gauges by name
        const EntityId gaugeAEntity = AliceUI::FindWidgetByName(*w, root, Get_gaugeWidgetNameA());
        TargetGaugeA = (gaugeAEntity != InvalidEntityId)
            ? w->GetComponent<UIGaugeComponent>(gaugeAEntity)
            : nullptr;

        const EntityId gaugeBEntity = AliceUI::FindWidgetByName(*w, root, Get_gaugeWidgetNameB());
        TargetGaugeB = (gaugeBEntity != InvalidEntityId)
            ? w->GetComponent<UIGaugeComponent>(gaugeBEntity)
            : nullptr;

        if (!TargetGaugeA || !TargetGaugeB)
        {
            ALICE_LOG_WARN("[ManageDoubleGauge] Gauge widget not found: %s / %s",
                Get_gaugeWidgetNameA().c_str(), Get_gaugeWidgetNameB().c_str());
            return;
        }

        if (Get_forceNormalized())
        {
            TargetGaugeA->normalized = true;
            TargetGaugeB->normalized = true;
        }

        //Find target entity/script and bind OnValueChanged
        GameObject go = w->FindGameObject(Get_targetEntityName());
        if (!go.IsValid())
        {
            ALICE_LOG_WARN("[ManageDoubleGauge] Target entity not found: %s", Get_targetEntityName().c_str());
            return;
        }

        auto* scripts = w->GetScripts(go.id());
        if (!scripts) return;

        for (auto& sc : *scripts)
        {
            if (sc.scriptName == Get_targetScriptName() && sc.instance)
            {
                auto* box = static_cast<BoxDeligateScript*>(sc.instance.get());
                box->OnBossHPChanged.BindObject(this, &ManageDoubleGauge::changeValue);
                // Apply initial value
                changeValue(box->Get_BossHP_Value());
                return;
            }
        }

        ALICE_LOG_WARN("[ManageDoubleGauge] Target script not found: %s on %s",
            Get_targetScriptName().c_str(), Get_targetEntityName().c_str());
    }

    void ManageDoubleGauge::changeValue(float newValue)
    {
        if (!TargetGaugeA || !TargetGaugeB)
            return;

        const float max = std::max(1e-6f, Get_maxValue());
        const float norm = std::clamp(newValue / max, 0.0f, 1.0f);
        nowValue = newValue;
        float split = Get_splitRatio();

        float aVal = 0.0f;
        float bVal = 0.0f;

        if (split <= 0.0f)
        {
            aVal = 0.0f;
            bVal = norm;
        }
        else if (split >= 1.0f)
        {
            aVal = norm;
            bVal = 0.0f;
        }
        else
        {
            if (norm <= split)
            {
                aVal = norm / split;
                bVal = 0.0f;
            }
            else
            {
                aVal = 1.0f;
                bVal = (norm - split) / (1.0f - split);
            }
        }

        aVal = std::clamp(aVal, 0.0f, 1.0f);
        bVal = std::clamp(bVal, 0.0f, 1.0f);

        TargetGaugeA->value = aVal;
        TargetGaugeB->value = bVal;
    }

    void ManageDoubleGauge::Update(float deltaTime)
    {
        (void)deltaTime;
    }

    void ManageDoubleGauge::ExampleFunction()
    {
        if (auto* transform = GetComponent<TransformComponent>())
        {
            transform->position = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
        }
    }
}
