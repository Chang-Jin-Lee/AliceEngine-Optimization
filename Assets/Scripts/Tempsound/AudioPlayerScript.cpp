#include "AudioPlayerScript.h"

#include "AudioEventBusScript.h"
#include "TempSoundPath.h"
#include "Runtime/Audio/SoundManager.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Foundation/Helper.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include <cstdlib>

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

    REGISTER_SCRIPT(AudioPlayerScript);

    void AudioPlayerScript::Start()
    {
        m_currentAttack = PlayerAttackState::None;
        m_currentMovement = PlayerMovementState::None;
        m_currentOther = PlayerOtherState::None;
        m_footstepIndex = 0;

        // 媛???⑤쭅 ?ъ슫???꾨━濡쒕뱶 (吏??諛⑹?)
        PreloadSound(Get_pathGuard());
        PreloadSound(Get_pathParry());

        if (Get_useBus() && GetWorld())
        {
            if (auto* bus = FindBus(*GetWorld(), Get_busEntityName()))
            {
                bus->OnPlayerSfxRequest.BindObject(this, &AudioPlayerScript::PlaySfxPath);
                bus->OnPlayerAttackSfxRequest.BindObject(this, &AudioPlayerScript::SetAttackState);
                bus->OnPlayerAttackSfxOneShotRequest.BindObject(this, &AudioPlayerScript::PlayAttackOneShot);
                bus->OnPlayerAttackSfxOneShotAtPositionRequest.BindObject(this, &AudioPlayerScript::PlayAttackOneShotAtPosition);
                bus->OnPlayerMovementSfxRequest.BindObject(this, &AudioPlayerScript::SetMovementState);
                bus->OnPlayerOtherSfxRequest.BindObject(this, &AudioPlayerScript::PlayOtherState);
            }
            else
            {
                ALICE_LOG_WARN("[AudioPlayer] Bus not found: %s", Get_busEntityName().c_str());
            }
        }
    }

    void AudioPlayerScript::Update(float)
    {
        if (!Get_is3D() || !m_loopPlaying)
            return;

        auto* audio = Audio();
        if (!audio)
            return;

        DirectX::XMFLOAT3 pos{ 0.0f, 0.0f, 0.0f };
        if (auto* tr = GetTransform())
            pos = tr->position;

        float volume = Get_volume();
        if (m_currentMovement != PlayerMovementState::None)
            volume *= GetMovementStateVolume(m_currentMovement);
        audio->Update3D(m_loopInstanceId, pos, volume, Get_minDistance(), Get_maxDistance());
    }

    void AudioPlayerScript::SetAttackState(PlayerAttackState state)
    {
        if (state == m_currentAttack)
            return;
        m_currentAttack = state;
        PlayAttackState(state);
    }

    void AudioPlayerScript::PlayAttackOneShot(PlayerAttackState state)
    {
        PlayAttackState(state);
    }

    void AudioPlayerScript::PlayAttackOneShotAtPosition(PlayerAttackState state, const DirectX::XMFLOAT3& position)
    {
        if (state == PlayerAttackState::None)
            return;
        std::string path = GetPathForAttackState(state);
        if (path.empty())
            return;

        const std::string resolvedPath = TempSound::ResolveTempSoundPath(path, "Player");
        auto* audio = Audio();
        if (!audio)
            return;
        auto* resources = Resources();
        if (!resources)
            return;

        const std::filesystem::path logicalPath = std::filesystem::u8path(resolvedPath);
        std::wstring key = WStringFromUtf8(resolvedPath);
        if (!audio->LoadAuto(*resources, key, logicalPath, Sound::Type::SFX))
        {
            ALICE_LOG_WARN("[AudioPlayer] Load failed: %s", resolvedPath.c_str());
            return;
        }

        const float volume = Get_volume() * GetAttackStateVolume(state);
        const float pitch = Get_pitch();
        
        // ?덊듃 ?꾩튂?먯꽌 3D ?ъ깮
        audio->Play3D(L"", key, position, volume, pitch, false);
    }

    void AudioPlayerScript::SetMovementState(PlayerMovementState state, bool playStopSfx)
    {
        if (state == PlayerMovementState::Stop)
        {
            StopLoop();
            m_currentMovement = PlayerMovementState::Stop;
            m_currentAttack = PlayerAttackState::None;
            if (playStopSfx)
            {
                std::string path = GetPathForMovementState(PlayerMovementState::Stop);
                if (!path.empty())
                    PlayPathInternal(path, false, GetMovementStateVolume(PlayerMovementState::Stop));
            }
            return;
        }
        if (state == m_currentMovement)
            return;
        m_currentMovement = state;
        if (state == PlayerMovementState::Run)
            m_currentAttack = PlayerAttackState::None;
        PlayMovementState(state);
    }

    void AudioPlayerScript::SetOtherState(PlayerOtherState state)
    {
        if (state == m_currentOther)
            return;
        m_currentOther = state;
        PlayOtherState(state);
    }

    std::string AudioPlayerScript::GetPathForAttackState(PlayerAttackState state) const
    {
        switch (state)
        {
        case PlayerAttackState::HeavyAttack: return Get_pathHeavyAttack();
        case PlayerAttackState::Attack1:     return Get_pathAttack1();
        case PlayerAttackState::Attack2:     return Get_pathAttack2();
        case PlayerAttackState::Attack3:     return Get_pathAttack3();
        case PlayerAttackState::Guard:
            return Get_pathGuard();
        case PlayerAttackState::Parry:
            return Get_pathParry();
        default:
            return "";
        }
    }

    std::string AudioPlayerScript::GetPathForMovementState(PlayerMovementState state) const
    {
        switch (state)
        {
        case PlayerMovementState::Roll:
            return Get_pathRoll();
        case PlayerMovementState::Run:
        {
            const std::string basePath = Get_pathRun();
            if (basePath.empty())
                return "";
            return basePath;
        }
        case PlayerMovementState::Dash:
            return Get_pathDash();
        case PlayerMovementState::Stop:
            return Get_pathStop();
        case PlayerMovementState::HitRoll:
            return Get_pathHitRoll();
        default:
            return "";
        }
    }

    std::string AudioPlayerScript::GetPathForOtherState(PlayerOtherState state) const
    {
        switch (state)
        {
        case PlayerOtherState::GuardBreakAlarm: return Get_pathGuardBreakAlarm();
        case PlayerOtherState::GuardBreak:      return Get_pathGuardBreak();
        case PlayerOtherState::EgoCombine:      return Get_pathEgoCombine();
        case PlayerOtherState::Heal:            return Get_pathHeal();
        case PlayerOtherState::Death:           return Get_pathDeath();
        default:
            return "";
        }
    }

    float AudioPlayerScript::GetAttackStateVolume(PlayerAttackState state) const
    {
        switch (state)
        {
        case PlayerAttackState::HeavyAttack: return Get_volumeHeavyAttack();
        case PlayerAttackState::Attack1:     return Get_volumeAttack1();
        case PlayerAttackState::Attack2:     return Get_volumeAttack2();
        case PlayerAttackState::Attack3:     return Get_volumeAttack3();
        case PlayerAttackState::Guard:       return Get_volumeGuard();
        case PlayerAttackState::Parry:       return Get_volumeParry();
        default:
            return 1.0f;
        }
    }

    float AudioPlayerScript::GetMovementStateVolume(PlayerMovementState state) const
    {
        switch (state)
        {
        case PlayerMovementState::Roll:    return Get_volumeRoll();
        case PlayerMovementState::Run:     return Get_volumeRun();
        case PlayerMovementState::Dash:    return Get_volumeDash();
        case PlayerMovementState::Stop:    return Get_volumeStop();
        case PlayerMovementState::HitRoll: return Get_volumeHitRoll();
        default:
            return 1.0f;
        }
    }

    float AudioPlayerScript::GetOtherStateVolume(PlayerOtherState state) const
    {
        switch (state)
        {
        case PlayerOtherState::GuardBreakAlarm: return Get_volumeGuardBreakAlarm();
        case PlayerOtherState::GuardBreak:      return Get_volumeGuardBreak();
        case PlayerOtherState::EgoCombine:      return Get_volumeEgoCombine();
        case PlayerOtherState::Heal:            return Get_volumeHeal();
        case PlayerOtherState::Death:           return Get_volumeDeath();
        default:
            return 1.0f;
        }
    }

    void AudioPlayerScript::PlayPathInternal(const std::string& path, bool isLooping, float volumeMul)
    {
        if (path.empty())
            return;

        if (m_loopPlaying && !isLooping)
            StopLoop();

        const std::string resolvedPath = TempSound::ResolveTempSoundPath(path, "Player");
        auto* audio = Audio();
        auto* resources = Resources();
        if (!audio || !resources)
            return;

        const std::filesystem::path logicalPath = std::filesystem::u8path(resolvedPath);
        std::wstring key = WStringFromUtf8(resolvedPath);
        if (!audio->LoadAuto(*resources, key, logicalPath, Sound::Type::SFX))
        {
            ALICE_LOG_WARN("[AudioPlayer] Load failed: %s", resolvedPath.c_str());
            return;
        }

        const float volume = Get_volume() * volumeMul;
        const float pitch = Get_pitch();

        if (Get_is3D())
        {
            DirectX::XMFLOAT3 pos{ 0.0f, 0.0f, 0.0f };
            if (auto* tr = GetTransform())
                pos = tr->position;

            if (isLooping)
            {
                if (m_loopInstanceId.empty())
                    m_loopInstanceId = L"PlayerLoop#" + std::to_wstring(static_cast<std::uint64_t>(GetOwnerId()));
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
            audio->PlaySFX(key, volume, pitch, isLooping);
            if (isLooping)
            {
                m_loopKey = key;
                m_loopPlaying = true;
            }
        }
    }

    void AudioPlayerScript::PlayPathInternal(const std::string& path, bool isLooping)
    {
        PlayPathInternal(path, isLooping, 1.0f);
    }

    void AudioPlayerScript::PlayAttackState(PlayerAttackState state)
    {
        if (state == PlayerAttackState::None)
            return;
        std::string path = GetPathForAttackState(state);
        if (!path.empty())
            PlayPathInternal(path, false, GetAttackStateVolume(state));
    }

    void AudioPlayerScript::PlayMovementState(PlayerMovementState state)
    {
        if (state == PlayerMovementState::None)
            return;
        std::string path = GetPathForMovementState(state);
        if (!path.empty())
            PlayPathInternal(path, state == PlayerMovementState::Run, GetMovementStateVolume(state));
    }

    void AudioPlayerScript::PlayOtherState(PlayerOtherState state)
    {
        if (state == PlayerOtherState::None)
            return;
        std::string path = GetPathForOtherState(state);
        if (!path.empty())
            PlayPathInternal(path, false, GetOtherStateVolume(state));
    }

    void AudioPlayerScript::PlaySfxPath(const std::string& path)
    {
        if (path.empty())
            return;

        const std::string resolvedPath = TempSound::ResolveTempSoundPath(path, "Player");
        auto* audio = Audio();
        if (!audio)
            return;
        auto* resources = Resources();
        if (!resources)
            return;

        const std::filesystem::path logicalPath = std::filesystem::u8path(resolvedPath);
        std::wstring key = WStringFromUtf8(resolvedPath);
        if (!audio->LoadAuto(*resources, key, logicalPath, Sound::Type::SFX))
        {
            ALICE_LOG_WARN("[AudioPlayer] Load failed: %s", resolvedPath.c_str());
            return;
        }

        const float volume = Get_volume();
        const float pitch = Get_pitch();
        DirectX::XMFLOAT3 pos{ 0.0f, 0.0f, 0.0f };
        if (auto* tr = GetTransform())
            pos = tr->position;

        if (Get_is3D())
            audio->Play3D(L"", key, pos, volume, pitch, false);
        else
            audio->PlaySFX(key, volume, pitch, false);
    }

    void AudioPlayerScript::StopLoop()
    {
        if (!m_loopPlaying)
            return;

        auto* audio = Audio();
        if (!audio)
            return;

        if (Get_is3D() && !m_loopInstanceId.empty())
            audio->Stop3D(m_loopInstanceId);
        else if (!m_loopKey.empty())
            audio->StopSfx(m_loopKey);

        m_loopPlaying = false;
    }

    void AudioPlayerScript::PlaySfx1()
    {
        std::string path = GetPathForAttackState(PlayerAttackState::Attack1);
        if (!path.empty())
            PlayPathInternal(path, false, GetAttackStateVolume(PlayerAttackState::Attack1));
    }
    void AudioPlayerScript::PlaySfx2()
    {
        std::string path = GetPathForAttackState(PlayerAttackState::Attack2);
        if (!path.empty())
            PlayPathInternal(path, false, GetAttackStateVolume(PlayerAttackState::Attack2));
    }
    void AudioPlayerScript::PlaySfx3()
    {
        std::string path = GetPathForAttackState(PlayerAttackState::Attack3);
        if (!path.empty())
            PlayPathInternal(path, false, GetAttackStateVolume(PlayerAttackState::Attack3));
    }
    void AudioPlayerScript::PlaySfx4()
    {
        std::string path = GetPathForAttackState(PlayerAttackState::Guard);
        if (!path.empty())
            PlayPathInternal(path, false, GetAttackStateVolume(PlayerAttackState::Guard));
    }
    void AudioPlayerScript::PlaySfx5()
    {
        std::string path = GetPathForMovementState(PlayerMovementState::Roll);
        if (!path.empty())
            PlayPathInternal(path, false, GetMovementStateVolume(PlayerMovementState::Roll));
    }

    void AudioPlayerScript::PreloadSound(const std::string& path)
    {
        if (path.empty())
            return;

        const std::string resolvedPath = TempSound::ResolveTempSoundPath(path, "Player");
        auto* audio = Audio();
        auto* resources = Resources();
        if (!audio || !resources)
        {
            ALICE_LOG_WARN("[AudioPlayer] PreloadSound: Audio or Resources is null");
            return;
        }

        const std::filesystem::path logicalPath = std::filesystem::u8path(resolvedPath);
        std::wstring key = WStringFromUtf8(resolvedPath);
        
        if (audio->LoadAuto(*resources, key, logicalPath, Sound::Type::SFX))
        {
            ALICE_LOG_INFO("[AudioPlayer] Preloaded: %s", resolvedPath.c_str());
        }
        else
        {
            ALICE_LOG_WARN("[AudioPlayer] PreloadSound failed: %s", resolvedPath.c_str());
        }
    }
}

