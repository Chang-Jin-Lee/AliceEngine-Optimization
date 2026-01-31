#include "UIGaugeController.h"

#include <algorithm>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Input/InputTypes.h"

namespace Alice
{
    REGISTER_SCRIPT(UIGaugeController);

    RTTR_REGISTRATION
    {
        rttr::registration::class_<UIGaugeController>(UIGaugeController::_Refl_ClassName)
            .property("rootWidgetName", &UIGaugeController::rootWidgetName)
                (rttr::metadata("SerializeField", true));
    }

    namespace
    {
        // UI 루트 위젯을 이름으로 찾는 헬퍼
        EntityId FindRootWidgetByName(World& world, const std::string& name)
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

    void UIGaugeController::Start()
    {
        // UI 트리에서 루트 위젯 찾기
        World* w = GetWorld();
        if (!w)
            return;

        const EntityId root = FindRootWidgetByName(*w, rootWidgetName);
        if (root == InvalidEntityId)
        {
            ALICE_LOG_WARN("[UIGaugeController] Root widget not found: %s", rootWidgetName.c_str());
            return;
        }

        // 변수명과 같은 UI 위젯을 바인딩
        const auto result = AliceUI::BindWidgets(this, *w, root);
        if (result.missingRequired > 0)
            ALICE_LOG_WARN("[UIGaugeController] Missing required widgets: %d", result.missingRequired);
    }

    void UIGaugeController::Update(float /*deltaTime*/)
    {
        // Q/W 키로 게이지 조절
        if (!TargetGauge)
            return;
        auto* input = Input();
        if (!input)
            return;

        float delta = 0.0f;
        if (input->GetKeyDown(KeyCode::Q)) delta -= 0.1f;
        if (input->GetKeyDown(KeyCode::W)) delta += 0.1f;
        if (delta == 0.0f)
            return;

        const float minV = TargetGauge->normalized ? 0.0f : TargetGauge->minValue;
        const float maxV = TargetGauge->normalized ? 1.0f : TargetGauge->maxValue;
        TargetGauge->value = std::clamp(TargetGauge->value + delta, minV, maxV);
    }
}
