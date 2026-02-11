#include "CombatVfxBridgeScript.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cmath>
#include <cctype>

#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Gameplay/Combat/AttackDriverComponent.h"
#include "Runtime/Gameplay/Combat/WeaponTraceComponent.h"
#include "Runtime/Rendering/Components/ComputeEffectComponent.h"
#include "Runtime/Rendering/Components/UnityVfxComponent.h"
#include "Runtime/Resources/Prefab.h"
#include "Runtime/Scripting/ScriptFactory.h"

#include "../Combat/C_CombatContracts.h"
#include "../Combat/C_CombatResolver.h"
#include "../Combat/C_CombatSessionComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(CombatVfxBridgeScript);

    namespace
    {
        using namespace DirectX;

        constexpr float kVectorEpsilonSq = 1e-6f;
        const XMFLOAT3 kTintWhite(1.0f, 1.0f, 1.0f);
        const XMFLOAT3 kTintRage(1.0f, 0.0f, 0.0f);
        const XMFLOAT3 kTintSparkYellow(1.0f, 0.47058824f, 0.0f);
        const XMFLOAT3 kTintGuardRingOrange(1.0f, 0.45f, 0.0f);
        const XMFLOAT3 kTintParryRingYellow(1.0f, 0.85f, 0.1f);
        const XMFLOAT3 kTintBossGroggyRingRed(1.0f, 0.1f, 0.1f);
        constexpr float kRingOverlayStartScaleRatio = 1.0f / 6.0f;
        constexpr float kAttack3ExtraSlashLateralOffset = 0.25f;
        constexpr float kAttack3ExtraSlashRollOffsetDeg = 20.0f;
        constexpr float kRedHitPivotYOffset = 1.5f;

        int ClampInt(int v, int minV, int maxV)
        {
            if (v < minV)
                return minV;
            if (v > maxV)
                return maxV;
            return v;
        }

        bool ContainsCaseInsensitive(std::string value, const std::string& needle)
        {
            if (needle.empty())
                return false;

            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            std::string lowerNeedle = needle;
            std::transform(lowerNeedle.begin(), lowerNeedle.end(), lowerNeedle.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });

            return value.find(lowerNeedle) != std::string::npos;
        }

        struct Basis
        {
            XMFLOAT3 right{ 1.0f, 0.0f, 0.0f };
            XMFLOAT3 up{ 0.0f, 1.0f, 0.0f };
            XMFLOAT3 forward{ 0.0f, 0.0f, 1.0f };
        };

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

        float LengthSq(const XMFLOAT3& v)
        {
            return v.x * v.x + v.y * v.y + v.z * v.z;
        }

        XMFLOAT3 Add(const XMFLOAT3& a, const XMFLOAT3& b)
        {
            return XMFLOAT3(a.x + b.x, a.y + b.y, a.z + b.z);
        }

        XMFLOAT3 Sub(const XMFLOAT3& a, const XMFLOAT3& b)
        {
            return XMFLOAT3(a.x - b.x, a.y - b.y, a.z - b.z);
        }

        XMFLOAT3 Mul(const XMFLOAT3& v, float s)
        {
            return XMFLOAT3(v.x * s, v.y * s, v.z * s);
        }

        XMFLOAT3 Cross(const XMFLOAT3& a, const XMFLOAT3& b)
        {
            const XMVECTOR va = XMLoadFloat3(&a);
            const XMVECTOR vb = XMLoadFloat3(&b);
            XMFLOAT3 out{};
            XMStoreFloat3(&out, XMVector3Cross(va, vb));
            return out;
        }

        XMFLOAT3 NormalizeOrFallback(const XMFLOAT3& v, const XMFLOAT3& fallback)
        {
            if (LengthSq(v) <= kVectorEpsilonSq)
                return fallback;

            XMFLOAT3 out{};
            XMStoreFloat3(&out, XMVector3Normalize(XMLoadFloat3(&v)));
            return out;
        }

        XMFLOAT3 QuaternionToYPR_Rad(FXMVECTOR q)
        {
            XMFLOAT4 qq{};
            XMStoreFloat4(&qq, q);
            const float x = qq.x;
            const float y = qq.y;
            const float z = qq.z;
            const float w = qq.w;

            const float sinp = 2.0f * (w * x - y * z);
            const float pitch = (std::abs(sinp) >= 1.0f) ? std::copysign(XM_PIDIV2, sinp) : std::asin(sinp);

            const float sinyCosp = 2.0f * (w * y + x * z);
            const float cosyCosp = 1.0f - 2.0f * (x * x + y * y);
            const float yaw = std::atan2(sinyCosp, cosyCosp);

            const float sinrCosp = 2.0f * (w * z + x * y);
            const float cosrCosp = 1.0f - 2.0f * (x * x + z * z);
            const float roll = std::atan2(sinrCosp, cosrCosp);

            return XMFLOAT3(pitch, yaw, roll);
        }

        Basis BuildBasis(const XMFLOAT3& forward, const XMFLOAT3& upHint)
        {
            const XMFLOAT3 fallbackForward(0.0f, 0.0f, 1.0f);
            const XMFLOAT3 fallbackUp(0.0f, 1.0f, 0.0f);
            const XMFLOAT3 fallbackRight(1.0f, 0.0f, 0.0f);

            Basis basis{};
            basis.forward = NormalizeOrFallback(forward, fallbackForward);

            XMFLOAT3 up = NormalizeOrFallback(upHint, fallbackUp);
            XMFLOAT3 right = Cross(up, basis.forward);
            if (LengthSq(right) <= kVectorEpsilonSq)
                right = Cross(fallbackUp, basis.forward);
            if (LengthSq(right) <= kVectorEpsilonSq)
                right = Cross(fallbackRight, basis.forward);
            basis.right = NormalizeOrFallback(right, fallbackRight);
            basis.up = NormalizeOrFallback(Cross(basis.forward, basis.right), fallbackUp);
            return basis;
        }


        XMFLOAT3 RotateLocalOffset(const XMFLOAT3& local, const Basis& basis)
        {
            const XMFLOAT3 xTerm = Mul(basis.right, local.x);
            const XMFLOAT3 yTerm = Mul(basis.up, local.y);
            const XMFLOAT3 zTerm = Mul(basis.forward, local.z);
            return Add(Add(xTerm, yTerm), zTerm);
        }

        XMFLOAT3 RotateAroundAxis(const XMFLOAT3& v, const XMFLOAT3& axis, float rad)
        {
            if (std::abs(rad) <= 1e-6f)
                return v;

            const XMVECTOR q = XMQuaternionRotationAxis(XMLoadFloat3(&axis), rad);
            XMFLOAT3 out{};
            XMStoreFloat3(&out, XMVector3Rotate(XMLoadFloat3(&v), q));
            return out;
        }

        Basis ApplyEulerOffsetDegLocal(const Basis& input, const XMFLOAT3& offsetDeg)
        {
            Basis out = input;

            const float pitch = XMConvertToRadians(offsetDeg.x);
            const float yaw = XMConvertToRadians(offsetDeg.y);
            const float roll = XMConvertToRadians(offsetDeg.z);

            if (std::abs(pitch) > 1e-6f)
            {
                const XMFLOAT3 axis = NormalizeOrFallback(out.right, XMFLOAT3(1.0f, 0.0f, 0.0f));
                out.up = RotateAroundAxis(out.up, axis, pitch);
                out.forward = RotateAroundAxis(out.forward, axis, pitch);
            }

            if (std::abs(yaw) > 1e-6f)
            {
                const XMFLOAT3 axis = NormalizeOrFallback(out.up, XMFLOAT3(0.0f, 1.0f, 0.0f));
                out.right = RotateAroundAxis(out.right, axis, yaw);
                out.forward = RotateAroundAxis(out.forward, axis, yaw);
            }

            if (std::abs(roll) > 1e-6f)
            {
                const XMFLOAT3 axis = NormalizeOrFallback(out.forward, XMFLOAT3(0.0f, 0.0f, 1.0f));
                out.right = RotateAroundAxis(out.right, axis, roll);
                out.up = RotateAroundAxis(out.up, axis, roll);
            }

            return BuildBasis(out.forward, out.up);
        }

        XMFLOAT3 RotationRadFromBasis(const Basis& basis)
        {
            const XMMATRIX rot = XMMATRIX(
                XMVectorSet(basis.right.x, basis.right.y, basis.right.z, 0.0f),
                XMVectorSet(basis.up.x, basis.up.y, basis.up.z, 0.0f),
                XMVectorSet(basis.forward.x, basis.forward.y, basis.forward.z, 0.0f),
                XMVectorSet(0.0f, 0.0f, 0.0f, 1.0f));

            return QuaternionToYPR_Rad(XMQuaternionRotationMatrix(rot));
        }

        bool DecomposeWorldMatrix(const XMMATRIX& worldMatrix, XMFLOAT3& outPos, XMFLOAT3& outRotRad, XMFLOAT3& outScale)
        {
            XMVECTOR scale{};
            XMVECTOR rotQ{};
            XMVECTOR trans{};
            if (!XMMatrixDecompose(&scale, &rotQ, &trans, worldMatrix))
                return false;

            XMStoreFloat3(&outPos, trans);
            XMStoreFloat3(&outScale, scale);
            outRotRad = QuaternionToYPR_Rad(rotQ);
            return true;
        }

    }

    using namespace DirectX;

    void CombatVfxBridgeScript::Start()
    {
        ResolveSessionAndActors(true);
        TryBindResolveDelegate();
        EnsurePoolRoot();

        if (m_session)
        {
            const Combat::ActionState state = m_session->GetPlayerState();
            const Combat::ActionFlags flags = m_session->GetPlayerFlags();
            m_prevIsLightAttack = (state == Combat::ActionState::Attack)
                && !(flags.chargeActive && flags.chargeLevel > 0);
            m_prevPlayerHitActive = flags.hitActive;
            m_prevBossGroggy = (m_session->GetBossState() == Combat::ActionState::Groggy);
        }
        else
        {
            m_prevIsLightAttack = false;
            m_prevPlayerHitActive = false;
            m_slashSignalOrdinalInAttack = 0;
            m_prevBossGroggy = false;
        }
        m_prevAttackTraceMask = 0u;
        m_lastAttackSlotIndex = -1;
        m_prevSlashWindowActive = IsSlashWindowActive();

        UpdatePathCacheAndPools();
        UpdateSocketTracking();
        ProcessSlashFromTraceSignals(false);
    }

    void CombatVfxBridgeScript::Update(float deltaTime)
    {
        ResolveSessionAndActors(false);
        TryBindResolveDelegate();
        UpdatePathCacheAndPools();
        UpdateSocketTracking();

        if (m_session)
        {
            const bool bossGroggyNow = (m_session->GetBossState() == Combat::ActionState::Groggy);
            if (!m_prevBossGroggy && bossGroggyNow)
            {
                XMFLOAT3 bossPointPos{};
                if (World* world = GetWorld(); world && m_bossId != InvalidEntityId)
                {
                    DirectX::XMFLOAT4X4 bossWorld{};
                    XMStoreFloat4x4(&bossWorld, world->ComputeWorldMatrix(m_bossId));
                    bossPointPos = XMFLOAT3(bossWorld._41, bossWorld._42, bossWorld._43);
                }
                if (!TryGetBossEffectPointPosition(bossPointPos))
                    ResolveBossEffectPointEntity(true);
                SpawnHitOverlayAtPosition(bossPointPos, OverlaySpawnKind::BossGroggyRing, -1, false);
            }
            m_prevBossGroggy = bossGroggyNow;
        }
        else
        {
            m_prevBossGroggy = false;
        }

        const bool slashWindowActive = IsSlashWindowActive();
        if (m_prevSlashWindowActive && !slashWindowActive)
            DetachActiveSlashInstances();

        if (m_session)
        {
            const Combat::ActionState playerState = m_session->GetPlayerState();
            const Combat::ActionFlags playerFlags = m_session->GetPlayerFlags();
            const bool isLightAttack = (playerState == Combat::ActionState::Attack)
                && !(playerFlags.chargeActive && playerFlags.chargeLevel > 0);
            const int comboIndex = playerFlags.attackComboIndex;
            const bool hasTraceSignals = ProcessSlashFromTraceSignals(isLightAttack);

            XMFLOAT3 tracePos{};
            XMFLOAT3 traceForward{};
            XMFLOAT3 traceUp{};
            const bool hasTracePose = TryGetTracePoseWorld(tracePos, traceForward, traceUp);
            const bool attackStarted = isLightAttack && (!m_prevIsLightAttack || !m_attackTraceActive || (comboIndex != m_attackTraceComboIndex));

            if (attackStarted)
            {
                m_attackTraceActive = true;
                m_attackTraceComboIndex = comboIndex;
                m_slashSignalOrdinalInAttack = 0;
                m_lastAttackSlotIndex = -1;
                m_slashSpawnedThisAttack = false;
                m_attackElapsedSec = 0.0f;
                m_attackDurationSec = ResolvePlayerAttackDurationSec();
                m_hasAttackStartPos = hasTracePose;
                if (hasTracePose)
                {
                    m_attackStartPos = tracePos;
                    m_attackStartUp = traceUp;
                }
            }
            else if (isLightAttack && m_attackTraceActive)
            {
                m_attackElapsedSec += (std::max)(0.0f, deltaTime);
                if (m_attackDurationSec <= 0.0f)
                    m_attackDurationSec = ResolvePlayerAttackDurationSec();
            }

            if (!hasTraceSignals && isLightAttack && m_attackTraceActive && !m_slashSpawnedThisAttack)
            {
                const float phase = std::clamp(Get_slashSpawnPhase(), 0.0f, 1.0f);
                const bool hasDuration = (m_attackDurationSec > 0.0f);
                const bool byPhase = hasDuration && (m_attackElapsedSec >= (m_attackDurationSec * phase));
                const bool byWindowFallback = !hasDuration && (playerFlags.hitActive && !m_prevPlayerHitActive);
                if (byPhase || byWindowFallback)
                {
                    SpawnSlashFromAttackWindow();
                    m_slashSpawnedThisAttack = true;
                }
            }

            if (!isLightAttack)
            {
                DetachActiveSlashInstances();
                m_attackTraceActive = false;
                m_attackTraceComboIndex = -1;
                m_slashSignalOrdinalInAttack = 0;
                m_prevAttackTraceMask = 0u;
                m_lastAttackSlotIndex = -1;
                m_slashSpawnedThisAttack = false;
                m_attackElapsedSec = 0.0f;
                m_attackDurationSec = 0.0f;
                m_hasAttackStartPos = false;
            }

            m_prevIsLightAttack = isLightAttack;
            m_prevPlayerHitActive = playerFlags.hitActive;
            m_prevSlashWindowActive = isLightAttack ? slashWindowActive : false;
        }
        else
        {
            if (m_prevSlashWindowActive)
                DetachActiveSlashInstances();
            m_prevIsLightAttack = false;
            m_prevPlayerHitActive = false;
            m_attackTraceActive = false;
            m_attackTraceComboIndex = -1;
            m_slashSignalOrdinalInAttack = 0;
            m_prevAttackTraceMask = 0u;
            m_lastAttackSlotIndex = -1;
            m_slashSpawnedThisAttack = false;
            m_attackElapsedSec = 0.0f;
            m_attackDurationSec = 0.0f;
            m_hasAttackStartPos = false;
            m_traceAttackInstanceSeen.clear();
            m_prevSlashWindowActive = false;
        }

        UpdateComputeOneShotEmission(deltaTime);
        UpdateActive(deltaTime);
    }

    void CombatVfxBridgeScript::OnDisable()
    {
        DeactivateAllActive();
        m_computeOneShotStates.clear();
        UnbindResolveDelegateSafe();
        m_session = nullptr;
        m_shockWaveId = InvalidEntityId;
        m_guardShockPointId = InvalidEntityId;
        m_bossEffectPointId = InvalidEntityId;

        m_prevIsLightAttack = false;
        m_prevPlayerHitActive = false;
        m_prevBossGroggy = false;
        m_hasPrevSocketPos = false;
        m_attackTraceActive = false;
        m_attackTraceComboIndex = -1;
        m_slashSignalOrdinalInAttack = 0;
        m_prevAttackTraceMask = 0u;
        m_lastAttackSlotIndex = -1;
        m_slashSpawnedThisAttack = false;
        m_attackElapsedSec = 0.0f;
        m_attackDurationSec = 0.0f;
        m_hasAttackStartPos = false;
        m_traceAttackInstanceSeen.clear();
        m_prevSlashWindowActive = false;
    }

    void CombatVfxBridgeScript::OnDestroy()
    {
        OnDisable();

        for (int slot = 0; slot < SlotCount; ++slot)
            ClearSlot(slot);
        ClearPoolRoot();
    }

    void CombatVfxBridgeScript::ResolveSessionAndActors(bool logWarnings)
    {
        World* world = GetWorld();
        if (!world)
        {
            UnbindResolveDelegateSafe();
            m_session = nullptr;
            m_playerId = InvalidEntityId;
            m_bossId = InvalidEntityId;
            m_shockWaveId = InvalidEntityId;
            m_guardShockPointId = InvalidEntityId;
            m_bossEffectPointId = InvalidEntityId;
            return;
        }

        C_CombatSessionComponent* resolvedSession = FindSession(*world, Get_sessionEntityName());
        if (resolvedSession != m_session)
        {
            UnbindResolveDelegateSafe();
            m_session = resolvedSession;
        }

        if (!m_session && logWarnings && !m_warnedMissingSession)
        {
            ALICE_LOG_WARN("[CombatVfxBridge] Missing combat session entity/script: %s", Get_sessionEntityName().c_str());
            m_warnedMissingSession = true;
        }

        std::string playerName = Get_playerEntityName();
        if (playerName.empty() && m_session)
            playerName = m_session->Get_m_playerName();

        std::string bossName = Get_bossEntityName();
        if (bossName.empty() && m_session)
            bossName = m_session->Get_m_bossName();

        const EntityId prevPlayerId = m_playerId;
        const EntityId prevBossId = m_bossId;
        GameObject playerGo = playerName.empty() ? GameObject{} : world->FindGameObject(playerName);
        GameObject bossGo = bossName.empty() ? GameObject{} : world->FindGameObject(bossName);
        m_playerId = playerGo.IsValid() ? playerGo.id() : InvalidEntityId;
        m_bossId = bossGo.IsValid() ? bossGo.id() : InvalidEntityId;
        if (m_playerId != prevPlayerId)
        {
            m_guardShockPointId = InvalidEntityId;
        }
        if (m_bossId != prevBossId)
            m_bossEffectPointId = InvalidEntityId;

        if (m_playerId == InvalidEntityId && logWarnings && !m_warnedMissingPlayer)
        {
            ALICE_LOG_WARN("[CombatVfxBridge] Missing player entity: %s", playerName.c_str());
            m_warnedMissingPlayer = true;
        }
        if (m_bossId == InvalidEntityId && logWarnings && !m_warnedMissingBoss)
        {
            ALICE_LOG_WARN("[CombatVfxBridge] Missing boss entity: %s", bossName.c_str());
            m_warnedMissingBoss = true;
        }
    }

    EntityId CombatVfxBridgeScript::ResolveShockWaveEntity(bool logWarnings)
    {
        World* world = GetWorld();
        if (!world)
        {
            m_shockWaveId = InvalidEntityId;
            return InvalidEntityId;
        }

        if (m_shockWaveId != InvalidEntityId && world->GetComponent<TransformComponent>(m_shockWaveId))
            return m_shockWaveId;

        const std::array<std::string, 2> candidates{
            Get_shockWaveEntityName(),
            "ShockWave"
        };

        EntityId resolved = InvalidEntityId;
        for (const std::string& candidate : candidates)
        {
            if (candidate.empty())
                continue;

            GameObject go = world->FindGameObject(candidate);
            if (go.IsValid())
            {
                resolved = go.id();
                break;
            }
        }

        if (resolved == InvalidEntityId)
        {
            if (logWarnings && !m_warnedMissingShockWave)
            {
                ALICE_LOG_WARN("[CombatVfxBridge] ShockWave entity not found. preferred=%s",
                               Get_shockWaveEntityName().c_str());
                m_warnedMissingShockWave = true;
            }
            m_shockWaveId = InvalidEntityId;
            return InvalidEntityId;
        }

        m_shockWaveId = resolved;
        return m_shockWaveId;
    }

    EntityId CombatVfxBridgeScript::ResolveGuardShockPointEntity(bool logWarnings)
    {
        World* world = GetWorld();
        if (!world)
        {
            m_guardShockPointId = InvalidEntityId;
            return InvalidEntityId;
        }

        if (m_guardShockPointId != InvalidEntityId && world->GetComponent<TransformComponent>(m_guardShockPointId))
            return m_guardShockPointId;

        const std::array<std::string, 2> candidates{
            Get_guardShockPointEntityName(),
            "PlayerEffectPoint"
        };

        EntityId resolved = InvalidEntityId;
        if (m_playerId != InvalidEntityId)
        {
            const auto children = world->GetChildren(m_playerId);
            for (const std::string& candidate : candidates)
            {
                if (candidate.empty())
                    continue;

                for (EntityId child : children)
                {
                    if (world->GetEntityName(child) == candidate)
                    {
                        resolved = child;
                        break;
                    }
                }

                if (resolved != InvalidEntityId)
                    break;
            }
        }

        if (resolved == InvalidEntityId)
        {
            for (const std::string& candidate : candidates)
            {
                if (candidate.empty())
                    continue;

                GameObject go = world->FindGameObject(candidate);
                if (go.IsValid())
                {
                    resolved = go.id();
                    break;
                }
            }
        }

        if (resolved == InvalidEntityId)
        {
            if (logWarnings && !m_warnedMissingGuardShockPoint)
            {
                ALICE_LOG_WARN("[CombatVfxBridge] Guard shock point not found. preferred=%s",
                               Get_guardShockPointEntityName().c_str());
                m_warnedMissingGuardShockPoint = true;
            }
            m_guardShockPointId = InvalidEntityId;
            return InvalidEntityId;
        }

        m_guardShockPointId = resolved;
        return m_guardShockPointId;
    }

    EntityId CombatVfxBridgeScript::ResolveBossEffectPointEntity(bool logWarnings)
    {
        World* world = GetWorld();
        if (!world)
        {
            m_bossEffectPointId = InvalidEntityId;
            return InvalidEntityId;
        }

        if (m_bossEffectPointId != InvalidEntityId && world->GetComponent<TransformComponent>(m_bossEffectPointId))
            return m_bossEffectPointId;

        const std::array<std::string, 2> candidates{
            Get_bossEffectPointEntityName(),
            "BossEffectPoint"
        };

        EntityId resolved = InvalidEntityId;
        if (m_bossId != InvalidEntityId)
        {
            const auto children = world->GetChildren(m_bossId);
            for (const std::string& candidate : candidates)
            {
                if (candidate.empty())
                    continue;

                for (EntityId child : children)
                {
                    if (world->GetEntityName(child) == candidate)
                    {
                        resolved = child;
                        break;
                    }
                }

                if (resolved != InvalidEntityId)
                    break;
            }
        }

        if (resolved == InvalidEntityId)
        {
            for (const std::string& candidate : candidates)
            {
                if (candidate.empty())
                    continue;

                GameObject go = world->FindGameObject(candidate);
                if (go.IsValid())
                {
                    resolved = go.id();
                    break;
                }
            }
        }

        if (resolved == InvalidEntityId)
        {
            if (logWarnings && !m_warnedMissingBossEffectPoint)
            {
                ALICE_LOG_WARN("[CombatVfxBridge] Boss effect point not found. preferred=%s",
                               Get_bossEffectPointEntityName().c_str());
                m_warnedMissingBossEffectPoint = true;
            }
            m_bossEffectPointId = InvalidEntityId;
            return InvalidEntityId;
        }

        m_bossEffectPointId = resolved;
        return m_bossEffectPointId;
    }

    bool CombatVfxBridgeScript::TryGetGuardShockWavePosition(DirectX::XMFLOAT3& outPos)
    {
        World* world = GetWorld();
        if (!world)
            return false;

        const EntityId pointId = ResolveGuardShockPointEntity(false);
        if (pointId == InvalidEntityId)
            return false;

        DirectX::XMFLOAT4X4 wm{};
        XMStoreFloat4x4(&wm, world->ComputeWorldMatrix(pointId));
        outPos = XMFLOAT3(wm._41, wm._42, wm._43);
        return true;
    }

    bool CombatVfxBridgeScript::TryGetBossEffectPointPosition(DirectX::XMFLOAT3& outPos)
    {
        World* world = GetWorld();
        if (!world)
            return false;

        const EntityId pointId = ResolveBossEffectPointEntity(false);
        if (pointId == InvalidEntityId)
            return false;

        DirectX::XMFLOAT4X4 wm{};
        XMStoreFloat4x4(&wm, world->ComputeWorldMatrix(pointId));
        outPos = XMFLOAT3(wm._41, wm._42, wm._43);
        return true;
    }

    void CombatVfxBridgeScript::TriggerShockWaveAtPosition(const DirectX::XMFLOAT3& spawnPos,
                                                           bool spawnShockWave,
                                                           bool spawnShockBlast)
    {
        World* world = GetWorld();
        if (!world)
            return;

        if (spawnShockWave)
        {
            bool spawnedFromPool = false;
            const EntityId shockWavePooledId = Acquire(ShockWaveSlot, true);
            if (shockWavePooledId != InvalidEntityId)
            {
                if (auto* tr = world->GetComponent<TransformComponent>(shockWavePooledId))
                {
                    tr->position = spawnPos;
                    tr->enabled = true;
                    tr->visible = true;
                    world->MarkTransformDirty(shockWavePooledId);
                }

                float shockWaveLifeSec = ResolveSpawnLifetimeSec(ShockWaveSlot, -1);
                const float oneShotKeepAliveSec = ComputeOneShotKeepAliveSec(shockWavePooledId);
                if (oneShotKeepAliveSec > 0.0f)
                    shockWaveLifeSec = std::max(shockWaveLifeSec, oneShotKeepAliveSec + 0.02f);

                m_active[ShockWaveSlot].push_back({ shockWavePooledId, shockWaveLifeSec, shockWaveLifeSec, false, InvalidEntityId });
                spawnedFromPool = true;
            }

            if (!spawnedFromPool)
            {
                const EntityId shockWaveId = ResolveShockWaveEntity(true);
                if (shockWaveId != InvalidEntityId)
                {
                    if (auto* tr = world->GetComponent<TransformComponent>(shockWaveId))
                    {
                        tr->position = spawnPos;
                        tr->enabled = true;
                        tr->visible = true;
                        world->MarkTransformDirty(shockWaveId);
                    }

                    SetEntityActiveRecursive(*world, shockWaveId, true, true);
                }
            }
        }

        if (!spawnShockBlast)
            return;

        const EntityId shockBlastId = Acquire(ShockBlastSlot, true);
        if (shockBlastId == InvalidEntityId)
            return;

        if (auto* tr = world->GetComponent<TransformComponent>(shockBlastId))
        {
            tr->position = spawnPos;
            tr->enabled = true;
            tr->visible = true;
            world->MarkTransformDirty(shockBlastId);
        }

        float shockBlastLifeSec = ResolveSpawnLifetimeSec(ShockBlastSlot, -1);
        const float oneShotKeepAliveSec = ComputeOneShotKeepAliveSec(shockBlastId);
        if (oneShotKeepAliveSec > 0.0f)
            shockBlastLifeSec = std::max(shockBlastLifeSec, oneShotKeepAliveSec + 0.02f);

        m_active[ShockBlastSlot].push_back({ shockBlastId, shockBlastLifeSec, shockBlastLifeSec, false, InvalidEntityId });
    }

    void CombatVfxBridgeScript::TryBindResolveDelegate()
    {
        if (!m_session || m_resolveBound)
            return;

        m_session->OnCombatResolvedVfx.BindObject(this, &CombatVfxBridgeScript::OnCombatResolve);
        m_resolveBound = true;
    }

    void CombatVfxBridgeScript::UnbindResolveDelegateSafe()
    {
        if (!m_resolveBound)
            return;

        World* world = GetWorld();
        if (world)
        {
            if (C_CombatSessionComponent* session = FindSession(*world, Get_sessionEntityName()))
                session->OnCombatResolvedVfx.Unbind();
        }

        m_resolveBound = false;
    }

    bool CombatVfxBridgeScript::IsSlashWindowActive()
    {
        World* world = GetWorld();
        if (!world || m_playerId == InvalidEntityId)
            return false;

        if (Get_useAttackDriverSlotSignals())
        {
            const auto* driver = world->GetComponent<AttackDriverComponent>(m_playerId);
            return (driver != nullptr) && (driver->attackTraceMaskActive != 0u);
        }

        if (!m_session)
            return false;

        const Combat::ActionFlags flags = m_session->GetPlayerFlags();
        return flags.hitActive;
    }

    void CombatVfxBridgeScript::DetachActiveSlashInstances()
    {
        for (int slot = SlashDefaultSlot; slot <= SlashStep6Slot; ++slot)
        {
            for (ActiveInstance& inst : m_active[slot])
                FreezeSlashInstanceToWorldAnchor(inst);
        }
    }

    void CombatVfxBridgeScript::FreezeSlashInstanceToWorldAnchor(ActiveInstance& inst)
    {
        World* world = GetWorld();
        if (!world || inst.id == InvalidEntityId)
            return;

        TransformComponent* tr = world->GetComponent<TransformComponent>(inst.id);
        if (!tr)
            return;

        // Slash is spawned under player. Once detached to a freeze anchor,
        // parent is no longer player and this block won't run again.
        const EntityId oldParent = tr->parent;
        if (oldParent == InvalidEntityId || oldParent != m_playerId)
            return;

        const EntityId anchor = world->CreateEmpty();
        if (anchor == InvalidEntityId)
        {
            world->SetParent(inst.id, InvalidEntityId, true);
            return;
        }

        world->SetEntityName(anchor, "__CombatVfxFreezeAnchor");
        world->SetParent(anchor, InvalidEntityId, false);

        bool anchorPoseReady = false;
        if (const auto* parentTr = world->GetComponent<TransformComponent>(oldParent))
        {
            if (parentTr->parent == InvalidEntityId)
            {
                if (auto* anchorTr = world->GetComponent<TransformComponent>(anchor))
                {
                    anchorTr->position = parentTr->position;
                    anchorTr->rotation = parentTr->rotation;
                    anchorTr->scale = parentTr->scale;
                    world->MarkTransformDirty(anchor);
                    anchorPoseReady = true;
                }
            }
        }

        if (!anchorPoseReady)
        {
            XMFLOAT3 worldPos{};
            XMFLOAT3 worldRot{};
            XMFLOAT3 worldScale{};
            const XMMATRIX parentWorld = world->ComputeWorldMatrix(oldParent);
            if (DecomposeWorldMatrix(parentWorld, worldPos, worldRot, worldScale))
            {
                if (auto* anchorTr = world->GetComponent<TransformComponent>(anchor))
                {
                    anchorTr->position = worldPos;
                    anchorTr->rotation = worldRot;
                    anchorTr->scale = worldScale;
                    world->MarkTransformDirty(anchor);
                    anchorPoseReady = true;
                }
            }
        }

        if (!anchorPoseReady)
        {
            world->DestroyEntity(anchor);
            world->SetParent(inst.id, InvalidEntityId, true);
            return;
        }

        world->SetParent(inst.id, anchor, false);

        if (inst.freezeAnchor != InvalidEntityId && inst.freezeAnchor != anchor)
            world->DestroyEntity(inst.freezeAnchor);
        inst.freezeAnchor = anchor;
    }

    void CombatVfxBridgeScript::UpdatePathCacheAndPools()
    {
        const bool forceBuildPrewarm = IsGameMode();
        for (int slot = 0; slot < SlotCount; ++slot)
        {
            const std::string path = GetPrefabPath(slot);
            if (path != m_cachedPaths[slot])
            {
                ClearSlot(slot);
                m_cachedPaths[slot] = path;

                if ((Get_prewarm() || forceBuildPrewarm) && !path.empty())
                    PrewarmSlot(slot);
            }

            if (path.empty())
            {
                if (slot == SlashDefaultSlot && !m_warnedMissingSlashPrefabPath)
                {
                    ALICE_LOG_WARN("[CombatVfxBridge] slashPrefabPath is empty.");
                    m_warnedMissingSlashPrefabPath = true;
                }
                if (slot == HitSlot && !m_warnedMissingHitPrefabPath)
                {
                    ALICE_LOG_WARN("[CombatVfxBridge] hitPrefabPath is empty.");
                    m_warnedMissingHitPrefabPath = true;
                }
            }
        }
    }

    void CombatVfxBridgeScript::UpdateSocketTracking()
    {
        XMFLOAT3 trackPos{};
        XMFLOAT3 trackForward{};
        XMFLOAT3 trackUp{};

        const bool hasTrackingPose = TryGetTracePoseWorld(trackPos, trackForward, trackUp);

        if (!hasTrackingPose)
        {
            m_hasPrevSocketPos = false;
            return;
        }

        if (!m_hasPrevSocketPos)
        {
            m_prevSocketPos = trackPos;
            m_currSocketPos = trackPos;
            m_hasPrevSocketPos = true;
            return;
        }

        m_prevSocketPos = m_currSocketPos;
        m_currSocketPos = trackPos;
    }

    void CombatVfxBridgeScript::UpdateActive(float deltaTime)
    {
        World* world = GetWorld();
        for (int slot = 0; slot < SlotCount; ++slot)
        {
            const bool isRingOverlaySlot = (slot == HitOverlayGuardRingSlot
                || slot == HitOverlayGuardBreakRingSlot
                || slot == HitOverlayParryRingSlot
                || slot == HitOverlayBossGroggyRingSlot);
            auto& activeList = m_active[slot];
            for (size_t i = 0; i < activeList.size();)
            {
                ActiveInstance& inst = activeList[i];
                if (inst.id == InvalidEntityId)
                {
                    activeList.erase(activeList.begin() + static_cast<std::ptrdiff_t>(i));
                    continue;
                }

                inst.remainingSec -= deltaTime;
                if (isRingOverlaySlot && world && inst.totalSec > 0.0f)
                {
                    if (auto* tr = world->GetComponent<TransformComponent>(inst.id))
                    {
                        XMFLOAT3 baseScale = tr->scale;
                        const auto itScale = m_cachedBaseScales[slot].find(inst.id);
                        if (itScale != m_cachedBaseScales[slot].end())
                            baseScale = itScale->second;

                        const float clampedRemaining = std::clamp(inst.remainingSec, 0.0f, inst.totalSec);
                        const float elapsedSec = inst.totalSec - clampedRemaining;
                        const float growDurationSec = std::max(0.0001f, inst.totalSec * 0.5f);
                        const float growT = std::clamp(elapsedSec / growDurationSec, 0.0f, 1.0f);
                        const float scaleRatio = kRingOverlayStartScaleRatio
                            + (1.0f - kRingOverlayStartScaleRatio) * growT;

                        tr->scale = XMFLOAT3(
                            baseScale.x * scaleRatio,
                            baseScale.y * scaleRatio,
                            baseScale.z * scaleRatio);
                        world->MarkTransformDirty(inst.id);
                    }
                }

                if (inst.fadeOut && world && inst.totalSec > 0.0f)
                {
                    const float clampedRemaining = std::clamp(inst.remainingSec, 0.0f, inst.totalSec);
                    const float alphaRatio = clampedRemaining / inst.totalSec;
                    ApplyEntityAlphaFadeRecursive(*world, slot, inst.id, alphaRatio);
                }

                if (inst.remainingSec <= 0.0f)
                {
                    Release(slot, inst.id);
                    if (world && inst.freezeAnchor != InvalidEntityId)
                        world->DestroyEntity(inst.freezeAnchor);
                    inst.freezeAnchor = InvalidEntityId;
                    activeList.erase(activeList.begin() + static_cast<std::ptrdiff_t>(i));
                    continue;
                }

                ++i;
            }
        }
    }

    void CombatVfxBridgeScript::DeactivateAllActive()
    {
        World* world = GetWorld();
        for (int slot = 0; slot < SlotCount; ++slot)
        {
            for (const ActiveInstance& inst : m_active[slot])
            {
                if (inst.id != InvalidEntityId)
                    Release(slot, inst.id);
                if (world && inst.freezeAnchor != InvalidEntityId)
                    world->DestroyEntity(inst.freezeAnchor);
            }
            m_active[slot].clear();
        }
    }

    void CombatVfxBridgeScript::CollectPlayerTraceEntities(std::vector<EntityId>& out) const
    {
        out.clear();

        World* world = GetWorld();
        if (!world || m_playerId == InvalidEntityId)
            return;

        auto pushUniqueTrace = [&](EntityId id) {
            if (id == InvalidEntityId)
                return;
            if (!world->GetComponent<WeaponTraceComponent>(id))
                return;
            if (std::find(out.begin(), out.end(), id) != out.end())
                return;
            out.push_back(id);
        };

        if (const auto* driver = world->GetComponent<AttackDriverComponent>(m_playerId))
        {
            if (driver->traceGuid != 0)
            {
                const EntityId slot0 = world->FindEntityByGuid(driver->traceGuid);
                pushUniqueTrace(slot0);
            }
            else
            {
                // traceGuid==0 means owner/self trace slot in attack driver convention.
                pushUniqueTrace(m_playerId);
            }

            for (std::uint64_t guid : driver->traceGuids)
            {
                if (guid == 0)
                    continue;
                pushUniqueTrace(world->FindEntityByGuid(guid));
            }
        }

        if (!out.empty())
            return;

        // Name fallback when attack driver trace GUID wiring is missing.
        const std::string traceName = Get_weaponTraceEntityName();
        if (traceName.empty())
            return;

        GameObject traceGo = world->FindGameObject(traceName);
        if (!traceGo.IsValid())
            return;
        pushUniqueTrace(traceGo.id());
    }

    bool CombatVfxBridgeScript::ProcessSlashFromTraceSignals(bool allowSpawn)
    {
        World* world = GetWorld();
        if (!world || m_playerId == InvalidEntityId)
            return false;

        if (Get_useAttackDriverSlotSignals())
        {
            const auto* driver = world->GetComponent<AttackDriverComponent>(m_playerId);
            if (!driver)
            {
                m_prevAttackTraceMask = 0u;
                return false;
            }

            const std::uint32_t currentMask = driver->attackTraceMaskActive;
            const std::uint32_t risingMask = currentMask & ~m_prevAttackTraceMask;
            m_prevAttackTraceMask = currentMask;

            if (allowSpawn && risingMask != 0u)
            {
                for (int bit = 0; bit < 32; ++bit)
                {
                    const std::uint32_t bitMask = (1u << bit);
                    if ((risingMask & bitMask) == 0u)
                        continue;

                    const int attackSlotIndex = bit + 1; // bit0 -> slot1
                    m_lastAttackSlotIndex = attackSlotIndex;
                    SpawnSlashFromAttackWindow(attackSlotIndex);
                }
            }
            return true;
        }

        std::vector<EntityId> traces;
        CollectPlayerTraceEntities(traces);
        if (traces.empty())
        {
            m_traceAttackInstanceSeen.clear();
            return false;
        }

        for (auto it = m_traceAttackInstanceSeen.begin(); it != m_traceAttackInstanceSeen.end();)
        {
            if (std::find(traces.begin(), traces.end(), it->first) == traces.end())
                it = m_traceAttackInstanceSeen.erase(it);
            else
                ++it;
        }

        for (EntityId traceId : traces)
        {
            const auto* trace = world->GetComponent<WeaponTraceComponent>(traceId);
            if (!trace)
                continue;

            const std::uint32_t currentAttackInstance = trace->attackInstanceId;
            auto [it, inserted] = m_traceAttackInstanceSeen.emplace(traceId, currentAttackInstance);
            if (inserted)
                continue;

            if (it->second == currentAttackInstance)
                continue;

            it->second = currentAttackInstance;
            if (allowSpawn && trace->active)
                SpawnSlashFromAttackWindow();
        }

        return true;
    }

    void CombatVfxBridgeScript::SpawnSlashFromAttackWindow(int attackSlotIndex)
    {
        int resolvedAttackSlotIndex = attackSlotIndex;
        int slashSlot = SlashDefaultSlot;
        if (resolvedAttackSlotIndex > 0)
        {
            slashSlot = ResolveSlashSlotForAttackSlotIndex(resolvedAttackSlotIndex);
        }
        else
        {
            const int slashSignalOrdinal = (std::max)(1, m_slashSignalOrdinalInAttack + 1);
            m_slashSignalOrdinalInAttack = slashSignalOrdinal;
            m_lastAttackSlotIndex = slashSignalOrdinal;
            resolvedAttackSlotIndex = slashSignalOrdinal;
            slashSlot = ResolveSlashSlotForAttackSlotIndex(slashSignalOrdinal);
        }

        XMFLOAT3 slashOffsetLocal = Get_slashOffsetLocal();
        XMFLOAT3 slashRotationOffsetDeg = Get_slashRotationOffsetDeg();
        float slashScaleMul = GetScaleMulSafe(slashSlot);
        if (Get_useSlashSlotTransformTuning() && resolvedAttackSlotIndex > 0)
        {
            slashRotationOffsetDeg = Add(slashRotationOffsetDeg, GetSlashSlotRotationOffsetDeg(resolvedAttackSlotIndex));
            slashScaleMul *= GetSlashSlotScaleMul(resolvedAttackSlotIndex);
        }

        XMFLOAT3 anchorLocal(0.0f, Get_slashAnchorPlayerYOffset(), Get_slashAnchorPlayerForwardOffset());
        if (Get_useSlashSlotTransformTuning() && resolvedAttackSlotIndex > 0)
            anchorLocal = GetSlashSlotAnchorOffsetLocal(resolvedAttackSlotIndex);

        World* world = GetWorld();
        if (!world)
            return;

        const bool isHeavySlash = IsHeavyAttackSlotIndex(resolvedAttackSlotIndex);
        const bool applyRageTint = ShouldApplyRageTint(resolvedAttackSlotIndex);
        const bool heavyFade = Get_enableHeavyFadeOut() && isHeavySlash;
        const float lifeSec = ResolveSpawnLifetimeSec(slashSlot, resolvedAttackSlotIndex);
        const bool isAttack3 = (resolvedAttackSlotIndex == 3);
        const std::array<float, 3> lateralOffsets{
            0.0f,
            -kAttack3ExtraSlashLateralOffset,
            kAttack3ExtraSlashLateralOffset
        };
        const std::array<float, 3> rollOffsetsDeg{
            0.0f,
            -kAttack3ExtraSlashRollOffsetDeg,
            kAttack3ExtraSlashRollOffsetDeg
        };
        const std::array<int, 2> attack3SpawnOffsetIndices{ 0, 2 }; // center + right
        const int spawnCount = isAttack3 ? static_cast<int>(attack3SpawnOffsetIndices.size()) : 1;

        for (int spawnIndex = 0; spawnIndex < spawnCount; ++spawnIndex)
        {
            const EntityId id = Acquire(slashSlot);
            if (id == InvalidEntityId)
            {
                if (spawnIndex == 0)
                    return;
                continue;
            }

            if (heavyFade)
                SetEntityLoopModeRecursive(*world, id, true);
            SetEntityColorTintRecursive(*world, id, applyRageTint ? kTintRage : kTintWhite);

            if (m_playerId != InvalidEntityId)
                world->SetParent(id, m_playerId, false);
            else
                world->SetParent(id, InvalidEntityId, false);

            TransformComponent* tr = world->GetComponent<TransformComponent>(id);
            if (!tr)
            {
                Release(slashSlot, id);
                continue;
            }

            XMFLOAT3 spawnAnchorLocal = anchorLocal;
            const int offsetIndex = isAttack3 ? attack3SpawnOffsetIndices[spawnIndex] : 0;
            spawnAnchorLocal.x += lateralOffsets[offsetIndex];
            const XMFLOAT3 localPos = Add(spawnAnchorLocal, slashOffsetLocal);
            XMFLOAT3 spawnRotationOffsetDeg = slashRotationOffsetDeg;
            spawnRotationOffsetDeg.z += rollOffsetsDeg[offsetIndex];
            tr->position = localPos;
            tr->rotation = XMFLOAT3(
                XMConvertToRadians(spawnRotationOffsetDeg.x),
                XMConvertToRadians(spawnRotationOffsetDeg.y),
                XMConvertToRadians(spawnRotationOffsetDeg.z));

            XMFLOAT3 baseScale = tr->scale;
            const auto itScale = m_cachedBaseScales[slashSlot].find(id);
            if (itScale != m_cachedBaseScales[slashSlot].end())
                baseScale = itScale->second;
            const float safeScaleMul = (std::max)(0.0f, slashScaleMul);
            tr->scale = XMFLOAT3(baseScale.x * safeScaleMul, baseScale.y * safeScaleMul, baseScale.z * safeScaleMul);
            world->MarkTransformDirty(id);

            m_active[slashSlot].push_back({ id, lifeSec, lifeSec, heavyFade, InvalidEntityId });

            // Heavy slash should leave player immediately to keep a stable world afterimage.
            if (isHeavySlash)
                FreezeSlashInstanceToWorldAnchor(m_active[slashSlot].back());
        }
    }

    void CombatVfxBridgeScript::SpawnHitFromResolve(const DirectX::XMFLOAT3& hitPos,
                                                    EntityId victimId,
                                                    int attackSlotIndex)
    {
        World* world = GetWorld();
        const int hitSlot = ResolveHitSlotForAttackSlotIndex(attackSlotIndex);
        if (!world)
            return;

        const bool heavyFade = Get_enableHeavyFadeOut() && IsHeavyAttackSlotIndex(attackSlotIndex);
        const bool applyRageTint = ShouldApplyRageTint(attackSlotIndex);
        const float lifeSec = ResolveSpawnLifetimeSec(hitSlot, attackSlotIndex);

        XMFLOAT3 forward = NormalizeOrFallback(GetPlayerForward(), XMFLOAT3(0.0f, 0.0f, 1.0f));
        XMFLOAT3 upHint(0.0f, 1.0f, 0.0f);
        if (m_playerId != InvalidEntityId)
        {
            XMFLOAT4X4 wm{};
            XMStoreFloat4x4(&wm, world->ComputeWorldMatrix(m_playerId));
            forward = NormalizeOrFallback(XMFLOAT3(wm._31, wm._32, wm._33), forward);
            upHint = NormalizeOrFallback(XMFLOAT3(wm._21, wm._22, wm._23), XMFLOAT3(0.0f, 1.0f, 0.0f));
        }
        if (LengthSq(Cross(upHint, forward)) <= kVectorEpsilonSq)
            upHint = XMFLOAT3(0.0f, 0.0f, 1.0f);

        XMFLOAT3 spawnPos = hitPos;
        const std::string hitPrefabPath = GetPrefabPath(hitSlot);
        const bool isRedHitEffect =
            ContainsCaseInsensitive(hitPrefabPath, "red_hit")
            || ContainsCaseInsensitive(hitPrefabPath, "red hit");
        const XMFLOAT3 hitTint = applyRageTint
            ? kTintRage
            : (isRedHitEffect ? kTintWhite : kTintSparkYellow);
        if (isRedHitEffect && victimId != InvalidEntityId)
        {
            DirectX::XMFLOAT4X4 victimWorld{};
            XMStoreFloat4x4(&victimWorld, world->ComputeWorldMatrix(victimId));
            spawnPos = XMFLOAT3(victimWorld._41, victimWorld._42 + kRedHitPivotYOffset, victimWorld._43);
        }

        XMFLOAT3 hitRotationOffsetDeg{};
        if (isRedHitEffect)
        {
            // Red hit uses dedicated hit rotation, and faces player at spawn time.
            hitRotationOffsetDeg = Get_hitRotationOffsetDeg();
            if (m_playerId != InvalidEntityId)
            {
                DirectX::XMFLOAT4X4 playerWorld{};
                XMStoreFloat4x4(&playerWorld, world->ComputeWorldMatrix(m_playerId));
                const XMFLOAT3 toPlayer(
                    playerWorld._41 - spawnPos.x,
                    (playerWorld._42 + kRedHitPivotYOffset) - spawnPos.y,
                    playerWorld._43 - spawnPos.z);
                if (LengthSq(toPlayer) > kVectorEpsilonSq)
                    forward = NormalizeOrFallback(toPlayer, forward);
            }
        }
        else
        {
            // Keep legacy white-hit orientation behavior.
            hitRotationOffsetDeg = Get_slashRotationOffsetDeg();
            if (Get_useSlashSlotTransformTuning() && attackSlotIndex > 0)
                hitRotationOffsetDeg = Add(hitRotationOffsetDeg, GetSlashSlotRotationOffsetDeg(attackSlotIndex));
        }

        const bool isAttack3 = (attackSlotIndex == 3);
        const std::array<float, 2> attack3HitRollOffsetsDeg{
            0.0f,
            kAttack3ExtraSlashRollOffsetDeg
        };
        const int spawnCount = isAttack3 ? static_cast<int>(attack3HitRollOffsetsDeg.size()) : 1;

        for (int spawnIndex = 0; spawnIndex < spawnCount; ++spawnIndex)
        {
            const EntityId id = Acquire(hitSlot);
            if (id == InvalidEntityId)
            {
                if (spawnIndex == 0)
                    return;
                continue;
            }

            if (heavyFade)
                SetEntityLoopModeRecursive(*world, id, true);
            SetEntityColorTintRecursive(*world, id, hitTint);

            XMFLOAT3 spawnRotationOffsetDeg = hitRotationOffsetDeg;
            if (!isRedHitEffect && isAttack3)
                spawnRotationOffsetDeg.z += attack3HitRollOffsetsDeg[spawnIndex];

            ApplySpawnTransformTuned(hitSlot,
                                     id,
                                     spawnPos,
                                     forward,
                                     upHint,
                                     GetOffsetLocal(hitSlot),
                                     spawnRotationOffsetDeg,
                                     GetScaleMulSafe(hitSlot));
            m_active[hitSlot].push_back({ id, lifeSec, lifeSec, heavyFade, InvalidEntityId });
        }
        // Heavy attack does not spawn spark overlay.
        if (!IsHeavyAttackSlotIndex(attackSlotIndex))
            SpawnHitOverlayAtPosition(hitPos, OverlaySpawnKind::Spark, attackSlotIndex, applyRageTint);
    }

    void CombatVfxBridgeScript::SpawnHitOverlayAtPosition(const DirectX::XMFLOAT3& spawnPos,
                                                          CombatVfxBridgeScript::OverlaySpawnKind kind,
                                                          int attackSlotIndex,
                                                          bool applyRageTint)
    {
        int overlaySlot = HitOverlaySparkSlot;
        switch (kind)
        {
        case OverlaySpawnKind::Spark:
            overlaySlot = HitOverlaySparkSlot;
            if (Get_hitOverlayPrefabPath().empty())
                return;
            break;
        case OverlaySpawnKind::Guard:
            overlaySlot = HitOverlayGuardSlot;
            if (Get_guardOverlayPrefabPath().empty())
                return;
            break;
        case OverlaySpawnKind::Damage:
            overlaySlot = HitOverlayDamageSlot;
            if (Get_damageOverlayPrefabPath().empty())
                return;
            break;
        case OverlaySpawnKind::GuardRing:
            overlaySlot = HitOverlayGuardRingSlot;
            if (Get_guardRingOverlayPrefabPath().empty())
                return;
            break;
        case OverlaySpawnKind::GuardBreakRing:
            overlaySlot = HitOverlayGuardBreakRingSlot;
            if (Get_guardBreakRingOverlayPrefabPath().empty())
                return;
            break;
        case OverlaySpawnKind::ParryRing:
            overlaySlot = HitOverlayParryRingSlot;
            if (Get_parryRingOverlayPrefabPath().empty())
                return;
            break;
        case OverlaySpawnKind::BossGroggyRing:
            overlaySlot = HitOverlayBossGroggyRingSlot;
            if (Get_bossGroggyRingOverlayPrefabPath().empty())
                return;
            break;
        default:
            return;
        }

        World* world = GetWorld();
        if (!world)
            return;

        const bool isRingOverlayKind = (kind == OverlaySpawnKind::GuardRing
            || kind == OverlaySpawnKind::GuardBreakRing
            || kind == OverlaySpawnKind::ParryRing
            || kind == OverlaySpawnKind::BossGroggyRing);
        const bool useOneShotActivation = !isRingOverlayKind;
        const EntityId overlayId = Acquire(overlaySlot, useOneShotActivation);
        if (overlayId == InvalidEntityId)
            return;

        XMFLOAT3 forward = NormalizeOrFallback(GetPlayerForward(), XMFLOAT3(0.0f, 0.0f, 1.0f));
        XMFLOAT3 upHint(0.0f, 1.0f, 0.0f);
        if (m_playerId != InvalidEntityId)
        {
            XMFLOAT4X4 wm{};
            XMStoreFloat4x4(&wm, world->ComputeWorldMatrix(m_playerId));
            forward = NormalizeOrFallback(XMFLOAT3(wm._31, wm._32, wm._33), forward);
            upHint = NormalizeOrFallback(XMFLOAT3(wm._21, wm._22, wm._23), XMFLOAT3(0.0f, 1.0f, 0.0f));
        }
        if (LengthSq(Cross(upHint, forward)) <= kVectorEpsilonSq)
            upHint = XMFLOAT3(0.0f, 0.0f, 1.0f);

        XMFLOAT3 overlayTint = applyRageTint ? kTintRage : kTintWhite;
        switch (kind)
        {
        case OverlaySpawnKind::GuardRing:
        case OverlaySpawnKind::GuardBreakRing:
            overlayTint = kTintGuardRingOrange;
            break;
        case OverlaySpawnKind::ParryRing:
            overlayTint = kTintParryRingYellow;
            break;
        case OverlaySpawnKind::BossGroggyRing:
            overlayTint = kTintBossGroggyRingRed;
            break;
        default:
            break;
        }

        float overlayLifeSec = ResolveSpawnLifetimeSec(overlaySlot, attackSlotIndex);
        SetEntityColorTintRecursive(*world, overlayId, overlayTint);
        EntityId overlayAnchor = InvalidEntityId;
        if (isRingOverlayKind)
        {
            overlayAnchor = world->CreateEmpty();
            if (overlayAnchor != InvalidEntityId)
            {
                world->SetEntityName(
                    overlayAnchor,
                    (kind == OverlaySpawnKind::BossGroggyRing) ? "__BossGroggyRingAnchor" : "__HitOverlayRingAnchor");
                world->SetParent(overlayAnchor, InvalidEntityId, false);
                if (auto* anchorTr = world->GetComponent<TransformComponent>(overlayAnchor))
                {
                    anchorTr->position = spawnPos;
                    anchorTr->rotation = XMFLOAT3(0.0f, 0.0f, 0.0f);
                    anchorTr->scale = XMFLOAT3(1.0f, 1.0f, 1.0f);
                    world->MarkTransformDirty(overlayAnchor);
                }
                world->SetParent(overlayId, overlayAnchor, false);
            }

        }

        if (kind == OverlaySpawnKind::BossGroggyRing)
        {
            // Keep prefab-authored rotation for groggy ring.
            if (auto* tr = world->GetComponent<TransformComponent>(overlayId))
            {
                tr->position = (overlayAnchor != InvalidEntityId)
                    ? XMFLOAT3(0.0f, 0.0f, 0.0f)
                    : spawnPos;

                XMFLOAT3 baseScale = tr->scale;
                const auto itScale = m_cachedBaseScales[overlaySlot].find(overlayId);
                if (itScale != m_cachedBaseScales[overlaySlot].end())
                    baseScale = itScale->second;

                const float safeScaleMul = (std::max)(0.0f, GetScaleMulSafe(overlaySlot));
                tr->scale = XMFLOAT3(baseScale.x * safeScaleMul, baseScale.y * safeScaleMul, baseScale.z * safeScaleMul);
                world->MarkTransformDirty(overlayId);
            }
        }
        else
        {
            const XMFLOAT3 applyAnchorPos = (overlayAnchor != InvalidEntityId)
                ? XMFLOAT3(0.0f, 0.0f, 0.0f)
                : spawnPos;
            ApplySpawnTransformTuned(overlaySlot,
                                     overlayId,
                                     applyAnchorPos,
                                     forward,
                                     upHint,
                                     GetOffsetLocal(overlaySlot),
                                     GetRotationOffsetDeg(overlaySlot),
                                     GetScaleMulSafe(overlaySlot));
        }

        // One-shot compute effects should stay alive for emission window + particle lifetime.
        // This keeps already-emitted particles visible even after emission has stopped.
        float requiredComputeLifeSec = 0.0f;
        std::vector<EntityId> stack;
        stack.push_back(overlayId);
        while (!stack.empty())
        {
            const EntityId current = stack.back();
            stack.pop_back();

            if (const auto* compute = world->GetComponent<ComputeEffectComponent>(current))
            {
                if (!compute->loop)
                {
                    const float emissionSec = std::max(0.0f, compute->emissionDurationSec);
                    const float particleLifeSec = std::max(0.0f, std::max(compute->lifeMin, compute->lifeMax));
                    requiredComputeLifeSec = std::max(requiredComputeLifeSec, emissionSec + particleLifeSec);
                }
            }

            const auto children = world->GetChildren(current);
            for (EntityId child : children)
                stack.push_back(child);
        }

        if (requiredComputeLifeSec > 0.0f)
            overlayLifeSec = std::max(overlayLifeSec, requiredComputeLifeSec + 0.02f);

        if (isRingOverlayKind)
        {
            if (auto* tr = world->GetComponent<TransformComponent>(overlayId))
            {
                XMFLOAT3 baseScale = tr->scale;
                const auto itScale = m_cachedBaseScales[overlaySlot].find(overlayId);
                if (itScale != m_cachedBaseScales[overlaySlot].end())
                    baseScale = itScale->second;

                tr->scale = XMFLOAT3(
                    baseScale.x * kRingOverlayStartScaleRatio,
                    baseScale.y * kRingOverlayStartScaleRatio,
                    baseScale.z * kRingOverlayStartScaleRatio);
                world->MarkTransformDirty(overlayId);
            }
        }

        m_active[overlaySlot].push_back({ overlayId, overlayLifeSec, overlayLifeSec, false, overlayAnchor });
    }

    void CombatVfxBridgeScript::OnCombatResolve(EntityId victimId,
                                                EntityId attackerId,
                                                std::uint8_t resolveResult,
                                                float damage,
                                                const DirectX::XMFLOAT3& hitPos)
    {
        (void)damage;

        if (m_playerId == InvalidEntityId || m_bossId == InvalidEntityId)
            ResolveSessionAndActors(false);

        const Combat::ResolveResult result = static_cast<Combat::ResolveResult>(resolveResult);
        const bool playerHitBoss = (attackerId == m_playerId && victimId == m_bossId);
        const bool bossHitPlayer = (attackerId == m_bossId && victimId == m_playerId);

        if (result == Combat::ResolveResult::Hit)
        {
            if (playerHitBoss)
            {
                int attackSlotIndex = ResolveCurrentAttackSlotIndexFromDriverMask();
                if (attackSlotIndex <= 0)
                    attackSlotIndex = m_lastAttackSlotIndex;
                else
                    m_lastAttackSlotIndex = attackSlotIndex;
                SpawnHitFromResolve(hitPos, victimId, attackSlotIndex);
                return;
            }

            if (bossHitPlayer)
            {
                TriggerShockWaveAtPosition(hitPos, true, false);
                return;
            }

            return;
        }

        if (result == Combat::ResolveResult::Guard && bossHitPlayer)
        {
            XMFLOAT3 shockPos = hitPos;
            if (!TryGetGuardShockWavePosition(shockPos))
                ResolveGuardShockPointEntity(true);
            TriggerShockWaveAtPosition(shockPos, false, true);
            SpawnHitOverlayAtPosition(shockPos, OverlaySpawnKind::GuardRing, -1, false);
            return;
        }

        if (result == Combat::ResolveResult::GuardBreak && bossHitPlayer)
        {
            XMFLOAT3 shockPos = hitPos;
            if (!TryGetGuardShockWavePosition(shockPos))
                ResolveGuardShockPointEntity(true);
            TriggerShockWaveAtPosition(shockPos, false, true);
            SpawnHitOverlayAtPosition(shockPos, OverlaySpawnKind::GuardRing, -1, false);
            SpawnHitOverlayAtPosition(shockPos, OverlaySpawnKind::GuardBreakRing, -1, false);
            return;
        }

        if (result == Combat::ResolveResult::Parry && bossHitPlayer)
        {
            XMFLOAT3 parryPos = hitPos;
            if (!TryGetGuardShockWavePosition(parryPos))
                ResolveGuardShockPointEntity(true);
            SpawnHitOverlayAtPosition(parryPos, OverlaySpawnKind::Spark, -1, false);
            SpawnHitOverlayAtPosition(parryPos, OverlaySpawnKind::ParryRing, -1, false);
            return;
        }
    }

    void CombatVfxBridgeScript::UpdateComputeOneShotEmission(float deltaTime)
    {
        World* world = GetWorld();
        if (!world || m_computeOneShotStates.empty())
            return;

        const float safeDt = (std::max)(0.0f, deltaTime);
        for (auto it = m_computeOneShotStates.begin(); it != m_computeOneShotStates.end();)
        {
            const EntityId id = it->first;
            ComputeOneShotState& state = it->second;
            auto* ce = world->GetComponent<ComputeEffectComponent>(id);
            if (!ce)
            {
                it = m_computeOneShotStates.erase(it);
                continue;
            }

            if (!state.baseCaptured)
            {
                state.baseSpawnRate = (std::max)(0.0f, ce->spawnRate);
                state.baseLoop = ce->loop;
                state.baseCaptured = true;
            }

            if (!state.armed || !ce->enabled)
            {
                ++it;
                continue;
            }

            if (state.emissionDurationSec > 0.0f)
            {
                state.elapsedSec += safeDt;
                if (state.elapsedSec >= state.emissionDurationSec)
                {
                    ce->spawnRate = 0.0f;
                    ce->emitNewParticles = false;
                    state.armed = false;
                }
            }
            else
            {
                if (state.oneFrameConsumed)
                {
                    ce->spawnRate = 0.0f;
                    ce->emitNewParticles = false;
                    state.armed = false;
                }
                else
                {
                    state.oneFrameConsumed = true;
                }
            }

            ++it;
        }
    }

    void CombatVfxBridgeScript::PrewarmSlot(int slot)
    {
        const std::string path = GetPrefabPath(slot);
        if (path.empty())
            return;

        const int count = GetPoolSizeSafe(slot);
        for (int i = 0; i < count; ++i)
        {
            const EntityId id = CreateInstance(slot);
            if (id == InvalidEntityId)
                continue;
            Release(slot, id);
        }
    }

    void CombatVfxBridgeScript::ClearSlot(int slot)
    {
        World* world = GetWorld();
        if (!world)
        {
            m_pool[slot].clear();
            m_active[slot].clear();
            m_cachedBaseScales[slot].clear();
            m_cachedBaseAlphas[slot].clear();
            return;
        }

        for (const ActiveInstance& inst : m_active[slot])
        {
            if (inst.id != InvalidEntityId)
            {
                EraseCachedAlphaRecursive(*world, slot, inst.id);
                world->DestroyEntity(inst.id);
            }
            if (inst.freezeAnchor != InvalidEntityId)
                world->DestroyEntity(inst.freezeAnchor);
        }
        m_active[slot].clear();

        for (EntityId id : m_pool[slot])
        {
            if (id != InvalidEntityId)
            {
                EraseCachedAlphaRecursive(*world, slot, id);
                world->DestroyEntity(id);
            }
        }
        m_pool[slot].clear();
        m_cachedBaseScales[slot].clear();
        m_cachedBaseAlphas[slot].clear();
    }

    EntityId CombatVfxBridgeScript::Acquire(int slot, bool triggerOneShot)
    {
        World* world = GetWorld();
        if (!world)
            return InvalidEntityId;

        EntityId id = InvalidEntityId;
        auto& pool = m_pool[slot];
        const bool isRingOverlaySlot = (slot == HitOverlayGuardRingSlot
            || slot == HitOverlayGuardBreakRingSlot
            || slot == HitOverlayParryRingSlot
            || slot == HitOverlayBossGroggyRingSlot);
        if (!pool.empty())
        {
            id = pool.back();
            pool.pop_back();
        }
        else
        {
            const int cap = GetPoolSizeSafe(slot);
            if (!isRingOverlaySlot && cap > 0 && static_cast<int>(m_active[slot].size()) >= cap)
            {
                // Reuse oldest active instance in this slot as a ring buffer.
                // This keeps spawn count bounded while avoiding dropped VFX spawns.
                auto& activeList = m_active[slot];
                if (!activeList.empty())
                {
                    ActiveInstance recycled = activeList.front();
                    activeList.erase(activeList.begin());

                    if (recycled.id != InvalidEntityId)
                        Release(slot, recycled.id);
                    if (world && recycled.freezeAnchor != InvalidEntityId)
                        world->DestroyEntity(recycled.freezeAnchor);

                    if (!pool.empty())
                    {
                        id = pool.back();
                        pool.pop_back();
                    }
                }
            }

            if (id == InvalidEntityId)
                id = CreateInstance(slot);
        }

        if (id == InvalidEntityId)
            return InvalidEntityId;

        world->SetParent(id, InvalidEntityId, false);
        SetEntityActiveRecursive(*world, id, true, triggerOneShot);
        RestoreEntityAlphaRecursive(*world, slot, id);
        return id;
    }

    void CombatVfxBridgeScript::Release(int slot, EntityId id)
    {
        World* world = GetWorld();
        if (!world || id == InvalidEntityId)
            return;

        SetEntityColorTintRecursive(*world, id, kTintWhite);
        RestoreEntityAlphaRecursive(*world, slot, id);
        const bool isRingOverlaySlot = (slot == HitOverlayGuardRingSlot
            || slot == HitOverlayGuardBreakRingSlot
            || slot == HitOverlayParryRingSlot
            || slot == HitOverlayBossGroggyRingSlot);
        if (!isRingOverlaySlot)
            SetEntityLoopModeRecursive(*world, id, false);
        SetEntityActiveRecursive(*world, id, false, false);

        const int cap = GetPoolSizeSafe(slot);
        if (cap > 0 && static_cast<int>(m_pool[slot].size()) >= cap)
        {
            EraseCachedAlphaRecursive(*world, slot, id);
            world->DestroyEntity(id);
            m_cachedBaseScales[slot].erase(id);
            return;
        }

        if (Get_organizePoolUnderRoot())
        {
            const EntityId poolRoot = EnsurePoolRoot();
            if (poolRoot != InvalidEntityId)
                world->SetParent(id, poolRoot, false);
        }
        m_pool[slot].push_back(id);
    }

    EntityId CombatVfxBridgeScript::CreateInstance(int slot)
    {
        const std::string path = GetPrefabPath(slot);
        if (path.empty())
            return InvalidEntityId;

        World* world = GetWorld();
        if (!world)
            return InvalidEntityId;

        Prefab::SetDefaultWorld(world);
        const EntityId id = Prefab::InstantiateFromFileAuto(path);
        if (id == InvalidEntityId)
        {
            if (slot < HitDefaultSlot && !m_warnedSlashInstantiateFailed)
            {
                ALICE_LOG_WARN("[CombatVfxBridge] Failed to instantiate slash prefab: %s", path.c_str());
                m_warnedSlashInstantiateFailed = true;
            }
            if (slot >= HitDefaultSlot && !m_warnedHitInstantiateFailed)
            {
                ALICE_LOG_WARN("[CombatVfxBridge] Failed to instantiate hit prefab: %s", path.c_str());
                m_warnedHitInstantiateFailed = true;
            }
            return InvalidEntityId;
        }

        if (Get_organizePoolUnderRoot())
        {
            const EntityId poolRoot = EnsurePoolRoot();
            if (poolRoot != InvalidEntityId)
                world->SetParent(id, poolRoot, false);
            else
                world->SetParent(id, InvalidEntityId, false);
        }
        else
        {
            world->SetParent(id, InvalidEntityId, false);
        }

        CacheEntityAlphaRecursive(*world, slot, id);

        if (auto* tr = world->GetComponent<TransformComponent>(id))
        {
            m_cachedBaseScales[slot][id] = tr->scale;
            tr->position = XMFLOAT3(0.0f, 0.0f, 0.0f);
            if (slot != HitOverlayBossGroggyRingSlot
                && slot != ShockWaveSlot
                && slot != ShockBlastSlot)
                tr->rotation = XMFLOAT3(0.0f, 0.0f, 0.0f);
            world->MarkTransformDirty(id);
        }

        return id;
    }

    EntityId CombatVfxBridgeScript::EnsurePoolRoot()
    {
        World* world = GetWorld();
        if (!world || !Get_organizePoolUnderRoot())
            return InvalidEntityId;

        if (m_poolRoot != InvalidEntityId && world->GetComponent<TransformComponent>(m_poolRoot))
            return m_poolRoot;

        std::string baseName = Get_poolRootName();
        if (baseName.empty())
            baseName = "__CombatVfxPool";
        const std::string uniqueName = baseName + "_" + std::to_string(static_cast<unsigned long long>(GetOwnerId()));

        GameObject existing = world->FindGameObject(uniqueName);
        if (existing.IsValid())
        {
            m_poolRoot = existing.id();
        }
        else
        {
            m_poolRoot = world->CreateEmpty();
            if (m_poolRoot == InvalidEntityId)
                return InvalidEntityId;
            world->SetEntityName(m_poolRoot, uniqueName);
        }

        if (GetOwnerId() != InvalidEntityId)
            world->SetParent(m_poolRoot, GetOwnerId(), false);

        if (auto* tr = world->GetComponent<TransformComponent>(m_poolRoot))
        {
            tr->position = XMFLOAT3(0.0f, 0.0f, 0.0f);
            tr->rotation = XMFLOAT3(0.0f, 0.0f, 0.0f);
            tr->scale = XMFLOAT3(1.0f, 1.0f, 1.0f);
            tr->enabled = false;
            tr->visible = false;
            world->MarkTransformDirty(m_poolRoot);
        }

        return m_poolRoot;
    }

    void CombatVfxBridgeScript::ClearPoolRoot()
    {
        World* world = GetWorld();
        if (!world)
        {
            m_poolRoot = InvalidEntityId;
            return;
        }

        if (m_poolRoot != InvalidEntityId)
            world->DestroyEntity(m_poolRoot);
        m_poolRoot = InvalidEntityId;
    }

    bool CombatVfxBridgeScript::TryGetTracePointWorld(DirectX::XMFLOAT3& outPos)
    {
        XMFLOAT3 forward{};
        XMFLOAT3 up{};
        return TryGetTracePoseWorld(outPos, forward, up);
    }

    bool CombatVfxBridgeScript::TryGetTracePoseWorld(DirectX::XMFLOAT3& outPos, DirectX::XMFLOAT3& outForward, DirectX::XMFLOAT3& outUp)
    {
        World* world = GetWorld();
        if (!world)
            return false;

        const std::string traceName = Get_weaponTraceEntityName();
        if (traceName.empty())
            return false;

        GameObject traceGo = world->FindGameObject(traceName);
        if (!traceGo.IsValid())
        {
            if (!m_warnedMissingTracePoint)
            {
                ALICE_LOG_WARN("[CombatVfxBridge] Missing trace entity: '%s'", traceName.c_str());
                m_warnedMissingTracePoint = true;
            }
            return false;
        }

        DirectX::XMFLOAT4X4 wm{};
        XMStoreFloat4x4(&wm, world->ComputeWorldMatrix(traceGo.id()));
        outPos = XMFLOAT3(wm._41, wm._42, wm._43);
        outForward = NormalizeOrFallback(XMFLOAT3(wm._31, wm._32, wm._33), GetPlayerForward());
        outUp = NormalizeOrFallback(XMFLOAT3(wm._21, wm._22, wm._23), XMFLOAT3(0.0f, 1.0f, 0.0f));
        return true;
    }

    bool CombatVfxBridgeScript::TryGetAttackDirectionWorld(DirectX::XMFLOAT3& outForward)
    {
        if (m_hasPrevSocketPos)
        {
            const XMFLOAT3 delta = Sub(m_currSocketPos, m_prevSocketPos);
            if (LengthSq(delta) > kVectorEpsilonSq)
            {
                outForward = NormalizeOrFallback(delta, GetPlayerForward());
                return true;
            }
        }

        XMFLOAT3 tracePos{};
        XMFLOAT3 traceForward{};
        XMFLOAT3 traceUp{};
        if (TryGetTracePoseWorld(tracePos, traceForward, traceUp))
        {
            outForward = NormalizeOrFallback(traceForward, GetPlayerForward());
            return true;
        }

        outForward = GetPlayerForward();
        return false;
    }

    int CombatVfxBridgeScript::ResolveSlashSlotForAttackSlotIndex(int attackSlotIndex) const
    {
        if (!Get_useSlashStepPrefabs())
            return SlashDefaultSlot;

        const int clamped = ClampInt(attackSlotIndex, 1, SlashStepSlotCount);
        switch (clamped)
        {
        case 1:
            return Get_slashStepPrefabPath1().empty() ? SlashDefaultSlot : SlashStep1Slot;
        case 2:
            return Get_slashStepPrefabPath2().empty() ? SlashDefaultSlot : SlashStep2Slot;
        case 3:
            return Get_slashStepPrefabPath3().empty() ? SlashDefaultSlot : SlashStep3Slot;
        case 4:
            return Get_slashStepPrefabPath4().empty() ? SlashDefaultSlot : SlashStep4Slot;
        case 5:
            return Get_slashStepPrefabPath5().empty() ? SlashDefaultSlot : SlashStep5Slot;
        case 6:
            return Get_slashStepPrefabPath6().empty() ? SlashDefaultSlot : SlashStep6Slot;
        default:
            return SlashDefaultSlot;
        }
    }

    int CombatVfxBridgeScript::ResolveHitSlotForAttackSlotIndex(int attackSlotIndex) const
    {
        if (!Get_useHitSlotPrefabs())
            return HitDefaultSlot;

        const int clamped = ClampInt(attackSlotIndex, 1, SlashStepSlotCount);
        switch (clamped)
        {
        case 1:
            return Get_hitSlotPrefabPath1().empty() ? HitDefaultSlot : HitStep1Slot;
        case 2:
            return Get_hitSlotPrefabPath2().empty() ? HitDefaultSlot : HitStep2Slot;
        case 3:
            return Get_hitSlotPrefabPath3().empty() ? HitDefaultSlot : HitStep3Slot;
        case 4:
            return Get_hitSlotPrefabPath4().empty() ? HitDefaultSlot : HitStep4Slot;
        case 5:
            return Get_hitSlotPrefabPath5().empty() ? HitDefaultSlot : HitStep5Slot;
        case 6:
            return Get_hitSlotPrefabPath6().empty() ? HitDefaultSlot : HitStep6Slot;
        default:
            return HitDefaultSlot;
        }
    }

    int CombatVfxBridgeScript::ResolveCurrentAttackSlotIndexFromDriverMask() const
    {
        World* world = GetWorld();
        if (!world || m_playerId == InvalidEntityId)
            return -1;

        const auto* driver = world->GetComponent<AttackDriverComponent>(m_playerId);
        if (!driver)
            return -1;

        const std::uint32_t mask = driver->attackTraceMaskActive;
        if (mask == 0u)
            return -1;

        for (int bit = 0; bit < 32; ++bit)
        {
            if ((mask & (1u << bit)) != 0u)
                return bit + 1; // bit0 -> slot1
        }
        return -1;
    }

    DirectX::XMFLOAT3 CombatVfxBridgeScript::GetPlayerPosition()
    {
        World* world = GetWorld();
        if (!world || m_playerId == InvalidEntityId)
            return XMFLOAT3(0.0f, 0.0f, 0.0f);

        DirectX::XMFLOAT4X4 wm{};
        XMStoreFloat4x4(&wm, world->ComputeWorldMatrix(m_playerId));
        return XMFLOAT3(wm._41, wm._42, wm._43);
    }

    DirectX::XMFLOAT3 CombatVfxBridgeScript::GetPlayerForward()
    {
        World* world = GetWorld();
        if (!world || m_playerId == InvalidEntityId)
            return XMFLOAT3(0.0f, 0.0f, 1.0f);

        DirectX::XMFLOAT4X4 wm{};
        XMStoreFloat4x4(&wm, world->ComputeWorldMatrix(m_playerId));
        const XMFLOAT3 forward(wm._31, wm._32, wm._33);
        return NormalizeOrFallback(forward, XMFLOAT3(0.0f, 0.0f, 1.0f));
    }

    float CombatVfxBridgeScript::ResolvePlayerAttackDurationSec() const
    {
        World* world = GetWorld();
        if (!world || m_playerId == InvalidEntityId)
            return 0.0f;

        const auto* driver = world->GetComponent<AttackDriverComponent>(m_playerId);
        if (!driver)
            return 0.0f;

        const float duration = (driver->attackStateDurationSec > 0.0f)
            ? driver->attackStateDurationSec
            : driver->attackStateDurationAutoSec;
        return (std::max)(0.0f, duration);
    }

    void CombatVfxBridgeScript::ApplySpawnTransformTuned(int slot,
                                                         EntityId id,
                                                         const DirectX::XMFLOAT3& anchorPos,
                                                         const DirectX::XMFLOAT3& forward,
                                                         const DirectX::XMFLOAT3& upHint,
                                                         const DirectX::XMFLOAT3& localOffset,
                                                         const DirectX::XMFLOAT3& rotationOffsetDeg,
                                                         float scaleMul)
    {
        World* world = GetWorld();
        if (!world || id == InvalidEntityId)
            return;

        TransformComponent* tr = world->GetComponent<TransformComponent>(id);
        if (!tr)
            return;

        const Basis basis = BuildBasis(forward, upHint);
        const XMFLOAT3 worldOffset = RotateLocalOffset(localOffset, basis);
        tr->position = Add(anchorPos, worldOffset);

        const Basis rotatedBasis = ApplyEulerOffsetDegLocal(basis, rotationOffsetDeg);
        tr->rotation = RotationRadFromBasis(rotatedBasis);

        XMFLOAT3 baseScale = tr->scale;
        const auto itScale = m_cachedBaseScales[slot].find(id);
        if (itScale != m_cachedBaseScales[slot].end())
            baseScale = itScale->second;

        const float safeScaleMul = (std::max)(0.0f, scaleMul);
        tr->scale = XMFLOAT3(baseScale.x * safeScaleMul, baseScale.y * safeScaleMul, baseScale.z * safeScaleMul);
        world->MarkTransformDirty(id);
    }

    void CombatVfxBridgeScript::ApplySpawnTransform(int slot,
                                                    EntityId id,
                                                    const DirectX::XMFLOAT3& anchorPos,
                                                    const DirectX::XMFLOAT3& forward,
                                                    const DirectX::XMFLOAT3& upHint)
    {
        ApplySpawnTransformTuned(
            slot,
            id,
            anchorPos,
            forward,
            upHint,
            GetOffsetLocal(slot),
            GetRotationOffsetDeg(slot),
            GetScaleMulSafe(slot));
    }

    void CombatVfxBridgeScript::SetEntityActiveRecursive(World& world, EntityId id, bool active, bool triggerOneShot)
    {
        if (id == InvalidEntityId)
            return;

        if (auto* t = world.GetComponent<TransformComponent>(id))
        {
            t->enabled = active;
            t->visible = active;
            world.MarkTransformDirty(id);
        }

        if (auto* vfx = world.GetComponent<UnityVfxComponent>(id))
        {
            vfx->enabled = active;
            if (active && triggerOneShot)
            {
                vfx->overrideLoop = true;
                vfx->loop = false;
                vfx->emitNewParticles = true;
                vfx->playId += 1;
            }
            else if (active)
            {
                vfx->emitNewParticles = true;
                vfx->playId += 1;
            }
            else if (!active)
            {
                vfx->emitNewParticles = false;
            }
        }

        if (auto* ce = world.GetComponent<ComputeEffectComponent>(id))
        {
            ComputeOneShotState& state = m_computeOneShotStates[id];
            if (active && triggerOneShot)
            {
                if (!state.baseCaptured)
                {
                    // Capture baseline once. Runtime one-shot logic mutates spawnRate/loop.
                    state.baseSpawnRate = (std::max)(0.0f, ce->spawnRate);
                    state.baseLoop = ce->loop;
                    state.baseCaptured = true;
                }

                state.armed = true;
                state.oneFrameConsumed = false;
                state.elapsedSec = 0.0f;
                state.emissionDurationSec = (std::max)(0.0f, ce->emissionDurationSec);

                ce->loop = false;
                ce->spawnRate = (std::max)(0.0f, state.baseSpawnRate);
                ce->emitNewParticles = true;
            }
            else if (active)
            {
                ce->emitNewParticles = true;
                state.armed = false;
                state.oneFrameConsumed = false;
                state.elapsedSec = 0.0f;
                state.emissionDurationSec = 0.0f;
            }
            else if (!active)
            {
                if (state.baseCaptured)
                {
                    ce->spawnRate = (std::max)(0.0f, state.baseSpawnRate);
                    ce->loop = state.baseLoop;
                }
                ce->emitNewParticles = false;
                state.armed = false;
                state.oneFrameConsumed = false;
                state.elapsedSec = 0.0f;
                state.emissionDurationSec = 0.0f;
            }

            ce->enabled = active;
        }
        else
        {
            m_computeOneShotStates.erase(id);
        }

        const auto children = world.GetChildren(id);
        for (EntityId child : children)
            SetEntityActiveRecursive(world, child, active, triggerOneShot);
    }

    void CombatVfxBridgeScript::SetEntityColorTintRecursive(World& world, EntityId id, const DirectX::XMFLOAT3& tint) const
    {
        if (id == InvalidEntityId)
            return;

        if (auto* vfx = world.GetComponent<UnityVfxComponent>(id))
            vfx->colorTint = tint;

        const auto children = world.GetChildren(id);
        for (EntityId child : children)
            SetEntityColorTintRecursive(world, child, tint);
    }

    void CombatVfxBridgeScript::SetEntityLoopModeRecursive(World& world, EntityId id, bool loopEnabled) const
    {
        if (id == InvalidEntityId)
            return;

        if (auto* vfx = world.GetComponent<UnityVfxComponent>(id))
        {
            vfx->overrideLoop = true;
            vfx->loop = loopEnabled;
            if (loopEnabled)
                vfx->emitNewParticles = true;
        }

        const auto children = world.GetChildren(id);
        for (EntityId child : children)
            SetEntityLoopModeRecursive(world, child, loopEnabled);
    }

    void CombatVfxBridgeScript::ApplyEntityAlphaFadeRecursive(World& world, int slot, EntityId id, float alphaRatio) const
    {
        if (id == InvalidEntityId || slot < 0 || slot >= SlotCount)
            return;

        const float clampedRatio = std::clamp(alphaRatio, 0.0f, 1.0f);
        if (auto* vfx = world.GetComponent<UnityVfxComponent>(id))
        {
            float baseAlpha = vfx->alphaScale;
            const auto it = m_cachedBaseAlphas[slot].find(id);
            if (it != m_cachedBaseAlphas[slot].end())
                baseAlpha = it->second;
            vfx->alphaScale = baseAlpha * clampedRatio;
        }

        const auto children = world.GetChildren(id);
        for (EntityId child : children)
            ApplyEntityAlphaFadeRecursive(world, slot, child, clampedRatio);
    }

    void CombatVfxBridgeScript::CacheEntityAlphaRecursive(World& world, int slot, EntityId id)
    {
        if (id == InvalidEntityId || slot < 0 || slot >= SlotCount)
            return;

        if (auto* vfx = world.GetComponent<UnityVfxComponent>(id))
            m_cachedBaseAlphas[slot][id] = vfx->alphaScale;

        const auto children = world.GetChildren(id);
        for (EntityId child : children)
            CacheEntityAlphaRecursive(world, slot, child);
    }

    void CombatVfxBridgeScript::RestoreEntityAlphaRecursive(World& world, int slot, EntityId id) const
    {
        if (id == InvalidEntityId || slot < 0 || slot >= SlotCount)
            return;

        if (auto* vfx = world.GetComponent<UnityVfxComponent>(id))
        {
            const auto it = m_cachedBaseAlphas[slot].find(id);
            if (it != m_cachedBaseAlphas[slot].end())
                vfx->alphaScale = it->second;
        }

        const auto children = world.GetChildren(id);
        for (EntityId child : children)
            RestoreEntityAlphaRecursive(world, slot, child);
    }

    void CombatVfxBridgeScript::EraseCachedAlphaRecursive(World& world, int slot, EntityId id)
    {
        if (id == InvalidEntityId || slot < 0 || slot >= SlotCount)
            return;

        m_cachedBaseAlphas[slot].erase(id);
        const auto children = world.GetChildren(id);
        for (EntityId child : children)
            EraseCachedAlphaRecursive(world, slot, child);
    }

    float CombatVfxBridgeScript::ComputeOneShotKeepAliveSec(EntityId rootId)
    {
        World* world = GetWorld();
        if (!world || rootId == InvalidEntityId)
            return 0.0f;

        float requiredComputeLifeSec = 0.0f;
        std::vector<EntityId> stack;
        stack.push_back(rootId);
        while (!stack.empty())
        {
            const EntityId current = stack.back();
            stack.pop_back();

            if (const auto* compute = world->GetComponent<ComputeEffectComponent>(current))
            {
                if (!compute->loop)
                {
                    const float emissionSec = std::max(0.0f, compute->emissionDurationSec);
                    const float particleLifeSec = std::max(0.0f, std::max(compute->lifeMin, compute->lifeMax));
                    requiredComputeLifeSec = std::max(requiredComputeLifeSec, emissionSec + particleLifeSec);
                }
            }

            const auto children = world->GetChildren(current);
            for (EntityId child : children)
                stack.push_back(child);
        }

        return requiredComputeLifeSec;
    }

    std::string CombatVfxBridgeScript::GetPrefabPath(int slot) const
    {
        switch (slot)
        {
        case SlashDefaultSlot: return Get_slashPrefabPath();
        case SlashStep1Slot: return Get_slashStepPrefabPath1();
        case SlashStep2Slot: return Get_slashStepPrefabPath2();
        case SlashStep3Slot: return Get_slashStepPrefabPath3();
        case SlashStep4Slot: return Get_slashStepPrefabPath4();
        case SlashStep5Slot: return Get_slashStepPrefabPath5();
        case SlashStep6Slot: return Get_slashStepPrefabPath6();
        case HitDefaultSlot: return Get_hitPrefabPath();
        case HitStep1Slot: return Get_hitSlotPrefabPath1();
        case HitStep2Slot: return Get_hitSlotPrefabPath2();
        case HitStep3Slot: return Get_hitSlotPrefabPath3();
        case HitStep4Slot: return Get_hitSlotPrefabPath4();
        case HitStep5Slot: return Get_hitSlotPrefabPath5();
        case HitStep6Slot: return Get_hitSlotPrefabPath6();
        case HitOverlaySparkSlot: return Get_hitOverlayPrefabPath();
        case HitOverlayGuardSlot: return Get_guardOverlayPrefabPath();
        case HitOverlayDamageSlot: return Get_damageOverlayPrefabPath();
        case HitOverlayGuardRingSlot: return Get_guardRingOverlayPrefabPath();
        case HitOverlayGuardBreakRingSlot: return Get_guardBreakRingOverlayPrefabPath();
        case HitOverlayParryRingSlot: return Get_parryRingOverlayPrefabPath();
        case HitOverlayBossGroggyRingSlot: return Get_bossGroggyRingOverlayPrefabPath();
        case ShockWaveSlot: return Get_shockWavePrefabPath();
        case ShockBlastSlot: return Get_shockBlastPrefabPath();
        default: return std::string();
        }
    }

    int CombatVfxBridgeScript::GetPoolSizeSafe(int slot) const
    {
        if (slot == ShockWaveSlot)
            return (std::max)(0, Get_shockWavePoolSize());
        if (slot == ShockBlastSlot)
            return (std::max)(0, Get_shockBlastPoolSize());

        const bool isHitSlot = (slot >= HitDefaultSlot);
        const int size = isHitSlot ? Get_hitPoolSize() : Get_slashPoolSize();
        return (std::max)(0, size);
    }

    float CombatVfxBridgeScript::GetLifeTimeSafe(int slot) const
    {
        if (slot == ShockWaveSlot)
            return (std::max)(0.01f, Get_shockWaveLifeTimeSec());
        if (slot == ShockBlastSlot)
            return (std::max)(0.01f, Get_shockBlastLifeTimeSec());

        if (slot == HitOverlayBossGroggyRingSlot)
            return (std::max)(0.01f, Get_bossGroggyRingOverlayLifeTimeSec());

        const bool isHitSlot = (slot >= HitDefaultSlot);
        float v = isHitSlot ? Get_hitLifeTimeSec() : Get_slashLifeTimeSec();

        if (isHitSlot && Get_useHitSlotLifeTimes())
        {
            const auto resolveHitSlotLife = [&](int hitSlot) -> float
            {
                switch (hitSlot)
                {
                case HitStep1Slot: return Get_hitSlotLifeTimeSec1();
                case HitStep2Slot: return Get_hitSlotLifeTimeSec2();
                case HitStep3Slot: return Get_hitSlotLifeTimeSec3();
                case HitStep4Slot: return Get_hitSlotLifeTimeSec4();
                case HitStep5Slot: return Get_hitSlotLifeTimeSec5();
                case HitStep6Slot: return Get_hitSlotLifeTimeSec6();
                default: return 0.0f;
                }
            };

            const float slotLife = resolveHitSlotLife(slot);
            if (slotLife > 0.0f)
                v = slotLife;
        }

        return (v > 0.0f) ? v : 0.01f;
    }

    bool CombatVfxBridgeScript::IsHeavyAttackSlotIndex(int attackSlotIndex) const
    {
        return (attackSlotIndex > 0) && (attackSlotIndex == Get_heavyAttackSlotIndex());
    }

    bool CombatVfxBridgeScript::ShouldApplyRageTint(int attackSlotIndex) const
    {
        if (!m_session || !m_session->IsPlayerRageActive())
            return false;
        if (IsHeavyAttackSlotIndex(attackSlotIndex))
            return false;
        return true;
    }

    float CombatVfxBridgeScript::ResolveSpawnLifetimeSec(int slot, int attackSlotIndex) const
    {
        if (Get_enableHeavyFadeOut() && IsHeavyAttackSlotIndex(attackSlotIndex))
            return (std::max)(0.01f, Get_heavyFadeDurationSec());
        return GetLifeTimeSafe(slot);
    }

    float CombatVfxBridgeScript::GetScaleMulSafe(int slot) const
    {
        const bool isRingOverlaySlot = (slot == HitOverlayGuardRingSlot
            || slot == HitOverlayGuardBreakRingSlot
            || slot == HitOverlayParryRingSlot
            || slot == HitOverlayBossGroggyRingSlot);
        if (isRingOverlaySlot)
            return 1.0f;

        const bool isHitSlot = (slot >= HitDefaultSlot);
        const float v = isHitSlot ? Get_hitScaleMul() : Get_slashScaleMul();
        return (v >= 0.0f) ? v : 0.0f;
    }

    DirectX::XMFLOAT3 CombatVfxBridgeScript::GetOffsetLocal(int slot) const
    {
        const bool isRingOverlaySlot = (slot == HitOverlayGuardRingSlot
            || slot == HitOverlayGuardBreakRingSlot
            || slot == HitOverlayParryRingSlot
            || slot == HitOverlayBossGroggyRingSlot);
        if (isRingOverlaySlot)
            return XMFLOAT3(0.0f, 0.0f, 0.0f);

        const bool isHitSlot = (slot >= HitDefaultSlot);
        return isHitSlot ? Get_hitOffsetLocal() : Get_slashOffsetLocal();
    }

    DirectX::XMFLOAT3 CombatVfxBridgeScript::GetRotationOffsetDeg(int slot) const
    {
        const bool isRingOverlaySlot = (slot == HitOverlayGuardRingSlot
            || slot == HitOverlayGuardBreakRingSlot
            || slot == HitOverlayParryRingSlot
            || slot == HitOverlayBossGroggyRingSlot);
        if (isRingOverlaySlot)
            return XMFLOAT3(0.0f, 0.0f, 0.0f);

        const bool isHitSlot = (slot >= HitDefaultSlot);
        return isHitSlot ? Get_hitRotationOffsetDeg() : Get_slashRotationOffsetDeg();
    }

    DirectX::XMFLOAT3 CombatVfxBridgeScript::GetSlashSlotAnchorOffsetLocal(int attackSlotIndex) const
    {
        const int clamped = ClampInt(attackSlotIndex, 1, SlashStepSlotCount);
        switch (clamped)
        {
        case 1: return Get_slashSlotAnchorOffsetLocal1();
        case 2: return Get_slashSlotAnchorOffsetLocal2();
        case 3: return Get_slashSlotAnchorOffsetLocal3();
        case 4: return Get_slashSlotAnchorOffsetLocal4();
        case 5: return Get_slashSlotAnchorOffsetLocal5();
        case 6: return Get_slashSlotAnchorOffsetLocal6();
        default: return XMFLOAT3(0.0f, Get_slashAnchorPlayerYOffset(), Get_slashAnchorPlayerForwardOffset());
        }
    }

    DirectX::XMFLOAT3 CombatVfxBridgeScript::GetSlashSlotRotationOffsetDeg(int attackSlotIndex) const
    {
        const int clamped = ClampInt(attackSlotIndex, 1, SlashStepSlotCount);
        switch (clamped)
        {
        case 1: return Get_slashSlotRotationOffsetDeg1();
        case 2: return Get_slashSlotRotationOffsetDeg2();
        case 3: return Get_slashSlotRotationOffsetDeg3();
        case 4: return Get_slashSlotRotationOffsetDeg4();
        case 5: return Get_slashSlotRotationOffsetDeg5();
        case 6: return Get_slashSlotRotationOffsetDeg6();
        default: return XMFLOAT3(0.0f, 0.0f, 0.0f);
        }
    }

    float CombatVfxBridgeScript::GetSlashSlotScaleMul(int attackSlotIndex) const
    {
        const int clamped = ClampInt(attackSlotIndex, 1, SlashStepSlotCount);
        switch (clamped)
        {
        case 1: return Get_slashSlotScaleMul1();
        case 2: return Get_slashSlotScaleMul2();
        case 3: return Get_slashSlotScaleMul3();
        case 4: return Get_slashSlotScaleMul4();
        case 5: return Get_slashSlotScaleMul5();
        case 6: return Get_slashSlotScaleMul6();
        default: return 1.0f;
        }
    }

}
