#include "UISoundScript.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Foundation/Helper.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Audio/SoundManager.h"
#include <algorithm>
#include <cstdlib>

namespace Alice
{
    REGISTER_SCRIPT(UISoundScript);

    void UISoundScript::Start()
    {
        m_loadedKeys.clear();
        m_lastPlayTime.clear();
        m_accumTime = 0.0f;

        if (!Get_clickSoundPath().empty())
        {
            m_keyToPath["UI_Button_Click"] = Get_clickSoundPath();
            m_keyToPath["click"] = Get_clickSoundPath();
        }
        if (!Get_hoverSoundPath().empty())
        {
            m_keyToPath["UI_Button_Hover"] = Get_hoverSoundPath();
            m_keyToPath["hover"] = Get_hoverSoundPath();
        }
    }

    void UISoundScript::Update(float deltaTime)
    {
        m_accumTime += deltaTime;
    }

    bool UISoundScript::EnsureLoaded(const std::string& key, const std::string& path)
    {
        if (path.empty())
            return false;

        auto it = m_loadedKeys.find(key);
        if (it != m_loadedKeys.end() && it->second)
            return true;

        auto* audio = Audio();
        auto* resources = Resources();
        if (!audio || !resources)
            return false;

        const std::wstring wkey = WStringFromUtf8(key);
        const std::filesystem::path logicalPath = std::filesystem::u8path(path);
        if (!audio->LoadAuto(*resources, wkey, logicalPath, Sound::Type::SFX))
        {
            ALICE_LOG_WARN("[UISound] Load failed: key=%s path=%s", key.c_str(), path.c_str());
            return false;
        }

        m_loadedKeys[key] = true;
        return true;
    }

    void UISoundScript::PlayInternal(const std::wstring& soundKey, float volumeMul, float pitch)
    {
        auto* audio = Audio();
        if (!audio)
            return;

        float vol = std::clamp(Get_volume() * volumeMul, 0.0f, 1.0f);
        float p = std::clamp(pitch, 0.5f, 2.0f);
        audio->PlaySFX(soundKey, vol, p, false);
    }

    void UISoundScript::Play(const std::string& key)
    {
        std::string canonicalKey = key;
        if (key == "click") canonicalKey = "UI_Button_Click";
        else if (key == "hover") canonicalKey = "UI_Button_Hover";

        std::string path;
        auto it = m_keyToPath.find(canonicalKey);
        if (it != m_keyToPath.end())
            path = it->second;
        else
        {
            ALICE_LOG_WARN("[UISound] Unknown key: %s (use UI_Button_Click / UI_Button_Hover or click / hover)", key.c_str());
            return;
        }

        if (!EnsureLoaded(canonicalKey, path))
            return;

        float now = m_accumTime;
        auto lastIt = m_lastPlayTime.find(canonicalKey);
        if (lastIt != m_lastPlayTime.end())
        {
            const float cooldown = std::max(0.0f, Get_cooldownSec());
            if (cooldown > 0.0f && (now - lastIt->second) < cooldown)
                return;
        }
        m_lastPlayTime[canonicalKey] = now;

        float pitch = 1.0f;
        if (Get_pitchRandomEnabled())
        {
            const float mn = std::min(Get_pitchRandomMin(), Get_pitchRandomMax());
            const float mx = std::max(Get_pitchRandomMin(), Get_pitchRandomMax());
            const float t = (std::rand() / static_cast<float>(RAND_MAX));
            pitch = mn + t * (mx - mn);
        }

        const std::wstring wkey = WStringFromUtf8(canonicalKey);
        PlayInternal(wkey, 1.0f, pitch);
    }

    void UISoundScript::PlayClick()
    {
        Play("UI_Button_Click");
    }

    void UISoundScript::PlayHover()
    {
        Play("UI_Button_Hover");
    }
}
