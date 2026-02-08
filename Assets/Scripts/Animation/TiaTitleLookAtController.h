#pragma once

#include <string>
#include <cstdint>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    // Title scene helper:
    // - Orbit/zoom camera with mouse
    // - Drive AdvancedAnimation aim so the character subtly follows camera
    class TiaTitleLookAtController : public IScript
    {
        ALICE_BODY(TiaTitleLookAtController);

    public:
        void Awake() override;
        void Start() override;
        void Update(float deltaTime) override;
        void OnDisable() override;
        void OnDestroy() override;

    private:
        void ResolveTarget();
        void ApplyInitialIdleClip();
        void RestoreOriginalAim();
        float SmoothAlpha(float speed, float dt) const;
        static float WrapPi(float rad);

        ALICE_PROPERTY(std::string, m_targetName, "Player(Tia)");
        ALICE_PROPERTY(std::string, m_faceBoneName, "head.x");
        ALICE_PROPERTY(std::string, m_idleClipName, "rig|Tia_IDLE");

        ALICE_PROPERTY(bool, m_forceIdleOnStart, true);
        ALICE_PROPERTY(bool, m_enableLookAt, true);

        ALICE_PROPERTY(float, m_orbitSensitivity, 0.22f);
        ALICE_PROPERTY(float, m_pitchMinDeg, -10.0f);
        ALICE_PROPERTY(float, m_pitchMaxDeg, 45.0f);

        ALICE_PROPERTY(float, m_zoomMinDistance, 1.1f);
        ALICE_PROPERTY(float, m_zoomMaxDistance, 3.2f);
        ALICE_PROPERTY(float, m_zoomSpeed, 0.20f);

        ALICE_PROPERTY(float, m_faceFallbackHeight, 1.55f);
        ALICE_PROPERTY(float, m_faceExtraHeightOffset, 0.00f);
        ALICE_PROPERTY(float, m_characterForwardOffsetDeg, 180.0f);

        ALICE_PROPERTY(float, m_maxLookYawDeg, 120.0f);
        ALICE_PROPERTY(float, m_maxLookPitchDeg, 90.0f);
        ALICE_PROPERTY(float, m_lookBoundarySoftnessDeg, 8.0f);
        ALICE_PROPERTY(float, m_aimWeight, 1.0f);
        ALICE_PROPERTY(float, m_aimYawScale, 1.0f);
        ALICE_PROPERTY(float, m_aimPitchScale, 1.0f);
        ALICE_PROPERTY(float, m_yawSmoothSpeed, 10.0f);
        ALICE_PROPERTY(float, m_pitchSmoothSpeed, 10.0f);
        ALICE_PROPERTY(float, m_weightSmoothSpeed, 8.0f);

        ALICE_PROPERTY(bool, m_debugAimLog, false);
        ALICE_PROPERTY(float, m_debugLogInterval, 0.20f);

        EntityId m_targetId = InvalidEntityId;
        std::uint32_t m_targetGen = 0;
        bool m_idleApplied = false;

        bool m_savedAimValid = false;
        bool m_savedAimEnabled = false;
        float m_savedAimYawRad = 0.0f;
        float m_savedAimPitchRad = 0.0f;
        float m_savedAimWeight = 1.0f;

        float m_smoothedYawRad = 0.0f;
        float m_smoothedPitchRad = 0.0f;
        float m_smoothedWeight = 0.0f;
        float m_debugLogTimer = 0.0f;
    };
}
