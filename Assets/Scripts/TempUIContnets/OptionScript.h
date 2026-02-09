#pragma once

#include <string>
#include <DirectXMath.h>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/ECS/Entity.h"
#include "Runtime/UI/UICommon.h"

namespace Alice
{
    class OptionScript : public IScript
    {
        ALICE_BODY(OptionScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        void OnDestroy() override;

        // Editor-exposed example property.
        ALICE_PROPERTY(std::string, rootWidgetName, "");
        // If false, skip ESC-driven visibility toggle (use when attaching to buttons).
        ALICE_PROPERTY(bool, enableToggleChildrenOnEsc, true);

        // Button hover/click visuals + sounds (SceneChangeButtonScript subset)
        ALICE_PROPERTY(std::string, buttonRootWidgetName, "");
        ALICE_PROPERTY(std::string, buttonWidgetName, "");
        ALICE_PROPERTY(std::string, TextWidgetName, "");
        ALICE_PROPERTY(std::string, UnderLineWidgetName, "");
        ALICE_PROPERTY(std::string, uiSoundEntityName, "SoundObject");
        ALICE_PROPERTY(std::string, underImageNormalPath, "");
        ALICE_PROPERTY(std::string, underImageHoverPath, "");
        ALICE_PROPERTY(bool, showUnderImageOnHoverOnly, false);
        ALICE_PROPERTY(bool, showButtonImageOnHoverOnly, false);
        // Optional: close/toggle a target window when the button is clicked.
        ALICE_PROPERTY(std::string, clickTargetWidgetName, "");
        ALICE_PROPERTY(bool, clickToggleTarget, false);


    private:
        void ApplyChildColors(AliceUI::UIButtonState state);
        void HandleClickTarget();

        EntityId rootEntity = InvalidEntityId;
        bool childrenVisible = true;

        struct UIButtonComponent* m_button = nullptr;
        EntityId m_buttonEntityId = InvalidEntityId;
        EntityId m_textEntityId = InvalidEntityId;
        EntityId m_underLineEntityId = InvalidEntityId;
        class UISoundScript* m_uiSound = nullptr;
        EntityId m_clickTargetEntity = InvalidEntityId;

        DirectX::XMFLOAT4 m_textNormalColor{ 0.06f, 0.06f, 0.06f, 0.93f };
        DirectX::XMFLOAT4 m_lineNormalColor{ 0.1f, 0.1f, 0.1f, 0.93f };
        DirectX::XMFLOAT4 m_buttonNormalColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 m_hoverColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 m_pressedColor{ 0.85f, 0.85f, 0.85f, 1.0f };
    };
}
