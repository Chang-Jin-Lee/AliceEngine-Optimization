#include "AudioBossScript.h"

#include "AudioEventBusScript.h"
#include "TempSoundPath.h"
#include "Runtime/Audio/SoundManager.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Foundation/Helper.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include <fmod.hpp>
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

    void AudioBossScript::InitializeAttackGroupVolume(BossAttackState state, float volume)
    {
        std::wstring groupName = L"Attack" + std::to_wstring(static_cast<int>(state));
        Sound::GetOrCreateBossAttackGroup(groupName);
        Sound::SetBossGroupVolume(groupName, volume);
        m_attackGroupNames[state] = groupName;
        ALICE_LOG_INFO("[AudioBoss] InitializeAttackGroupVolume: state=%d, volume=%.2f", 
            static_cast<int>(state), volume);
    }

    void AudioBossScript::InitializeMovementGroupVolume(BossMovementState state, float volume)
    {
        std::wstring groupName = L"Movement" + std::to_wstring(static_cast<int>(state));
        Sound::GetOrCreateBossMovementGroup(groupName);
        Sound::SetBossGroupVolume(groupName, volume);
        m_movementGroupNames[state] = groupName;
        ALICE_LOG_INFO("[AudioBoss] InitializeMovementGroupVolume: state=%d, volume=%.2f", 
            static_cast<int>(state), volume);
    }

    void AudioBossScript::InitializeOtherGroupVolume(BossOtherState state, float volume)
    {
        std::wstring groupName = L"Other" + std::to_wstring(static_cast<int>(state));
        Sound::GetOrCreateBossOtherGroup(groupName);
        Sound::SetBossGroupVolume(groupName, volume);
        m_otherGroupNames[state] = groupName;
        ALICE_LOG_INFO("[AudioBoss] InitializeOtherGroupVolume: state=%d, volume=%.2f", 
            static_cast<int>(state), volume);
    }

    void AudioBossScript::Start()
    {
        m_currentAttack = BossAttackState::None;
        m_currentMovement = BossMovementState::None;
        m_currentOther = BossOtherState::None;
        m_delayedSoundQueue.clear();
        m_attackGroupNames.clear();
        m_movementGroupNames.clear();
        m_otherGroupNames.clear();

        // 인스펙터에서 설정한 볼륨을 각 그룹에 적용
        // Attack 상태별 볼륨 초기화
        InitializeAttackGroupVolume(BossAttackState::AttackAlarm, Get_volumeAttackAlarm());
        InitializeAttackGroupVolume(BossAttackState::Attack1, Get_volumeAttack1());
        InitializeAttackGroupVolume(BossAttackState::Attack2, Get_volumeAttack2());
        InitializeAttackGroupVolume(BossAttackState::Attack3, Get_volumeAttack3());
        InitializeAttackGroupVolume(BossAttackState::AttackABC, Get_volumeAttackABC());
        InitializeAttackGroupVolume(BossAttackState::SoulSwordCharge, Get_volumeSoulSwordCharge());
        InitializeAttackGroupVolume(BossAttackState::SoulSwordAttack, Get_volumeSoulSwordAttack());
        InitializeAttackGroupVolume(BossAttackState::SideAttack, Get_volumeSideAttack());
        InitializeAttackGroupVolume(BossAttackState::DashAttack, Get_volumeDashAttack());

        // Movement 상태별 볼륨 초기화
        InitializeMovementGroupVolume(BossMovementState::Walk, Get_volumeWalk());
        InitializeMovementGroupVolume(BossMovementState::Rotate, Get_volumeRotate());

        // Other 상태별 볼륨 초기화
        InitializeOtherGroupVolume(BossOtherState::GroggyEnter, Get_volumeGroggyEnter());
        InitializeOtherGroupVolume(BossOtherState::Roar, Get_volumeRoar());
        InitializeOtherGroupVolume(BossOtherState::Hit, Get_volumeHit());
        InitializeOtherGroupVolume(BossOtherState::Death, Get_volumeDeath());

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
                if (!Get_targetEntityName().empty())
                {
                    GameObject targetGo = GetWorld()->FindGameObject(Get_targetEntityName());
                    if (targetGo.IsValid())
                    {
                        if (auto* tr = GetWorld()->GetComponent<TransformComponent>(targetGo.id()))
                            pos = tr->position;
                    }
                }
                else
                {
                    if (auto* tr = GetTransform())
                        pos = tr->position;
                }
                float volume = Get_volume();
                if (m_currentMovement != BossMovementState::None)
                    volume *= GetMovementStateVolume(m_currentMovement);
                audio->Update3D(m_loopInstanceId, pos, volume, Get_minDistance(), Get_maxDistance());
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
                        if (!Get_targetEntityName().empty())
                        {
                            GameObject targetGo = GetWorld()->FindGameObject(Get_targetEntityName());
                            if (targetGo.IsValid())
                            {
                                if (auto* tr = GetWorld()->GetComponent<TransformComponent>(targetGo.id()))
                                    pos = tr->position;
                            }
                        }
                        else
                        {
                            if (auto* tr = GetTransform())
                                pos = tr->position;
                        }
                        const float volume = it->volume * GetAttackStateVolume(it->state);
                        audio->Play3D(L"", it->key, pos, volume, it->pitch, false);
                    }
                    else
                    {
                        std::wstring groupName = L"Attack" + std::to_wstring(static_cast<int>(it->state));
                        FMOD::ChannelGroup* group = Sound::GetOrCreateBossAttackGroup(groupName);
                        m_attackGroupNames[it->state] = groupName;
                        if (group)
                            Sound::PlaySFXWithGroup(it->key, group, it->volume, it->pitch, false);
                        else
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

    float AudioBossScript::GetAttackStateVolume(BossAttackState state) const
    {
        switch (state)
        {
        case BossAttackState::AttackAlarm:     return Get_volumeAttackAlarm();
        case BossAttackState::Attack1:         return Get_volumeAttack1();
        case BossAttackState::Attack2:         return Get_volumeAttack2();
        case BossAttackState::Attack3:         return Get_volumeAttack3();
        case BossAttackState::AttackABC:       return Get_volumeAttackABC();
        case BossAttackState::SoulSwordCharge: return Get_volumeSoulSwordCharge();
        case BossAttackState::SoulSwordAttack: return Get_volumeSoulSwordAttack();
        case BossAttackState::SideAttack:      return Get_volumeSideAttack();
        case BossAttackState::DashAttack:      return Get_volumeDashAttack();
        default:
            return 1.0f;
        }
    }

    float AudioBossScript::GetMovementStateVolume(BossMovementState state) const
    {
        switch (state)
        {
        case BossMovementState::Walk:   return Get_volumeWalk();
        case BossMovementState::Rotate: return Get_volumeRotate();
        default:
            return 1.0f;
        }
    }

    float AudioBossScript::GetOtherStateVolume(BossOtherState state) const
    {
        switch (state)
        {
        case BossOtherState::GroggyEnter: return Get_volumeGroggyEnter();
        case BossOtherState::Roar:        return Get_volumeRoar();
        case BossOtherState::Hit:         return Get_volumeHit();
        case BossOtherState::Death:       return Get_volumeDeath();
        default:
            return 1.0f;
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
        case BossAttackState::AttackABC:       return Get_delayAttackABC();
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

    void AudioBossScript::PlayPathInternal(const std::string& path, bool isLooping,
                                           BossAttackState attackState,
                                           BossMovementState movementState,
                                           BossOtherState otherState)
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

        float volume = Get_volume();
        const float pitch = Get_pitch();
        
        // 상태별 그룹 결정
        FMOD::ChannelGroup* targetGroup = nullptr;
        std::wstring groupName;
        
        if (attackState != BossAttackState::None)
        {
            groupName = L"Attack" + std::to_wstring(static_cast<int>(attackState));
            targetGroup = Sound::GetOrCreateBossAttackGroup(groupName);
            m_attackGroupNames[attackState] = groupName;
        }
        else if (movementState != BossMovementState::None)
        {
            groupName = L"Movement" + std::to_wstring(static_cast<int>(movementState));
            targetGroup = Sound::GetOrCreateBossMovementGroup(groupName);
            m_movementGroupNames[movementState] = groupName;
        }
        else if (otherState != BossOtherState::None)
        {
            groupName = L"Other" + std::to_wstring(static_cast<int>(otherState));
            targetGroup = Sound::GetOrCreateBossOtherGroup(groupName);
            m_otherGroupNames[otherState] = groupName;
        }

        if (Get_is3D())
        {
            if (attackState != BossAttackState::None)
                volume *= GetAttackStateVolume(attackState);
            else if (movementState != BossMovementState::None)
                volume *= GetMovementStateVolume(movementState);
            else if (otherState != BossOtherState::None)
                volume *= GetOtherStateVolume(otherState);

            DirectX::XMFLOAT3 pos{ 0.0f, 0.0f, 0.0f };
            if (!Get_targetEntityName().empty())
            {
                GameObject targetGo = GetWorld()->FindGameObject(Get_targetEntityName());
                if (targetGo.IsValid())
                {
                    if (auto* tr = GetWorld()->GetComponent<TransformComponent>(targetGo.id()))
                        pos = tr->position;
                }
            }
            else
            {
                if (auto* tr = GetTransform())
                    pos = tr->position;
            }

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
                // 3D one-shot은 그룹 지원이 없으므로 기존 방식 유지
                audio->Play3D(L"", key, pos, volume, pitch, false);
            }
        }
        else
        {
            if (isLooping)
            {
                // Loop는 그룹 지원 함수 사용
                if (targetGroup)
                    Sound::PlaySFXWithGroup(key, targetGroup, volume, pitch, true);
                else
                    audio->PlaySFX(key, volume, pitch, true);
                m_loopKey = key;
                m_loopPlaying = true;
            }
            else
            {
                // One-shot은 그룹 지원 함수 사용
                if (targetGroup)
                    Sound::PlaySFXWithGroup(key, targetGroup, volume, pitch, false);
                else
                    audio->PlaySFX(key, volume, pitch, false);
            }
        }
    }

    void AudioBossScript::PlayAttackState(BossAttackState state)
    {
        if (state == BossAttackState::None)
            return;
        std::string path = GetPathForAttackState(state);
        if (!path.empty())
            PlayPathInternal(path, false, state, BossMovementState::None, BossOtherState::None);
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
        {
            // None 상태는 Walk 사운드 중지를 의미
            if (m_currentMovement == BossMovementState::Walk)
            {
                StopLoop();
                m_currentMovement = BossMovementState::None;
            }
            return;
        }
        
        // 상태가 변경되었을 때만 재생
        if (state == m_currentMovement)
            return;
        
        // 이전 Walk 사운드가 재생 중이면 중지
        if (m_currentMovement == BossMovementState::Walk)
        {
            StopLoop();
        }
        
        m_currentMovement = state;
        std::string path = GetPathForMovementState(state);
        if (!path.empty())
            PlayPathInternal(path, state == BossMovementState::Walk, 
                            BossAttackState::None, state, BossOtherState::None);
    }

    void AudioBossScript::PlayOtherState(BossOtherState state)
    {
        if (state == BossOtherState::None)
            return;
        ALICE_LOG_INFO("[AudioBoss] PlayOtherState state=%d", static_cast<int>(state));
        std::string path = GetPathForOtherState(state);
        ALICE_LOG_INFO("[AudioBoss] path=%s", path.c_str());
        if (!path.empty())
            PlayPathInternal(path, false, BossAttackState::None, BossMovementState::None, state);
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

    void AudioBossScript::SetAttackStateVolume(BossAttackState state, float volume)
    {
        // 1.0f 이상도 허용 (증폭)
        volume = std::max(0.0f, volume);
        
        auto it = m_attackGroupNames.find(state);
        if (it != m_attackGroupNames.end() && !it->second.empty())
        {
            Sound::SetBossGroupVolume(it->second, volume);
            ALICE_LOG_INFO("[AudioBoss] SetAttackStateVolume: state=%d, group='%ls', volume=%.2f", 
                static_cast<int>(state), it->second.c_str(), volume);
        }
        else
        {
            // 그룹이 아직 생성되지 않았으면 미리 생성
            std::wstring groupName = L"Attack" + std::to_wstring(static_cast<int>(state));
            Sound::GetOrCreateBossAttackGroup(groupName);
            m_attackGroupNames[state] = groupName;
            Sound::SetBossGroupVolume(groupName, volume);
            ALICE_LOG_INFO("[AudioBoss] SetAttackStateVolume: created group '%ls', volume=%.2f", 
                groupName.c_str(), volume);
        }
    }

    void AudioBossScript::SetMovementStateVolume(BossMovementState state, float volume)
    {
        volume = std::max(0.0f, volume);
        
        auto it = m_movementGroupNames.find(state);
        if (it != m_movementGroupNames.end() && !it->second.empty())
        {
            Sound::SetBossGroupVolume(it->second, volume);
            ALICE_LOG_INFO("[AudioBoss] SetMovementStateVolume: state=%d, group='%ls', volume=%.2f", 
                static_cast<int>(state), it->second.c_str(), volume);
        }
        else
        {
            std::wstring groupName = L"Movement" + std::to_wstring(static_cast<int>(state));
            Sound::GetOrCreateBossMovementGroup(groupName);
            m_movementGroupNames[state] = groupName;
            Sound::SetBossGroupVolume(groupName, volume);
            ALICE_LOG_INFO("[AudioBoss] SetMovementStateVolume: created group '%ls', volume=%.2f", 
                groupName.c_str(), volume);
        }
    }

    void AudioBossScript::SetOtherStateVolume(BossOtherState state, float volume)
    {
        volume = std::max(0.0f, volume);
        
        auto it = m_otherGroupNames.find(state);
        if (it != m_otherGroupNames.end() && !it->second.empty())
        {
            Sound::SetBossGroupVolume(it->second, volume);
            ALICE_LOG_INFO("[AudioBoss] SetOtherStateVolume: state=%d, group='%ls', volume=%.2f", 
                static_cast<int>(state), it->second.c_str(), volume);
        }
        else
        {
            std::wstring groupName = L"Other" + std::to_wstring(static_cast<int>(state));
            Sound::GetOrCreateBossOtherGroup(groupName);
            m_otherGroupNames[state] = groupName;
            Sound::SetBossGroupVolume(groupName, volume);
            ALICE_LOG_INFO("[AudioBoss] SetOtherStateVolume: created group '%ls', volume=%.2f", 
                groupName.c_str(), volume);
        }
    }

    void AudioBossScript::PlaySfx1() { PlaySfxPath(GetPathForAttackState(BossAttackState::Attack1)); }
    void AudioBossScript::PlaySfx2() { PlaySfxPath(GetPathForAttackState(BossAttackState::Attack2)); }
    void AudioBossScript::PlaySfx3() { PlaySfxPath(GetPathForAttackState(BossAttackState::Attack3)); }
    void AudioBossScript::PlaySfx4() { PlaySfxPath(GetPathForOtherState(BossOtherState::Roar)); }
    void AudioBossScript::PlaySfx5() { PlaySfxPath(GetPathForOtherState(BossOtherState::Hit)); }
}

