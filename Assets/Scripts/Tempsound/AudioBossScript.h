#pragma once

#include <string>
#include <cstdint>
#include <vector>
#include <map>

#include "AudioSoundState.h"
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

// FMOD forward declaration
namespace FMOD { class Channel; }

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
        ALICE_PROPERTY(std::string, targetEntityName, ""); // 3D 사운드 위치를 가져올 대상 엔티티 이름 (비어있으면 스크립트가 붙은 엔티티 사용)

        // 보스 공격 상태별 경로 (ImGui Inspector에서 설정 가능)
        ALICE_PROPERTY(std::string, pathAttackAlarm, "Resource/Test/4_Resources/sound/SFX/보스/공격 전조 알림/Boss_Attack_Alarm_01.mp3");
        ALICE_PROPERTY(std::string, pathAttack1, "Resource/Test/4_Resources/sound/SFX/보스/공격 1/Boss_Attack_01.mp3");
        ALICE_PROPERTY(std::string, pathAttack2, "Resource/Test/4_Resources/sound/SFX/보스/공격 2/Boss_Attack_02.wav");
        ALICE_PROPERTY(std::string, pathAttack3, "Resource/Test/4_Resources/sound/SFX/보스/공격 3/Boss_Attack_03.wav");
        ALICE_PROPERTY(std::string, pathAttackABC, "Resource/Test/4_Resources/sound/SFX/보스/공격 ABC/Boss_Attack_ABC.wav");
        ALICE_PROPERTY(std::string, pathSoulSwordCharge, "Resource/Test/4_Resources/sound/SFX/보스/영혼대검 차지/Boss_SoulAttack_Charging_01.mp3");
        ALICE_PROPERTY(std::string, pathSoulSwordAttack, "Resource/Test/4_Resources/sound/SFX/보스/영혼대검 공격/Boss_SoulAttack_Attack_01.wav");
        ALICE_PROPERTY(std::string, pathSideAttack, "Resource/Test/4_Resources/sound/SFX/보스/옆, 견제 공격/Boss_Attack_Side_01.mp3");
        ALICE_PROPERTY(std::string, pathDashAttack, "Resource/Test/4_Resources/sound/SFX/보스/대쉬공격/Boss_DashAttack_01.mp3");

        // 보스 공격 상태별 딜레이 시간 (초 단위, ImGui Inspector에서 설정 가능)
        // delaySeconds가 0 이하로 전달되면 이 값이 사용됨
        ALICE_PROPERTY(float, delayAttackAlarm, 0.0f);
        ALICE_PROPERTY(float, delayAttack1, 0.0f);
        ALICE_PROPERTY(float, delayAttack2, 0.0f);
        ALICE_PROPERTY(float, delayAttack3, 0.0f);
        ALICE_PROPERTY(float, delayAttackABC, 0.0f);
        ALICE_PROPERTY(float, delaySoulSwordCharge, 0.0f);
        ALICE_PROPERTY(float, delaySoulSwordAttack, 0.0f);
        ALICE_PROPERTY(float, delaySideAttack, 0.0f);
        ALICE_PROPERTY(float, delayDashAttack, 0.0f);
        
        // 보스 연격 딜레이 (패턴별, 공격 수에 따라 개별 설정)
        ALICE_PROPERTY(float, attackADelay1Sec, 0.2f);
        ALICE_PROPERTY(float, attackBDelay2Sec, 0.2f);
        ALICE_PROPERTY(float, attackBDelay3Sec, 0.6f);
        ALICE_PROPERTY(float, attackCDelay1Sec, 0.2f);
        ALICE_PROPERTY(float, attackCDelay2Sec, 0.8f);
        ALICE_PROPERTY(float, attackCDelay3Sec, 1.6f);
        ALICE_PROPERTY(float, attackCDelayABCSec, 0.0f);

        // 보스 움직임 상태별 경로
        ALICE_PROPERTY(std::string, pathWalk, "Resource/Test/4_Resources/sound/SFX/보스/걷기/Boss_Footstep_01.mp3");
        ALICE_PROPERTY(std::string, pathRotate, "Resource/Test/4_Resources/sound/SFX/보스/몸 돌리기/Boss_Rotate_01.wav");

        // 보스 기타 상태별 경로
        ALICE_PROPERTY(std::string, pathGroggyEnter, "Resource/Test/4_Resources/sound/SFX/보스/그로기 진입/Boss_Groggy_Alarm_01.wav");
        ALICE_PROPERTY(std::string, pathRoar, "Resource/Test/4_Resources/sound/SFX/보스/포효/Boss_Roaring_01.mp3");
        ALICE_PROPERTY(std::string, pathHit, "Resource/Test/4_Resources/sound/SFX/보스/피격/Boss_Hit_01.wav");
        ALICE_PROPERTY(std::string, pathDeath, "Resource/Sound/SFX/보스/사망/Boss_Death_01.wav");

        // 보스 공격 상태별 볼륨 (인스펙터에서 설정 가능, 기본값 1.0f, 1.0f 이상도 허용)
        ALICE_PROPERTY(float, volumeAttackAlarm, 1.0f);
        ALICE_PROPERTY(float, volumeAttack1, 1.0f);
        ALICE_PROPERTY(float, volumeAttack2, 1.0f);
        ALICE_PROPERTY(float, volumeAttack3, 1.0f);
        ALICE_PROPERTY(float, volumeAttackABC, 1.0f);
        ALICE_PROPERTY(float, volumeSoulSwordCharge, 1.0f);
        ALICE_PROPERTY(float, volumeSoulSwordAttack, 1.0f);
        ALICE_PROPERTY(float, volumeSideAttack, 1.0f);
        ALICE_PROPERTY(float, volumeDashAttack, 1.0f);

        // 보스 움직임 상태별 볼륨
        ALICE_PROPERTY(float, volumeWalk, 1.0f);
        ALICE_PROPERTY(float, volumeRotate, 1.0f);

        // 보스 기타 상태별 볼륨
        ALICE_PROPERTY(float, volumeGroggyEnter, 1.0f);
        ALICE_PROPERTY(float, volumeRoar, 1.0f);
        ALICE_PROPERTY(float, volumeHit, 1.0f);
        ALICE_PROPERTY(float, volumeDeath, 1.0f);

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

        // 특정 공격 상태의 볼륨 조절 (0.0f ~ 1.0f)
        void SetAttackStateVolume(BossAttackState state, float volume);
        // 특정 움직임 상태의 볼륨 조절
        void SetMovementStateVolume(BossMovementState state, float volume);
        // 특정 기타 상태의 볼륨 조절
        void SetOtherStateVolume(BossOtherState state, float volume);

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
        ALICE_FUNC(SetAttackStateVolume);
        ALICE_FUNC(SetMovementStateVolume);
        ALICE_FUNC(SetOtherStateVolume);

    private:
        float GetAttackStateVolume(BossAttackState state) const;
        float GetMovementStateVolume(BossMovementState state) const;
        float GetOtherStateVolume(BossOtherState state) const;

        struct DelayedSoundRequest
        {
            BossAttackState state;
            float remainingDelay;
            std::wstring key;  // 미리 로드된 사운드 키
            float volume;
            float pitch;
        };

        void PlayAttackState(BossAttackState state);
        void PlayAttackStateDelayed(BossAttackState state, float delaySeconds);
        void PlayMovementState(BossMovementState state);
        void PlayOtherState(BossOtherState state);
        std::string GetPathForAttackState(BossAttackState state) const;
        std::string GetPathForMovementState(BossMovementState state) const;
        std::string GetPathForOtherState(BossOtherState state) const;
        float GetDelayForAttackState(BossAttackState state) const;
        void PlayPathInternal(const std::string& path, bool isLooping, 
                              BossAttackState attackState = BossAttackState::None,
                              BossMovementState movementState = BossMovementState::None,
                              BossOtherState otherState = BossOtherState::None);
        
        // 인스펙터 볼륨 초기화 헬퍼 함수
        void InitializeAttackGroupVolume(BossAttackState state, float volume);
        void InitializeMovementGroupVolume(BossMovementState state, float volume);
        void InitializeOtherGroupVolume(BossOtherState state, float volume);

        BossAttackState m_currentAttack{ BossAttackState::None };
        BossMovementState m_currentMovement{ BossMovementState::None };
        BossOtherState m_currentOther{ BossOtherState::None };
        std::wstring m_loopKey;
        std::wstring m_loopInstanceId;
        bool m_loopPlaying = false;
        
        // C++ deltaTime 기반 딜레이 큐
        std::vector<DelayedSoundRequest> m_delayedSoundQueue;
        
        // 상태별 그룹 이름 저장 (ChannelGroup 기반 볼륨 조절용)
        std::map<BossAttackState, std::wstring> m_attackGroupNames;
        std::map<BossMovementState, std::wstring> m_movementGroupNames;
        std::map<BossOtherState, std::wstring> m_otherGroupNames;





   

    };

}


