#pragma once

#include <string>

#include <DirectXMath.h>

#include "Runtime/ECS/Entity.h"
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    class CameraBossIntroProduction : public IScript
    {
        ALICE_BODY(CameraBossIntroProduction);

    public:
        void Update(float deltaTime) override;
        void OnDisable() override;
        void OnDestroy() override;

    private:
        struct Pose
        {
            DirectX::XMFLOAT3 position{};
            DirectX::XMFLOAT3 rotationRad{};
            float fovDeg = 60.0f;
        };

        enum class Phase
        {
            None,
            ShoulderPush,
            BossUpperSweep
        };

        struct BlendState
        {
            bool active = false;
            Pose from{};
            Pose to{};
            float duration = 0.0f;
            float elapsed = 0.0f;
            bool useSmoothStep = true;
        };

        void StartSequence();
        void AdvanceAfterBlend();
        void BeginBlend(const Pose& from, const Pose& to, float duration, bool useSmoothStep);
        bool UpdateBlend(float deltaTime);
        void ApplyPose(const Pose& pose);

        Pose BuildShoulderPose(const DirectX::XMFLOAT3& playerPos,
                               const DirectX::XMFLOAT3& bossPos,
                               float towardBossOffset,
                               float fovDeg) const;

        Pose BuildBossUpperPose(const DirectX::XMFLOAT3& playerPos,
                                const DirectX::XMFLOAT3& bossPos,
                                float sideOffset,
                                float distanceFromBoss,
                                float towardBossOffset,
                                float fovDeg) const;

        Pose MakeLookPose(const DirectX::XMFLOAT3& cameraPos,
                          const DirectX::XMFLOAT3& lookTarget,
                          float fovDeg) const;

        bool TryResolveOutputCamera(EntityId& outId) const;
        bool TryGetEntityPositionByName(const std::string& name, DirectX::XMFLOAT3& outPos) const;
        bool ResolveActorPositions(DirectX::XMFLOAT3& outPlayerPos, DirectX::XMFLOAT3& outBossPos) const;

        void SetGameplayCameraControl(bool enable);
        void RestoreIfNeeded();

        static DirectX::XMFLOAT3 AddVec(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b);
        static DirectX::XMFLOAT3 SubVec(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b);
        static DirectX::XMFLOAT3 MulVec(const DirectX::XMFLOAT3& v, float s);
        static DirectX::XMFLOAT3 LerpVec(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, float t);
        static DirectX::XMFLOAT3 NormalizeSafe(const DirectX::XMFLOAT3& v, const DirectX::XMFLOAT3& fallback);
        static DirectX::XMFLOAT3 CrossVec(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b);
        static DirectX::XMFLOAT3 DirectionToEuler(const DirectX::XMFLOAT3& dir);
        static DirectX::XMFLOAT4 EulerToQuaternion(const DirectX::XMFLOAT3& eulerRad);
        static DirectX::XMFLOAT3 QuaternionToEuler(const DirectX::XMFLOAT4& quat);
        static float ApplySmoothStep(float t);
        static float ResolveDuration(float baseDuration, float speedMultiplier);

        ALICE_PROPERTY(std::string, m_outputCameraName, "MainCamera");
        ALICE_PROPERTY(std::string, m_playerName, "Player(Tia)");
        ALICE_PROPERTY(std::string, m_bossName, "Boss");

        ALICE_PROPERTY(bool, m_enableHotkey, true);
        ALICE_PROPERTY(bool, m_restartOnKey, true);
        ALICE_PROPERTY(bool, m_restoreGameplayCameraOnFinish, true);
        ALICE_PROPERTY(bool, m_disableFollowDuringProduction, true);
        ALICE_PROPERTY(bool, m_disableLookAtDuringProduction, true);
        ALICE_PROPERTY(bool, m_disableSpringArmDuringProduction, false);

        ALICE_PROPERTY(float, m_phase1Duration, 3.27f);
        ALICE_PROPERTY(float, m_phase1Speed, 0.82f);
        ALICE_PROPERTY(bool, m_phase1UseSmoothStep, true);
        ALICE_PROPERTY(float, m_phase2Duration, 2.93f);
        ALICE_PROPERTY(float, m_phase2Speed, 1.04f);
        ALICE_PROPERTY(bool, m_phase2UseSmoothStep, true);

        ALICE_PROPERTY(float, m_shoulderHeight, 1.58f);
        ALICE_PROPERTY(float, m_shoulderBackDistance, 1.35f);
        ALICE_PROPERTY(float, m_shoulderLeftOffset, 0.18f);
        ALICE_PROPERTY(float, m_phase1MoveTowardBoss, 1.20f);
        ALICE_PROPERTY(float, m_phase1LookHeight, -0.07f);
        ALICE_PROPERTY(float, m_phase1FovStartDeg, 65.29f);
        ALICE_PROPERTY(float, m_phase1FovEndDeg, 39.89f);

        ALICE_PROPERTY(float, m_phase2LookHeight, 1.62f);
        ALICE_PROPERTY(float, m_phase2CameraLift, 0.32f);
        ALICE_PROPERTY(float, m_phase2SideOffsetStart, 1.05f);
        ALICE_PROPERTY(float, m_phase2SideOffsetEnd, 0.30f);
        ALICE_PROPERTY(float, m_phase2DistanceStart, 1.82f);
        ALICE_PROPERTY(float, m_phase2DistanceEnd, 4.62f);
        ALICE_PROPERTY(float, m_phase2TowardBossStart, 0.10f);
        ALICE_PROPERTY(float, m_phase2TowardBossEnd, 0.34f);
        ALICE_PROPERTY(float, m_phase2FovEndDeg, 42.74f);

        BlendState m_blend{};
        Phase m_phase = Phase::None;
        bool m_sequenceRunning = false;
        bool m_controlsOverridden = false;

        Pose m_phase1StartPose{};
        Pose m_phase1EndPose{};
        Pose m_phase2StartPose{};
        Pose m_phase2EndPose{};

        bool m_hasFollow = false;
        bool m_hasLookAt = false;
        bool m_hasSpringArm = false;

        bool m_savedFollowEnabled = true;
        bool m_savedLookAtEnabled = false;
        bool m_savedSpringArmEnabled = true;
    };
}
