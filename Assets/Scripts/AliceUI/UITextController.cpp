#include "UITextController.h"

#include <array>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Input/InputTypes.h"

namespace Alice
{
    REGISTER_SCRIPT(UITextController);

    RTTR_REGISTRATION
    {
        rttr::registration::class_<UITextController>(UITextController::_Refl_ClassName)
            .property("rootWidgetName", &UITextController::rootWidgetName)
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

        // KeyCode -> 문자열 매핑
        const std::array<std::pair<KeyCode, const char*>, 57> kKeyNames = {
            std::pair<KeyCode, const char*>{ KeyCode::Alpha0, "0" },
            { KeyCode::Alpha1, "1" }, { KeyCode::Alpha2, "2" }, { KeyCode::Alpha3, "3" }, { KeyCode::Alpha4, "4" },
            { KeyCode::Alpha5, "5" }, { KeyCode::Alpha6, "6" }, { KeyCode::Alpha7, "7" }, { KeyCode::Alpha8, "8" }, { KeyCode::Alpha9, "9" },
            { KeyCode::A, "A" }, { KeyCode::B, "B" }, { KeyCode::C, "C" }, { KeyCode::D, "D" }, { KeyCode::E, "E" },
            { KeyCode::F, "F" }, { KeyCode::G, "G" }, { KeyCode::H, "H" }, { KeyCode::I, "I" }, { KeyCode::J, "J" },
            { KeyCode::K, "K" }, { KeyCode::L, "L" }, { KeyCode::M, "M" }, { KeyCode::N, "N" }, { KeyCode::O, "O" },
            { KeyCode::P, "P" }, { KeyCode::Q, "Q" }, { KeyCode::R, "R" }, { KeyCode::S, "S" }, { KeyCode::T, "T" },
            { KeyCode::U, "U" }, { KeyCode::V, "V" }, { KeyCode::W, "W" }, { KeyCode::X, "X" }, { KeyCode::Y, "Y" }, { KeyCode::Z, "Z" },
            { KeyCode::Up, "Up" }, { KeyCode::Down, "Down" }, { KeyCode::Left, "Left" }, { KeyCode::Right, "Right" },
            { KeyCode::Space, "Space" }, { KeyCode::Enter, "Enter" }, { KeyCode::Escape, "Escape" }, { KeyCode::Tab, "Tab" }, { KeyCode::Backspace, "Backspace" },
            { KeyCode::F1, "F1" }, { KeyCode::F2, "F2" }, { KeyCode::F3, "F3" }, { KeyCode::F4, "F4" }, { KeyCode::F5, "F5" }, { KeyCode::F6, "F6" },
            { KeyCode::F7, "F7" }, { KeyCode::F8, "F8" }, { KeyCode::F9, "F9" }, { KeyCode::F10, "F10" }, { KeyCode::F11, "F11" }, { KeyCode::F12, "F12" }
        };
    }

    void UITextController::Start()
    {
        // UI 트리에서 루트 위젯 찾기
        World* w = GetWorld();
        if (!w)
            return;

        const EntityId root = FindRootWidgetByName(*w, rootWidgetName);
        if (root == InvalidEntityId)
        {
            ALICE_LOG_WARN("[UITextController] Root widget not found: %s", rootWidgetName.c_str());
            return;
        }

        // 변수명과 같은 UI 위젯을 바인딩
        const auto result = AliceUI::BindWidgets(this, *w, root);
        if (result.missingRequired > 0)
            ALICE_LOG_WARN("[UITextController] Missing required widgets: %d", result.missingRequired);
    }

    void UITextController::Update(float /*deltaTime*/)
    {
        // 키 입력에 따라 텍스트 갱신
        if (!TargetText)
            return;
        auto* input = Input();
        if (!input)
            return;

        for (const auto& [key, name] : kKeyNames)
        {
            if (input->GetKeyDown(key))
            {
                TargetText->text = std::string(name) + " 키가 눌렸음";
                break;
            }
        }
    }
}
