#include "SoundBridgeScript.h"
#include "AudioEventBusScript.h"
#include "AudioSoundState.h"

#include "../Combat/C_CombatSessionComponent.h"
#include "../Combat/C_CombatContracts.h"
#include "../Combat/C_CombatResolver.h"

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/GameObject.h"

namespace Alice
{
    REGISTER_SCRIPT(SoundBridgeScript);

    namespace
    {
        C_CombatSessionComponent* FindSession(World& world, const std::string& name)
        {
            /*if (name.empty())
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
            return nullptr;*/
        }

        AudioEventBusScript* FindBus(World& world, const std::string& name)
        {
            /*if (name.empty())
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
            return nullptr;*/
        }
    }

    void SoundBridgeScript::Start()
    {
        /* m_combo2ExtraPending = false;
         m_combo2ExtraTimer = 0.0f;
         m_prevActionState = 0xFF;
         m_prevComboIndex = -1;

         C_CombatSessionComponent* session = FindSession();
         if (!session)
             return;

         session->OnCombatStateEntered.BindLambda(
             [this](EntityId entityId, Combat::ActionState state, const Combat::ActionFlags* flags)
             {
                 OnCombatStateEntered(entityId, static_cast<std::uint8_t>(state), flags);
             });

         session->OnCombatResolve.BindLambda(
             [this](EntityId victimId, EntityId attackerId, Combat::ResolveResult result, float damage)
             {
                 OnCombatResolve(victimId, attackerId, static_cast<std::uint8_t>(result), damage);
             });*/
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

    void SoundBridgeScript::OnCombatStateEntered(EntityId entityId, std::uint8_t actionState, const void* flagsPtr)
    {
        //(void)entityId;
        //AudioEventBusScript* bus = FindBus();
        //if (!bus)
        //    return;

        //const Combat::ActionState state = static_cast<Combat::ActionState>(actionState);
        //const Combat::ActionFlags* flags = static_cast<const Combat::ActionFlags*>(flagsPtr);
        //const int attackComboIndex = flags ? flags->attackComboIndex : 0;
        //const bool chargeActive = flags ? flags->chargeActive : false;
        //const int chargeLevel = flags ? flags->chargeLevel : 0;

        //C_CombatSessionComponent* session = FindSession();
        //if (!session)
        //    return;
        //const EntityId playerId = session->GetPlayerEntityId();
        //const EntityId bossId = session->GetBossEntityId();
        //const bool isPlayer = (entityId == playerId);
        //if (!isPlayer && entityId != bossId)
        //    return;

        //if (isPlayer)
        //{
        //    if (state != Combat::ActionState::Move)
        //        bus->RequestPlayerMovementSfx(PlayerMovementState::Stop, state == Combat::ActionState::Idle);
        //    switch (state)
        //    {
        //    case Combat::ActionState::Attack:
        //        if (chargeActive && chargeLevel > 0)
        //            bus->RequestPlayerAttackSfx(PlayerAttackState::HeavyAttack);
        //        else
        //        {
        //            switch (attackComboIndex)
        //            {
        //            case 1: bus->RequestPlayerAttackSfx(PlayerAttackState::Attack1); break;
        //            case 2: bus->RequestPlayerAttackSfx(PlayerAttackState::Attack2); break;
        //            case 3: bus->RequestPlayerAttackSfx(PlayerAttackState::Attack3); break;
        //            default: bus->RequestPlayerAttackSfx(PlayerAttackState::Attack1); break;
        //            }
        //        }
        //        if (Get_combo2ExtraEnabled() && attackComboIndex == 3 && !(chargeActive && chargeLevel > 0))
        //        {
        //            m_combo2ExtraPending = true;
        //            m_combo2ExtraTimer = 0.0f;
        //        }
        //        else
        //        {
        //            m_combo2ExtraPending = false;
        //            m_combo2ExtraTimer = 0.0f;
        //        }
        //        break;
        //    case Combat::ActionState::Guard:
        //        m_combo2ExtraPending = false;
        //        m_combo2ExtraTimer = 0.0f;
        //        // 가드 사운드는 OnCombatResolve(Guard)에서만 재생 (그냥 가드만 할 땐 소리 없음)
        //        break;
        //    case Combat::ActionState::JustGuardSuccess:
        //        m_combo2ExtraPending = false;
        //        m_combo2ExtraTimer = 0.0f;
        //        // 패링 사운드는 OnCombatResolve(Parry)에서만 재생 (중복/가드+패링 방지)
        //        break;
        //    case Combat::ActionState::Dodge:
        //        m_combo2ExtraPending = false;
        //        m_combo2ExtraTimer = 0.0f;
        //        bus->RequestPlayerMovementSfx(PlayerMovementState::Roll);
        //        break;
        //    case Combat::ActionState::Move:
        //        m_combo2ExtraPending = false;
        //        m_combo2ExtraTimer = 0.0f;
        //        bus->RequestPlayerMovementSfx(PlayerMovementState::Run);
        //        break;
        //    case Combat::ActionState::Idle:
        //        m_combo2ExtraPending = false;
        //        m_combo2ExtraTimer = 0.0f;
        //        break;
        //    case Combat::ActionState::GuardBreakWeak:
        //        m_combo2ExtraPending = false;
        //        m_combo2ExtraTimer = 0.0f;
        //        bus->RequestPlayerOtherSfx(PlayerOtherState::GuardBreak);
        //        break;
        //    default:
        //        m_combo2ExtraPending = false;
        //        m_combo2ExtraTimer = 0.0f;
        //        break;
        //    }
        //}
        //else
        //{
        //    switch (state)
        //    {
        //    case Combat::ActionState::Attack:
        //        switch (attackComboIndex)
        //        {
        //        case 1: bus->RequestBossAttackSfx(BossAttackState::Attack1); break;
        //        case 2: bus->RequestBossAttackSfx(BossAttackState::Attack2); break;
        //        case 3: bus->RequestBossAttackSfx(BossAttackState::Attack3); break;
        //        default: bus->RequestBossAttackSfx(BossAttackState::Attack1); break;
        //        }
        //        break;
        //    case Combat::ActionState::Move:
        //        bus->RequestBossMovementSfx(BossMovementState::Walk);
        //        break;
        //    case Combat::ActionState::Dodge:
        //        bus->RequestBossMovementSfx(BossMovementState::DashAttack);
        //        break;
        //    case Combat::ActionState::Groggy:
        //        bus->RequestBossOtherSfx(BossOtherState::GroggyEnter);
        //        break;
        //    case Combat::ActionState::Hitstun:
        //        bus->RequestBossOtherSfx(BossOtherState::Hit);
        //        break;
        //    default:
        //        break;
        //    }
        //}
    }

    void SoundBridgeScript::OnCombatResolve(EntityId victimId, EntityId attackerId, std::uint8_t resolveResult, float damage)
    {
        /* (void)damage;
         AudioEventBusScript* bus = FindBus();
         if (!bus)
             return;

         const Combat::ResolveResult result = static_cast<Combat::ResolveResult>(resolveResult);
         C_CombatSessionComponent* session = FindSession();
         if (!session)
             return;
         const EntityId playerId = session->GetPlayerEntityId();
         const EntityId bossId = session->GetBossEntityId();

         if (result == Combat::ResolveResult::Guard && victimId == playerId)
             bus->RequestPlayerAttackSfxOneShot(PlayerAttackState::Guard);
         else if (result == Combat::ResolveResult::Parry && victimId == playerId)
             bus->RequestPlayerAttackSfxOneShot(PlayerAttackState::Parry);
         else if (result == Combat::ResolveResult::GuardBreak && victimId == playerId)
             bus->RequestPlayerOtherSfx(PlayerOtherState::GuardBreak);
         else if (result == Combat::ResolveResult::Hit && victimId == bossId)
             bus->RequestBossOtherSfx(BossOtherState::Hit);
         (void)attackerId;*/
    }

    void SoundBridgeScript::Update(float deltaTime)
    {
        /*  if (!Get_combo2ExtraEnabled())
              return;

          C_CombatSessionComponent* session = FindSession();
          AudioEventBusScript* bus = FindBus();
          if (!session || !bus)
              return;

          const Combat::ActionState state = session->GetPlayerState();
          const Combat::ActionFlags flags = session->GetPlayerFlags();
          const std::uint8_t rawState = static_cast<std::uint8_t>(state);
          const bool attackState = (state == Combat::ActionState::Attack);
          const bool comboChanged = (flags.attackComboIndex != m_prevComboIndex);
          const bool combo2Now = attackState
              && flags.attackComboIndex == 3
              && !(flags.chargeActive && flags.chargeLevel > 0);

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
                  if (flags.hitActive && flags.attackComboIndex == 3)
                      bus->RequestPlayerAttackSfxOneShot(PlayerAttackState::Attack3);
                  m_combo2ExtraPending = false;
              }
          }

          m_prevActionState = rawState;
          m_prevComboIndex = flags.attackComboIndex;
      }*/
    }
}