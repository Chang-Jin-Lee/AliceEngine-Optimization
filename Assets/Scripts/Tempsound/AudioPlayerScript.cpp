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

        if (Get_useBus() && GetWorld())
        {
            if (auto* bus = FindBus(*GetWorld(), Get_busEntityName()))
            {
                bus->OnPlayerSfxRequest.BindObject(this, &AudioPlayerScript::PlaySfxPath);
                bus->OnPlayerAttackSfxRequest.BindObject(this, &AudioPlayerScript::SetAttackState);
                bus->OnPlayerAttackSfxOneShotRequest.BindObject(this, &AudioPlayerScript::PlayAttackOneShot);
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

        audio->Update3D(m_loopInstanceId, pos, Get_volume(), Get_minDistance(), Get_maxDistance());
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
                    PlayPathInternal(path, false);
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
        case PlayerAttackState::HeavyAttack: return "Resource/Sound/SFX/플레이어/강공격/Player_HeavyAttack_01.mp3";
        case PlayerAttackState::Attack1:     return "Resource/Sound/SFX/플레이어/공격_1/Player_Attack_01.wav";
        case PlayerAttackState::Attack2:     return "Resource/Sound/SFX/플레이어/공격_2/Player_Attack_02.wav";
        case PlayerAttackState::Attack3:     return "Resource/Sound/SFX/플레이어/공격_3/Player_Attack_03.wav";
        case PlayerAttackState::Guard:
            return (std::rand() % 2 == 0)
                ? "Resource/Sound/SFX/플레이어/가드/Player_Guard_01.mp3"
                : "Resource/Sound/SFX/플레이어/가드/Player_Guard_02.wav";
        case PlayerAttackState::Parry:
        {
            static const char* parryPaths[] = {
                "Resource/Sound/SFX/플레이어/패링/Player_Parry_01.wav",
                "Resource/Sound/SFX/플레이어/패링/Player_Parry_02.wav",
                "Resource/Sound/SFX/플레이어/패링/Player_Parry_03.wav",
                "Resource/Sound/SFX/플레이어/패링/Player_Parry_04.wav",
                "Resource/Sound/SFX/플레이어/패링/Player_Parry_05.wav",
            };
            return parryPaths[std::rand() % 5];
        }
        default:
            return "";
        }
    }

    std::string AudioPlayerScript::GetPathForMovementState(PlayerMovementState state) const
    {
        switch (state)
        {
        case PlayerMovementState::Roll:
            return "Resource/Sound/SFX/플레이어/구르기/Player_Rolling_01.mp3";
        case PlayerMovementState::Run:
        {
            char buf[128];
            snprintf(buf, sizeof(buf), "Resource/Sound/SFX/플레이어/달리기/Player_Footstep_%d.wav", (m_footstepIndex % 4) + 1);
            return std::string(buf);
        }
        case PlayerMovementState::Dash:
            return "Resource/Sound/SFX/플레이어/대시/Player_Dash_01.mp3";
        case PlayerMovementState::Stop:
            return "Resource/Sound/SFX/플레이어/멈추기/Player_Stop_1.wav";
        case PlayerMovementState::HitRoll:
            return "Resource/Sound/SFX/플레이어/피격_후_구르기/Player_Attacked_Rolling.mp3";
        default:
            return "";
        }
    }

    std::string AudioPlayerScript::GetPathForOtherState(PlayerOtherState state) const
    {
        switch (state)
        {
        case PlayerOtherState::GuardBreakAlarm: return "Resource/Sound/SFX/플레이어/가드_브레이크_전조음/Player_GuardBreak_Alarm_01.wav";
        case PlayerOtherState::GuardBreak:      return "Resource/Sound/SFX/플레이어/가드_브레이크/Player_GuardBreak_01.mp3";
        case PlayerOtherState::EgoCombine:      return "Resource/Sound/SFX/플레이어/에고웨폰_재결합/Player_Weapon_Gather_01.wav";
        case PlayerOtherState::Heal:            return "Resource/Sound/SFX/플레이어/회복/Player_Healing_01.wav";
        default:
            return "";
        }
    }

    void AudioPlayerScript::PlayPathInternal(const std::string& path, bool isLooping)
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

        const float volume = Get_volume();
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

    void AudioPlayerScript::PlayAttackState(PlayerAttackState state)
    {
        if (state == PlayerAttackState::None)
            return;
        std::string path = GetPathForAttackState(state);
        if (!path.empty())
            PlayPathInternal(path, false);
    }

    void AudioPlayerScript::PlayMovementState(PlayerMovementState state)
    {
        if (state == PlayerMovementState::None)
            return;
        std::string path = GetPathForMovementState(state);
        if (state == PlayerMovementState::Run)
            m_footstepIndex++;
        if (!path.empty())
            PlayPathInternal(path, state == PlayerMovementState::Run);
    }

    void AudioPlayerScript::PlayOtherState(PlayerOtherState state)
    {
        if (state == PlayerOtherState::None)
            return;
        std::string path = GetPathForOtherState(state);
        if (!path.empty())
            PlayPathInternal(path, false);
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

    void AudioPlayerScript::PlaySfx1() { PlaySfxPath(GetPathForAttackState(PlayerAttackState::Attack1)); }
    void AudioPlayerScript::PlaySfx2() { PlaySfxPath(GetPathForAttackState(PlayerAttackState::Attack2)); }
    void AudioPlayerScript::PlaySfx3() { PlaySfxPath(GetPathForAttackState(PlayerAttackState::Attack3)); }
    void AudioPlayerScript::PlaySfx4() { PlaySfxPath(GetPathForAttackState(PlayerAttackState::Guard)); }
    void AudioPlayerScript::PlaySfx5() { PlaySfxPath(GetPathForMovementState(PlayerMovementState::Roll)); }
}
