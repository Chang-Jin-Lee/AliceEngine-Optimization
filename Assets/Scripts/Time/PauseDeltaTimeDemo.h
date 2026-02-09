#pragma once

#include <string>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/ECS/Entity.h"

namespace Alice
{
    class PauseDeltaTimeDemo : public IScript
    {
        ALICE_BODY(PauseDeltaTimeDemo);

    public:
        const char* GetName() const override { return "PauseDeltaTimeDemo"; }

        void Start() override;
        void Update(float deltaTime) override;

    private:
        void EnsureOverlayUI();
        void SetOverlayVisible(bool visible);
        void UpdateOverlayText(bool paused);

    private:
        ALICE_PROPERTY(float, m_minX, -3.0f);
        ALICE_PROPERTY(float, m_maxX, 3.0f);
        ALICE_PROPERTY(float, m_moveSpeed, 2.0f);
        ALICE_PROPERTY(std::string, m_pauseMessage, "Paused");
        ALICE_PROPERTY(std::string, m_fontPath, "Resource/Fonts/NotoSansKR-Regular.ttf");
        ALICE_PROPERTY(std::string, m_panelTexturePath, "Resource/Image/Hanako.png");

        bool m_moveToPositive = true;
        float m_pausedUnscaledAccum = 0.0f;

        EntityId m_overlayRoot = InvalidEntityId;
        EntityId m_overlayText = InvalidEntityId;
    };
}

