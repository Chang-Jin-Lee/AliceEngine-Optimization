#include "tmpTextScript.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "Runtime/UI/BindWidget.h"
#include "Runtime/ECS/GameObject.h"
#include "PoiseGauge.h"

namespace Alice
{
    REGISTER_SCRIPT(TmpTextScript);

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

    void TmpTextScript::Start()
    {
        World* w = GetWorld();
        if (!w)
            return;

        const EntityId root = SearchRootWidgetByName(*w, Get_rootWidgetName());
        if (root == InvalidEntityId)
        {
            ALICE_LOG_WARN("[TmpTextScript] Root widget not found: %s", Get_rootWidgetName().c_str());
            return;
        }

        // rootWidgetName과 textWidgetName이 같으면 루트 위젯 자체를 텍스트 위젯으로 사용
        EntityId textEntity = InvalidEntityId;
        if (Get_rootWidgetName() == Get_textWidgetName())
        {
            textEntity = root;
        }
        else
        {
            textEntity = AliceUI::FindWidgetByName(*w, root, Get_textWidgetName());
        }
        
        TargetText = (textEntity != InvalidEntityId)
            ? w->GetComponent<UITextComponent>(textEntity)
            : nullptr;

        if (!TargetText)
        {
            ALICE_LOG_WARN("[TmpTextScript] Text widget not found: %s", Get_textWidgetName().c_str());
            return;
        }

        GameObject go = w->FindGameObject(Get_targetEntityName());
        if (!go.IsValid())
        {
            ALICE_LOG_WARN("[TmpTextScript] Target entity not found: %s", Get_targetEntityName().c_str());
            return;
        }

        auto* scripts = w->GetScripts(go.id());
        if (!scripts)
            return;

        for (auto& sc : *scripts)
        {
            if (sc.scriptName == Get_targetScriptName() && sc.instance)
            {
                auto* poise = static_cast<PoiseGauge*>(sc.instance.get());
                poise->OnPoiseValueChanged.BindObject(this, &TmpTextScript::changeValue);
                changeValue(poise->Get_HP_Value());
                return;
            }
        }

        ALICE_LOG_WARN("[TmpTextScript] Target script not found: %s on %s",
            Get_targetScriptName().c_str(), Get_targetEntityName().c_str());
    }

    void TmpTextScript::Update(float deltaTime)
    {
        (void)deltaTime;
    }

    void TmpTextScript::changeValue(float newValue)
    {
        if (!TargetText)
            return;

        const float eps = std::max(0.0f, Get_matchEpsilon());
        std::string text;
        bool matched = false;

        if (std::abs(newValue - 1.0f) <= eps)
        {
            text = Get_value1Text();
            matched = !text.empty();
        }
        else if (std::abs(newValue - 2.0f) <= eps)
        {
            text = Get_value2Text();
            matched = !text.empty();
        }
        else if (std::abs(newValue - 3.0f) <= eps)
        {
            text = Get_value3Text();
            matched = !text.empty();
        }

        if (!matched)
        {
            text = Get_defaultText();
        }

        if (text.empty())
        {
            std::ostringstream oss;
            const int decimals = std::max(0, Get_decimalPlaces());
            if (decimals == 0)
            {
                oss << static_cast<int>(std::round(newValue));
            }
            else
            {
                oss << std::fixed << std::setprecision(decimals) << newValue;
            }
            text = oss.str();
        }

        // 텍스트를 실제로 UI 컴포넌트에 설정
        TargetText->text = text;
    }

    void TmpTextScript::ExampleFunction()
    {
        if (auto* transform = GetComponent<TransformComponent>())
        {
            transform->position = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
        }
    }
}
