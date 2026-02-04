#include "AudioBGMScript.h"

#include "AudioEventBusScript.h"
#include "TempSoundPath.h"
#include "Runtime/Audio/SoundManager.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Foundation/Helper.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Scripting/ScriptFactory.h"

namespace Alice
{
    namespace
    {
        AudioEventBusScript* FindBus(World& world, const std::string& name)
        {
            if (name.empty())
                return nullptr;

            GameObject go = world.FindGameObject(name);
            if (!go.IsValid())
                return nullptr;

            auto* scripts = world.GetScripts(go.id());
            if (!scripts)
                return nullptr;

            for (auto& sc : *scripts)
            {
                if (sc.scriptName == "AudioEventBusScript" && sc.instance)
                    return static_cast<AudioEventBusScript*>(sc.instance.get());
            }

            return nullptr;
        }
    }

    REGISTER_SCRIPT(AudioBGMScript);

    void AudioBGMScript::Start()
    {
        if (Get_useBus())
        {
            if (auto* world = GetWorld())
            {
                if (auto* bus = FindBus(*world, Get_busEntityName()))
                {
                    bus->OnBgmRequest.BindObject(this, &AudioBGMScript::PlayBgmPath);
                    bus->OnBgmStopRequest.BindObject(this, &AudioBGMScript::StopBgm);
                }
                else
                {
                    ALICE_LOG_WARN("[AudioBGM] Bus not found: %s", Get_busEntityName().c_str());
                }
            }
        }

        if (Get_playOnStart() && !Get_bgmPath().empty())
        {
            PlayBgmPath(Get_bgmPath());
        }
    }

    void AudioBGMScript::Update(float)
    {
    }

    void AudioBGMScript::PlayBgmPath(const std::string& path)
    {
        if (path.empty())
            return;

        const std::string resolvedPath = TempSound::ResolveTempSoundPath(path, "BGM");
        auto* audio = Audio();
        if (!audio)
        {
            ALICE_LOG_WARN("[AudioBGM] Audio service not available.");
            return;
        }
        auto* resources = Resources();
        if (!resources)
        {
            ALICE_LOG_WARN("[AudioBGM] ResourceManager not available.");
            return;
        }

        const std::filesystem::path logicalPath = std::filesystem::u8path(resolvedPath);
        std::wstring key = WStringFromUtf8(resolvedPath);
        if (!audio->LoadAuto(*resources, key, logicalPath, Sound::Type::BGM))
        {
            ALICE_LOG_WARN("[AudioBGM] Load failed: %s", resolvedPath.c_str());
            return;
        }

        audio->SetBGMVolume(Get_volume());
        audio->PlayBGM(key);
        m_currentKey = key;
    }

    void AudioBGMScript::StopBgm()
    {
        if (auto* audio = Audio())
            audio->StopBGM();
    }
}
