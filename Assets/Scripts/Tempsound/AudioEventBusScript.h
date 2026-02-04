#pragma once

#include <string>

#include "Runtime/Foundation/Delegate.h"
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    // Single-cast event bus for audio routing.
    ALICE_DECLARE_DELEGATE_OneParam(FOnBgmRequest, const std::string&);
    ALICE_DECLARE_DELEGATE(FOnBgmStopRequest);
    ALICE_DECLARE_DELEGATE_OneParam(FOnBossSfxRequest, const std::string&);
    ALICE_DECLARE_DELEGATE_OneParam(FOnPlayerSfxRequest, const std::string&);

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

        void RequestBgm(const std::string& path);
        void RequestStopBgm();
        void RequestBossSfx(const std::string& path);
        void RequestPlayerSfx(const std::string& path);

        ALICE_FUNC(RequestBgm);
        ALICE_FUNC(RequestStopBgm);
        ALICE_FUNC(RequestBossSfx);
        ALICE_FUNC(RequestPlayerSfx);
    };
}
