#pragma once

#include <string>

#include "AudioSoundState.h"
#include "Runtime/Foundation/Delegate.h"
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    // BGM
    ALICE_DECLARE_DELEGATE_OneParam(FOnBgmRequest, const std::string&);
    ALICE_DECLARE_DELEGATE(FOnBgmStopRequest);

    // 레거시: 경로 문자열로 요청
    ALICE_DECLARE_DELEGATE_OneParam(FOnBossSfxRequest, const std::string&);
    ALICE_DECLARE_DELEGATE_OneParam(FOnPlayerSfxRequest, const std::string&);

    // 보스: 공격 / 움직임 / 나머지 별도 델리게이트
    ALICE_DECLARE_DELEGATE_OneParam(FOnBossAttackSfxRequest, BossAttackState);
    ALICE_DECLARE_DELEGATE_OneParam(FOnBossMovementSfxRequest, BossMovementState);
    ALICE_DECLARE_DELEGATE_OneParam(FOnBossOtherSfxRequest, BossOtherState);

    // 플레이어: 공격 / 움직임 / 나머지 별도 델리게이트
    ALICE_DECLARE_DELEGATE_OneParam(FOnPlayerAttackSfxRequest, PlayerAttackState);
    ALICE_DECLARE_DELEGATE_OneParam(FOnPlayerAttackSfxOneShotRequest, PlayerAttackState);
    ALICE_DECLARE_DELEGATE_TwoParams(FOnPlayerMovementSfxRequest, PlayerMovementState, bool);
    ALICE_DECLARE_DELEGATE_OneParam(FOnPlayerOtherSfxRequest, PlayerOtherState);

    class AudioEventBusScript : public IScript
    {
        ALICE_BODY(AudioEventBusScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        FOnBgmRequest OnBgmRequest;
        FOnBgmStopRequest OnBgmStopRequest;
        FOnBossSfxRequest OnBossSfxRequest;
        FOnPlayerSfxRequest OnPlayerSfxRequest;

        FOnBossAttackSfxRequest OnBossAttackSfxRequest;
        FOnBossMovementSfxRequest OnBossMovementSfxRequest;
        FOnBossOtherSfxRequest OnBossOtherSfxRequest;
        FOnPlayerAttackSfxRequest OnPlayerAttackSfxRequest;
        FOnPlayerAttackSfxOneShotRequest OnPlayerAttackSfxOneShotRequest;
        FOnPlayerMovementSfxRequest OnPlayerMovementSfxRequest;
        FOnPlayerOtherSfxRequest OnPlayerOtherSfxRequest;

        void RequestBgm(const std::string& path);
        void RequestStopBgm();
        void RequestBossSfx(const std::string& path);
        void RequestPlayerSfx(const std::string& path);

        void RequestBossAttackSfx(BossAttackState state);
        void RequestBossMovementSfx(BossMovementState state);
        void RequestBossOtherSfx(BossOtherState state);
        void RequestPlayerAttackSfx(PlayerAttackState state);
        void RequestPlayerAttackSfxOneShot(PlayerAttackState state);
        void RequestPlayerMovementSfx(PlayerMovementState state, bool playStopSfx = true);
        void RequestPlayerOtherSfx(PlayerOtherState state);

        ALICE_FUNC(RequestBgm);
        ALICE_FUNC(RequestStopBgm);
        ALICE_FUNC(RequestBossSfx);
        ALICE_FUNC(RequestPlayerSfx);
        ALICE_FUNC(RequestBossAttackSfx);
        ALICE_FUNC(RequestBossMovementSfx);
        ALICE_FUNC(RequestBossOtherSfx);
        ALICE_FUNC(RequestPlayerAttackSfx);
        ALICE_FUNC(RequestPlayerAttackSfxOneShot);
        ALICE_FUNC(RequestPlayerMovementSfx);
        ALICE_FUNC(RequestPlayerOtherSfx);
    };
}
