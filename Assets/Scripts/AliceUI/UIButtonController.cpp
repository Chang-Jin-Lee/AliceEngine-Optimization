#include "UIButtonController.h"

#include "Core/ScriptFactory.h"
#include "Core/Logger.h"
#include "Core/World.h"

namespace Alice
{
    REGISTER_SCRIPT(UIButtonController);

    RTTR_REGISTRATION
    {
        rttr::registration::class_<UIButtonController>(UIButtonController::_Refl_ClassName)
            .property("rootWidgetName", &UIButtonController::rootWidgetName)
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

    void UIButtonController::Start()
    {
        // UI 트리에서 루트 위젯 찾기
        World* w = GetWorld();
        if (!w)
            return;

        const EntityId root = FindRootWidgetByName(*w, rootWidgetName);
        if (root == InvalidEntityId)
        {
            ALICE_LOG_WARN("[UIButtonController] Root widget not found: %s", rootWidgetName.c_str());
            return;
        }

        // 변수명과 같은 UI 위젯을 바인딩
        const auto result = AliceUI::BindWidgets(this, *w, root);
        if (result.missingRequired > 0)
            ALICE_LOG_WARN("[UIButtonController] Missing required widgets: %d", result.missingRequired);

        if (!TargetButton)
            return;

        const EntityId ownerId = GetOwnerId();
        const std::uint32_t ownerGen = w->GetEntityGeneration(ownerId);
        const auto isValid = [w, ownerId, ownerGen, self = this]() -> bool
        {
            if (!w)
                return false;
            if (!w->IsEntityValid(ownerId, ownerGen))
                return false;
            const auto* scripts = w->GetScripts(ownerId);
            if (!scripts)
                return false;
            for (const auto& sc : *scripts)
            {
                if (sc.instance.get() == self)
                    return true;
            }
            return false;
        };

        // 눌림/호버/뗌 델리게이트 등록
        TargetButton->AddOnPressedSafe([]()
        {
            ALICE_LOG_INFO("[UIButtonController] 눌렸음");
        }, isValid);
        TargetButton->AddOnHoveredSafe([]()
        {
            ALICE_LOG_INFO("[UIButtonController] 호버임");
        }, isValid);
        TargetButton->AddOnReleasedSafe([]()
        {
            ALICE_LOG_INFO("[UIButtonController] 뗏음");
        }, isValid);
    }

    void UIButtonController::OnDestroy()
    {
        // 델리게이트 정리 (스크립트 제거 시 안전)
        if (TargetButton)
            TargetButton->ClearDelegates();
    }
}
