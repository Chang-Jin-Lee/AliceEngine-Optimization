#pragma once

#include <string>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    class FlareFlickerDemo : public IScript
    {
        ALICE_BODY(FlareFlickerDemo);

    public:
        void Start() override;
        void Update(float deltaTime) override;

    private:
        void EnsureWorldUI();
        void ApplyVisual(float flicker01);
        std::string ResolveTexturePath() const;

    private:
        ALICE_PROPERTY(bool, m_useFrameAnimation, false);
        ALICE_PROPERTY(float, m_frameRate, 11.46f);
        ALICE_PROPERTY(bool, m_loopFrames, false);
        ALICE_PROPERTY(std::string, m_framePath1, std::string("Resource/Image/candle_fire.png"));
        ALICE_PROPERTY(std::string, m_framePath2, std::string(""));
        ALICE_PROPERTY(std::string, m_framePath3, std::string(""));
        ALICE_PROPERTY(std::string, m_framePath4, std::string(""));
        ALICE_PROPERTY(std::string, m_framePath5, std::string(""));
        ALICE_PROPERTY(std::string, m_framePath6, std::string(""));
        ALICE_PROPERTY(std::string, m_framePath7, std::string(""));
        ALICE_PROPERTY(std::string, m_framePath8, std::string(""));

        ALICE_PROPERTY(std::string, m_texturePath, std::string("Resource/Image/candle_fire.png"));
        ALICE_PROPERTY(bool, m_billboard, false);
        ALICE_PROPERTY(DirectX::XMFLOAT2, m_baseSize, DirectX::XMFLOAT2(2.5f, 2.5f));

        ALICE_PROPERTY(DirectX::XMFLOAT3, m_baseColor, DirectX::XMFLOAT3(1.0f, 0.55f, 0.35f));
        ALICE_PROPERTY(float, m_minColorScale, 0.75f);
        ALICE_PROPERTY(float, m_maxColorScale, 0.92f);

        ALICE_PROPERTY(float, m_baseAlpha, 0.8f);
        ALICE_PROPERTY(float, m_alphaAmplitude, 0.11f);
        ALICE_PROPERTY(float, m_minAlpha, 0.42f);
        ALICE_PROPERTY(float, m_maxAlpha, 1.07f);
        ALICE_PROPERTY(bool, m_useAlphaFlicker, true);

        ALICE_PROPERTY(float, m_primarySpeed, 2.85f);
        ALICE_PROPERTY(float, m_secondarySpeed, 5.86f);
        ALICE_PROPERTY(float, m_secondaryWeight, 0.45f);
        ALICE_PROPERTY(float, m_responseCurve, 0.99f);
        ALICE_PROPERTY(float, m_scaleAmplitude, 0.08f);
        ALICE_PROPERTY(float, m_timeScale, 1.0f);

        float m_timeSec{ 0.0f };
        DirectX::XMFLOAT2 m_baseUiScale{ 1.0f, 1.0f };
    };
}
