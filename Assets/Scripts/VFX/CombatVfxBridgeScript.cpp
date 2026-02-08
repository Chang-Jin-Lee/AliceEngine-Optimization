#include "CombatVfxBridgeScript.h"

#include <algorithm>
#include <cstddef>
#include <cmath>

#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Gameplay/Animation/AdvancedAnimationComponent.h"
#include "Runtime/Gameplay/Combat/AttackDriverComponent.h"
#include "Runtime/Gameplay/Combat/WeaponTraceComponent.h"
#include "Runtime/Gameplay/Sockets/SocketComponent.h"
#include "Runtime/Gameplay/Sockets/SocketPoseOutputComponent.h"
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

        bool TryGetSocketWorldMatrixOnEntity(World& world, EntityId owner, const std::string& socketName, XMMATRIX& out)
        {
            if (auto* poses = world.GetComponent<SocketPoseOutputComponent>(owner))
            {
                for (const auto& p : poses->poses)
                {
                    if (p.name == socketName)
                    {
                        out = XMLoadFloat4x4(&p.world);
                        return true;
                    }
                }
            }

            if (auto* adv = world.GetComponent<AdvancedAnimationComponent>(owner))
            {
                for (const auto& s : adv->sockets)
                {
                    if (s.name == socketName)
                    {
                        out = XMLoadFloat4x4(&s.worldMatrix);
                        return true;
                    }
                }
                for (const auto& s : adv->sockets)
                {
                    if (s.parentBone == socketName)
                    {
                        out = XMLoadFloat4x4(&s.worldMatrix);
                        return true;
                    }
                }
            }

            if (auto* sc = world.GetComponent<SocketComponent>(owner))
            {
                for (const auto& s : sc->sockets)
                {
                    if (s.name == socketName)
                    {
                        out = XMLoadFloat4x4(&s.world);
                        return true;
                    }
                }
                for (const auto& s : sc->sockets)
                {
                    if (s.parentBone == socketName)
                    {
                        out = XMLoadFloat4x4(&s.world);
                        return true;
                    }
                }
            }

            return false;
        }

        bool TryGetSocketWorldMatrixRecursive(World& world, EntityId owner, const std::string& socketName, XMMATRIX& out)
        {
            if (TryGetSocketWorldMatrixOnEntity(world, owner, socketName, out))
                return true;

            std::vector<EntityId> stack = world.GetChildren(owner);
            for (size_t i = 0; i < stack.size(); ++i)
            {
                const EntityId child = stack[i];
                if (TryGetSocketWorldMatrixOnEntity(world, child, socketName, out))
                    return true;

                auto kids = world.GetChildren(child);
                if (!kids.empty())
                    stack.insert(stack.end(), kids.begin(), kids.end());
            }
            return false;
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

        XMFLOAT3 DegToRad(const XMFLOAT3& deg)
        {
            return XMFLOAT3(XMConvertToRadians(deg.x), XMConvertToRadians(deg.y), XMConvertToRadians(deg.z));
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

        Basis BuildBasisFromQuaternion(const XMFLOAT4& rotQ)
        {
            XMFLOAT4 q = rotQ;
            if ((q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w) <= kVectorEpsilonSq)
                q = XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);

            XMFLOAT4X4 m{};
            XMStoreFloat4x4(&m, XMMatrixRotationQuaternion(XMLoadFloat4(&q)));

            Basis basis{};
            basis.right = NormalizeOrFallback(XMFLOAT3(m._11, m._12, m._13), XMFLOAT3(1.0f, 0.0f, 0.0f));
            basis.up = NormalizeOrFallback(XMFLOAT3(m._21, m._22, m._23), XMFLOAT3(0.0f, 1.0f, 0.0f));
            basis.forward = NormalizeOrFallback(XMFLOAT3(m._31, m._32, m._33), XMFLOAT3(0.0f, 0.0f, 1.0f));
            return basis;
        }

        XMFLOAT3 RotateLocalOffset(const XMFLOAT3& local, const Basis& basis)
        {
            const XMFLOAT3 xTerm = Mul(basis.right, local.x);
            const XMFLOAT3 yTerm = Mul(basis.up, local.y);
            const XMFLOAT3 zTerm = Mul(basis.forward, local.z);
            return Add(Add(xTerm, yTerm), zTerm);
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
        }

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
                m_attackTraceActive = false;
                m_attackTraceComboIndex = -1;
                m_slashSpawnedThisAttack = false;
                m_attackElapsedSec = 0.0f;
                m_attackDurationSec = 0.0f;
                m_hasAttackStartPos = false;
            }

            m_prevIsLightAttack = isLightAttack;
            m_prevPlayerHitActive = playerFlags.hitActive;
        }
        else
        {
            m_prevIsLightAttack = false;
            m_prevPlayerHitActive = false;
            m_attackTraceActive = false;
            m_attackTraceComboIndex = -1;
            m_slashSpawnedThisAttack = false;
            m_attackElapsedSec = 0.0f;
            m_attackDurationSec = 0.0f;
            m_hasAttackStartPos = false;
            m_traceAttackInstanceSeen.clear();
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
        m_slashSpawnedThisAttack = false;
        m_attackElapsedSec = 0.0f;
        m_attackDurationSec = 0.0f;
        m_hasAttackStartPos = false;
        m_traceAttackInstanceSeen.clear();
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
                if (slot == SlashSlot && !m_warnedMissingSlashPrefabPath)
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

        bool hasTrackingPose = TryGetWeaponBasisWorld(trackPos, trackForward);
        if (!hasTrackingPose)
            hasTrackingPose = TryGetSocketWorld(trackPos, trackForward);

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
                if (inst.remainingSec <= 0.0f)
                {
                    Release(slot, inst.id);
                    activeList.erase(activeList.begin() + static_cast<std::ptrdiff_t>(i));
                    continue;
                }

                ++i;
            }
        }
    }

    void CombatVfxBridgeScript::DeactivateAllActive()
    {
        for (int slot = 0; slot < SlotCount; ++slot)
        {
            for (const ActiveInstance& inst : m_active[slot])
            {
                if (inst.id != InvalidEntityId)
                    Release(slot, inst.id);
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

    void CombatVfxBridgeScript::SpawnSlashFromAttackWindow()
    {
        const EntityId id = Acquire(SlashSlot);
        if (id == InvalidEntityId)
            return;

        XMFLOAT3 anchorPos = GetPlayerPosition();
        anchorPos.y += Get_slashAnchorPlayerYOffset();
        const XMFLOAT3 playerForward = GetPlayerForward();
        const XMFLOAT3 forwardOffset = Mul(playerForward, Get_slashAnchorPlayerForwardOffset());
        anchorPos = Add(anchorPos, forwardOffset);

        XMFLOAT3 basisPos{};
        XMFLOAT4 basisRot{};
        const bool hasBasisPose = TryGetWeaponBasisPose(basisPos, basisRot);
        if (hasBasisPose)
        {
            ApplySpawnTransformFromRotation(SlashSlot, id, anchorPos, basisRot);
            m_active[SlashSlot].push_back({ id, GetLifeTimeSafe(SlashSlot) });
            return;
        }

        XMFLOAT3 tracePos{};
        XMFLOAT3 traceForward{};
        XMFLOAT3 traceUp{};
        const bool hasTracePose = TryGetTracePoseWorld(tracePos, traceForward, traceUp);
        if (hasTracePose && m_hasAttackStartPos)
        {
            const XMFLOAT3 delta = Sub(tracePos, m_attackStartPos);
            const XMFLOAT3 attackDir = NormalizeOrFallback(delta, traceForward);
            const XMFLOAT3 upHint = NormalizeOrFallback(traceUp, m_attackStartUp);
            ApplySpawnTransform(SlashSlot, id, anchorPos, attackDir, upHint);
        }
        else
        {
            XMFLOAT3 socketPos{};
            XMFLOAT3 socketForward{};
            const bool hasSocket = TryGetSocketWorld(socketPos, socketForward);
            const XMFLOAT3 attackDir = hasSocket ? socketForward : GetPlayerForward();
            ApplySpawnTransform(SlashSlot, id, anchorPos, attackDir, XMFLOAT3(0.0f, 1.0f, 0.0f));
        }

        m_active[SlashSlot].push_back({ id, GetLifeTimeSafe(SlashSlot) });
    }

    void CombatVfxBridgeScript::SpawnHitFromResolve(const DirectX::XMFLOAT3& hitPos, const DirectX::XMFLOAT3& hitNormal)
    {
        const EntityId id = Acquire(HitSlot);
        if (id == InvalidEntityId)
            return;

        const XMFLOAT3 playerPos = GetPlayerPosition();
        const XMFLOAT3 toPlayer = NormalizeOrFallback(Sub(playerPos, hitPos), GetPlayerForward());
        const XMFLOAT3 forward = Mul(toPlayer, -1.0f); // local -Z points to player by default

        ApplySpawnTransform(HitSlot, id, hitPos, forward, hitNormal);
        m_active[HitSlot].push_back({ id, GetLifeTimeSafe(HitSlot) });
    }

    void CombatVfxBridgeScript::OnCombatResolve(EntityId victimId,
                                                EntityId attackerId,
                                                std::uint8_t resolveResult,
                                                float damage,
                                                const DirectX::XMFLOAT3& hitPos,
                                                const DirectX::XMFLOAT3& hitNormal)
    {
        (void)damage;

        if (m_playerId == InvalidEntityId || m_bossId == InvalidEntityId)
            ResolveSessionAndActors(false);

        if (attackerId != m_playerId || victimId != m_bossId)
            return;

        if (resolveResult != static_cast<std::uint8_t>(Combat::ResolveResult::Hit))
            return;

        SpawnHitFromResolve(hitPos, hitNormal);
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
                if (slot == SlashSlot && !m_warnedSlashPoolCapReached)
                {
                    ALICE_LOG_WARN("[CombatVfxBridge] Slash pool cap reached (%d). Increase slashPoolSize if needed.", cap);
                    m_warnedSlashPoolCapReached = true;
                }
                if (slot == HitSlot && !m_warnedHitPoolCapReached)
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
        return id;
    }

    void CombatVfxBridgeScript::Release(int slot, EntityId id)
    {
        World* world = GetWorld();
        if (!world || id == InvalidEntityId)
            return;

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
            if (slot == SlashSlot && !m_warnedSlashInstantiateFailed)
            {
                ALICE_LOG_WARN("[CombatVfxBridge] Failed to instantiate slash prefab: %s", path.c_str());
                m_warnedSlashInstantiateFailed = true;
            }
            if (slot == HitSlot && !m_warnedHitInstantiateFailed)
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

    bool CombatVfxBridgeScript::TryGetWeaponBasisPose(DirectX::XMFLOAT3& outPos, DirectX::XMFLOAT4& outRot)
    {
        World* world = GetWorld();
        if (!world)
            return false;

        const std::string basisName = Get_weaponBasisEntityName();
        if (basisName.empty())
            return false;

        GameObject basisGo = world->FindGameObject(basisName);
        if (!basisGo.IsValid())
        {
            if (!m_warnedMissingWeaponBasis)
            {
                ALICE_LOG_WARN("[CombatVfxBridge] Missing weapon basis entity: '%s'", basisName.c_str());
                m_warnedMissingWeaponBasis = true;
            }
            return false;
        }

        const XMMATRIX worldM = world->ComputeWorldMatrix(basisGo.id());
        XMVECTOR s{};
        XMVECTOR r{};
        XMVECTOR t{};
        if (!XMMatrixDecompose(&s, &r, &t, worldM))
            return false;

        XMStoreFloat3(&outPos, t);
        XMStoreFloat4(&outRot, r);
        return true;
    }

    bool CombatVfxBridgeScript::TryGetWeaponBasisWorld(DirectX::XMFLOAT3& outPos, DirectX::XMFLOAT3& outForward)
    {
        XMFLOAT4 basisRot{};
        if (!TryGetWeaponBasisPose(outPos, basisRot))
            return false;

        const Basis basis = BuildBasisFromQuaternion(basisRot);
        const XMFLOAT3 forwardFromBasis = basis.forward;
        outForward = NormalizeOrFallback(forwardFromBasis, GetPlayerForward());
        return true;
    }

    bool CombatVfxBridgeScript::TryGetSocketWorld(DirectX::XMFLOAT3& outPos, DirectX::XMFLOAT3& outForward)
    {
        World* world = GetWorld();
        if (!world)
            return false;

        std::string ownerName = Get_weaponSocketOwnerName();
        if (ownerName.empty())
            ownerName = Get_playerEntityName();
        if (ownerName.empty() && m_session)
            ownerName = m_session->Get_m_playerName();

        if (ownerName.empty())
            return false;

        GameObject ownerGo = world->FindGameObject(ownerName);
        if (!ownerGo.IsValid())
        {
            if (!m_warnedMissingSocketOwner)
            {
                ALICE_LOG_WARN("[CombatVfxBridge] Missing weapon socket owner: '%s'", ownerName.c_str());
                m_warnedMissingSocketOwner = true;
            }
            return false;
        }

        DirectX::XMFLOAT4X4 ownerWorld{};
        XMStoreFloat4x4(&ownerWorld, world->ComputeWorldMatrix(ownerGo.id()));
        const XMFLOAT3 ownerPos(ownerWorld._41, ownerWorld._42, ownerWorld._43);
        const XMFLOAT3 ownerForward = NormalizeOrFallback(XMFLOAT3(ownerWorld._31, ownerWorld._32, ownerWorld._33), GetPlayerForward());

        if (Get_weaponSocketName().empty())
        {
            outPos = ownerPos;
            outForward = ownerForward;
            return true;
        }

        XMMATRIX socketWorldM = XMMatrixIdentity();
        if (!TryGetSocketWorldMatrixRecursive(*world, ownerGo.id(), Get_weaponSocketName(), socketWorldM))
        {
            if (!m_warnedMissingSocket)
            {
                ALICE_LOG_WARN("[CombatVfxBridge] Missing socket '%s' on owner '%s'",
                    Get_weaponSocketName().c_str(), ownerName.c_str());
                m_warnedMissingSocket = true;
            }

            // Fallback to owner world when socket name is wrong/missing.
            outPos = ownerPos;
            outForward = ownerForward;
            return true;
        }

        DirectX::XMFLOAT4X4 socketWorld{};
        XMStoreFloat4x4(&socketWorld, socketWorldM);
        outPos = XMFLOAT3(socketWorld._41, socketWorld._42, socketWorld._43);
        const XMFLOAT3 socketForward(socketWorld._31, socketWorld._32, socketWorld._33);
        outForward = NormalizeOrFallback(socketForward, ownerForward);
        return true;
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

    void CombatVfxBridgeScript::ApplySpawnTransform(int slot,
                                                    EntityId id,
                                                    const DirectX::XMFLOAT3& anchorPos,
                                                    const DirectX::XMFLOAT3& forward,
                                                    const DirectX::XMFLOAT3& upHint)
    {
        World* world = GetWorld();
        if (!world || id == InvalidEntityId)
            return;

        TransformComponent* tr = world->GetComponent<TransformComponent>(id);
        if (!tr)
            return;

        const Basis basis = BuildBasis(forward, upHint);
        const XMFLOAT3 localOffset = GetOffsetLocal(slot);
        const XMFLOAT3 worldOffset = RotateLocalOffset(localOffset, basis);

        tr->position = Add(anchorPos, worldOffset);

        XMFLOAT3 rotRad = RotationRadFromBasis(basis);
        const XMFLOAT3 rotOffsetRad = DegToRad(GetRotationOffsetDeg(slot));
        rotRad.x += rotOffsetRad.x;
        rotRad.y += rotOffsetRad.y;
        rotRad.z += rotOffsetRad.z;
        tr->rotation = rotRad;

        XMFLOAT3 baseScale = tr->scale;
        const auto itScale = m_cachedBaseScales[slot].find(id);
        if (itScale != m_cachedBaseScales[slot].end())
            baseScale = itScale->second;

        const float scaleMul = GetScaleMulSafe(slot);
        tr->scale = XMFLOAT3(baseScale.x * scaleMul, baseScale.y * scaleMul, baseScale.z * scaleMul);
        world->MarkTransformDirty(id);
    }

    void CombatVfxBridgeScript::ApplySpawnTransformFromRotation(int slot,
                                                                EntityId id,
                                                                const DirectX::XMFLOAT3& anchorPos,
                                                                const DirectX::XMFLOAT4& worldRot)
    {
        World* world = GetWorld();
        if (!world || id == InvalidEntityId)
            return;

        TransformComponent* tr = world->GetComponent<TransformComponent>(id);
        if (!tr)
            return;

        const Basis basis = BuildBasisFromQuaternion(worldRot);
        const XMFLOAT3 localOffset = GetOffsetLocal(slot);
        const XMFLOAT3 worldOffset = RotateLocalOffset(localOffset, basis);
        tr->position = Add(anchorPos, worldOffset);

        XMFLOAT3 rotRad = QuaternionToYPR_Rad(XMLoadFloat4(&worldRot));
        const XMFLOAT3 rotOffsetRad = DegToRad(GetRotationOffsetDeg(slot));
        rotRad.x += rotOffsetRad.x;
        rotRad.y += rotOffsetRad.y;
        rotRad.z += rotOffsetRad.z;
        tr->rotation = rotRad;

        XMFLOAT3 baseScale = tr->scale;
        const auto itScale = m_cachedBaseScales[slot].find(id);
        if (itScale != m_cachedBaseScales[slot].end())
            baseScale = itScale->second;

        const float scaleMul = GetScaleMulSafe(slot);
        tr->scale = XMFLOAT3(baseScale.x * scaleMul, baseScale.y * scaleMul, baseScale.z * scaleMul);
        world->MarkTransformDirty(id);
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

    std::string CombatVfxBridgeScript::GetPrefabPath(int slot) const
    {
        switch (slot)
        {
        case SlashSlot: return Get_slashPrefabPath();
        case HitSlot: return Get_hitPrefabPath();
        default: return std::string();
        }
    }

    int CombatVfxBridgeScript::GetPoolSizeSafe(int slot) const
    {
        const int size = (slot == SlashSlot) ? Get_slashPoolSize() : Get_hitPoolSize();
        return (std::max)(0, size);
    }

    float CombatVfxBridgeScript::GetLifeTimeSafe(int slot) const
    {
        const float v = (slot == SlashSlot) ? Get_slashLifeTimeSec() : Get_hitLifeTimeSec();
        return (v > 0.0f) ? v : 0.01f;
    }

    float CombatVfxBridgeScript::GetScaleMulSafe(int slot) const
    {
        const float v = (slot == SlashSlot) ? Get_slashScaleMul() : Get_hitScaleMul();
        return (v >= 0.0f) ? v : 0.0f;
    }

    DirectX::XMFLOAT3 CombatVfxBridgeScript::GetOffsetLocal(int slot) const
    {
        return (slot == SlashSlot) ? Get_slashOffsetLocal() : Get_hitOffsetLocal();
    }

    DirectX::XMFLOAT3 CombatVfxBridgeScript::GetRotationOffsetDeg(int slot) const
    {
        return (slot == SlashSlot) ? Get_slashRotationOffsetDeg() : Get_hitRotationOffsetDeg();
    }
}
