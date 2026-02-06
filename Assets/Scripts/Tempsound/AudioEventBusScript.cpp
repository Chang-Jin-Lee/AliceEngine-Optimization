#include "AudioEventBusScript.h"

#include "Runtime/Scripting/ScriptFactory.h"

namespace Alice
{
    REGISTER_SCRIPT(AudioEventBusScript);

    void AudioEventBusScript::Start()
    {
    }

    void AudioEventBusScript::Update(float)
    {
    }

    void AudioEventBusScript::RequestBgm(const std::string& path)
    {
        OnBgmRequest.Execute(path);
    }

    void AudioEventBusScript::RequestStopBgm()
    {
        OnBgmStopRequest.Execute();
    }

    void AudioEventBusScript::RequestBossSfx(const std::string& path)
    {
        OnBossSfxRequest.Execute(path);
    }

    void AudioEventBusScript::RequestPlayerSfx(const std::string& path)
    {
        OnPlayerSfxRequest.Execute(path);
    }

    void AudioEventBusScript::RequestBossAttackSfx(BossAttackState state)
    {
        OnBossAttackSfxRequest.Execute(state);
    }

    void AudioEventBusScript::RequestBossMovementSfx(BossMovementState state)
    {
        OnBossMovementSfxRequest.Execute(state);
    }

    void AudioEventBusScript::RequestBossOtherSfx(BossOtherState state)
    {
        OnBossOtherSfxRequest.Execute(state);
    }

    void AudioEventBusScript::RequestPlayerAttackSfx(PlayerAttackState state)
    {
        OnPlayerAttackSfxRequest.Execute(state);
    }

    void AudioEventBusScript::RequestPlayerAttackSfxOneShot(PlayerAttackState state)
    {
        OnPlayerAttackSfxOneShotRequest.Execute(state);
    }

    void AudioEventBusScript::RequestPlayerMovementSfx(PlayerMovementState state, bool playStopSfx)
    {
        OnPlayerMovementSfxRequest.Execute(state, playStopSfx);
    }

    void AudioEventBusScript::RequestPlayerOtherSfx(PlayerOtherState state)
    {
        OnPlayerOtherSfxRequest.Execute(state);
    }
}
