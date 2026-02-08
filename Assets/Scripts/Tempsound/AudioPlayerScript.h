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

        // 플레이어 공격 상태별 경로 (ImGui Inspector에서 설정 가능)
        ALICE_PROPERTY(std::string, pathHeavyAttack, "Resource/Test/4_Resources/sound/SFX/플레이어/강공격/Player_HeavyAttack_01.mp3");
        ALICE_PROPERTY(std::string, pathAttack1, "Resource/Test/4_Resources/sound/SFX/플레이어/공격 1/Player_Attack_01.wav");
        ALICE_PROPERTY(std::string, pathAttack2, "Resource/Test/4_Resources/sound/SFX/플레이어/공격 2/Player_Attack_02.wav");
        ALICE_PROPERTY(std::string, pathAttack3, "Resource/Test/4_Resources/sound/SFX/플레이어/공격 3/Player_Attack_03.wav");
        ALICE_PROPERTY(std::string, pathGuard, "Resource/Test/4_Resources/sound/SFX/플레이어/가드/Player_Guard_01.mp3");
        ALICE_PROPERTY(std::string, pathParry, "Resource/Test/4_Resources/sound/SFX/플레이어/패링/Player_Parry_01.wav");

        // 플레이어 움직임 상태별 경로
        ALICE_PROPERTY(std::string, pathRoll, "Resource/Test/4_Resources/sound/SFX/플레이어/구르기/Player_Rolling_01.mp3");
        ALICE_PROPERTY(std::string, pathRun, "Resource/Test/4_Resources/sound/SFX/플레이어/달리기/Player_Footstep_1.wav");
        ALICE_PROPERTY(std::string, pathDash, "Resource/Test/4_Resources/sound/SFX/플레이어/대시/Player_Dash_01.mp3");
        ALICE_PROPERTY(std::string, pathStop, "Resource/Test/4_Resources/sound/SFX/플레이어/멈추기/Player_Stop_1.wav");
        ALICE_PROPERTY(std::string, pathHitRoll, "Resource/Test/4_Resources/sound/SFX/플레이어/피격 후 구르기/Player_Attacked_Rolling.mp3");

        // 플레이어 기타 상태별 경로
        ALICE_PROPERTY(std::string, pathGuardBreakAlarm, "Resource/Test/4_Resources/sound/SFX/플레이어/가드 브레이크 전조음/Player_GuardBreak_Alarm_01.wav");
        ALICE_PROPERTY(std::string, pathGuardBreak, "Resource/Test/4_Resources/sound/SFX/플레이어/가드 브레이크/Player_GuardBreak_01.mp3");
        ALICE_PROPERTY(std::string, pathEgoCombine, "Resource/Test/4_Resources/sound/SFX/플레이어/에고웨폰 재결합/Player_Weapon_Gather_01.wav");
        ALICE_PROPERTY(std::string, pathHeal, "Resource/Sound/SFX/플레이어/회복/Player_Healing_01.wav");
        ALICE_PROPERTY(std::string, pathDeath, "Resource/Sound/SFX/플레이어/사망/Player_Death_01.wav");

        /// 공격 상태 → OnPlayerAttackSfxRequest 델리게이트로 바인딩
        void SetAttackState(PlayerAttackState state);
        PlayerAttackState GetAttackState() const { return m_currentAttack; }

        /// 공격 소리 즉시 재생(상태 변경 없음) → 콤보 2추가 등 동일 상태 재재생용
        void PlayAttackOneShot(PlayerAttackState state);
        /// 공격 소리 즉시 재생(지정된 위치에서) → 가드/패링 등 히트 위치에서 재생용
        void PlayAttackOneShotAtPosition(PlayerAttackState state, const DirectX::XMFLOAT3& position);

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
        ALICE_FUNC(PlayAttackOneShotAtPosition);
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
        void PreloadSound(const std::string& path);

        PlayerAttackState m_currentAttack{ PlayerAttackState::None };
        PlayerMovementState m_currentMovement{ PlayerMovementState::None };
        PlayerOtherState m_currentOther{ PlayerOtherState::None };
        std::wstring m_loopKey;
        std::wstring m_loopInstanceId;
        bool m_loopPlaying = false;
        int m_footstepIndex = 0;
    };
}
