#include "C_CombatSessionComponent.h"

#include <unordered_map>
#include <algorithm>
#include <cmath>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/Components/IDComponent.h"
#include "Runtime/Gameplay/Combat/HealthComponent.h"
#include "Runtime/Gameplay/Combat/AttackDriverComponent.h"
#include "Runtime/Gameplay/Animation/AdvancedAnimationComponent.h"
#include "Runtime/Physics/Components/Phy_CCTComponent.h"
#include "Runtime/ECS/Components/TransformComponent.h"

#include "C_CombatContracts.h"
#include "C_CombatEventBus.h"
#include "C_ActionFsm.h"
#include "C_Fighter.h"
#include "C_CombatResolver.h"
#include "C_CombatApply.h"
#include "C_PlayerInputSourceComponent.h"
#include "C_BossBrainComponent.h"

namespace Alice
{
    struct C_CombatSessionComponent::SessionState
    {
        Combat::Fighter player{};
        Combat::Fighter boss{};
        Combat::ActionFsm playerFsm{};
        Combat::ActionFsm bossFsm{};
        Combat::CombatEventBus bus{};
        Combat::CombatResolver resolver{};
        Combat::CombatApply apply{};
        std::unordered_map<EntityId, Combat::Fighter*> fighterMap;
        Combat::ActionState prevPlayerState = Combat::ActionState::Idle;
        Combat::ActionState prevBossState = Combat::ActionState::Idle;

        void Init()
        {
            fighterMap.clear();
            player = Combat::Fighter{};
            boss = Combat::Fighter{};
            playerFsm.Reset();
            bossFsm.Reset();
            bus.ClearAll();
        }
    };

    REGISTER_SCRIPT(C_CombatSessionComponent);

    static IScript* FindScriptOnEntity(World& world, EntityId entityId, const char* name)
    {
        auto* scripts = world.GetScripts(entityId);
        if (!scripts)
            return nullptr;
        for (auto& sc : *scripts)
        {
            if (!sc.instance)
                continue;
            if (sc.scriptName == name)
                return sc.instance.get();
        }
        return nullptr;
    }

    static bool HasDeferredEvent(const Combat::ResolveOutput& resolved, Combat::CombatEventType type)
    {
        for (const auto& ev : resolved.deferred)
        {
            if (ev.type == type)
                return true;
        }
        return false;
    }

    static bool HitSortLess(const Combat::HitEvent& a, const Combat::HitEvent& b)
    {
        if (a.attackInstanceId != b.attackInstanceId)
            return a.attackInstanceId < b.attackInstanceId;
        if (a.attackerOwner != b.attackerOwner)
            return a.attackerOwner < b.attackerOwner;
        if (a.victimOwner != b.victimOwner)
            return a.victimOwner < b.victimOwner;

        if (a.hasSweepFraction != b.hasSweepFraction)
            return a.hasSweepFraction;
        if (a.hasSweepFraction && b.hasSweepFraction && a.sweepFraction != b.sweepFraction)
            return a.sweepFraction < b.sweepFraction;
        if (a.subShapeIndex != b.subShapeIndex)
            return a.subShapeIndex < b.subShapeIndex;
        if (a.hurtboxEntity != b.hurtboxEntity)
            return a.hurtboxEntity < b.hurtboxEntity;
        return a.part < b.part;
    }

    static void UpdateHealthHitInfo(World& world,
                                    const Combat::HitEvent& hit,
                                    const Combat::ResolveOutput& resolved,
                                    const Combat::FighterSnapshot& victim)
    {
        auto* hc = world.GetComponent<HealthComponent>(hit.victimOwner);
        if (!hc)
            return;

        hc->lastHitAttacker = hit.attackerOwner;
        hc->lastHitPart = hit.part;
        hc->lastHitPosWS = hit.hitPosWS;
        hc->lastHitNormalWS = hit.hitNormalWS;

        const bool wasHit = HasDeferredEvent(resolved, Combat::CombatEventType::OnHit);
        const bool wasGuard = HasDeferredEvent(resolved, Combat::CombatEventType::OnGuarded);
        const bool wasGuardBreak = HasDeferredEvent(resolved, Combat::CombatEventType::OnGuardBreak);
        const bool wasParry = HasDeferredEvent(resolved, Combat::CombatEventType::OnParried);

        if (wasHit || wasGuard || wasGuardBreak || wasParry)
            hc->hitThisFrame = true;

        if (wasGuard || wasGuardBreak || wasParry)
            hc->guardHitThisFrame = true;

        if (wasHit)
            hc->lastHitDamage = hit.damage;
        else
            hc->lastHitDamage = 0.0f;

        if (!wasHit && !wasGuard && !wasGuardBreak && !wasParry && victim.flags.invulnActive)
            hc->dodgeAvoidedThisFrame = true;
    }

