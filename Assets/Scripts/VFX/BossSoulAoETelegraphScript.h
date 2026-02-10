#pragma once

#include <string>

#include "Runtime/ECS/Entity.h"
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    class BossSoulAoETelegraphScript : public IScript
    {
        ALICE_BODY(BossSoulAoETelegraphScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        void OnDisable() override;

        // ALICE_PROPERTY(std::string, sessionEntityName, "SceneManager"); // unused
        ALICE_PROPERTY(std::string, bossEntityName, "Boss");
        ALICE_PROPERTY(std::string, aoeEntityName, "AoE");
        ALICE_PROPERTY(std::string, soulClipName, "Boss|Boss|Soul_Attack");
        ALICE_PROPERTY(int, telegraphSlotIndex, 8);
        ALICE_PROPERTY(float, alphaIdle, 0.0f);
        ALICE_PROPERTY(float, alphaPeak, 1.0f);
        ALICE_PROPERTY(bool, useAttackWindowPlateau, false);
        ALICE_PROPERTY(bool, holdPeakWhileActive, false);
        ALICE_PROPERTY(float, alphaFadeInSpeed, 0.0f);
        ALICE_PROPERTY(float, alphaFadeOutSpeed, 0.0f);
        ALICE_PROPERTY(bool, useSlotPeakTiming, false);
        ALICE_PROPERTY(float, soulPeakTimeSec, -1.0f);
        ALICE_PROPERTY(float, soulDurationFallbackSec, 2.4f);
        ALICE_PROPERTY(std::string, howlClipName, "Boss|Boss|Phase_Howling");
        ALICE_PROPERTY(std::string, howlDustEntityName, "W_EYE_TrailVfx");
        ALICE_PROPERTY(float, howlDustAlphaPeak, 1.0f);
        // ALICE_PROPERTY(float, howlDustFadeInSpeed, 2.0f); // unused
        // ALICE_PROPERTY(float, howlDustFadeOutSpeed, 3.0f); // unused
        ALICE_PROPERTY(bool, debugLogs, false);

    private:
        void ResolveBoss(bool logWarnings);
        void ResolveAoe(bool logWarnings);
        void ResolveSoulTiming(bool logWarnings);
        void ResolveHowlDust(bool logWarnings);
        void UpdateHowlDust(float deltaTime);
        void ResetHowlDust();
        float ResolveCurrentSoulClipDurationSec() const;
        bool TryGetCurrentClipTimeSec(const std::string& clipName, float& outTimeSec) const;
        bool TryGetCurrentSoulTimeSec(float& outTimeSec) const;
        bool IsTelegraphSlotActive() const;
        void ApplyAlpha(float alpha);

        EntityId m_bossId = InvalidEntityId;
        EntityId m_aoeId = InvalidEntityId;
        EntityId m_howlDustVfxId = InvalidEntityId;
        float m_howlDustAlpha = 0.0f;
        float m_appliedAlpha = 0.0f;
        bool m_alphaInitialized = false;
        float m_soulStartSec = 0.0f;
        float m_slotStartSec = 0.0f;
        float m_slotEndSec = 0.0f;
        bool m_hasTiming = false;
        bool m_warnedMissingBoss = false;
        bool m_warnedMissingAoe = false;
        bool m_warnedMissingAoeVfx = false;
        bool m_warnedMissingDustEntity = false;
        bool m_warnedMissingDustVfx = false;
        bool m_warnedMissingDriver = false;
        bool m_warnedMissingTiming = false;
        bool m_warnedBadSlotIndex = false;
    };
}
