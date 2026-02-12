#pragma once

#include <string>
#include <cstdint>
#include <vector>

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
        ALICE_PROPERTY(std::string, sessionEntityName, "SceneManager"); // C_CombatSessionComponent를 찾기 위한 엔티티 이름
        ALICE_PROPERTY(bool, useBus, true);
        ALICE_PROPERTY(bool, is3D, true);
        ALICE_PROPERTY(float, volume, 1.0f);
        ALICE_PROPERTY(float, pitch, 1.0f);
        ALICE_PROPERTY(float, minDistance, 1.0f);
        ALICE_PROPERTY(float, maxDistance, 50.0f);
        ALICE_PROPERTY(bool, loop, false);
        ALICE_PROPERTY(std::string, targetEntityName, ""); // 3D 사운드 위치를 가져올 대상 엔티티 이름 (비어있으면 스크립트가 붙은 엔티티 사용)

        // 플레이어 공격 상태별 경로 (ImGui Inspector에서 설정 가능)
        ALICE_PROPERTY(std::string, pathHeavyAttack, "Resource/Test/4_Resources/sound/SFX/Player/HeavyAttack/Player_HeavyAttack_01.mp3");
        ALICE_PROPERTY(std::string, pathAttack1, "Resource/Test/4_Resources/sound/SFX/Player/Attack1/Player_Attack_01.wav");
        ALICE_PROPERTY(std::string, pathAttack2, "Resource/Test/4_Resources/sound/SFX/Player/Attack2/Player_Attack_02.wav");
        ALICE_PROPERTY(std::string, pathAttack3, "Resource/Test/4_Resources/sound/SFX/Player/Attack3/Player_Attack_03.wav");
        ALICE_PROPERTY(std::string, pathGuard, "Resource/Test/4_Resources/sound/SFX/Player/Guard/Player_Guard_01.mp3");
        ALICE_PROPERTY(std::string, pathParry, "Resource/Test/4_Resources/sound/SFX/Player/parry/Player_Parry_01.wav");
        
        // 광폭화 공격 경로 (광폭화 상태일 때 Attack1/2/3 대신 사용)
        ALICE_PROPERTY(std::string, pathRageAttack1, "Resource/Test/4_Resources/sound/SFX/Player/BuffAttack/Player_Buff_Attack_A_01.mp3");
        ALICE_PROPERTY(std::string, pathRageAttack2, "Resource/Test/4_Resources/sound/SFX/Player/BuffAttack/Player_Buff_Attack_B_01.mp3");
        ALICE_PROPERTY(std::string, pathRageAttack3, "Resource/Test/4_Resources/sound/SFX/Player/BuffAttack/Player_Buff_Attack_C_01.mp3");

        // 플레이어 움직임 상태별 경로
        ALICE_PROPERTY(std::string, pathRoll, "Resource/Test/4_Resources/sound/SFX/Player/Rolling/Player_Rolling_01.mp3");
        ALICE_PROPERTY(std::string, pathRun, "Resource/Test/4_Resources/sound/SFX/Player/Run/Player_Footstep_1.wav");
        ALICE_PROPERTY(std::string, pathDash, "Resource/Test/4_Resources/sound/SFX/Player/Dash/Player_Dash_01.mp3");
        ALICE_PROPERTY(std::string, pathStop, "Resource/Test/4_Resources/sound/SFX/Player/stop/Player_Stop_1.wav");
        ALICE_PROPERTY(std::string, pathHitRoll, "Resource/Test/4_Resources/sound/SFX/Player/HitAndRoll/Player_Attacked_Rolling.mp3");

        // 플레이어 기타 상태별 경로
        ALICE_PROPERTY(std::string, pathGuardBreakAlarm, "Resource/Test/4_Resources/sound/SFX/Player/preGuardBreak/Player_GuardBreak_Alarm_01.wav");
        ALICE_PROPERTY(std::string, pathGuardBreak, "Resource/Test/4_Resources/sound/SFX/Player/GuardBreak/Player_GuardBreak_01.mp3");
        ALICE_PROPERTY(std::string, pathEgoCombine, "Resource/Test/4_Resources/sound/SFX/Player/WeaponCombine/Player_Weapon_Gather_01.wav");
        ALICE_PROPERTY(std::string, pathHeal, "Resource/Test/4_Resources/sound/SFX/Player/heal/Player_Healing_01.wav");
        ALICE_PROPERTY(std::string, pathGroggyAttack, "Resource/Test/4_Resources/sound/SFX/Player/GroggyAttack/Player_GroggyAttack_01.mp3");
        ALICE_PROPERTY(std::string, pathDeath, "Resource/Test/4_Resources/sound/SFX/Player/Dead/Player_Death_01.wav");

        /// 공격 상태 → OnPlayerAttackSfxRequest 델리게이트로 바인딩
        // Per-file volume controls (Inspector)
        ALICE_PROPERTY(float, volumeHeavyAttack, 1.0f);
        ALICE_PROPERTY(float, volumeAttack1, 1.0f);
        ALICE_PROPERTY(float, volumeAttack2, 1.0f);
        ALICE_PROPERTY(float, volumeAttack3, 1.0f);
        ALICE_PROPERTY(float, volumeGuard, 1.0f);
        ALICE_PROPERTY(float, volumeParry, 1.0f);
        ALICE_PROPERTY(float, volumeRoll, 1.0f);
        ALICE_PROPERTY(float, volumeRun, 1.0f);
        ALICE_PROPERTY(float, volumeDash, 1.0f);
        ALICE_PROPERTY(float, volumeStop, 1.0f);
        ALICE_PROPERTY(float, volumeHitRoll, 1.0f);
        ALICE_PROPERTY(float, volumeGuardBreakAlarm, 1.0f);
        ALICE_PROPERTY(float, volumeGuardBreak, 1.0f);
        ALICE_PROPERTY(float, volumeEgoCombine, 1.0f);
        ALICE_PROPERTY(float, volumeHeal, 1.0f);
        ALICE_PROPERTY(float, volumeGroggyAttack, 1.0f);
        ALICE_PROPERTY(float, volumeDeath, 1.0f);

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
        struct DelayedSoundRequest
        {
            PlayerAttackState state;
            float remainingDelay;
            std::wstring key;  // 미리 로드된 사운드 키
            float volume;
            float pitch;
        };

        void PlayAttackState(PlayerAttackState state);
        void PlayAttackStateDelayed(PlayerAttackState state, float delaySeconds);
        void PlayMovementState(PlayerMovementState state);
        void PlayOtherState(PlayerOtherState state);
        std::string GetPathForAttackState(PlayerAttackState state) const;
        std::string GetPathForMovementState(PlayerMovementState state) const;
        std::string GetPathForOtherState(PlayerOtherState state) const;
        float GetAttackStateVolume(PlayerAttackState state) const;
        float GetMovementStateVolume(PlayerMovementState state) const;
        float GetOtherStateVolume(PlayerOtherState state) const;
        void PlayPathInternal(const std::string& path, bool isLooping, float volumeMul);
        void PlayPathInternal(const std::string& path, bool isLooping);
        void PreloadSound(const std::string& path);

        PlayerAttackState m_currentAttack{ PlayerAttackState::None };
        PlayerMovementState m_currentMovement{ PlayerMovementState::None };
        PlayerOtherState m_currentOther{ PlayerOtherState::None };
        std::wstring m_loopKey;
        std::wstring m_loopInstanceId;
        bool m_loopPlaying = false;
        int m_footstepIndex = 0;
        
        // C++ deltaTime 기반 딜레이 큐
        std::vector<DelayedSoundRequest> m_delayedSoundQueue;
    };
}
