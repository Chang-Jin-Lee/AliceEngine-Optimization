#pragma once

#include <string>

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
        ALICE_PROPERTY(bool, loop, false);
        ALICE_PROPERTY(float, volume, 1.0f);
        ALICE_PROPERTY(float, pitch, 1.0f);
        ALICE_PROPERTY(float, minDistance, 1.0f);
        ALICE_PROPERTY(float, maxDistance, 50.0f);

        ALICE_PROPERTY(std::string, sfxPath1, std::string("Resource/Sound/Attack.mp3"));
        ALICE_PROPERTY(std::string, sfxPath2, std::string("Resource/Sound/Jump.mp3"));
        ALICE_PROPERTY(std::string, sfxPath3, std::string("Resource/Sound/Hit.wav"));
        ALICE_PROPERTY(std::string, sfxPath4, std::string("Resource/Sound/Coin.wav"));
        ALICE_PROPERTY(std::string, sfxPath5, std::string("Resource/Sound/Explosion.wav"));

        void PlaySfxPath(const std::string& path);
        void StopLoop();

        void PlaySfx1();
        void PlaySfx2();
        void PlaySfx3();
        void PlaySfx4();
        void PlaySfx5();

        ALICE_FUNC(PlaySfxPath);
        ALICE_FUNC(StopLoop);
        ALICE_FUNC(PlaySfx1);
        ALICE_FUNC(PlaySfx2);
        ALICE_FUNC(PlaySfx3);
        ALICE_FUNC(PlaySfx4);
        ALICE_FUNC(PlaySfx5);

    private:
        std::wstring m_loopKey;
        std::wstring m_loopInstanceId;
        bool m_loopPlaying = false;
    };
}
