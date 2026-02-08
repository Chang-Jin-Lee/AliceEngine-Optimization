#pragma once

#include <string>
#include <unordered_map>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    // 간단한 예제 스크립트입니다. 필요에 맞게 수정해서 사용하세요.
    class UISoundScript : public IScript
    {
        ALICE_BODY(UISoundScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        /// 키로 재생 (예: "UI_Button_Click", "UI_Button_Hover"). 키에 해당하는 경로가 등록되어 있어야 함.
        void Play(const std::string& key);

        /// 클릭/호버용 단축 API (clickSoundPath / hoverSoundPath 사용)
        void PlayClick();
        void PlayHover();

        ALICE_PROPERTY(std::string, clickSoundPath, "Resource/Sound/SFX/그외 SFX/클릭.wav");
        ALICE_PROPERTY(std::string, hoverSoundPath, "Resource/Sound/SFX/그외 SFX/호버.wav");
        ALICE_PROPERTY(float, volume, 1.0f);
        ALICE_PROPERTY(float, cooldownSec, 0.08f);
        ALICE_PROPERTY(bool, pitchRandomEnabled, true);
        ALICE_PROPERTY(float, pitchRandomMin, 0.95f);
        ALICE_PROPERTY(float, pitchRandomMax, 1.05f);

        ALICE_FUNC(Play);
        ALICE_FUNC(PlayClick);
        ALICE_FUNC(PlayHover);

    private:
        bool EnsureLoaded(const std::string& key, const std::string& path);
        void PlayInternal(const std::wstring& soundKey, float volumeMul, float pitch);

        std::unordered_map<std::string, std::string> m_keyToPath;
        std::unordered_map<std::string, bool> m_loadedKeys;
        std::unordered_map<std::string, float> m_lastPlayTime;
        float m_accumTime = 0.0f;
    };
}
