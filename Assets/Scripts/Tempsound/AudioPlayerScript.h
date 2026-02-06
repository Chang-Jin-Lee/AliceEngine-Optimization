#pragma once

#include <string>
#include <cstdint>

#include "AudioSoundState.h"
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    class AudioPlayerScript : public IScript
    {
        ALICE_BODY(AudioPlayerScript);

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

        ALICE_PROPERTY(std::string, sfxPath1, std::string("Resource/Sound/SFX/플레이어/공격_1/Player_Attack_01.wav"));
        ALICE_PROPERTY(std::string, sfxPath2, std::string("Resource/Sound/SFX/플레이어/공격_2/Player_Attack_02.wav"));
        ALICE_PROPERTY(std::string, sfxPath3, std::string("Resource/Sound/SFX/플레이어/공격_3/Player_Attack_03.wav"));
        ALICE_PROPERTY(std::string, sfxPath4, std::string("Resource/Sound/SFX/플레이어/가드/Player_Guard_01.mp3"));
        ALICE_PROPERTY(std::string, sfxPath5, std::string("Resource/Sound/SFX/플레이어/구르기/Player_Rolling_01.mp3"));

        /// 공격 상태 → OnPlayerAttackSfxRequest 델리게이트로 바인딩
        void SetAttackState(PlayerAttackState state);
        PlayerAttackState GetAttackState() const { return m_currentAttack; }

        /// 공격 소리 즉시 재생(상태 변경 없음) → 콤보 2추가 등 동일 상태 재재생용
        void PlayAttackOneShot(PlayerAttackState state);

        /// 움직임 상태 → OnPlayerMovementSfxRequest 델리게이트로 바인딩 (playStopSfx: Stop일 때 정지음 재생 여부)
        void SetMovementState(PlayerMovementState state, bool playStopSfx = true);
        PlayerMovementState GetMovementState() const { return m_currentMovement; }

        /// 나머지 상태 → OnPlayerOtherSfxRequest 델리게이트로 바인딩
        void SetOtherState(PlayerOtherState state);
        PlayerOtherState GetOtherState() const { return m_currentOther; }

        void PlaySfxPath(const std::string& path);
        void StopLoop();

        void PlaySfx1();
        void PlaySfx2();
        void PlaySfx3();
        void PlaySfx4();
        void PlaySfx5();

        ALICE_FUNC(SetAttackState);
        ALICE_FUNC(PlayAttackOneShot);
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
        void PlayAttackState(PlayerAttackState state);
        void PlayMovementState(PlayerMovementState state);
        void PlayOtherState(PlayerOtherState state);
        std::string GetPathForAttackState(PlayerAttackState state) const;
        std::string GetPathForMovementState(PlayerMovementState state) const;
        std::string GetPathForOtherState(PlayerOtherState state) const;
        void PlayPathInternal(const std::string& path, bool isLooping);

        PlayerAttackState m_currentAttack{ PlayerAttackState::None };
        PlayerMovementState m_currentMovement{ PlayerMovementState::None };
        PlayerOtherState m_currentOther{ PlayerOtherState::None };
        std::wstring m_loopKey;
        std::wstring m_loopInstanceId;
        bool m_loopPlaying = false;
        int m_footstepIndex = 0;
    };
}
