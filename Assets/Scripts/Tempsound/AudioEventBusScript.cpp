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
}
