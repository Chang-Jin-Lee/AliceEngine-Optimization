#pragma once

#include <string>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    class AudioBGMScript : public IScript
    {
        ALICE_BODY(AudioBGMScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        ALICE_PROPERTY(std::string, busEntityName, std::string("AudioBus"));
        ALICE_PROPERTY(bool, useBus, true);
        ALICE_PROPERTY(bool, playOnStart, true);
        ALICE_PROPERTY(std::string, bgmPath, std::string("Resource/Sound/CaliforniaGirls.wav"));
        ALICE_PROPERTY(float, volume, 0.5f);

        void PlayBgmPath(const std::string& path);
        void StopBgm();

        ALICE_FUNC(PlayBgmPath);
        ALICE_FUNC(StopBgm);

    private:
        std::wstring m_currentKey;
    };
}
