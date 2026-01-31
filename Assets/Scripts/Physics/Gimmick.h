#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

#include <string>
#include <vector>
#include <cstdint>
#include <random>

#include "Runtime/ECS/Entity.h"
#include <DirectXMath.h>

namespace Alice
{
    // Weapon break/assemble gimmick controller
    class Gimmick : public IScript
    {
        ALICE_BODY(Gimmick);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        ALICE_PROPERTY(std::string, m_weaponCombinedName, "Weapon(combined)");
        ALICE_PROPERTY(std::string, m_coreName, "W_Core");
        ALICE_PROPERTY(std::string, m_tendonName, "W_Tendon");
        ALICE_PROPERTY(std::string, m_eyeName, "W_EYE");
        ALICE_PROPERTY(std::string, m_bindTargetName, "W_Target");

        ALICE_PROPERTY(float, m_breakMaxImpulse, 20.0f);

        ALICE_PROPERTY(float, m_magnetizeInterval, 1.0f);
        ALICE_PROPERTY(float, m_eyeFloatMoveSpeed, 2.0f);
        ALICE_PROPERTY(float, m_orbitAngularSpeed, 2.0f);
        ALICE_PROPERTY(float, m_orbitAngularBlendDuration, 0.35f);
        ALICE_PROPERTY(float, m_orbitRadiusScale, 1.0f);
        ALICE_PROPERTY(float, m_orbitMinRadius, 0.2f);

        ALICE_PROPERTY(float, m_assembleInterval, 1.0f);
        ALICE_PROPERTY(float, m_assembleMoveSpeed, 4.0f);
        ALICE_PROPERTY(float, m_assembleDistanceAccel, 3.0f);
        ALICE_PROPERTY(float, m_eyeMoveSpeed, 8.0f);
        ALICE_PROPERTY(float, m_eyeDistanceAccel, 4.0f);
        ALICE_PROPERTY(float, m_tendonVisibleDelay, 1.0f);

        ALICE_PROPERTY(float, m_arriveThreshold, 0.02f);
        ALICE_PROPERTY(uint32_t, m_ignoreLayersMask, 0u);

        ALICE_PROPERTY(float, m_capturePullBaseSpeed, 6.0f);
        ALICE_PROPERTY(float, m_capturePullDistanceAccel, 4.0f);
        ALICE_PROPERTY(float, m_capturePullTargetDuration, 0.0f);
        ALICE_PROPERTY(float, m_capturePullMinDuration, 0.08f);
        ALICE_PROPERTY(float, m_capturePullMaxDuration, 0.6f);

    private:
        enum class Phase
        {
            Normal = 0,
            Break,
            Magnetize,
            AssembleShards,
            AssembleEye,
            Restore
        };

        struct ShardState
        {
            EntityId id = InvalidEntityId;
            std::string name;
            bool captured = false;
            bool pulling = false;
            bool assembling = false;
            bool assembled = false;
            float orbitAngle = 0.0f;
            DirectX::XMFLOAT3 orbitAxis{ 0.0f, 1.0f, 0.0f };
            DirectX::XMFLOAT3 orbitBaseDir{ 1.0f, 0.0f, 0.0f };
            float orbitRadius = 0.0f;
            DirectX::XMFLOAT3 pullStartPos{ 0.0f, 0.0f, 0.0f };
            float pullTimer = 0.0f;
            float pullDuration = 0.35f;
            float pullSpeed = 0.0f;
            float orbitAngularSpeed = 0.0f;
            float orbitAngularStartSpeed = 0.0f;
            float orbitBlendTimer = 0.0f;
            bool orbitBlending = false;
        };

        EntityId m_weaponCombined = InvalidEntityId;
        EntityId m_core = InvalidEntityId;
        EntityId m_tendon = InvalidEntityId;
        EntityId m_eye = InvalidEntityId;
        EntityId m_bindTarget = InvalidEntityId;

        std::vector<ShardState> m_shards;
        std::vector<size_t> m_assembleOrder;

        Phase m_phase = Phase::Normal;
        bool m_initialized = false;

        float m_phaseTime = 0.0f;
        float m_captureTimer = 0.0f;
        float m_assembleTimer = 0.0f;
        size_t m_nextAssembleIndex = 0;

        bool m_pendingBreakImpulse = false;
        bool m_eyeArrived = false;
        float m_tendonTimer = 0.0f;
        bool m_tendonFading = false;
        bool m_magnetizeInitialized = false;
        bool m_eyeFloatAnchorValid = false;
        DirectX::XMFLOAT3 m_eyeFloatAnchor{ 0.0f, 0.0f, 0.0f };

        std::mt19937 m_rng;

        void FindEntities();

        void AdvancePhase();
        void EnterPhase(Phase phase);

        void UpdateMagnetize(float dt);
        void UpdateAssembleShards(float dt);
        void UpdateAssembleEye(float dt);

        void UpdateEyeFloat(float dt);
        void UpdateOrbitingShards(float dt);

        void ApplyBreakImpulse();
        bool CanApplyBreakImpulse() const;

        void ResetShardState();

        void SetEnabled(EntityId id, bool enabled);
        void SetVisible(EntityId id, bool visible);
        void SetMaterialAlpha(EntityId id, float alpha);
        void SetMaterialTransparent(EntityId id, bool transparent);
        void SetColliderTrigger(EntityId id, bool trigger);
        void SetIgnoreLayers(EntityId id, uint32_t mask);
        void AddIgnoreSelfLayer(EntityId id);
        void SetRigidBodyKinematic(EntityId id, bool kinematic, bool gravityEnabled);
        void ClearRigidBodyVelocity(EntityId id);
        void TeleportRigidBody(EntityId id);

        bool MoveTowards(EntityId id, const DirectX::XMFLOAT3& targetPos,
                         const DirectX::XMFLOAT3& targetRot, const DirectX::XMFLOAT3& targetScale,
                         float speed, float dt);

        DirectX::XMFLOAT3 GetEyeWorldPosition() const;
        bool GetBindTargetWorldPose(DirectX::XMFLOAT3& outPos,
                                    DirectX::XMFLOAT3& outRot,
                                    DirectX::XMFLOAT3& outScale) const;
    };
}
