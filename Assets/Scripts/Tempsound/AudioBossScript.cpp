#include "AudioBossScript.h"

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

    REGISTER_SCRIPT(AudioBossScript);

    void AudioBossScript::Start()
    {
        m_currentAttack = BossAttackState::None;
        m_currentMovement = BossMovementState::None;
        m_currentOther = BossOtherState::None;
        m_footstepIndex = 0;

        if (Get_useBus() && GetWorld())
        {
            if (auto* bus = FindBus(*GetWorld(), Get_busEntityName()))
            {
                bus->OnBossSfxRequest.BindObject(this, &AudioBossScript::PlaySfxPath);
                bus->OnBossAttackSfxRequest.BindObject(this, &AudioBossScript::PlayAttackState);
                bus->OnBossMovementSfxRequest.BindObject(this, &AudioBossScript::PlayMovementState);
                bus->OnBossOtherSfxRequest.BindObject(this, &AudioBossScript::PlayOtherState);
            }
            else
            {
                ALICE_LOG_WARN("[AudioBoss] Bus not found: %s", Get_busEntityName().c_str());
            }
        }
    }

    void AudioBossScript::Update(float)
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

    void AudioBossScript::SetAttackState(BossAttackState state)
    {
        if (state == m_currentAttack)
            return;
        m_currentAttack = state;
        PlayAttackState(state);
    }

    void AudioBossScript::SetMovementState(BossMovementState state)
    {
        if (state == m_currentMovement)
            return;
        m_currentMovement = state;
        PlayMovementState(state);
    }

    void AudioBossScript::SetOtherState(BossOtherState state)
    {
        if (state == m_currentOther)
            return;
        m_currentOther = state;
        PlayOtherState(state);
    }

    std::string AudioBossScript::GetPathForAttackState(BossAttackState state) const
    {
        switch (state)
        {
        case BossAttackState::AttackAlarm:     return "Resource/Sound/SFX/보스/공격_전조_알림/Boss_Attack_Alarm_01.mp3";
        case BossAttackState::Attack1:        return "Resource/Sound/SFX/보스/공격_1/Boss_Attack_01.mp3";
        case BossAttackState::Attack2:        return "Resource/Sound/SFX/보스/공격_2/Boss_Attack_02.wav";
        case BossAttackState::Attack3:        return "Resource/Sound/SFX/보스/공격_3/Boss_Attack_03.wav";
        case BossAttackState::SoulSwordCharge: return "Resource/Sound/SFX/보스/영혼대검_차지/Boss_SoulAttack_Charging_01.mp3";
        case BossAttackState::SoulSwordAttack: return "Resource/Sound/SFX/보스/영혼대검_공격/Boss_SoulAttack_Attack_01.wav";
        case BossAttackState::SideAttack:      return "Resource/Sound/SFX/보스/옆,견제_공격/Boss_Attack_Side_01.mp3";
        case BossAttackState::DashAttack:      return "Resource/Sound/SFX/보스/대쉬공격/Boss_DashAttack_01.mp3";
        default:
            return "";
        }
    }

    std::string AudioBossScript::GetPathForMovementState(BossMovementState state) const
    {
        switch (state)
        {
        case BossMovementState::DashAttack:
            return "Resource/Sound/SFX/보스/대쉬공격/Boss_DashAttack_01.mp3";
        case BossMovementState::Walk:
        {
            char buf[128];
            snprintf(buf, sizeof(buf), "Resource/Sound/SFX/보스/걷기/Boss_Footstep_%02d.mp3", (m_footstepIndex % 4) + 1);
            return std::string(buf);
        }
        case BossMovementState::Rotate:
            return "Resource/Sound/SFX/보스/몸_돌리기/Boss_Rotate_01.wav";
        default:
            return "";
        }
    }

    std::string AudioBossScript::GetPathForOtherState(BossOtherState state) const
    {
        switch (state)
        {
        case BossOtherState::GroggyEnter: return "Resource/Sound/SFX/보스/그로기_진입/Boss_Groggy_Alarm_01.wav";
        case BossOtherState::Roar:        return "Resource/Sound/SFX/보스/포효/Boss_Roaring_01.mp3";
        case BossOtherState::Hit:
            return (std::rand() % 2 == 0)
                ? "Resource/Sound/SFX/보스/피격/Boss_Hit_01.wav"
                : "Resource/Sound/SFX/보스/피격/Boss_Hit_02.wav";
        default:
            return "";
        }
    }

    void AudioBossScript::PlayPathInternal(const std::string& path, bool isLooping)
    {
        if (path.empty())
            return;

        if (m_loopPlaying && !isLooping)
            StopLoop();

        const std::string resolvedPath = TempSound::ResolveTempSoundPath(path, "Boss");
        auto* audio = Audio();
        auto* resources = Resources();
        if (!audio || !resources)
            return;

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

            if (isLooping)
            {
                if (m_loopInstanceId.empty())
                    m_loopInstanceId = L"BossLoop#" + std::to_wstring(static_cast<std::uint64_t>(GetOwnerId()));
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

    void AudioBossScript::PlayAttackState(BossAttackState state)
    {
        if (state == BossAttackState::None)
            return;
        std::string path = GetPathForAttackState(state);
        if (!path.empty())
            PlayPathInternal(path, false);
    }

    void AudioBossScript::PlayMovementState(BossMovementState state)
    {
        if (state == BossMovementState::None)
            return;
        std::string path = GetPathForMovementState(state);
        if (state == BossMovementState::Walk)
            m_footstepIndex++;
        if (!path.empty())
            PlayPathInternal(path, state == BossMovementState::Walk);
    }

    void AudioBossScript::PlayOtherState(BossOtherState state)
    {
        if (state == BossOtherState::None)
            return;
        std::string path = GetPathForOtherState(state);
        if (!path.empty())
            PlayPathInternal(path, false);
    }

    void AudioBossScript::PlaySfxPath(const std::string& path)
    {
        if (path.empty())
            return;

        const std::string resolvedPath = TempSound::ResolveTempSoundPath(path, "Boss");
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
            ALICE_LOG_WARN("[AudioBoss] Load failed: %s", resolvedPath.c_str());
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

    void AudioBossScript::StopLoop()
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

    void AudioBossScript::PlaySfx1() { PlaySfxPath(GetPathForAttackState(BossAttackState::Attack1)); }
    void AudioBossScript::PlaySfx2() { PlaySfxPath(GetPathForAttackState(BossAttackState::Attack2)); }
    void AudioBossScript::PlaySfx3() { PlaySfxPath(GetPathForAttackState(BossAttackState::Attack3)); }
    void AudioBossScript::PlaySfx4() { PlaySfxPath(GetPathForOtherState(BossOtherState::Roar)); }
    void AudioBossScript::PlaySfx5() { PlaySfxPath(GetPathForOtherState(BossOtherState::Hit)); }
}