    static Combat::ActionFlags BuildFlagsFromSensors(const Combat::Sensors& sensors,
                                                     Combat::ActionState state)
    {
        Combat::ActionFlags flags{};
        flags.hitActive = sensors.attackWindowActive;
        flags.guardActive = sensors.guardWindowActive;
        flags.invulnActive = sensors.dodgeWindowActive || sensors.invulnActive;
        flags.parryWindowActive = false;
        flags.canBeInterrupted = (state != Combat::ActionState::Dodge)
            && (state != Combat::ActionState::Hitstun)
            && (state != Combat::ActionState::Groggy)
            && (state != Combat::ActionState::Dead);
        return flags;
    }

    EntityId C_CombatSessionComponent::ResolveEntity(uint64_t guid) const
    {
        if (guid == 0)
            return InvalidEntityId;
        if (!GetWorld())
            return InvalidEntityId;
        return GetWorld()->FindEntityByGuid(guid);
    }

    void C_CombatSessionComponent::Start()
    {
        if (!m_state)
            m_state = std::make_unique<SessionState>();
        m_state->Init();

        if (auto* world = GetWorld())
            world->SetScriptCombatEnabled(true);
    }

    void C_CombatSessionComponent::OnEnable()
    {
        if (!m_state)
            m_state = std::make_unique<SessionState>();

        if (auto* world = GetWorld())
            world->SetScriptCombatEnabled(true);
    }

    void C_CombatSessionComponent::OnDisable()
    {
        if (m_state)
            m_state->Init();

        if (auto* world = GetWorld())
            world->SetScriptCombatEnabled(false);
    }

    void C_CombatSessionComponent::ForceReset()
    {
        if (m_state)
            m_state->Init();
    }

