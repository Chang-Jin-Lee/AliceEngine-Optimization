#include "SoundBridgeScript.h"
#include "AudioEventBusScript.h"
#include "AudioSoundState.h"

#include "../Combat/C_CombatSessionComponent.h"
#include "../Combat/C_CombatContracts.h"
#include "../Combat/C_CombatResolver.h"
#include "../Combat/C_BossBrainComponent.h"

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Rendering/SkinnedMeshRegistry.h"
#include "Runtime/Rendering/Components/SkinnedMeshComponent.h"
#include "Runtime/Gameplay/Animation/AdvancedAnimationComponent.h"
#include "Runtime/Rendering/Components/SkinnedAnimationComponent.h"
#include "Runtime/Importing/FbxModel.h"
#include <assimp/scene.h>
#include <vector>
#include <cctype>
#include <cstdlib>

namespace Alice
{
    REGISTER_SCRIPT(SoundBridgeScript);

    namespace
    {
        C_CombatSessionComponent* FindSession(World& world, const std::string& name)
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
                if (sc.scriptName == "C_CombatSessionComponent" && sc.instance)
                    return static_cast<C_CombatSessionComponent*>(sc.instance.get());
            }
            return nullptr;
        }

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

    void SoundBridgeScript::Start()
    {
        m_combo2ExtraPending = false;
         m_combo2ExtraTimer = 0.0f;
         m_prevActionState = 0xFF;
        m_prevBossState = 0xFF;
         m_prevComboIndex = -1;
        m_prevBossPattern = -1;
        m_suppressBossHitSfxTimer = 0.0f;

        // Bind to combat resolve delegate
        World* world = GetWorld();
        if (world)
        {
            C_CombatSessionComponent* session = FindSession(*world, Get_sessionEntityName());
            if (session)
            {
                session->OnCombatResolved.BindObject(this, &SoundBridgeScript::OnCombatResolve);
                
                // Bind to Phase2 entered delegate
                GameObject bossGo = world->FindGameObject(session->Get_m_bossName());
                if (bossGo.IsValid())
                {
                    auto* scripts = world->GetScripts(bossGo.id());
                    if (scripts)
                    {
                        for (auto& sc : *scripts)
             {
                            if (sc.scriptName == "C_BossBrainComponent" && sc.instance)
                            {
                                auto* bossBrain = static_cast<C_BossBrainComponent*>(sc.instance.get());
                                bossBrain->OnPhase2Entered.BindObject(this, &SoundBridgeScript::OnPhase2Entered);
                                break;
                            }
                        }
                    }
                }
            }
        }
    }

    //C_CombatSessionComponent* SoundBridgeScript::FindSession()
    //{
    //     World* world = GetWorld();
    //     return world ? Alice::FindSession(*world, Get_sessionEntityName()) : nullptr;
    //}

    //AudioEventBusScript* SoundBridgeScript::FindBus()
    //{
    //     World* world = GetWorld();
    //     return world ? Alice::FindBus(*world, Get_busEntityName()) : nullptr;
    //}

    void SoundBridgeScript::OnCombatStateEntered(EntityId entityId, std::uint8_t prevState, std::uint8_t curState, const void* flagsPtr)
    {
        World* world = GetWorld();
        if (!world)
            return;

        AudioEventBusScript* bus = FindBus(*world, Get_busEntityName());
        if (!bus)
            return;

        const Combat::ActionState prev = static_cast<Combat::ActionState>(prevState);
        const Combat::ActionState state = static_cast<Combat::ActionState>(curState);
        const Combat::ActionFlags* flags = static_cast<const Combat::ActionFlags*>(flagsPtr);
        const int attackComboIndex = flags ? flags->attackComboIndex : 0;
        const bool chargeActive = flags ? flags->chargeActive : false;
        const int chargeLevel = flags ? flags->chargeLevel : 0;

        C_CombatSessionComponent* session = FindSession(*world, Get_sessionEntityName());
        if (!session)
            return;

        // 플레이어/보스 ID 확인을 위해 엔티티 이름으로 판단
        GameObject playerGo = world->FindGameObject(session->Get_m_playerName());
        GameObject bossGo = world->FindGameObject(session->Get_m_bossName());
        const EntityId playerId = playerGo.IsValid() ? playerGo.id() : InvalidEntityId;
        const EntityId bossId = bossGo.IsValid() ? bossGo.id() : InvalidEntityId;
        const bool isPlayer = (entityId == playerId);

        if (isPlayer)
        {
            // Move에서 다른 상태로 전환될 때만 Stop 재생
            if (prev == Combat::ActionState::Move && state != Combat::ActionState::Move)
            {
                bus->RequestPlayerMovementSfx(PlayerMovementState::Stop, state == Combat::ActionState::Idle);
            }
            
            switch (state)
            {
            case Combat::ActionState::Attack:
                if (chargeActive && chargeLevel > 0)
                    bus->RequestPlayerAttackSfx(PlayerAttackState::HeavyAttack);
                else
                {
                    switch (attackComboIndex)
                    {
                    case 1: bus->RequestPlayerAttackSfx(PlayerAttackState::Attack1); break;
                    case 2: bus->RequestPlayerAttackSfx(PlayerAttackState::Attack2); break;
                    case 3: bus->RequestPlayerAttackSfx(PlayerAttackState::Attack3); break;
                    default: bus->RequestPlayerAttackSfx(PlayerAttackState::Attack1); break;
                    }
                }
                if (Get_combo2ExtraEnabled() && attackComboIndex == 3 && !(chargeActive && chargeLevel > 0))
                {
                    m_combo2ExtraPending = true;
                    m_combo2ExtraTimer = 0.0f;
                }
                else
                {
                    m_combo2ExtraPending = false;
                    m_combo2ExtraTimer = 0.0f;
                }
                break;
            case Combat::ActionState::Guard:
                m_combo2ExtraPending = false;
                m_combo2ExtraTimer = 0.0f;
                // 가드 사운드는 OnCombatResolve(Guard)에서만 재생 (그냥 가드만 할 땐 소리 없음)
                break;
            case Combat::ActionState::JustGuardSuccess:
                m_combo2ExtraPending = false;
                m_combo2ExtraTimer = 0.0f;
                // 패링 사운드는 OnCombatResolve(Parry)에서만 재생 (중복/가드+패링 방지)
                break;
            case Combat::ActionState::Dodge:
                m_combo2ExtraPending = false;
                m_combo2ExtraTimer = 0.0f;
                bus->RequestPlayerMovementSfx(PlayerMovementState::Roll, false);
                break;
            case Combat::ActionState::Move:
                m_combo2ExtraPending = false;
                m_combo2ExtraTimer = 0.0f;
                bus->RequestPlayerMovementSfx(PlayerMovementState::Run, false);
                break;
            case Combat::ActionState::Idle:
                m_combo2ExtraPending = false;
                m_combo2ExtraTimer = 0.0f;
                break;
            case Combat::ActionState::GuardBreakWeak:
                m_combo2ExtraPending = false;
                m_combo2ExtraTimer = 0.0f;
                bus->RequestPlayerOtherSfx(PlayerOtherState::GuardBreak);
                break;
            case Combat::ActionState::HealEnter:
            case Combat::ActionState::HealLoop:
            case Combat::ActionState::HealExit:
                m_combo2ExtraPending = false;
                m_combo2ExtraTimer = 0.0f;
                bus->RequestPlayerOtherSfx(PlayerOtherState::Heal);
                break;
            case Combat::ActionState::Dead:
                m_combo2ExtraPending = false;
                m_combo2ExtraTimer = 0.0f;
                bus->RequestPlayerOtherSfx(PlayerOtherState::Death);
                break;
            default:
                m_combo2ExtraPending = false;
                m_combo2ExtraTimer = 0.0f;
                break;
            }
        }
        else
        {
            // 보스 상태 처리
            // C_BossBrainComponent에서 현재 패턴 가져오기
            C_BossBrainComponent* bossBrain = nullptr;
            if (entityId == bossId)
            {
                auto* scripts = world->GetScripts(bossId);
                if (scripts)
                {
                    for (auto& sc : *scripts)
                    {
                        if (sc.scriptName == "C_BossBrainComponent" && sc.instance)
                        {
                            bossBrain = static_cast<C_BossBrainComponent*>(sc.instance.get());
                            break;
                        }
                    }
                }
            }

            switch (state)
            {
            case Combat::ActionState::Attack:
                if (bossBrain)
                {
                    const auto pattern = bossBrain->GetActivePattern();
                    switch (pattern)
                    {
                    case C_BossBrainComponent::PatternType::Charge:
                        // Charge 패턴일 때 차지 사운드 재생
                        bus->RequestBossAttackSfx(BossAttackState::SoulSwordCharge);
                        break;
                    case C_BossBrainComponent::PatternType::Side:
                        bus->RequestBossAttackSfx(BossAttackState::SideAttack);
                        break;
                    case C_BossBrainComponent::PatternType::Dash:
                        bus->RequestBossAttackSfx(BossAttackState::DashAttack);
                        break;
                    case C_BossBrainComponent::PatternType::Ranged:
                        // Ranged는 SoulSwordAttack 사운드 사용
                        bus->RequestBossAttackSfx(BossAttackState::SoulSwordAttack);
                        break;
                    case C_BossBrainComponent::PatternType::AttackA:
                        // AttackA: 바로 1번 재생
                        bus->RequestBossAttackSfx(BossAttackState::Attack1);
                        break;
                    case C_BossBrainComponent::PatternType::AttackB:
                        {
                            // AttackB: Attack1 사운드 즉시 + 1초 후 Attack2 사운드
                            bus->RequestBossAttackSfx(BossAttackState::Attack1);
                            bus->RequestBossAttackSfxDelayed(BossAttackState::Attack2, 1.0f);
                        }
                        break;
                    case C_BossBrainComponent::PatternType::AttackC:
                        {
                            // AttackC: Attack1 사운드 즉시 + 1초 후 Attack2 사운드 + Attack2 실행 후 1초 뒤 Attack3 사운드
                            bus->RequestBossAttackSfx(BossAttackState::Attack1);
                            bus->RequestBossAttackSfxDelayed(BossAttackState::Attack2, 1.0f);
                            // Attack2가 1초 후 실행되므로, Attack3는 2초 후 실행 (1초 + 1초)
                            bus->RequestBossAttackSfxDelayed(BossAttackState::Attack3, 2.0f);
                            // AttackABC 사운드도 재생
                            bus->RequestBossAttackSfx(BossAttackState::AttackABC);
                        }
                        break;
                    default:
                        // 기본 공격 패턴
                        switch (attackComboIndex)
                        {
                        case 1: bus->RequestBossAttackSfx(BossAttackState::Attack1); break;
                        case 2: bus->RequestBossAttackSfx(BossAttackState::Attack2); break;
                        case 3: bus->RequestBossAttackSfx(BossAttackState::Attack3); break;
                        default: bus->RequestBossAttackSfx(BossAttackState::Attack1); break;
                        }
                        break;
                    }
                }
                else
                {
                    // bossBrain을 찾을 수 없을 때 기본 처리
                    switch (attackComboIndex)
                    {
                    case 1: bus->RequestBossAttackSfx(BossAttackState::Attack1); break;
                    case 2: bus->RequestBossAttackSfx(BossAttackState::Attack2); break;
                    case 3: bus->RequestBossAttackSfx(BossAttackState::Attack3); break;
                    default: bus->RequestBossAttackSfx(BossAttackState::Attack1); break;
                    }
                }
                break;
            case Combat::ActionState::Move:
                bus->RequestBossMovementSfx(BossMovementState::Walk);
                break;
            case Combat::ActionState::Dodge:
                bus->RequestBossMovementSfx(BossMovementState::DashAttack);
                break;
            case Combat::ActionState::Groggy:
                // 이전 상태가 Groggy가 아닐 때만 재생 (중복 방지)
                // Update()에서 이미 재생했을 수 있지만, OnCombatStateEntered가 먼저 호출되는 경우를 대비
                if (prev != Combat::ActionState::Groggy)
                {
                    bus->RequestBossOtherSfx(BossOtherState::GroggyEnter);
                }
                break;
            case Combat::ActionState::Hitstun:
                bus->RequestBossOtherSfx(BossOtherState::Hit);
                break;
            case Combat::ActionState::Dead:
                bus->RequestBossOtherSfx(BossOtherState::Death);
                break;
            default:
                break;
            }
        }
    }

    void SoundBridgeScript::OnCombatResolve(EntityId victimId, EntityId attackerId, std::uint8_t resolveResult, float damage, const DirectX::XMFLOAT3& hitPos)
    {
        (void)damage;
        (void)attackerId;
        
        World* world = GetWorld();
        if (!world)
            return;

        AudioEventBusScript* bus = FindBus(*world, Get_busEntityName());
         if (!bus)
             return;

         const Combat::ResolveResult result = static_cast<Combat::ResolveResult>(resolveResult);
        C_CombatSessionComponent* session = FindSession(*world, Get_sessionEntityName());
         if (!session)
             return;

        // 플레이어/보스 판단
        GameObject playerGo = world->FindGameObject(session->Get_m_playerName());
        GameObject bossGo = world->FindGameObject(session->Get_m_bossName());
        const EntityId playerId = playerGo.IsValid() ? playerGo.id() : InvalidEntityId;
        const EntityId bossId = bossGo.IsValid() ? bossGo.id() : InvalidEntityId;

        if (victimId == playerId)
        {
            switch (result)
            {
                case Combat::ResolveResult::Guard:
                    // 가드는 히트 위치에서 재생
                    bus->RequestPlayerAttackSfxOneShotAtPosition(PlayerAttackState::Guard, hitPos);
                    break;
                case Combat::ResolveResult::Parry:
                    // 패링은 히트 위치에서 재생
                    // 그로기 진입 직후 패링 SFX 억제 (그로기 사운드가 묻히는 것 방지)
                    if (m_suppressBossHitSfxTimer > 0.0f)
                    {
                        ALICE_LOG_INFO("[SoundBridge] Parry SFX suppressed (groggy just entered, remaining=%.2f)", m_suppressBossHitSfxTimer);
                        break;
                    }
                    bus->RequestPlayerAttackSfxOneShotAtPosition(PlayerAttackState::Parry, hitPos);
                    break;
                case Combat::ResolveResult::GuardBreak:
                    // GuardBreak는 기존 방식 (플레이어 위치)
             bus->RequestPlayerOtherSfx(PlayerOtherState::GuardBreak);
                    break;
                case Combat::ResolveResult::Hit:
                    // 플레이어가 맞았을 때 (현재는 사운드 없음)
                    break;
                default:
                    break;
            }
        }
        else if (victimId == bossId)
        {
            switch (result)
            {
                case Combat::ResolveResult::Hit:
                    // 그로기 진입 직후 Hit SFX 억제
                    if (m_suppressBossHitSfxTimer > 0.0f)
                    {
                        ALICE_LOG_INFO("[SoundBridge] Boss Hit SFX suppressed (groggy just entered, remaining=%.2f)", m_suppressBossHitSfxTimer);
                        break;
                    }
                    ALICE_LOG_INFO("[SoundBridge] Boss Hit resolve -> request Hit SFX");
             bus->RequestBossOtherSfx(BossOtherState::Hit);
                    break;
                // 보스가 가드/패링할 때는 현재 BossAttackState에 Guard/Parry가 없음
                // 나중에 추가되면 여기에 case 추가 가능
                case Combat::ResolveResult::Guard:
                case Combat::ResolveResult::Parry:
                case Combat::ResolveResult::GuardBreak:
                default:
                    break;
            }
        }
    }

    void SoundBridgeScript::Update(float deltaTime)
    {
        World* world = GetWorld();
        if (!world)
            return;

        // 상태 변화 감지를 위해 C_CombatSessionComponent에서 상태를 가져옴
        C_CombatSessionComponent* session = FindSession(*world, Get_sessionEntityName());
        if (!session)
              return;

        AudioEventBusScript* bus = FindBus(*world, Get_busEntityName());
        if (!bus)
              return;

        // 딜레이 사운드는 이제 FMOD Delay를 사용하므로 Update()에서 처리할 필요 없음

        // 플레이어 상태 변화 감지
        const Combat::ActionState playerState = session->GetPlayerState();
        const Combat::ActionFlags playerFlags = session->GetPlayerFlags();
        const std::uint8_t rawPlayerState = static_cast<std::uint8_t>(playerState);
        
        if (rawPlayerState != m_prevActionState)
        {
            GameObject playerGo = world->FindGameObject(session->Get_m_playerName());
            if (playerGo.IsValid())
            {
                OnCombatStateEntered(playerGo.id(), m_prevActionState, rawPlayerState, &playerFlags);
            }
            // 상태 전환 이벤트(Entered)에서만 prev 갱신
            m_prevActionState = rawPlayerState;
        }

        // 보스 상태 변화 감지
        const Combat::ActionState bossState = session->GetBossState();
        const Combat::ActionFlags bossFlags = session->GetBossFlags();
        const std::uint8_t rawBossState = static_cast<std::uint8_t>(bossState);
        
        // 보스 패턴 추적 (Charge → SoulSwordAttack 전환 감지)
        GameObject bossGo = world->FindGameObject(session->Get_m_bossName());
        C_BossBrainComponent* bossBrain = nullptr;
        int currentBossPattern = -1;
        if (bossGo.IsValid())
        {
            auto* scripts = world->GetScripts(bossGo.id());
            if (scripts)
            {
                for (auto& sc : *scripts)
                {
                    if (sc.scriptName == "C_BossBrainComponent" && sc.instance)
                    {
                        bossBrain = static_cast<C_BossBrainComponent*>(sc.instance.get());
                        currentBossPattern = static_cast<int>(bossBrain->GetActivePattern());
                        break;
                    }
                }
            }
        }
        
        // Charge에서 다른 패턴으로 전환될 때 SoulSwordAttack 재생
        if (bossBrain && m_prevBossPattern >= 0)
        {
            const int prevPattern = m_prevBossPattern;
            if (prevPattern == static_cast<int>(C_BossBrainComponent::PatternType::Charge)
                && currentBossPattern != static_cast<int>(C_BossBrainComponent::PatternType::Charge)
                && currentBossPattern != static_cast<int>(C_BossBrainComponent::PatternType::None))
            {
                bus->RequestBossAttackSfx(BossAttackState::SoulSwordAttack);
            }
        }
        
        // 보스 상태 변화 감지 또는 Attack 상태에서 패턴 변경 감지
        bool bossStateChanged = (rawBossState != m_prevBossState);
        bool bossPatternChanged = false;
        
        // 그로기 진입 감지 및 즉시 사운드 재생 (OnCombatStateEntered 호출 전에)
        const std::uint8_t groggyStateValue = static_cast<std::uint8_t>(Combat::ActionState::Groggy);
        const bool groggyEntered = (rawBossState == groggyStateValue)
            && (m_prevBossState != groggyStateValue);
        
        if (groggyEntered && bossGo.IsValid())
        {
            ALICE_LOG_INFO("[SoundBridge] GroggyEntered: prev=%u cur=%u", m_prevBossState, rawBossState);
            // 그로기 진입 시 즉시 사운드 요청 (OnCombatStateEntered가 호출되지 않거나 늦게 호출되는 경우 대비)
            bus->RequestBossOtherSfx(BossOtherState::GroggyEnter);
            // 그로기 진입 후 0.15초 동안 Hit SFX 억제
            m_suppressBossHitSfxTimer = 0.15f;
        }
        
        // Hit SFX 억제 타이머 감소
        if (m_suppressBossHitSfxTimer > 0.0f)
        {
            m_suppressBossHitSfxTimer = std::max(0.0f, m_suppressBossHitSfxTimer - deltaTime);
        }
        
        // 포효 애니메이션 시작 감지 (패턴이 Special로 전환될 때)
        if (bossBrain && currentBossPattern >= 0 && m_prevBossPattern >= 0)
        {
            const int prevPattern = m_prevBossPattern;
            const int specialPattern = static_cast<int>(C_BossBrainComponent::PatternType::Special);
            
            // 이전 패턴이 Special이 아니고 현재 패턴이 Special일 때 포효 소리와 BGM 재생
            if (prevPattern != specialPattern && currentBossPattern == specialPattern)
            {
                // 포효 소리 재생
                bus->RequestBossOtherSfx(BossOtherState::Roar);
                // Phase2 BGM 변경
                bus->RequestBgm("Resource/Test/4_Resources/sound/SFX/BGM/BGM_Boss_SecondPhase_01.mp3");
            }
        }
        
        if (bossBrain && currentBossPattern >= 0)
        {
            // Attack 상태로 전환될 때 항상 감지
            if (rawBossState == static_cast<std::uint8_t>(Combat::ActionState::Attack))
            {
                if (bossStateChanged)
                {
                    // Orbit/Idle → Attack 전환 시 항상 감지
                    bossPatternChanged = true;
                }
                else if (m_prevBossPattern >= 0)
                {
                    // Attack 상태에서 패턴만 바뀐 경우
                    if (currentBossPattern != m_prevBossPattern
                        && currentBossPattern != static_cast<int>(C_BossBrainComponent::PatternType::None))
                    {
                        bossPatternChanged = true;
                    }
                }
            }
        }
        
        // 상태 전환 확정 및 이벤트 발행
        if (bossStateChanged || bossPatternChanged)
        {
            // 전환 전 prev 상태 저장
            const std::uint8_t prevBossStateForEvent = m_prevBossState;
            
            // 상태 전환 확정: Update에서만 m_prevBossState 갱신
            if (bossStateChanged)
                m_prevBossState = rawBossState;
            
            // 전환 이벤트 발행 (갱신 전 prev를 전달)
            if (bossGo.IsValid())
            {
                OnCombatStateEntered(bossGo.id(), prevBossStateForEvent, rawBossState, &bossFlags);
            }
        }
        
        // 보스 패턴 갱신
        if (currentBossPattern >= 0)
            m_prevBossPattern = currentBossPattern;
        
        // Combo2 Extra 처리
        if (Get_combo2ExtraEnabled())
        {
            const bool attackState = (playerState == Combat::ActionState::Attack);
            const bool comboChanged = (playerFlags.attackComboIndex != m_prevComboIndex);
          const bool combo2Now = attackState
                && playerFlags.attackComboIndex == 3
                && !(playerFlags.chargeActive && playerFlags.chargeLevel > 0);

          if (!attackState)
          {
              m_combo2ExtraPending = false;
              m_combo2ExtraTimer = 0.0f;
          }
          else if (combo2Now && comboChanged)
          {
              m_combo2ExtraPending = true;
              m_combo2ExtraTimer = 0.0f;
          }

          if (m_combo2ExtraPending)
          {
              float delay = Get_combo2ExtraDelaySec();
              if (delay < 0.0f)
                  delay = 0.0f;

              m_combo2ExtraTimer += deltaTime;
              if (m_combo2ExtraTimer >= delay)
              {
                    if (playerFlags.hitActive && playerFlags.attackComboIndex == 3)
                      bus->RequestPlayerAttackSfxOneShot(PlayerAttackState::Attack3);
                  m_combo2ExtraPending = false;
              }
          }
        }

        m_prevComboIndex = playerFlags.attackComboIndex;
    }

    namespace
    {
        // C_CombatSessionComponent의 GetClipDurationSecByName 함수 복사
        static bool TryParseIndex(const std::string& key, int& outIdx)
        {
            if (key.empty())
                return false;
            for (char c : key)
            {
                if (!std::isdigit(static_cast<unsigned char>(c)))
                    return false;
            }
            outIdx = std::atoi(key.c_str());
            return true;
        }

        static float GetClipDurationSecByName(const SkinnedMeshRegistry* registry,
            World& world,
            EntityId entityId,
            const std::string& clipName)
        {
            if (clipName.empty())
                return 0.0f;
            if (!registry)
                return 0.0f;
            auto* skinned = world.GetComponent<SkinnedMeshComponent>(entityId);
            if (!skinned || skinned->meshAssetPath.empty())
                return 0.0f;
            auto mesh = registry->Find(skinned->meshAssetPath);
            if (!mesh || !mesh->sourceModel)
                return 0.0f;
            const auto& names = mesh->sourceModel->GetAnimationNames();
            const auto* scene = mesh->sourceModel->GetScenePtr();
            const size_t clipCount = scene ? scene->mNumAnimations : names.size();
            for (size_t i = 0; i < names.size() && i < clipCount; ++i)
            {
                if (names[i] == clipName)
                    return static_cast<float>(mesh->sourceModel->GetClipDurationSec(static_cast<int>(i)));
            }
            if (scene)
            {
                for (size_t i = 0; i < scene->mNumAnimations; ++i)
                {
                    const auto* anim = scene->mAnimations[i];
                    if (anim && anim->mName.length > 0 && clipName == anim->mName.C_Str())
                        return static_cast<float>(mesh->sourceModel->GetClipDurationSec(static_cast<int>(i)));
                }
            }
            int idx = -1;
            if (TryParseIndex(clipName, idx))
            {
                if (idx >= 0 && static_cast<size_t>(idx) < clipCount)
                    return static_cast<float>(mesh->sourceModel->GetClipDurationSec(idx));
            }
            return 0.0f;
        }

        // C_CombatSessionComponent의 ResolveClipSpeed 함수 복사
        static float ResolveClipSpeed(const AdvancedAnimationComponent& anim, const std::string& clip)
        {
            if (clip.empty())
                return 1.0f;
            if (anim.base.clipA == clip)
                return anim.base.speedA;
            if (anim.base.clipB == clip)
                return anim.base.speedB;
            if (anim.upper.clipA == clip)
                return anim.upper.speedA;
            if (anim.upper.clipB == clip)
                return anim.upper.speedB;
            if (anim.additive.clip == clip)
                return anim.additive.speed;
            return 1.0f;
        }
    }

    float SoundBridgeScript::GetBossPatternActualDuration(EntityId bossId, C_BossBrainComponent::PatternType patternType)
    {
        World* world = GetWorld();
        if (!world || bossId == InvalidEntityId)
            return 0.0f;

        auto* registry = SkinnedRegistry();
        if (!registry)
            return 0.0f;

        auto* bossBrain = world->GetComponent<C_BossBrainComponent>(bossId);
        if (!bossBrain)
            return 0.0f;

        // 클립 이름 가져오기
        std::string clipName = bossBrain->GetPatternClip(patternType);
        if (clipName.empty())
            return 0.0f;

        // 원본 재생 시간 가져오기
        float baseDuration = GetClipDurationSecByName(registry, *world, bossId, clipName);
        if (baseDuration <= 0.0f)
            return 0.0f;

        // 애니메이션 속도 가져오기
        float speed = 1.0f;
        if (auto* anim = world->GetComponent<AdvancedAnimationComponent>(bossId))
        {
            speed = ResolveClipSpeed(*anim, clipName);
        }
        else if (auto* skinnedAnim = world->GetComponent<SkinnedAnimationComponent>(bossId))
        {
            speed = skinnedAnim->speed;
        }

        // 실제 재생 시간 계산 (속도가 빠르면 재생 시간이 짧아짐)
        const float speedAbs = std::abs(speed);
        if (speedAbs > 0.0001f)
            return baseDuration / speedAbs;

        return baseDuration;
    }

    void SoundBridgeScript::OnPhase2Entered()
    {
        // Phase2 전환은 감지했지만, 실제 BGM과 포효 소리는 포효 애니메이션이 시작될 때 재생됨
        // (Update()에서 패턴이 Special로 전환될 때 처리)
    }
}