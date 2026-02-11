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
        ALICE_PROPERTY(float, m_frameRate, 12.0f);
        ALICE_PROPERTY(bool, m_loopFrames, true);
        ALICE_PROPERTY(std::string, m_framePath1, std::string("Resource/Image/redCircle.png"));
        ALICE_PROPERTY(std::string, m_framePath2, std::string(""));
        ALICE_PROPERTY(std::string, m_framePath3, std::string(""));
        ALICE_PROPERTY(std::string, m_framePath4, std::string(""));
        ALICE_PROPERTY(std::string, m_framePath5, std::string(""));
        ALICE_PROPERTY(std::string, m_framePath6, std::string(""));
        ALICE_PROPERTY(std::string, m_framePath7, std::string(""));
        ALICE_PROPERTY(std::string, m_framePath8, std::string(""));

        ALICE_PROPERTY(std::string, m_texturePath, std::string("Resource/Image/redCircle.png"));
        ALICE_PROPERTY(bool, m_billboard, false);
        ALICE_PROPERTY(DirectX::XMFLOAT2, m_baseSize, DirectX::XMFLOAT2(2.5f, 2.5f));

        ALICE_PROPERTY(DirectX::XMFLOAT3, m_baseColor, DirectX::XMFLOAT3(1.0f, 0.55f, 0.35f));
        ALICE_PROPERTY(float, m_minColorScale, 0.8f);
        ALICE_PROPERTY(float, m_maxColorScale, 1.3f);

        ALICE_PROPERTY(float, m_baseAlpha, 0.72f);
        ALICE_PROPERTY(float, m_alphaAmplitude, 0.22f);
        ALICE_PROPERTY(float, m_minAlpha, 0.35f);
        ALICE_PROPERTY(float, m_maxAlpha, 0.95f);
        ALICE_PROPERTY(bool, m_useAlphaFlicker, true);

        ALICE_PROPERTY(float, m_primarySpeed, 8.0f);
        ALICE_PROPERTY(float, m_secondarySpeed, 17.0f);
        ALICE_PROPERTY(float, m_secondaryWeight, 0.45f);
        ALICE_PROPERTY(float, m_responseCurve, 1.35f);
        ALICE_PROPERTY(float, m_scaleAmplitude, 0.12f);
        ALICE_PROPERTY(float, m_timeScale, 1.0f);

        float m_timeSec{ 0.0f };
        DirectX::XMFLOAT2 m_baseUiScale{ 1.0f, 1.0f };
    };
}
