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
        m_delayedSoundQueue.clear();

        // 디버그: pathGroggyEnter 값 확인
        std::string groggyPath = Get_pathGroggyEnter();
        ALICE_LOG_INFO("[AudioBoss] Start: pathGroggyEnter='%s' (length=%zu)", 
            groggyPath.c_str(), groggyPath.length());

        if (Get_useBus() && GetWorld())
        {
            if (auto* bus = FindBus(*GetWorld(), Get_busEntityName()))
            {
                bus->OnBossSfxRequest.BindObject(this, &AudioBossScript::PlaySfxPath);
                bus->OnBossAttackSfxRequest.BindObject(this, &AudioBossScript::PlayAttackState);
                bus->OnBossAttackSfxDelayedRequest.BindObject(this, &AudioBossScript::PlayAttackStateDelayed);
                bus->OnBossMovementSfxRequest.BindObject(this, &AudioBossScript::PlayMovementState);
                bus->OnBossOtherSfxRequest.BindObject(this, &AudioBossScript::PlayOtherState);
            }
            else
            {
                ALICE_LOG_WARN("[AudioBoss] Bus not found: %s", Get_busEntityName().c_str());
            }
        }
    }

    void AudioBossScript::Update(float deltaTime)
    {
        // 기존 3D 사운드 업데이트
        if (Get_is3D() && m_loopPlaying)
        {
            auto* audio = Audio();
            if (audio)
            {
                DirectX::XMFLOAT3 pos{ 0.0f, 0.0f, 0.0f };
                if (auto* tr = GetTransform())
                    pos = tr->position;
                audio->Update3D(m_loopInstanceId, pos, Get_volume(), Get_minDistance(), Get_maxDistance());
            }
        }
        
        // 딜레이된 사운드 처리 (C++ deltaTime 기반)
        if (!m_delayedSoundQueue.empty())
        {
            auto* audio = Audio();
            if (!audio)
            {
                // Audio가 없으면 큐를 비움
                m_delayedSoundQueue.clear();
                return;
            }
            
            for (auto it = m_delayedSoundQueue.begin(); it != m_delayedSoundQueue.end();)
            {
                it->remainingDelay -= deltaTime;
                
                if (it->remainingDelay <= 0.0f)
                {
                    // 딜레이 시간이 지났으므로 재생
                    ALICE_LOG_INFO("[AudioBoss] Delayed sound ready: state=%d, key=%ls, is3D=%d", 
                        static_cast<int>(it->state), it->key.c_str(), Get_is3D() ? 1 : 0);
                    
                    // PlayPathInternal과 동일한 로직: 3D/2D 분기 처리
                    if (Get_is3D())
                    {
                        DirectX::XMFLOAT3 pos{ 0.0f, 0.0f, 0.0f };
                        if (auto* tr = GetTransform())
                            pos = tr->position;
                        audio->Play3D(L"", it->key, pos, it->volume, it->pitch, false);
                    }
                    else
                    {
                        audio->PlaySFX(it->key, it->volume, it->pitch, false);
                    }
                    
                    it = m_delayedSoundQueue.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }
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
        case BossAttackState::AttackAlarm:     return Get_pathAttackAlarm();
        case BossAttackState::Attack1:         return Get_pathAttack1();
        case BossAttackState::Attack2:         return Get_pathAttack2();
        case BossAttackState::Attack3:         return Get_pathAttack3();
        case BossAttackState::AttackABC:       return Get_pathAttackABC();
        case BossAttackState::SoulSwordCharge: return Get_pathSoulSwordCharge();
        case BossAttackState::SoulSwordAttack: return Get_pathSoulSwordAttack();
        case BossAttackState::SideAttack:      return Get_pathSideAttack();
        case BossAttackState::DashAttack:      return Get_pathDashAttack();
        default:
            return "";
        }
    }

    float AudioBossScript::GetDelayForAttackState(BossAttackState state) const
    {
        switch (state)
        {
        case BossAttackState::AttackAlarm:     return Get_delayAttackAlarm();
        case BossAttackState::Attack1:         return Get_delayAttack1();
        case BossAttackState::Attack2:         return Get_delayAttack2();
        case BossAttackState::Attack3:         return Get_delayAttack3();
        case BossAttackState::SoulSwordCharge: return Get_delaySoulSwordCharge();
        case BossAttackState::SoulSwordAttack: return Get_delaySoulSwordAttack();
        case BossAttackState::SideAttack:      return Get_delaySideAttack();
        case BossAttackState::DashAttack:      return Get_delayDashAttack();
        default:
            return 0.0f;
        }
    }

    std::string AudioBossScript::GetPathForMovementState(BossMovementState state) const
    {
        switch (state)
        {
        case BossMovementState::DashAttack:
            return Get_pathDashAttack();  // DashAttack은 공격 상태와 동일
        case BossMovementState::Walk:
            return Get_pathWalk();  // 인스펙터에 설정된 경로 그대로 사용
        case BossMovementState::Rotate:
            return Get_pathRotate();
        default:
            return "";
        }
    }

    std::string AudioBossScript::GetPathForOtherState(BossOtherState state) const
    {
        switch (state)
        {
        case BossOtherState::GroggyEnter: 
        {
            std::string path = Get_pathGroggyEnter();
            ALICE_LOG_INFO("[AudioBoss] GetPathForOtherState(GroggyEnter) -> path='%s' (length=%zu, empty=%d)", 
                path.c_str(), path.length(), path.empty());
            if (path.empty())
            {
                ALICE_LOG_WARN("[AudioBoss] pathGroggyEnter is EMPTY! Check scene file property.");
            }
            return path;
        }
        case BossOtherState::Roar:        return Get_pathRoar();
        case BossOtherState::Hit:
            return Get_pathHit();
        case BossOtherState::Death:       return Get_pathDeath();
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

    void AudioBossScript::PlayAttackStateDelayed(BossAttackState state, float delaySeconds)
    {
        ALICE_LOG_INFO("[AudioBoss] PlayAttackStateDelayed: state=%d, delay=%.2f", static_cast<int>(state), delaySeconds);
        
        if (state == BossAttackState::None)
        {
            ALICE_LOG_WARN("[AudioBoss] PlayAttackStateDelayed: state is None");
            return;
        }
        
        // delaySeconds가 0 이하이면 상태별 딜레이 사용
        if (delaySeconds <= 0.0f)
        {
            delaySeconds = GetDelayForAttackState(state);
            ALICE_LOG_INFO("[AudioBoss] PlayAttackStateDelayed: using state-specific delay=%.2f", delaySeconds);
        }
        
        // 딜레이가 0 이하면 즉시 재생
        if (delaySeconds <= 0.0f)
        {
            PlayAttackState(state);
            return;
        }
        
        // 사운드 경로 가져오기
        std::string path = GetPathForAttackState(state);
        if (path.empty())
        {
            ALICE_LOG_WARN("[AudioBoss] PlayAttackStateDelayed: path is empty for state=%d", static_cast<int>(state));
            return;
        }
        
        const std::string resolvedPath = TempSound::ResolveTempSoundPath(path, "Boss");
        ALICE_LOG_INFO("[AudioBoss] PlayAttackStateDelayed: resolvedPath=%s, delay=%.2f", resolvedPath.c_str(), delaySeconds);
        
        auto* audio = Audio();
        auto* resources = Resources();
        if (!audio || !resources)
        {
            ALICE_LOG_WARN("[AudioBoss] PlayAttackStateDelayed: Audio() or Resources() is null");
            return;
        }
        
        // 사운드 미리 로드
        const std::filesystem::path logicalPath = std::filesystem::u8path(resolvedPath);
        std::wstring key = WStringFromUtf8(resolvedPath);
        if (!audio->LoadAuto(*resources, key, logicalPath, Sound::Type::SFX))
        {
            ALICE_LOG_WARN("[AudioBoss] PlayAttackStateDelayed: Load failed: %s", resolvedPath.c_str());
            return;
        }
        
        // 딜레이 큐에 추가 (C++ deltaTime 기반)
        const float volume = Get_volume();
        const float pitch = Get_pitch();
        
        DelayedSoundRequest request;
        request.state = state;
        request.remainingDelay = delaySeconds;
        request.key = key;
        request.volume = volume;
        request.pitch = pitch;
        
        m_delayedSoundQueue.push_back(request);
        ALICE_LOG_INFO("[AudioBoss] PlayAttackStateDelayed: Added to queue, queue size=%zu, remainingDelay=%.2f", 
            m_delayedSoundQueue.size(), delaySeconds);
    }

    void AudioBossScript::PlayMovementState(BossMovementState state)
    {
        if (state == BossMovementState::None)
            return;
        std::string path = GetPathForMovementState(state);
        if (!path.empty())
            PlayPathInternal(path, state == BossMovementState::Walk);
    }

    void AudioBossScript::PlayOtherState(BossOtherState state)
    {
        if (state == BossOtherState::None)
            return;
        ALICE_LOG_INFO("[AudioBoss] PlayOtherState state=%d", static_cast<int>(state));
        std::string path = GetPathForOtherState(state);
        ALICE_LOG_INFO("[AudioBoss] path=%s", path.c_str());
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

