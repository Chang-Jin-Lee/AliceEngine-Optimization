#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/ECS/Entity.h"
#include "Runtime/UI/UIImageComponent.h"
#include "Runtime/UI/UITextComponent.h"

namespace Alice
{
    class FadeInOutScript : public IScript
    {
        ALICE_BODY(FadeInOutScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        void OnEnable() override;

        ALICE_PROPERTY(bool, isFadeIn, true);
        ALICE_PROPERTY(float, fadeSpeed, 1.5f);
        ALICE_PROPERTY(float, startAlpha, 0.0f);

        // Optional: resolve target widget by name (UI only). If empty, use owner.
        ALICE_PROPERTY(std::string, rootWidgetName, "");
        ALICE_PROPERTY(std::string, targetWidgetName, "");

    public:
        // Boolean-based fade control.
        void SetFadeToBlack(bool fadeToBlack);
        void StartFadeIn();
        void StartFadeOut();
        bool IsFadingToBlack() const;
        bool IsFadeComplete() const;

    private:
        bool InitTarget();
        void ApplyAlpha();
        float m_currentAlpha{ 0.0f };
        float m_baseImageAlpha{ 1.0f };
        float m_baseTextAlpha{ 1.0f };
        bool m_baseAlphaInitialized{ false };
        bool m_shouldFadeToBlack{ true };
        EntityId m_targetId{ InvalidEntityId };
        UIImageComponent* m_image{ nullptr };
        UITextComponent* m_text{ nullptr };
    };
}
