#include "CombatVfxBridgeScript.h"

#include <algorithm>
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
        }
        else
        {
            m_prevIsLightAttack = false;
            m_prevPlayerHitActive = false;
            m_slashSignalOrdinalInAttack = 0;
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

        UpdateActive(deltaTime);
    }

    void CombatVfxBridgeScript::OnDisable()
    {
        DeactivateAllActive();
        UnbindResolveDelegateSafe();
        m_session = nullptr;

        m_prevIsLightAttack = false;
        m_prevPlayerHitActive = false;
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

        GameObject playerGo = playerName.empty() ? GameObject{} : world->FindGameObject(playerName);
        GameObject bossGo = bossName.empty() ? GameObject{} : world->FindGameObject(bossName);
        m_playerId = playerGo.IsValid() ? playerGo.id() : InvalidEntityId;
        m_bossId = bossGo.IsValid() ? bossGo.id() : InvalidEntityId;

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
        for (int slot = 0; slot < SlotCount; ++slot)
        {
            const std::string path = GetPrefabPath(slot);
            if (path != m_cachedPaths[slot])
            {
                ClearSlot(slot);
                m_cachedPaths[slot] = path;

                if (Get_prewarm() && !path.empty())
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
                if (inst.fadeOut && inst.totalSec > 0.0f && world)
                {
                    const float alpha = std::clamp(inst.remainingSec / inst.totalSec, 0.0f, 1.0f);
                    SetEntityAlphaRecursive(*world, inst.id, alpha);
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

        const EntityId id = Acquire(slashSlot);
        if (id == InvalidEntityId)
            return;

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
        {
            Release(slashSlot, id);
            return;
        }

        const bool isHeavySlash = IsHeavyAttackSlotIndex(resolvedAttackSlotIndex);
        const bool applyRageTint = ShouldApplyRageTint(resolvedAttackSlotIndex);
        const bool heavyFade = Get_enableHeavyFadeOut() && isHeavySlash;
        const float lifeSec = ResolveSpawnLifetimeSec(slashSlot, resolvedAttackSlotIndex);
        if (heavyFade)
            SetEntityLoopModeRecursive(*world, id, true);
        SetEntityAlphaRecursive(*world, id, 1.0f);
        SetEntityColorTintRecursive(*world, id, applyRageTint ? kTintRage : kTintWhite);

        if (m_playerId != InvalidEntityId)
            world->SetParent(id, m_playerId, false);
        else
            world->SetParent(id, InvalidEntityId, false);

        TransformComponent* tr = world->GetComponent<TransformComponent>(id);
        if (!tr)
        {
            Release(slashSlot, id);
            return;
        }

        const XMFLOAT3 localPos = Add(anchorLocal, slashOffsetLocal);
        tr->position = localPos;
        tr->rotation = XMFLOAT3(
            XMConvertToRadians(slashRotationOffsetDeg.x),
            XMConvertToRadians(slashRotationOffsetDeg.y),
            XMConvertToRadians(slashRotationOffsetDeg.z));

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

    void CombatVfxBridgeScript::SpawnHitFromResolve(const DirectX::XMFLOAT3& hitPos,
                                                    EntityId victimId,
                                                    int attackSlotIndex)
    {
        const int hitSlot = ResolveHitSlotForAttackSlotIndex(attackSlotIndex);
        const EntityId id = Acquire(hitSlot);
        if (id == InvalidEntityId)
            return;

        World* world = GetWorld();
        if (!world)
        {
            Release(hitSlot, id);
            return;
        }

        const bool heavyFade = Get_enableHeavyFadeOut() && IsHeavyAttackSlotIndex(attackSlotIndex);
        const bool applyRageTint = ShouldApplyRageTint(attackSlotIndex);
        const float lifeSec = ResolveSpawnLifetimeSec(hitSlot, attackSlotIndex);
        if (heavyFade)
            SetEntityLoopModeRecursive(*world, id, true);
        SetEntityAlphaRecursive(*world, id, 1.0f);
        SetEntityColorTintRecursive(*world, id, applyRageTint ? kTintRage : kTintWhite);

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
        if (isRedHitEffect && victimId != InvalidEntityId)
        {
            DirectX::XMFLOAT4X4 victimWorld{};
            XMStoreFloat4x4(&victimWorld, world->ComputeWorldMatrix(victimId));
            spawnPos = XMFLOAT3(victimWorld._41, victimWorld._42 + 1.1f, victimWorld._43);
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
                    (playerWorld._42 + 1.1f) - spawnPos.y,
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

        ApplySpawnTransformTuned(hitSlot,
                                 id,
                                 spawnPos,
                                 forward,
                                 upHint,
                                 GetOffsetLocal(hitSlot),
                                 hitRotationOffsetDeg,
                                 GetScaleMulSafe(hitSlot));
        m_active[hitSlot].push_back({ id, lifeSec, lifeSec, heavyFade, InvalidEntityId });
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

        if (attackerId != m_playerId || victimId != m_bossId)
            return;

        if (resolveResult != static_cast<std::uint8_t>(Combat::ResolveResult::Hit))
            return;

        int attackSlotIndex = ResolveCurrentAttackSlotIndexFromDriverMask();
        if (attackSlotIndex <= 0)
            attackSlotIndex = m_lastAttackSlotIndex;
        else
            m_lastAttackSlotIndex = attackSlotIndex;
        SpawnHitFromResolve(hitPos, victimId, attackSlotIndex);
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
            return;
        }

        for (const ActiveInstance& inst : m_active[slot])
        {
            if (inst.id != InvalidEntityId)
                world->DestroyEntity(inst.id);
            if (inst.freezeAnchor != InvalidEntityId)
                world->DestroyEntity(inst.freezeAnchor);
        }
        m_active[slot].clear();

        for (EntityId id : m_pool[slot])
        {
            if (id != InvalidEntityId)
                world->DestroyEntity(id);
        }
        m_pool[slot].clear();
        m_cachedBaseScales[slot].clear();
    }

    EntityId CombatVfxBridgeScript::Acquire(int slot)
    {
        World* world = GetWorld();
        if (!world)
            return InvalidEntityId;

        EntityId id = InvalidEntityId;
        auto& pool = m_pool[slot];
        if (!pool.empty())
        {
            id = pool.back();
            pool.pop_back();
        }
        else
        {
            const int cap = GetPoolSizeSafe(slot);
            if (cap > 0 && static_cast<int>(m_active[slot].size()) >= cap)
            {
                if (slot < HitDefaultSlot && !m_warnedSlashPoolCapReached)
                {
                    ALICE_LOG_WARN("[CombatVfxBridge] Slash pool cap reached (%d). Increase slashPoolSize if needed.", cap);
                    m_warnedSlashPoolCapReached = true;
                }
                if (slot >= HitDefaultSlot && !m_warnedHitPoolCapReached)
                {
                    ALICE_LOG_WARN("[CombatVfxBridge] Hit pool cap reached (%d). Increase hitPoolSize if needed.", cap);
                    m_warnedHitPoolCapReached = true;
                }
                return InvalidEntityId;
            }

            id = CreateInstance(slot);
        }

        if (id == InvalidEntityId)
            return InvalidEntityId;

        world->SetParent(id, InvalidEntityId, false);
        SetEntityActiveRecursive(*world, id, true, true);
        SetEntityAlphaRecursive(*world, id, 1.0f);
        return id;
    }

    void CombatVfxBridgeScript::Release(int slot, EntityId id)
    {
        World* world = GetWorld();
        if (!world || id == InvalidEntityId)
            return;

        SetEntityAlphaRecursive(*world, id, 1.0f);
        SetEntityColorTintRecursive(*world, id, kTintWhite);
        SetEntityLoopModeRecursive(*world, id, false);
        SetEntityActiveRecursive(*world, id, false, false);
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
        if (auto* tr = world->GetComponent<TransformComponent>(id))
        {
            m_cachedBaseScales[slot][id] = tr->scale;
            tr->position = XMFLOAT3(0.0f, 0.0f, 0.0f);
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

    void CombatVfxBridgeScript::SetEntityActiveRecursive(World& world, EntityId id, bool active, bool triggerOneShot) const
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
            else if (!active)
            {
                vfx->emitNewParticles = false;
            }
        }

        if (auto* ce = world.GetComponent<ComputeEffectComponent>(id))
            ce->enabled = active;

        const auto children = world.GetChildren(id);
        for (EntityId child : children)
            SetEntityActiveRecursive(world, child, active, triggerOneShot);
    }

    void CombatVfxBridgeScript::SetEntityAlphaRecursive(World& world, EntityId id, float alpha) const
    {
        if (id == InvalidEntityId)
            return;

        const float clampedAlpha = std::clamp(alpha, 0.0f, 1.0f);
        if (auto* vfx = world.GetComponent<UnityVfxComponent>(id))
            vfx->alphaScale = clampedAlpha;

        const auto children = world.GetChildren(id);
        for (EntityId child : children)
            SetEntityAlphaRecursive(world, child, clampedAlpha);
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
        default: return std::string();
        }
    }

    int CombatVfxBridgeScript::GetPoolSizeSafe(int slot) const
    {
        const bool isHitSlot = (slot >= HitDefaultSlot);
        const int size = isHitSlot ? Get_hitPoolSize() : Get_slashPoolSize();
        return (std::max)(0, size);
    }

    float CombatVfxBridgeScript::GetLifeTimeSafe(int slot) const
    {
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
        const bool isHitSlot = (slot >= HitDefaultSlot);
        const float v = isHitSlot ? Get_hitScaleMul() : Get_slashScaleMul();
        return (v >= 0.0f) ? v : 0.0f;
    }

    DirectX::XMFLOAT3 CombatVfxBridgeScript::GetOffsetLocal(int slot) const
    {
        const bool isHitSlot = (slot >= HitDefaultSlot);
        return isHitSlot ? Get_hitOffsetLocal() : Get_slashOffsetLocal();
    }

    DirectX::XMFLOAT3 CombatVfxBridgeScript::GetRotationOffsetDeg(int slot) const
    {
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
