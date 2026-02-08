#include "GaugeScript.h"
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
    REGISTER_SCRIPT(GaugeScript);

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

    void GaugeScript::Start()
    {
        World* w = GetWorld();
        if (!w) return;

        // 1) UI root 찾기
        const EntityId root = SearchRootWidgetByName(*w, Get_rootWidgetName());
        if (root == InvalidEntityId)
        {
            ALICE_LOG_WARN("[GaugeScript] Root widget not found: %s", Get_rootWidgetName().c_str());
            return;
        }

        // 2) 게이지 위젯 이름으로 찾기
        const EntityId gaugeEntity = AliceUI::FindWidgetByName(*w, root, Get_gaugeWidgetName());
        TargetGauge = (gaugeEntity != InvalidEntityId)
            ? w->GetComponent<UIGaugeComponent>(gaugeEntity)
            : nullptr;

        if (!TargetGauge)
        {
            ALICE_LOG_WARN("[GaugeScript] Gauge widget not found: %s", Get_gaugeWidgetName().c_str());
            return;
        }

        // 3) 대상 엔티티/스크립트 찾기 및 OnValueChanged 바인딩
        GameObject go = w->FindGameObject(Get_targetEntityName());
        if (!go.IsValid())
        {
            ALICE_LOG_WARN("[GaugeScript] Target entity not found: %s", Get_targetEntityName().c_str());
            return;
        }

        auto* scripts = w->GetScripts(go.id());
        if (!scripts) return;

        for (auto& sc : *scripts)
        {
            if (sc.scriptName == Get_targetScriptName() && sc.instance)
            {
                ALICE_LOG_INFO("[GaugeScript] script on %s: %s (instance=%d)",
                    Get_targetEntityName().c_str(),
                    sc.scriptName.c_str(),
                    sc.instance ? 1 : 0);

                auto* box = static_cast<BoxDeligateScript*>(sc.instance.get());
                box->OnBossHPChanged.BindObject(this, &GaugeScript::changeValue);
                // 초기값 반영 (바인딩 직후 한 번 호출)
                changeValue(box->Get_BossHP_Value());
                break;
            }
        }
    }

    void GaugeScript::changeValue(float newValue)
    {
        if (TargetGauge)
        {
            const float max = std::max(1e-6f, Get_maxValue());
            TargetGauge->value = std::clamp(newValue / max, 0.0f, 1.0f);
            nowValue = TargetGauge->value;
        }
    }

    void GaugeScript::Update(float deltaTime)
    {

        // changeValue 콜백으로 이미 갱신되므로 Update에서는 추가 처리 불필요
        // (콜백 기반이 아닌 폴링이 필요하면 여기서 처리)
        (void)deltaTime;
    }

    void GaugeScript::ExampleFunction()
    {
        if (auto* transform = GetComponent<TransformComponent>())
        {
            transform->position = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
        }
    }
}
