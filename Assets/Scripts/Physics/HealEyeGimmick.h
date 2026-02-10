#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/ECS/Entity.h"

#include <string>
#include <vector>
#include <DirectXMath.h>

namespace Alice
{
    // Heal enter/loop/exit VFX controller (weapon shards + Heal_EYE crossfade)
    class HealEyeGimmick : public IScript
    {
        ALICE_BODY(HealEyeGimmick);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        void BeginHeal(float enterDurationSec);
        void BeginHealLoop();
        void EndHeal(float exitDurationSec);

        ALICE_PROPERTY(std::string, m_weaponCombinedName, "EGO_Blade(combined)");
        ALICE_PROPERTY(std::string, m_eyeName, "Heal_EYE");
        ALICE_PROPERTY(std::string, m_playerHealEffectName, "PlayerHealEffect");
        ALICE_PROPERTY(std::string, m_shardNamesCsv, "WB_Base,WB_Back,WB_FrontA,WB_FrontB,WB_FrontC,WB_FrontD");
        ALICE_PROPERTY(float, m_enterFadeRatio, 0.7f);
        ALICE_PROPERTY(float, m_exitFadeRatio, 0.7f);
        ALICE_PROPERTY(float, m_fadeMinSec, 0.05f);
        ALICE_PROPERTY(float, m_eyeIdleAlpha, 0.0f);
        ALICE_PROPERTY(float, m_bobAmplitude, 0.08f);
        ALICE_PROPERTY(float, m_bobSpeed, 2.0f);
        ALICE_PROPERTY(float, m_bobBaseY, 0.0f);
        ALICE_PROPERTY(float, m_bobHeightOffset, 0.0f);
        ALICE_PROPERTY(float, m_spinSpeedDeg, 180.0f);
        ALICE_PROPERTY(bool, m_enableLogs, false);

    private:
        enum class Phase
        {
            Idle = 0,
            Entering,
            Loop,
            Exiting
        };

        struct ShardState
        {
            EntityId id = InvalidEntityId;
            std::string name;
        };

        EntityId m_weaponCombined = InvalidEntityId;
        EntityId m_eye = InvalidEntityId;
        EntityId m_playerHealEffect = InvalidEntityId;
        std::vector<ShardState> m_shards;

        bool m_initialized = false;
        Phase m_phase = Phase::Idle;
        bool m_loopRequested = false;

        float m_transitionTimer = 0.0f;
        float m_transitionDuration = 0.0f;
        float m_weaponAlphaFrom = 1.0f;
        float m_weaponAlphaTo = 1.0f;
        float m_eyeAlphaFrom = 0.0f;
        float m_eyeAlphaTo = 0.0f;
        float m_currentWeaponAlpha = 1.0f;
        float m_currentEyeAlpha = 0.0f;
        float m_weaponDefaultAlpha = 1.0f;

        DirectX::XMFLOAT3 m_eyeFloatAnchor{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 m_eyeBaseRotation{ 0.0f, 0.0f, 0.0f };
        bool m_eyeFloatAnchorValid = false;
        bool m_eyeBaseRotationValid = false;
        float m_bobTime = 0.0f;
        float m_spinYawDeg = 0.0f;

        void FindEntities();
        void EnterIdle();
        void StartTransition(float weaponFrom, float weaponTo,
                             float eyeFrom, float eyeTo,
                             float durationSec);
        void UpdateTransition(float dt);
        void UpdateEyeFloat(float dt);
        float ResolveFadeDuration(float baseDurationSec, float ratio) const;

        void ShowShards(bool visible);
        void SetEnabled(EntityId id, bool enabled);
        void SetVisible(EntityId id, bool visible);
        void SetMaterialAlpha(EntityId id, float alpha);
        void SetUnityVfxAlpha(EntityId id, float alpha);
        float GetMaterialAlpha(EntityId id, float fallback) const;
    };
}
