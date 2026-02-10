#pragma once

#include <string>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/ECS/Entity.h"
#include "Runtime/Input/InputTypes.h"

namespace Alice
{
    class BloodVignetteDemo : public IScript
    {
        ALICE_BODY(BloodVignetteDemo);

    public:
        const char* GetName() const override { return "BloodVignetteDemo"; }

        void Start() override;
        void Update(float deltaTime) override;

    private:
        void EnsureOverlayUI();
        void SetOverlayVisible(bool visible);

    private:
        ALICE_PROPERTY(int, m_triggerKey, static_cast<int>(KeyCode::Alpha1));
        ALICE_PROPERTY(float, m_showDurationSec, 2.0f);
        ALICE_PROPERTY(std::string, m_texturePath, "Resource/Image/BloodVignette.png");
        ALICE_PROPERTY(float, m_overlayAlpha, 1.0f);
        ALICE_PROPERTY(int, m_sortOrder, 5000);

        EntityId m_overlayEntity{ InvalidEntityId };
        float m_elapsedSec{ 0.0f };
        bool m_showing{ false };
    };
}