    void C_CombatSessionComponent::Update(float deltaTime)
    {
        if (!m_state || !GetWorld())
            return;

        World& world = *GetWorld();
        const EntityId playerId = ResolveEntity(m_playerGuid);
        const EntityId bossId = ResolveEntity(m_bossGuid);
        if (playerId == InvalidEntityId || bossId == InvalidEntityId)
            return;

        m_state->player.id = playerId;
        m_state->player.team = Combat::Team::Player;
        m_state->player.canBeHitstunned = true;
        m_state->boss.id = bossId;
        m_state->boss.team = Combat::Team::Enemy;
        m_state->boss.canBeHitstunned = false;

        m_state->fighterMap.clear();
        m_state->fighterMap[playerId] = &m_state->player;
        m_state->fighterMap[bossId] = &m_state->boss;

        Combat::Intent playerIntent{};
        if (auto* script = FindScriptOnEntity(world, playerId, "C_PlayerInputSourceComponent"))
        {
            if (auto* input = dynamic_cast<C_PlayerInputSourceComponent*>(script))
                playerIntent = input->GetIntent(deltaTime);
        }

        Combat::Intent bossIntent{};
        if (auto* script = FindScriptOnEntity(world, bossId, "C_BossBrainComponent"))
        {
            if (auto* brain = dynamic_cast<C_BossBrainComponent*>(script))
                bossIntent = brain->Think(deltaTime, playerId);
        }

        Combat::Sensors sPlayer = m_state->player.BuildSensors(world, bossId, deltaTime);
        Combat::Sensors sBoss = m_state->boss.BuildSensors(world, playerId, deltaTime);

        m_state->player.hp = sPlayer.hp;
        m_state->boss.hp = sBoss.hp;

        const auto& ePlayer = m_state->bus.PeekDeferred(playerId);
        const auto& eBoss = m_state->bus.PeekDeferred(bossId);

        auto outPlayer = m_state->playerFsm.Update(playerId, playerIntent, sPlayer, ePlayer, deltaTime);
        std::vector<Combat::CombatEvent> bossEvents;
        bossEvents.reserve(eBoss.size());
        for (const auto& ev : eBoss)
        {
            if (ev.type == Combat::CombatEventType::OnHit)
                continue;
            bossEvents.push_back(ev);
        }
        auto outBoss = m_state->bossFsm.Update(bossId, bossIntent, sBoss, bossEvents, deltaTime);

        m_state->player.state = outPlayer.state;
        m_state->player.flags = outPlayer.flags;
        m_state->boss.state = outBoss.state;
        m_state->boss.flags = outBoss.flags;

        m_state->bus.ClearDeferred(playerId);
        m_state->bus.ClearDeferred(bossId);

        auto ApplyMove = [&](const std::vector<Combat::Command>& cmds)
        {
            for (const auto& cmd : cmds)
            {
                if (cmd.type != Combat::CommandType::RequestMove)
                    continue;
                const auto payload = std::get<Combat::CmdRequestMove>(cmd.payload);
                auto* cct = world.GetComponent<Phy_CCTComponent>(payload.target);
                if (!cct)
                    continue;
                float dx = payload.move.x;
                float dz = payload.move.y;
                const float len = std::sqrt(dx * dx + dz * dz);
                if (len > 0.0001f)
                {
                    dx /= len;
                    dz /= len;
                }
                cct->desiredVelocity.x = dx * payload.speed;
                cct->desiredVelocity.z = dz * payload.speed;
                cct->desiredVelocity.y = 0.0f;
            }
        };
        ApplyMove(outPlayer.commands);
        ApplyMove(outBoss.commands);

        auto ApplyAnimByState = [&](EntityId entityId, Combat::ActionState curr, Combat::ActionState& prev) {
            if (curr == prev)
                return;

            auto* anim = world.GetComponent<AdvancedAnimationComponent>(entityId);
            auto* driver = world.GetComponent<AttackDriverComponent>(entityId);
            if (!anim || !driver)
            {
                prev = curr;
                return;
            }

            auto resolveClipByType = [&](AttackDriverNotifyType type) -> std::string {
                for (const auto& clip : driver->clips)
                {
                    if (!clip.enabled || clip.type != type)
                        continue;

                    switch (clip.source)
                    {
                    case AttackDriverClipSource::BaseA: return anim->base.clipA;
                    case AttackDriverClipSource::BaseB: return anim->base.clipB;
                    case AttackDriverClipSource::UpperA: return anim->upper.clipA;
                    case AttackDriverClipSource::UpperB: return anim->upper.clipB;
                    case AttackDriverClipSource::Additive: return anim->additive.clip;
                    case AttackDriverClipSource::Explicit:
                    default: return clip.clipName;
                    }
                }
                return {};
            };

            std::string clipName;
            bool loop = false;
            bool immediate = true;

            if (curr == Combat::ActionState::Attack)
            {
                clipName = resolveClipByType(AttackDriverNotifyType::Attack);
            }
            else if (curr == Combat::ActionState::Dodge)
            {
                clipName = resolveClipByType(AttackDriverNotifyType::Dodge);
            }
            else if (curr == Combat::ActionState::Guard)
            {
                clipName = resolveClipByType(AttackDriverNotifyType::Guard);
                loop = true;
                immediate = false;
            }

            if (!clipName.empty())
            {
                anim->enabled = true;
                anim->playing = true;
                anim->base.clipA = clipName;
                anim->base.autoAdvance = true;
                anim->base.loopA = loop;
                if (immediate)
                    anim->base.timeA = 0.0f;
            }

            prev = curr;
        };

        ApplyAnimByState(playerId, outPlayer.state, m_state->prevPlayerState);
        ApplyAnimByState(bossId, outBoss.state, m_state->prevBossState);
    }

