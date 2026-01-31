#include "C_CombatSessionComponent.h"

#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <string>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/Components/IDComponent.h"
#include "Runtime/Gameplay/Combat/HealthComponent.h"
#include "Runtime/Gameplay/Combat/AttackDriverComponent.h"
#include "Runtime/Gameplay/Animation/AdvancedAnimationComponent.h"
#include "Runtime/Physics/Components/Phy_CCTComponent.h"
#include "Runtime/ECS/Components/TransformComponent.h"
//TODO : Include 확인해야함

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
        struct AnimOverrideState
        {
            bool saved = false;
            AdvancedAnimLayer savedBase{};
            bool overrideActive = false;
            bool blending = false;
            bool blendingToOverride = false;
            float blendTimer = 0.0f;
            std::string overrideClip{};
            bool overrideLoop = false;
        };

        Combat::Fighter player{};
        Combat::Fighter boss{};
        Combat::FighterSnapshot playerSnapshot{};
        Combat::FighterSnapshot bossSnapshot{};
        Combat::ActionFsm playerFsm{};
        Combat::ActionFsm bossFsm{};
        Combat::CombatEventBus bus{};
        Combat::CombatResolver resolver{};
        Combat::CombatApply apply{};
        std::unordered_map<EntityId, Combat::Fighter*> fighterMap;
        Combat::ActionState prevPlayerState = Combat::ActionState::Idle;
        Combat::ActionState prevBossState = Combat::ActionState::Idle;
        AnimOverrideState playerAnim{};
        AnimOverrideState bossAnim{};
        float playerMoveBlend = 0.0f;
        float bossMoveBlend = 0.0f;
        bool playerLockOnActive = false;
        EntityId playerLockOnTarget = InvalidEntityId;

        void Init()
        {
            fighterMap.clear();
            player = Combat::Fighter{};
            boss = Combat::Fighter{};
            playerSnapshot = Combat::FighterSnapshot{};
            bossSnapshot = Combat::FighterSnapshot{};
            playerFsm.Reset();
            bossFsm.Reset();
            bus.ClearAll();
            playerAnim = {};
            bossAnim = {};
            playerMoveBlend = 0.0f;
            bossMoveBlend = 0.0f;
            playerLockOnActive = false;
            playerLockOnTarget = InvalidEntityId;
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

    struct MoveBasis
    {
        bool valid = false;
        float forwardX = 0.0f;
        float forwardZ = 1.0f;
        float rightX = 1.0f;
        float rightZ = 0.0f;
    };

    static MoveBasis BuildYawBasis(float yawRad)
    {
        MoveBasis basis{};
        basis.forwardX = std::sin(yawRad);
        basis.forwardZ = std::cos(yawRad);
        basis.rightX = std::cos(yawRad);
        basis.rightZ = -std::sin(yawRad);
        basis.valid = true;
        return basis;
    }

    static EntityId ResolvePrimaryCamera(World& world)
    {
        for (auto&& [entityId, cam] : world.GetComponents<CameraComponent>())
        {
            if (cam.primary)
                return entityId;
        }

        auto go = world.FindGameObject("MainCamera");
        return go.IsValid() ? go.id() : InvalidEntityId;
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

    EntityId C_CombatSessionComponent::ResolveEntity(uint64_t guid) const
    {
        if (guid == 0)
            return InvalidEntityId;
        if (!GetWorld())
            return InvalidEntityId;
        return GetWorld()->FindEntityByGuid(guid);
    }

    EntityId C_CombatSessionComponent::ResolveEntityByName(const std::string& name) const
    {
        if (name.empty() || !GetWorld())
            return InvalidEntityId;
        auto go = GetWorld()->FindGameObject(name);
        return go.IsValid() ? go.id() : InvalidEntityId;
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
        EntityId playerId = ResolveEntity(m_playerGuid);
        EntityId bossId = ResolveEntity(m_bossGuid);
        if (playerId == InvalidEntityId && m_autoResolveByName)
            playerId = ResolveEntityByName(m_playerName);
        if (bossId == InvalidEntityId && m_autoResolveByName)
            bossId = ResolveEntityByName(m_bossName);
        if (playerId == InvalidEntityId || bossId == InvalidEntityId)
        {
            if (m_enableLogs)
            {
                ALICE_LOG_WARN("[CombatSession] Update skipped: player=%llu boss=%llu (guid=%llu/%llu name=%s/%s)",
                    static_cast<unsigned long long>(playerId),
                    static_cast<unsigned long long>(bossId),
                    static_cast<unsigned long long>(m_playerGuid),
                    static_cast<unsigned long long>(m_bossGuid),
                    m_playerName.c_str(),
                    m_bossName.c_str());
            }
            return;
        }

        m_state->player.id = playerId;
        m_state->player.team = Combat::Team::Player;
        m_state->player.canBeHitstunned = m_playerCanBeHitstunned;
        m_state->boss.id = bossId;
        m_state->boss.team = Combat::Team::Enemy;
        m_state->boss.canBeHitstunned = m_bossCanBeHitstunned;

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

        constexpr float kDegToRad = 0.01745329252f;

        const EntityId cameraId = ResolvePrimaryCamera(world);
        auto* camFollow = (cameraId != InvalidEntityId) ? world.GetComponent<CameraFollowComponent>(cameraId) : nullptr;
        auto* camLookAt = (cameraId != InvalidEntityId) ? world.GetComponent<CameraLookAtComponent>(cameraId) : nullptr;
        auto* camTr = (cameraId != InvalidEntityId) ? world.GetComponent<TransformComponent>(cameraId) : nullptr;

        MoveBasis camBasis{};
        if (cameraId != InvalidEntityId)
        {
            float yawRad = 0.0f;
            bool hasYaw = false;
            if (camFollow && camFollow->enabled)
            {
                yawRad = camFollow->yawDeg * kDegToRad;
                hasYaw = true;
            }
            else if (camTr)
            {
                yawRad = camTr->rotation.y;
                hasYaw = true;
            }
            if (hasYaw)
                camBasis = BuildYawBasis(yawRad);
        }

        const bool canLockOn = (camFollow && camFollow->enableLockOn);
        if (playerIntent.lockOnToggle && canLockOn)
        {
            if (m_state->playerLockOnActive)
            {
                m_state->playerLockOnActive = false;
                m_state->playerLockOnTarget = InvalidEntityId;
            }
            else
            {
                m_state->playerLockOnActive = true;
                m_state->playerLockOnTarget = bossId;
            }
        }

        if (m_state->playerLockOnActive)
            m_state->playerLockOnTarget = bossId;

        if (camFollow)
        {
            camFollow->lockOnActive = m_state->playerLockOnActive;
            if (m_state->playerLockOnActive)
            {
                camFollow->lockOnTargetId = m_state->playerLockOnTarget;
                camFollow->mode = 2;
            }
            else
            {
                camFollow->lockOnTargetId = InvalidEntityId;
                camFollow->mode = 0;
            }
        }

        if (camLookAt)
        {
            camLookAt->enabled = m_state->playerLockOnActive;
            if (m_state->playerLockOnActive)
                camLookAt->targetName = m_bossName.empty() ? std::string("Enemy") : m_bossName;
        }

        Combat::Sensors sPlayer = m_state->player.BuildSensors(world, bossId, deltaTime);
        Combat::Sensors sBoss = m_state->boss.BuildSensors(world, playerId, deltaTime);

        m_state->player.hp = sPlayer.hp;
        m_state->boss.hp = sBoss.hp;

        const auto& ePlayer = m_state->bus.PeekDeferred(playerId);
        const auto& eBoss = m_state->bus.PeekDeferred(bossId);

        auto outPlayer = m_state->playerFsm.Update(playerId, playerIntent, sPlayer, ePlayer, deltaTime);
        auto outBoss = m_state->bossFsm.Update(bossId, bossIntent, sBoss, eBoss, deltaTime);

        m_state->player.state = outPlayer.state;
        m_state->player.flags = outPlayer.flags;
        m_state->boss.state = outBoss.state;
        m_state->boss.flags = outBoss.flags;
        m_state->playerSnapshot = m_state->player.Snapshot();
        m_state->bossSnapshot = m_state->boss.Snapshot();

        m_state->bus.ClearDeferred(playerId);
        m_state->bus.ClearDeferred(bossId);

        auto ApplyMove = [&](EntityId entityId,
                             const Combat::Intent& intent,
                             const std::vector<Combat::Command>& cmds)
        {
            constexpr float kRadToDeg = 57.2957795f;
            const float offsetRad = m_rotationOffsetDeg * kDegToRad;

            for (const auto& cmd : cmds)
            {
                if (cmd.type != Combat::CommandType::RequestMove)
                    continue;
                const auto payload = std::get<Combat::CmdRequestMove>(cmd.payload);
                auto* cct = world.GetComponent<Phy_CCTComponent>(payload.target);
                if (!cct)
                {
                    if (m_enableLogs)
                    {
                        ALICE_LOG_WARN("[CombatSession] Missing CCT on entity=%llu",
                            static_cast<unsigned long long>(payload.target));
                    }
                    continue;
                }
                float inputX = payload.move.x;
                float inputZ = payload.move.y;
                float dx = inputX;
                float dz = inputZ;
                const bool isPlayer = (entityId == playerId);

                if (isPlayer && payload.useCameraRelative && camBasis.valid)
                {
                    dx = camBasis.rightX * inputX + camBasis.forwardX * inputZ;
                    dz = camBasis.rightZ * inputX + camBasis.forwardZ * inputZ;
                }

                const float len = std::sqrt(dx * dx + dz * dz);
                if (len > 0.0001f)
                {
                    dx /= len;
                    dz /= len;
                }
                cct->desiredVelocity.x = dx * payload.speed;
                cct->desiredVelocity.z = dz * payload.speed;
                cct->desiredVelocity.y = 0.0f;
                if (m_enableLogs && (dx != 0.0f || dz != 0.0f))
                {
                    ALICE_LOG_INFO("[CombatSession] Move entity=%llu dir(%.2f,%.2f) speed=%.2f",
                        static_cast<unsigned long long>(entityId),
                        dx, dz, payload.speed);
                }

                if (payload.faceMove && (dx != 0.0f || dz != 0.0f))
                {
                    if (auto* tr = world.GetComponent<TransformComponent>(entityId))
                    {
                        const float yawRad = std::atan2(dx, dz) + offsetRad;
                        tr->SetRotation(0.0f, yawRad * kRadToDeg, 0.0f);
                    }
                }
            }
        };
        ApplyMove(playerId, playerIntent, outPlayer.commands);
        ApplyMove(bossId, bossIntent, outBoss.commands);

        auto StopIfNotMoving = [&](EntityId entityId, Combat::ActionState state)
        {
            if (state == Combat::ActionState::Move || state == Combat::ActionState::Dodge)
                return;
            auto* cct = world.GetComponent<Phy_CCTComponent>(entityId);
            if (!cct)
                return;
            cct->desiredVelocity.x = 0.0f;
            cct->desiredVelocity.z = 0.0f;
            cct->desiredVelocity.y = 0.0f;
        };
        StopIfNotMoving(playerId, outPlayer.state);
        StopIfNotMoving(bossId, outBoss.state);

        if (m_enableLogs)
        {
            ALICE_LOG_INFO("[CombatSession] Player state=%u cmds=%zu",
                static_cast<unsigned>(outPlayer.state),
                outPlayer.commands.size());
        }

        auto ApplyTraceCommands = [&](const std::vector<Combat::Command>& cmds)
        {
            std::vector<Combat::Command> traceCmds;
            for (const auto& cmd : cmds)
            {
                if (cmd.type == Combat::CommandType::EnableTrace ||
                    cmd.type == Combat::CommandType::DisableTrace)
                {
                    traceCmds.push_back(cmd);
                }
            }
            if (!traceCmds.empty())
                m_state->apply.ApplyImmediate(world, m_state->fighterMap, m_state->bus, traceCmds, true);
        };
        ApplyTraceCommands(outPlayer.commands);
        ApplyTraceCommands(outBoss.commands);

        auto SmoothApproach = [&](float current, float target, float speed, float dt) {
            const float t = std::clamp(speed * dt, 0.0f, 1.0f);
            return current + (target - current) * t;
        };

        auto ApplyAnimByState = [&](EntityId entityId,
                                    Combat::ActionState curr,
                                    Combat::ActionState& prev,
                                    SessionState::AnimOverrideState& animState,
                                    float& moveBlend) {
            auto* anim = world.GetComponent<AdvancedAnimationComponent>(entityId);
            if (!anim)
                anim = &world.AddComponent<AdvancedAnimationComponent>(entityId);
            auto* driver = world.GetComponent<AttackDriverComponent>(entityId);
            if (!anim || !driver)
            {
                prev = curr;
                return;
            }

            anim->enabled = true;
            anim->playing = true;
            anim->upper.enabled = false;
            anim->additive.enabled = false;
            anim->ik.enabled = false;
            for (auto& ik : anim->ikChains)
                ik.enabled = false;

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

            auto ResolveOverrideSpeed = [&](EntityId targetId,
                                            Combat::ActionState state,
                                            const std::string& name) -> float
            {
                if (state != Combat::ActionState::Attack)
                    return 1.0f;
                if (targetId != playerId)
                    return 1.0f;
                if (m_attackSlowClipName.empty() || name != m_attackSlowClipName)
                    return 1.0f;
                return std::max(0.0f, m_attackSlowSpeed);
            };

            std::string clipName;
            bool loop = false;
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
            }

            const bool wantsOverride = !clipName.empty()
                && (curr == Combat::ActionState::Attack
                    || curr == Combat::ActionState::Dodge
                    || curr == Combat::ActionState::Guard);

            const bool isLocomotion = (curr == Combat::ActionState::Idle || curr == Combat::ActionState::Move);
            if (isLocomotion && !animState.overrideActive && !m_idleClip.empty())
            {
                const float targetBlend = (curr == Combat::ActionState::Move && !m_moveClip.empty()) ? 1.0f : 0.0f;
                moveBlend = SmoothApproach(moveBlend, targetBlend, m_moveBlendSpeed, deltaTime);

                anim->base.autoAdvance = true;
                anim->base.clipA = m_idleClip;
                anim->base.clipB = m_moveClip.empty() ? m_idleClip : m_moveClip;
                anim->base.loopA = true;
                anim->base.loopB = true;
                anim->base.speedA = 1.0f;
                anim->base.speedB = 1.0f;
                anim->base.blend01 = moveBlend;
            }

            const float overrideSpeed = ResolveOverrideSpeed(entityId, curr, clipName);

            auto BeginBlendToOverride = [&](const std::string& nextClip, bool nextLoop) {
                if (!animState.overrideActive)
                {
                    animState.savedBase = anim->base;
                    animState.saved = true;
                }

                animState.overrideActive = true;
                animState.overrideClip = nextClip;
                animState.overrideLoop = nextLoop;

                if (m_animBlendSec <= 0.0f)
                {
                    anim->base.autoAdvance = true;
                    anim->base.clipA = nextClip;
                    anim->base.clipB = nextClip;
                    anim->base.timeA = 0.0f;
                    anim->base.timeB = 0.0f;
                    anim->base.speedA = overrideSpeed;
                    anim->base.speedB = overrideSpeed;
                    anim->base.loopA = nextLoop;
                    anim->base.loopB = nextLoop;
                    anim->base.blend01 = 0.0f;
                    animState.blending = false;
                    animState.blendingToOverride = true;
                    return;
                }

                animState.blending = true;
                animState.blendingToOverride = true;
                animState.blendTimer = 0.0f;

                anim->base.autoAdvance = true;
                anim->base.clipB = nextClip;
                anim->base.timeB = 0.0f;
                anim->base.speedB = overrideSpeed;
                anim->base.loopB = nextLoop;
                anim->base.blend01 = 0.0f;
            };

            auto BeginBlendToSaved = [&]() {
                if (!animState.saved)
                {
                    animState.overrideActive = false;
                    animState.overrideClip.clear();
                    animState.blending = false;
                    return;
                }

                if (m_animBlendSec <= 0.0f)
                {
                    anim->base = animState.savedBase;
                    anim->base.timeA = 0.0f;
                    anim->base.timeB = 0.0f;
                    animState.overrideActive = false;
                    animState.overrideClip.clear();
                    animState.saved = false;
                    animState.blending = false;
                    return;
                }

                animState.blending = true;
                animState.blendingToOverride = false;
                animState.blendTimer = 0.0f;

                anim->base.autoAdvance = true;
                anim->base.clipB = animState.savedBase.clipA;
                anim->base.timeB = 0.0f;
                anim->base.speedB = animState.savedBase.speedA;
                anim->base.loopB = animState.savedBase.loopA;
                anim->base.blend01 = 0.0f;
            };

            auto StepBlend = [&]() {
                if (!animState.blending || m_animBlendSec <= 0.0f)
                    return;

                animState.blendTimer += deltaTime;
                float alpha = animState.blendTimer / m_animBlendSec;
                if (alpha > 1.0f)
                    alpha = 1.0f;

                anim->base.blend01 = alpha;

                if (alpha >= 1.0f)
                {
                    if (animState.blendingToOverride)
                    {
                        anim->base.clipA = animState.overrideClip;
                        anim->base.timeA = anim->base.timeB;
                        anim->base.speedA = anim->base.speedB;
                        anim->base.loopA = animState.overrideLoop;
                        anim->base.clipB = animState.overrideClip;
                        anim->base.timeB = anim->base.timeA;
                        anim->base.blend01 = 0.0f;
                        animState.blending = false;
                    }
                    else
                    {
                        anim->base = animState.savedBase;
                        anim->base.timeA = 0.0f;
                        anim->base.timeB = 0.0f;
                        animState.overrideActive = false;
                        animState.overrideClip.clear();
                        animState.saved = false;
                        animState.blending = false;
                    }
                }
            };

            if (wantsOverride)
            {
                const bool clipChanged = !animState.overrideActive
                    || animState.overrideClip != clipName
                    || animState.overrideLoop != loop;
                if (clipChanged)
                    BeginBlendToOverride(clipName, loop);
            }
            else if (animState.overrideActive)
            {
                if (!animState.blending || animState.blendingToOverride)
                    BeginBlendToSaved();
            }

            StepBlend();
            if (animState.overrideActive && !animState.blending)
            {
                anim->base.speedA = overrideSpeed;
                anim->base.speedB = overrideSpeed;
            }
            prev = curr;
        };

        ApplyAnimByState(playerId, outPlayer.state, m_state->prevPlayerState, m_state->playerAnim, m_state->playerMoveBlend);
        ApplyAnimByState(bossId, outBoss.state, m_state->prevBossState, m_state->bossAnim, m_state->bossMoveBlend);
    }

    void C_CombatSessionComponent::PostCombatUpdate(float deltaTime)
    {
        if (!m_state || !GetWorld())
            return;

        World& world = *GetWorld();
        EntityId playerId = ResolveEntity(m_playerGuid);
        EntityId bossId = ResolveEntity(m_bossGuid);
        if (playerId == InvalidEntityId && m_autoResolveByName)
            playerId = ResolveEntityByName(m_playerName);
        if (bossId == InvalidEntityId && m_autoResolveByName)
            bossId = ResolveEntityByName(m_bossName);
        if (playerId == InvalidEntityId || bossId == InvalidEntityId)
            return;

        m_state->player.id = playerId;
        m_state->player.team = Combat::Team::Player;
        m_state->player.canBeHitstunned = m_playerCanBeHitstunned;
        m_state->boss.id = bossId;
        m_state->boss.team = Combat::Team::Enemy;
        m_state->boss.canBeHitstunned = m_bossCanBeHitstunned;

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

        bool bossGroggyTriggered = false;
        for (const auto& hit : m_state->bus.Hits())
        {
            const Combat::FighterSnapshot& playerSnap = m_state->playerSnapshot;
            const Combat::FighterSnapshot& bossSnap = m_state->bossSnapshot;
            Combat::FighterSnapshot attacker = (hit.attackerOwner == playerId) ? playerSnap : bossSnap;
            Combat::FighterSnapshot victim = (hit.victimOwner == playerId) ? playerSnap : bossSnap;

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
