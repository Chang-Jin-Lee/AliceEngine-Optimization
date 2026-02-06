#pragma once

#include <string>
#include <cstdint>

#include "AudioSoundState.h"
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    class AudioBossScript : public IScript
    {
        ALICE_BODY(AudioBossScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        ALICE_PROPERTY(std::string, busEntityName, std::string("AudioBus"));
        ALICE_PROPERTY(bool, useBus, true);
        ALICE_PROPERTY(bool, is3D, true);
        ALICE_PROPERTY(float, volume, 1.0f);
        ALICE_PROPERTY(float, pitch, 1.0f);
        ALICE_PROPERTY(float, minDistance, 1.0f);
        ALICE_PROPERTY(float, maxDistance, 50.0f);
        ALICE_PROPERTY(bool, loop, false);

        ALICE_PROPERTY(std::string, sfxPath1, std::string("Resource/Sound/SFX/보스/공격 1/Boss_Attack_01.mp3"));
        ALICE_PROPERTY(std::string, sfxPath2, std::string("Resource/Sound/SFX/보스/공격 2/Boss_Attack_02.wav"));
        ALICE_PROPERTY(std::string, sfxPath3, std::string("Resource/Sound/SFX/보스/공격 3/Boss_Attack_03.wav"));
        ALICE_PROPERTY(std::string, sfxPath4, std::string("Resource/Sound/SFX/보스/포효/Boss_Roaring_01.mp3"));
        ALICE_PROPERTY(std::string, sfxPath5, std::string("Resource/Sound/SFX/보스/피격/Boss_Hit_01.wav"));

        /// 공격 상태 → OnBossAttackSfxRequest 델리게이트로 바인딩
        void SetAttackState(BossAttackState state);
        BossAttackState GetAttackState() const { return m_currentAttack; }

        /// 움직임 상태 → OnBossMovementSfxRequest 델리게이트로 바인딩
        void SetMovementState(BossMovementState state);
        BossMovementState GetMovementState() const { return m_currentMovement; }

        /// 나머지 상태 → OnBossOtherSfxRequest 델리게이트로 바인딩
        void SetOtherState(BossOtherState state);
        BossOtherState GetOtherState() const { return m_currentOther; }

        void PlaySfxPath(const std::string& path);
        void StopLoop();

        void PlaySfx1();
        void PlaySfx2();
        void PlaySfx3();
        void PlaySfx4();
        void PlaySfx5();

        ALICE_FUNC(SetAttackState);
        ALICE_FUNC(SetMovementState);
        ALICE_FUNC(SetOtherState);
        ALICE_FUNC(PlaySfxPath);
        ALICE_FUNC(StopLoop);
        ALICE_FUNC(PlaySfx1);
        ALICE_FUNC(PlaySfx2);
        ALICE_FUNC(PlaySfx3);
        ALICE_FUNC(PlaySfx4);
        ALICE_FUNC(PlaySfx5);

    private:
        void PlayAttackState(BossAttackState state);
        void PlayMovementState(BossMovementState state);
        void PlayOtherState(BossOtherState state);
        std::string GetPathForAttackState(BossAttackState state) const;
        std::string GetPathForMovementState(BossMovementState state) const;
        std::string GetPathForOtherState(BossOtherState state) const;
        void PlayPathInternal(const std::string& path, bool isLooping);

        BossAttackState m_currentAttack{ BossAttackState::None };
        BossMovementState m_currentMovement{ BossMovementState::None };
        BossOtherState m_currentOther{ BossOtherState::None };
        std::wstring m_loopKey;
        std::wstring m_loopInstanceId;
        bool m_loopPlaying = false;
        int m_footstepIndex = 0;
    };
}
