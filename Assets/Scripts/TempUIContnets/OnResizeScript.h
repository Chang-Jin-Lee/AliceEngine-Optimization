#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    class OnResizeScript : public IScript
    {
        ALICE_BODY(OnResizeScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        // Target widget name (optional). If empty, uses the owner entity.
        ALICE_PROPERTY(std::string, targetWidgetName, "");

        // Reference resolution used for sizing (기준 화면 크기).
        ALICE_PROPERTY(float, referenceWidth, 1600.0f);
        ALICE_PROPERTY(float, referenceHeight, 900.0f);

        // Base size: 원본 크기 (스크립트 내부 저장, 인스펙터 표시용)
        ALICE_PROPERTY(float, baseSizeX, 0.0f);
        ALICE_PROPERTY(float, baseSizeY, 0.0f);

        // Scale mode: 0=Fit(min), 1=Fill(max), 2=MatchWidth, 3=MatchHeight
        ALICE_PROPERTY(int, scaleMode, 0);
        // If true, scale X/Y independently (ignore aspect ratio).
        ALICE_PROPERTY(bool, useNonUniformScale, false);

        // Clamp for computed scale.
        ALICE_PROPERTY(float, minScale, 0.1f);
        ALICE_PROPERTY(float, maxScale, 10.0f);

        // If set, use these instead of window size (useful in editor).
        ALICE_PROPERTY(float, overrideWidth, 0.0f);
        ALICE_PROPERTY(float, overrideHeight, 0.0f);

        // Keep UI scale at 1.0 and adjust size only.
        ALICE_PROPERTY(bool, forceScaleOne, true);

        // Round size to integer pixels.
        ALICE_PROPERTY(bool, roundToInt, true);

    private:
        void ApplyLayout(bool force);
        EntityId ResolveTarget(World& world) const;

        EntityId m_targetId{ InvalidEntityId };
        float m_lastScreenW{ -1.0f };
        float m_lastScreenH{ -1.0f };
        bool m_baseSizeInitialized{ false }; // baseSize 초기화 여부
    };
}