    void C_CombatSessionComponent::PostCombatUpdate(float deltaTime)
    {
        if (!m_state || !GetWorld())
            return;

        World& world = *GetWorld();
        const EntityId playerId = ResolveEntity(m_playerGuid);
        const EntityId bossId = ResolveEntity(m_bossGuid);
        if (playerId == InvalidEntityId || bossId == InvalidEntityId)
            return;

        m_state->player.id = playerId;
        m_state->player.team = Combat::Team::Player;
        m_state->player.canBeHitstunned = true;
        m_state->boss.id = bossId;
        m_state->boss.team = Combat::Team::Enemy;
        m_state->boss.canBeHitstunned = false;

        m_state->bus.ClearFrame();
        if (world.HasFrameCombatHits())
        {
            std::vector<Combat::HitEvent> sortedHits = world.GetFrameCombatHits();
            std::sort(sortedHits.begin(), sortedHits.end(), HitSortLess);

            uint32_t lastAttackInstanceId = 0;
            EntityId lastAttacker = InvalidEntityId;
            EntityId lastVictim = InvalidEntityId;
            bool hasLast = false;

            for (const auto& hit : sortedHits)
            {
                const bool sameGroup = hasLast
                    && hit.attackInstanceId == lastAttackInstanceId
                    && hit.attackerOwner == lastAttacker
                    && hit.victimOwner == lastVictim;
                if (sameGroup)
                    continue;

                m_state->bus.PushHit(hit);
                hasLast = true;
                lastAttackInstanceId = hit.attackInstanceId;
                lastAttacker = hit.attackerOwner;
                lastVictim = hit.victimOwner;
            }
        }

        Combat::Sensors sPlayer = m_state->player.BuildSensors(world, bossId, deltaTime);
        Combat::Sensors sBoss = m_state->boss.BuildSensors(world, playerId, deltaTime);
        m_state->player.hp = sPlayer.hp;
        m_state->boss.hp = sBoss.hp;
        m_state->player.flags = BuildFlagsFromSensors(sPlayer, m_state->player.state);
        m_state->boss.flags = BuildFlagsFromSensors(sBoss, m_state->boss.state);

        bool bossGroggyTriggered = false;
        for (const auto& hit : m_state->bus.Hits())
        {
            Combat::FighterSnapshot attacker = (hit.attackerOwner == playerId)
                ? m_state->player.Snapshot()
                : m_state->boss.Snapshot();
            Combat::FighterSnapshot victim = (hit.victimOwner == playerId)
                ? m_state->player.Snapshot()
                : m_state->boss.Snapshot();

            auto resolved = m_state->resolver.ResolveOne(hit, attacker, victim);

            UpdateHealthHitInfo(world, hit, resolved, victim);
            m_state->apply.ApplyImmediate(world, m_state->fighterMap, m_state->bus, resolved.immediate, false);

            for (const auto& ev : resolved.deferred)
                m_state->bus.PushDeferred(ev);

            if (!bossGroggyTriggered && hit.victimOwner == bossId && hit.attackerOwner == playerId)
            {
                if (HasDeferredEvent(resolved, Combat::CombatEventType::OnHit))
                {
                    if (auto* hc = world.GetComponent<HealthComponent>(bossId))
                    {
                        if (hc->groggyMax > 0.0f && m_state->boss.state != Combat::ActionState::Groggy)
                        {
                            const float gainScale = (hc->groggyGainScale > 0.0f) ? hc->groggyGainScale : 0.0f;
                            const float gain = hit.damage * gainScale;
                            if (gain > 0.0f)
                                hc->groggy = std::min(hc->groggy + gain, hc->groggyMax);

                            if (hc->groggy >= hc->groggyMax)
                            {
                                hc->groggy = 0.0f;
                                bossGroggyTriggered = true;

                                std::vector<Combat::Command> groggyImmediate;
                                groggyImmediate.push_back({ Combat::CommandType::ForceCancelAttack, Combat::CmdForceCancelAttack{ bossId } });
                                groggyImmediate.push_back({ Combat::CommandType::DisableTrace, Combat::CmdDisableTrace{ bossId } });
                                m_state->apply.ApplyImmediate(world, m_state->fighterMap, m_state->bus, groggyImmediate, true);

                                m_state->bus.PushDeferred({ Combat::CombatEventType::OnGroggy, bossId, hit.attackerOwner, hit.attackInstanceId, 0.0f });
                            }
                        }
                    }
                }
            }
        }
    }
}
