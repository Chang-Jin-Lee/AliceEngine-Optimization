#pragma once

#include <DirectXMath.h>

#include "Runtime/ECS/Entity.h"
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/UI/UIButtonComponent.h"
#include "Runtime/UI/UICommon.h"

namespace Alice
{
    class SceneChangeButtonScript : public IScript
    {
        ALICE_BODY(SceneChangeButtonScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        void OnDestroy() override;

        ALICE_PROPERTY(std::string, rootWidgetName, "");
        ALICE_PROPERTY(std::string, targetScenePath, "Assets/Scenes/MainGameLoopScene/StartScene.scene");
        ALICE_PROPERTY(std::string, buttonWidgetName, "UI_StartButton");
        ALICE_PROPERTY(std::string, TextWidgetName, "UI_Text");
        ALICE_PROPERTY(std::string, UnderLineWidgetName, "UI_Image");
        ALICE_PROPERTY(std::string, uiSoundEntityName, "SoundObject");
        // Underline / bottom image swap on hover
        ALICE_PROPERTY(std::string, underImageNormalPath, "");
        ALICE_PROPERTY(std::string, underImageHoverPath, "");
        // If true, hide the underline image when not hovered (alpha = 0).
        ALICE_PROPERTY(bool, showUnderImageOnHoverOnly, false);
        // If true, hide the button image when not hovered (alpha = 0).
        ALICE_PROPERTY(bool, showButtonImageOnHoverOnly, false);

    private:
        void ApplyChildColors(AliceUI::UIButtonState state);

        UIButtonComponent* changeSceneButton = nullptr;
        EntityId m_buttonEntityId = InvalidEntityId;
        EntityId m_textEntityId = InvalidEntityId;
        EntityId m_underLineEntityId = InvalidEntityId;
        AliceUI::UIButtonState m_prevButtonState = AliceUI::UIButtonState::Normal;
        class UISoundScript* m_uiSound = nullptr;

        bool isChangeSceneRequested = false;
        bool m_pendingSceneChange = false;
        float m_sceneChangeTimer = 0.f;
        DirectX::XMFLOAT4 m_textNormalColor{ 0.06f, 0.06f, 0.06f, 0.93f };
        DirectX::XMFLOAT4 m_lineNormalColor{ 0.1f, 0.1f, 0.1f, 0.93f };
        DirectX::XMFLOAT4 m_buttonNormalColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 m_hoverColor{ 1.0f, 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT4 m_pressedColor{ 0.85f, 0.85f, 0.85f, 1.0f };
    };
}

