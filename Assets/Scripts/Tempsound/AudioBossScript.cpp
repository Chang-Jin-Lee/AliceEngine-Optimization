#include "AudioBossScript.h"

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

    REGISTER_SCRIPT(AudioBossScript);

    void AudioBossScript::Start()
    {
        if (Get_useBus())
        {
            if (auto* world = GetWorld())
            {
                if (auto* bus = FindBus(*world, Get_busEntityName()))
                {
                    bus->OnBossSfxRequest.BindObject(this, &AudioBossScript::PlaySfxPath);
                }
                else
                {
                    ALICE_LOG_WARN("[AudioBoss] Bus not found: %s", Get_busEntityName().c_str());
                }
            }
        }
    }

    void AudioBossScript::Update(float)
    {
        if (!Get_is3D() || !Get_loop() || !m_loopPlaying)
            return;

        auto* audio = Audio();
        if (!audio)
            return;

        DirectX::XMFLOAT3 pos{ 0.0f, 0.0f, 0.0f };
        if (auto* tr = GetTransform())
            pos = tr->position;

        audio->Update3D(m_loopInstanceId, pos, Get_volume(), Get_minDistance(), Get_maxDistance());
    }

    void AudioBossScript::PlaySfxPath(const std::string& path)
    {
        if (path.empty())
            return;

        const std::string resolvedPath = TempSound::ResolveTempSoundPath(path, "Boss");
        auto* audio = Audio();
        if (!audio)
        {
            ALICE_LOG_WARN("[AudioBoss] Audio service not available.");
            return;
        }
        auto* resources = Resources();
        if (!resources)
        {
            ALICE_LOG_WARN("[AudioBoss] ResourceManager not available.");
            return;
        }

        const std::filesystem::path logicalPath = std::filesystem::u8path(resolvedPath);
        std::wstring key = WStringFromUtf8(resolvedPath);
        if (!audio->LoadAuto(*resources, key, logicalPath, Sound::Type::SFX))
        {
            ALICE_LOG_WARN("[AudioBoss] Load failed: %s", resolvedPath.c_str());
            return;
        }

        const float volume = Get_volume();
        const float pitch = Get_pitch();

        if (Get_is3D())
        {
            DirectX::XMFLOAT3 pos{ 0.0f, 0.0f, 0.0f };
            if (auto* tr = GetTransform())
                pos = tr->position;

            if (Get_loop())
            {
                if (m_loopInstanceId.empty())
                {
                    m_loopInstanceId = L"BossLoop#" + std::to_wstring(static_cast<std::uint64_t>(GetOwnerId()));
                }

                audio->Play3D(m_loopInstanceId, key, pos, volume, pitch, true);
                m_loopKey = key;
                m_loopPlaying = true;
            }
            else
            {
                audio->Play3D(L"", key, pos, volume, pitch, false);
            }
        }
        else
        {
            audio->PlaySFX(key, volume, pitch, Get_loop());
            if (Get_loop())
            {
                m_loopKey = key;
                m_loopPlaying = true;
            }
        }
    }

    void AudioBossScript::StopLoop()
    {
        if (!m_loopPlaying)
            return;

        auto* audio = Audio();
        if (!audio)
            return;

        if (Get_is3D() && !m_loopInstanceId.empty())
        {
            audio->Stop3D(m_loopInstanceId);
        }
        else if (!m_loopKey.empty())
        {
            audio->StopSfx(m_loopKey);
        }

        m_loopPlaying = false;
    }

    void AudioBossScript::PlaySfx1() { PlaySfxPath(Get_sfxPath1()); }
    void AudioBossScript::PlaySfx2() { PlaySfxPath(Get_sfxPath2()); }
    void AudioBossScript::PlaySfx3() { PlaySfxPath(Get_sfxPath3()); }
    void AudioBossScript::PlaySfx4() { PlaySfxPath(Get_sfxPath4()); }
    void AudioBossScript::PlaySfx5() { PlaySfxPath(Get_sfxPath5()); }
}
