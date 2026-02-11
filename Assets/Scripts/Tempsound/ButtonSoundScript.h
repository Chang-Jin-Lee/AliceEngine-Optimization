#pragma once

#include <string>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/ECS/Entity.h"
#include "Runtime/UI/UIButtonComponent.h"

namespace Alice
{
    class ButtonSoundScript : public IScript
    {
        ALICE_BODY(ButtonSoundScript);

    public:
        void Start() override;
        void OnDestroy() override;

        // Optional root widget name. If empty, uses owner as root.
        ALICE_PROPERTY(std::string, rootWidgetName, "");
        // Button widget name. If empty, uses root/owner as the button.
        ALICE_PROPERTY(std::string, buttonWidgetName, "");
        // UISoundScript host entity name.
        ALICE_PROPERTY(std::string, uiSoundEntityName, "SoundObject");
        ALICE_PROPERTY(bool, enableHover, true);
        ALICE_PROPERTY(bool, enableClick, true);

    private:
        UIButtonComponent* m_button = nullptr;
        EntityId m_buttonEntityId = InvalidEntityId;
        class UISoundScript* m_uiSound = nullptr;
    };
}
