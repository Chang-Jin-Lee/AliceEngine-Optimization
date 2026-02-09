#pragma once

#include <string>
#include <DirectXMath.h>

#include "Runtime/ECS/Entity.h"
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    class CameraGameProduction : public IScript
    {
        ALICE_BODY(CameraGameProduction);

    public:
        void Awake() override;
        void Update(float deltaTime) override;
        void OnDisable() override;
        void OnDestroy() override;

    private:
        struct CameraPose
        {
            DirectX::XMFLOAT3 position{};
            DirectX::XMFLOAT3 rotationRad{};
            DirectX::XMFLOAT3 scale{ 1.0f, 1.0f, 1.0f };
        };

        enum class Phase
        {
            None,
            Blend12,
            Blend34,
            Blend56,
            Blend78,
            Blend910
        };

        struct BlendState
        {
            bool active = false;
            CameraPose from{};
            CameraPose to{};
            float duration = 0.0f;
            float elapsed = 0.0f;
            bool useSmoothStep = true;
        };

        void StartSequence();
        void AdvanceAfterBlend();
        void BeginBlend(const CameraPose& from, const CameraPose& to, float duration, bool useSmoothStep);
        bool UpdateBlend(float deltaTime);
        void ApplyPose(const CameraPose& pose);
        CameraPose GetCameraPose(int index);
        CameraPose GetPropertyPose(int index) const;
        CameraPose GetOutputCameraCurrentPose() const;
        bool TryResolveOutputCamera(EntityId& outId) const;
        bool TryGetEntityPose(EntityId entityId, CameraPose& outPose) const;
        bool TryGetSceneCameraPose(int index, CameraPose& outPose) const;
        std::string GetSceneCameraNameByIndex(int index) const;

        void SetGameplayCameraControl(bool enable);
        void RestoreIfNeeded();

        static DirectX::XMFLOAT3 DegreesToRadians(const DirectX::XMFLOAT3& deg);
        static DirectX::XMFLOAT3 LerpVec(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, float t);
        static DirectX::XMFLOAT4 EulerToQuaternion(const DirectX::XMFLOAT3& eulerRad);
        static DirectX::XMFLOAT3 QuaternionToEuler(const DirectX::XMFLOAT4& quat);
        static float ApplySmoothStep(float t);
        static float ResolveBlendDuration(float baseDuration, float speedMultiplier);

        ALICE_PROPERTY(std::string, m_outputCameraName, "MainCamera");
        ALICE_PROPERTY(bool, m_useSceneCameraEntities, true);
        ALICE_PROPERTY(bool, m_fallbackToPropertyPose, true);

        ALICE_PROPERTY(std::string, m_camera1Name, "Camera1");
        ALICE_PROPERTY(std::string, m_camera2Name, "Camera2");
        ALICE_PROPERTY(std::string, m_camera3Name, "Camera3");
        ALICE_PROPERTY(std::string, m_camera4Name, "Camera4");
        ALICE_PROPERTY(std::string, m_camera5Name, "Camera5");
        ALICE_PROPERTY(std::string, m_camera6Name, "Camera6");
        ALICE_PROPERTY(std::string, m_camera7Name, "Camera7");
        ALICE_PROPERTY(std::string, m_camera8Name, "Camera8");
        ALICE_PROPERTY(std::string, m_camera9Name, "Camera9");
        ALICE_PROPERTY(std::string, m_camera10Name, "Camera10");

        ALICE_PROPERTY(bool, m_enableHotkey, true);
        ALICE_PROPERTY(bool, m_restartOnKey, true);
        ALICE_PROPERTY(bool, m_restoreGameplayCameraOnFinish, true);
        ALICE_PROPERTY(bool, m_disableFollowDuringProduction, true);
        ALICE_PROPERTY(bool, m_disableLookAtDuringProduction, true);
        ALICE_PROPERTY(bool, m_disableSpringArmDuringProduction, false);

        ALICE_PROPERTY(float, m_blendDuration12, 1.20f);
        ALICE_PROPERTY(float, m_blendDuration34, 1.10f);
        ALICE_PROPERTY(float, m_blendDuration56, 1.10f);
        ALICE_PROPERTY(float, m_blendDuration78, 1.20f);
        ALICE_PROPERTY(float, m_blendDuration910, 1.40f);

        ALICE_PROPERTY(float, m_blendSpeed12, 1.00f);
        ALICE_PROPERTY(float, m_blendSpeed34, 1.00f);
        ALICE_PROPERTY(float, m_blendSpeed56, 1.00f);
        ALICE_PROPERTY(float, m_blendSpeed78, 1.00f);
        ALICE_PROPERTY(float, m_blendSpeed910, 1.00f);

        ALICE_PROPERTY(bool, m_useSmoothStep12, true);
        ALICE_PROPERTY(bool, m_useSmoothStep34, true);
        ALICE_PROPERTY(bool, m_useSmoothStep56, true);
        ALICE_PROPERTY(bool, m_useSmoothStep78, true);
        ALICE_PROPERTY(bool, m_useSmoothStep910, true);

        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera1Position, DirectX::XMFLOAT3(-43.90f, 1.85f, -23.62f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera1RotationDeg, DirectX::XMFLOAT3(19.34f, 79.00f, 0.00f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera1Scale, DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));

        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera2Position, DirectX::XMFLOAT3(-41.70f, 1.95f, -24.10f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera2RotationDeg, DirectX::XMFLOAT3(22.0f, -18.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera2Scale, DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));

        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera3Position, DirectX::XMFLOAT3(-42.80f, 4.10f, -24.40f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera3RotationDeg, DirectX::XMFLOAT3(48.0f, 16.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera3Scale, DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));

        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera4Position, DirectX::XMFLOAT3(-42.80f, 1.65f, -24.10f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera4RotationDeg, DirectX::XMFLOAT3(14.0f, 22.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera4Scale, DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));

        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera5Position, DirectX::XMFLOAT3(-44.80f, 1.70f, -22.10f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera5RotationDeg, DirectX::XMFLOAT3(17.0f, 110.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera5Scale, DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));

        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera6Position, DirectX::XMFLOAT3(-40.90f, 1.75f, -22.00f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera6RotationDeg, DirectX::XMFLOAT3(16.0f, -108.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera6Scale, DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));

        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera7Position, DirectX::XMFLOAT3(-44.90f, 1.20f, -25.60f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera7RotationDeg, DirectX::XMFLOAT3(11.0f, 58.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera7Scale, DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));

        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera8Position, DirectX::XMFLOAT3(-40.60f, 2.70f, -21.80f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera8RotationDeg, DirectX::XMFLOAT3(25.0f, -125.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera8Scale, DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));

        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera9Position, DirectX::XMFLOAT3(-42.00f, 2.10f, -20.20f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera9RotationDeg, DirectX::XMFLOAT3(18.0f, -180.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera9Scale, DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));

        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera10Position, DirectX::XMFLOAT3(-42.00f, 4.80f, -29.50f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera10RotationDeg, DirectX::XMFLOAT3(19.0f, 0.0f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_camera10Scale, DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f));

        BlendState m_blend{};
        Phase m_phase = Phase::None;
        bool m_sequenceRunning = false;
        bool m_controlsOverridden = false;

        bool m_hasFollow = false;
        bool m_hasLookAt = false;
        bool m_hasSpringArm = false;

        bool m_savedFollowEnabled = true;
        bool m_savedLookAtEnabled = false;
        bool m_savedSpringArmEnabled = true;
    };
}
