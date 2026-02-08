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
#include "BoxDeligateScript.h"

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

        // rootWidgetName
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

        baseAlpha = TargetText->color.w;
        fadeElapsed = 0.0f;
        holdElapsed = 0.0f;

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
                auto* box = static_cast<BoxDeligateScript*>(sc.instance.get());
                box->OnTextValueChanged.BindObject(this, &TmpTextScript::changeValue);
                changeValue(box->Get_TextValue());
                return;
            }
        }

        ALICE_LOG_WARN("[TmpTextScript] Target script not found: %s on %s",
            Get_targetScriptName().c_str(), Get_targetEntityName().c_str());
    }

    void TmpTextScript::Update(float deltaTime)
    {
        if (!TargetText)
            return;

        const float hold = std::max(0.0f, Get_holdDuration());
        const float fade = std::max(0.0f, Get_fadeDuration());

        if (holdElapsed < hold)
        {
            holdElapsed = std::min(holdElapsed + deltaTime, hold);
            fadeElapsed = 0.0f;
            TargetText->color.w = baseAlpha;
            return;
        }

        if (fade <= 0.0f)
        {
            TargetText->color.w = 0.0f;
            return;
        }

        fadeElapsed = std::min(fadeElapsed + deltaTime, fade);
        const float t = std::clamp(fadeElapsed / fade, 0.0f, 1.0f);
        TargetText->color.w = baseAlpha * (1.0f - t);
    }

    void TmpTextScript::changeValue(float newValue)
    {
        if (!TargetText)
            return;

        const float eps = std::max(0.0f, Get_matchEpsilon());
        std::string text;
        bool matched = false;


        text = Get_value3Text();
        matched = !text.empty();

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

        TargetText->text = text;
        holdElapsed = 0.0f;
        fadeElapsed = 0.0f;
        TargetText->color.w = baseAlpha;
    }

    void TmpTextScript::ExampleFunction()
    {
        if (auto* transform = GetComponent<TransformComponent>())
        {
            transform->position = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
        }
    }
}
