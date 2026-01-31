#include "Gimmick.h"

#include <algorithm>
#include <cmath>
#include <random>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Input/Input.h"
#include "Runtime/Input/InputTypes.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Physics/Components/Phy_RigidBodyComponent.h"
#include "Runtime/Physics/Components/Phy_ColliderComponent.h"
#include "Runtime/Physics/Components/Phy_MeshColliderComponent.h"
#include "Runtime/Physics/IPhysicsWorld.h"
//TODO : Include 확인 해야함

namespace Alice
{
    namespace
    {
        using namespace DirectX;

        XMFLOAT3 QuaternionToYPR_Rad(FXMVECTOR q)
        {
            XMFLOAT4 qq;
            XMStoreFloat4(&qq, q);
            const float x = qq.x, y = qq.y, z = qq.z, w = qq.w;

            float sinp = 2.0f * (w * x - y * z);
            float pitch = (std::abs(sinp) >= 1.0f) ? std::copysign(XM_PIDIV2, sinp) : std::asin(sinp);

            float siny_cosp = 2.0f * (w * y + x * z);
            float cosy_cosp = 1.0f - 2.0f * (x * x + y * y);
            float yaw = std::atan2(siny_cosp, cosy_cosp);

            float sinr_cosp = 2.0f * (w * z + x * y);
            float cosr_cosp = 1.0f - 2.0f * (x * x + z * z);
            float roll = std::atan2(sinr_cosp, cosr_cosp);

            return XMFLOAT3(pitch, yaw, roll);
        }

        bool DecomposeMatrix(const XMMATRIX& m, XMFLOAT3& position, XMFLOAT3& rotation, XMFLOAT3& scale)
        {
            XMVECTOR s, q, t;
            if (!XMMatrixDecompose(&s, &q, &t, m))
                return false;

            XMStoreFloat3(&position, t);
            XMStoreFloat3(&scale, s);
            rotation = QuaternionToYPR_Rad(q);
            return true;
        }

        float Length(const XMFLOAT3& v)
        {
            return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
        }

        XMFLOAT3 Normalize(const XMFLOAT3& v)
        {
            float len = Length(v);
            if (len <= 0.00001f)
                return XMFLOAT3(0.0f, 1.0f, 0.0f);
            return XMFLOAT3(v.x / len, v.y / len, v.z / len);
        }

        float Dot(const XMFLOAT3& a, const XMFLOAT3& b)
        {
            return a.x * b.x + a.y * b.y + a.z * b.z;
        }

        XMFLOAT3 Cross(const XMFLOAT3& a, const XMFLOAT3& b)
        {
            XMVECTOR va = XMLoadFloat3(&a);
            XMVECTOR vb = XMLoadFloat3(&b);
            XMVECTOR vc = XMVector3Cross(va, vb);
            XMFLOAT3 out{};
            XMStoreFloat3(&out, vc);
            return out;
        }

        XMFLOAT3 RotateAroundAxis(const XMFLOAT3& v, const XMFLOAT3& axis, float angle)
        {
            XMVECTOR vv = XMLoadFloat3(&v);
            XMVECTOR aa = XMLoadFloat3(&axis);
            XMVECTOR q = XMQuaternionRotationAxis(aa, angle);
            XMVECTOR rotated = XMVector3Rotate(vv, q);
            XMFLOAT3 out{};
            XMStoreFloat3(&out, rotated);
            return out;
        }

        XMFLOAT3 RandomUnitVector(std::mt19937& gen)
        {
            std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
            XMFLOAT3 v{ dist(gen), dist(gen), dist(gen) };
            return Normalize(v);
        }

        float Clamp(float v, float minV, float maxV)
        {
            return std::max(minV, std::min(maxV, v));
        }

        float SmoothStep(float t)
        {
            float clamped = Clamp(t, 0.0f, 1.0f);
            return clamped * clamped * (3.0f - 2.0f * clamped);
        }

        float ComputeAcceleratedSpeed(float baseSpeed, float accel, float distance)
        {
            return std::max(0.0f, baseSpeed + accel * std::max(0.0f, distance));
        }
    }

    REGISTER_SCRIPT(Gimmick);

    void Gimmick::Start()
    {
        m_rng = std::mt19937(std::random_device{}());
        FindEntities();

        m_initialized = (m_weaponCombined != InvalidEntityId && m_core != InvalidEntityId && m_eye != InvalidEntityId);
        if (!m_initialized)
        {
            ALICE_LOG_WARN("[Gimmick] Missing required entities. WeaponCombined=%llu Core=%llu Eye=%llu",
                static_cast<unsigned long long>(m_weaponCombined),
                static_cast<unsigned long long>(m_core),
                static_cast<unsigned long long>(m_eye));
        }

        EnterPhase(Phase::Normal);
    }

    void Gimmick::Update(float deltaTime)
    {
        if (!m_initialized)
            return;

        auto* input = Input();
        if (input && input->GetKeyDown(KeyCode::Space))
            AdvancePhase();

        if (m_pendingBreakImpulse && CanApplyBreakImpulse())
        {
            ApplyBreakImpulse();
            m_pendingBreakImpulse = false;
        }

        m_phaseTime += deltaTime;

        switch (m_phase)
        {
        case Phase::Magnetize:
            UpdateMagnetize(deltaTime);
            break;
        case Phase::AssembleShards:
            UpdateAssembleShards(deltaTime);
            break;
        case Phase::AssembleEye:
            UpdateAssembleEye(deltaTime);
            break;
        default:
            break;
        }
    }

    void Gimmick::FindEntities()
    {
        auto* world = GetWorld();
        if (!world)
            return;

        auto findByName = [&](const std::string& name) -> EntityId {
            GameObject go = world->FindGameObject(name);
            return go.IsValid() ? go.id() : InvalidEntityId;
        };

        m_weaponCombined = findByName(m_weaponCombinedName);
        m_core = findByName(m_coreName);
        m_tendon = findByName(m_tendonName);
        m_eye = findByName(m_eyeName);
        m_bindTarget = findByName(m_bindTargetName);

        if (m_bindTarget == InvalidEntityId && !m_bindTargetName.empty())
        {
            ALICE_LOG_WARN("[Gimmick] Bind target not found: %s (fallback to Core)", m_bindTargetName.c_str());
        }

        m_shards.clear();
        const char* shardNames[] =
        {
            "WB_Base",
            "WB_Back",
            "WB_FrontA",
            "WB_FrontB",
            "WB_FrontC",
            "WB_FrontD"
        };

        for (const char* name : shardNames)
        {
            EntityId id = findByName(name);
            if (id != InvalidEntityId)
            {
                ShardState shard{};
                shard.id = id;
                shard.name = name;
                m_shards.push_back(shard);
            }
            else
            {
                ALICE_LOG_WARN("[Gimmick] Shard not found: %s", name);
            }
        }

        m_assembleOrder.clear();
        for (const char* name : shardNames)
        {
            for (size_t i = 0; i < m_shards.size(); ++i)
            {
                if (m_shards[i].name == name)
                {
                    m_assembleOrder.push_back(i);
                    break;
                }
            }
        }
    }

    void Gimmick::AdvancePhase()
    {
        switch (m_phase)
        {
        case Phase::Normal:
            EnterPhase(Phase::Break);
            break;
        case Phase::Break:
            EnterPhase(Phase::Magnetize);
            break;
        case Phase::Magnetize:
            EnterPhase(Phase::AssembleShards);
            break;
        case Phase::AssembleShards:
            EnterPhase(Phase::AssembleEye);
            break;
        case Phase::AssembleEye:
            EnterPhase(Phase::Restore);
            break;
        default:
            EnterPhase(Phase::Normal);
            break;
        }
    }

    void Gimmick::EnterPhase(Phase phase)
    {
        auto* world = GetWorld();
        if (!world)
            return;

        auto prewarmHiddenPart = [&](EntityId id)
        {
            if (id == InvalidEntityId)
                return;
            SetColliderTrigger(id, true);
            SetIgnoreLayers(id, m_ignoreLayersMask);
            AddIgnoreSelfLayer(id);
            // Keep kinematic + no gravity so it won't fall while hidden.
            SetRigidBodyKinematic(id, true, false);
            ClearRigidBodyVelocity(id);
            SetVisible(id, false);
            SetEnabled(id, true);
        };

        m_phase = phase;
        m_phaseTime = 0.0f;

        if (phase == Phase::Restore)
        {
            SetEnabled(m_weaponCombined, true);
            SetVisible(m_core, false);
            SetVisible(m_tendon, true);
            SetEnabled(m_tendon, false);
            SetMaterialTransparent(m_tendon, false);
           // SetMaterialAlpha(m_tendon, 1.0f);
            prewarmHiddenPart(m_eye);

            for (auto& shard : m_shards)
            {
                prewarmHiddenPart(shard.id);
            }

            ResetShardState();
            m_eyeFloatAnchorValid = false;
            m_phase = Phase::Normal;
            return;
        }

        if (phase == Phase::Normal)
        {
            SetEnabled(m_weaponCombined, true);
            SetVisible(m_core, false);
            SetVisible(m_tendon, true);
            SetEnabled(m_tendon, false);
            SetMaterialTransparent(m_tendon, false);
           // SetMaterialAlpha(m_tendon, 1.0f);
            prewarmHiddenPart(m_eye);

            for (auto& shard : m_shards)
            {
                prewarmHiddenPart(shard.id);
            }

            ResetShardState();
            m_eyeFloatAnchorValid = false;
            return;
        }

        if (phase == Phase::Break)
        {
            SetEnabled(m_tendon, true);
            SetVisible(m_tendon, false);            
            SetVisible(m_core, true);
            SetVisible(m_eye, true);
            SetEnabled(m_weaponCombined, false);
            SetMaterialTransparent(m_tendon, false);
            //SetMaterialAlpha(m_tendon, 1.0f);

            for (auto& shard : m_shards)
            {
                SetEnabled(shard.id, true);
                SetVisible(shard.id, true);
                SetColliderTrigger(shard.id, false);
                SetIgnoreLayers(shard.id, m_ignoreLayersMask);
                AddIgnoreSelfLayer(shard.id);
                // Break 임펄스가 들어가기 전까지는 잠시 고정
                SetRigidBodyKinematic(shard.id, true, false);
                ClearRigidBodyVelocity(shard.id);                
            }

            if (m_eye != InvalidEntityId)
            {
                SetColliderTrigger(m_eye, false);
                SetIgnoreLayers(m_eye, m_ignoreLayersMask);
                AddIgnoreSelfLayer(m_eye);
                // Break 임펄스가 들어가기 전까지는 잠시 고정
                SetRigidBodyKinematic(m_eye, true, false);
                ClearRigidBodyVelocity(m_eye);
                SetEnabled(m_eye, true);
            }

            for (auto& shard : m_shards)
            {
                if (shard.id != InvalidEntityId)
                    world->SetParent(shard.id, InvalidEntityId, true);
            }
            if (m_eye != InvalidEntityId)
            {
                world->SetParent(m_eye, InvalidEntityId, true);
            }

            XMFLOAT3 spawnPos{};
            XMFLOAT3 spawnRot{};
            XMFLOAT3 spawnScale{};
            if (!GetBindTargetWorldPose(spawnPos, spawnRot, spawnScale))
            {
                spawnPos = XMFLOAT3(0.0f, 0.0f, 0.0f);
                spawnRot = XMFLOAT3(0.0f, 0.0f, 0.0f);
            }

            std::uniform_real_distribution<float> jitterDist(-0.2f, 0.2f);

            for (auto& shard : m_shards)
            {
                if (auto* tr = world->GetComponent<TransformComponent>(shard.id))
                {
                    XMFLOAT3 jitter{ jitterDist(m_rng), jitterDist(m_rng), jitterDist(m_rng) };
                    tr->position = XMFLOAT3(spawnPos.x + jitter.x, spawnPos.y + jitter.y, spawnPos.z + jitter.z);
                    tr->rotation = spawnRot;
                    tr->scale = XMFLOAT3(1.0f, 1.0f, 1.0f);
                    TeleportRigidBody(shard.id);
                    world->MarkTransformDirty(shard.id);
                }
            }

            if (auto* eyeTr = world->GetComponent<TransformComponent>(m_eye))
            {
                XMFLOAT3 jitter{ jitterDist(m_rng), jitterDist(m_rng), jitterDist(m_rng) };
                eyeTr->position = XMFLOAT3(spawnPos.x + jitter.x, spawnPos.y + jitter.y, spawnPos.z + jitter.z);
                eyeTr->rotation = spawnRot;
                eyeTr->scale = XMFLOAT3(1.0f, 1.0f, 1.0f);
                TeleportRigidBody(m_eye);
                world->MarkTransformDirty(m_eye);
            }

            ResetShardState();
            m_pendingBreakImpulse = true;
            return;
        }

        if (phase == Phase::Magnetize)
        {
            ResetShardState();
            m_captureTimer = 0.0f;
            m_magnetizeInitialized = false;
            m_eyeFloatAnchorValid = false;
            if (auto* tr = world->GetComponent<TransformComponent>(m_eye))
            {
                m_eyeFloatAnchor = tr->position;
                m_eyeFloatAnchorValid = true;
            }

            SetColliderTrigger(m_eye, true);
            SetRigidBodyKinematic(m_eye, true, false);
            ClearRigidBodyVelocity(m_eye);

            for (auto& shard : m_shards)
            {
                SetColliderTrigger(shard.id, true);
                SetRigidBodyKinematic(shard.id, true, false);
                ClearRigidBodyVelocity(shard.id);
            }

            return;
        }

        if (phase == Phase::AssembleShards)
        {
            m_nextAssembleIndex = 0;
            m_assembleTimer = 0.0f;

            for (auto& shard : m_shards)
            {
                shard.assembling = false;
                shard.assembled = false;
                SetColliderTrigger(shard.id, true);
                SetRigidBodyKinematic(shard.id, true, false);
                ClearRigidBodyVelocity(shard.id);
            }

            return;
        }

        if (phase == Phase::AssembleEye)
        {
            m_eyeArrived = false;
            m_tendonTimer = 0.0f;
            m_tendonFading = false;
            SetColliderTrigger(m_eye, true);
            SetRigidBodyKinematic(m_eye, true, false);
            ClearRigidBodyVelocity(m_eye);
            return;
        }
    }

    void Gimmick::UpdateMagnetize(float dt)
    {
        auto* world = GetWorld();
        if (!world || m_eye == InvalidEntityId || m_core == InvalidEntityId)
            return;

        UpdateEyeFloat(dt);
        const XMFLOAT3 eyePos = GetEyeWorldPosition();

        if (!m_magnetizeInitialized)
        {
            m_magnetizeInitialized = true;

            float maxPullDistance = 0.0f;
            for (auto& shard : m_shards)
            {
                if (shard.captured)
                    continue;
                if (auto* tr = world->GetComponent<TransformComponent>(shard.id))
                {
                    XMFLOAT3 delta{ tr->position.x - eyePos.x, tr->position.y - eyePos.y, tr->position.z - eyePos.z };
                    float radius = std::max(m_orbitMinRadius, Length(delta));
                    XMFLOAT3 baseDir = Normalize(delta);
                    float scaledRadius = std::max(m_orbitMinRadius, radius * m_orbitRadiusScale);
                    XMFLOAT3 targetPos{ eyePos.x + baseDir.x * scaledRadius,
                                        eyePos.y + baseDir.y * scaledRadius,
                                        eyePos.z + baseDir.z * scaledRadius };
                    XMFLOAT3 pullDelta{ targetPos.x - tr->position.x,
                                        targetPos.y - tr->position.y,
                                        targetPos.z - tr->position.z };
                    maxPullDistance = std::max(maxPullDistance, Length(pullDelta));
                }
            }

            float commonSpeed = ComputeAcceleratedSpeed(m_capturePullBaseSpeed, m_capturePullDistanceAccel, maxPullDistance);
            float duration = (commonSpeed > 0.0f) ? (maxPullDistance / commonSpeed) : 0.0f;
            if (m_capturePullTargetDuration > 0.0f)
                duration = m_capturePullTargetDuration;
            duration = Clamp(duration, m_capturePullMinDuration, m_capturePullMaxDuration);
            commonSpeed = (duration > 0.0f) ? (maxPullDistance / duration) : 0.0f;
            if (commonSpeed <= 0.0f)
                commonSpeed = std::max(0.0f, m_capturePullBaseSpeed);

            for (auto& shard : m_shards)
            {
                if (shard.captured)
                    continue;

                shard.captured = true;
                shard.pulling = true;
                shard.pullTimer = 0.0f;
                shard.orbitBlending = false;
                shard.orbitBlendTimer = 0.0f;
                shard.pullSpeed = commonSpeed;

                if (auto* tr = world->GetComponent<TransformComponent>(shard.id))
                {
                    shard.pullStartPos = tr->position;
                    XMFLOAT3 delta{ tr->position.x - eyePos.x, tr->position.y - eyePos.y, tr->position.z - eyePos.z };
                    float radius = std::max(m_orbitMinRadius, Length(delta));
                    XMFLOAT3 baseDir = Normalize(delta);

                    XMFLOAT3 axis = RandomUnitVector(m_rng);
                    if (std::abs(Dot(axis, baseDir)) > 0.95f)
                    {
                        axis = Normalize(Cross(baseDir, XMFLOAT3(0.0f, 1.0f, 0.0f)));
                        if (Length(axis) < 0.001f)
                            axis = Normalize(Cross(baseDir, XMFLOAT3(1.0f, 0.0f, 0.0f)));
                    }

                    shard.orbitRadius = radius;
                    shard.orbitBaseDir = baseDir;
                    shard.orbitAxis = axis;
                    shard.orbitAngle = 0.0f;

                    float scaledRadius = std::max(m_orbitMinRadius, radius * m_orbitRadiusScale);
                    XMFLOAT3 targetPos{ eyePos.x + baseDir.x * scaledRadius,
                                        eyePos.y + baseDir.y * scaledRadius,
                                        eyePos.z + baseDir.z * scaledRadius };
                    XMFLOAT3 pullDelta{ targetPos.x - shard.pullStartPos.x,
                                        targetPos.y - shard.pullStartPos.y,
                                        targetPos.z - shard.pullStartPos.z };
                    float pullDistance = Length(pullDelta);
                    shard.pullDuration = (commonSpeed > 0.0f) ? (pullDistance / commonSpeed) : 0.0f;
                    shard.orbitAngularSpeed = (radius > 0.001f) ? (commonSpeed / radius) : m_orbitAngularSpeed;
                }
            }
        }

        UpdateOrbitingShards(dt);
    }

    void Gimmick::UpdateAssembleShards(float dt)
    {
        auto* world = GetWorld();
        if (!world || m_core == InvalidEntityId)
            return;

        UpdateEyeFloat(dt);
        UpdateOrbitingShards(dt);

        bool assemblingInProgress = false;
        for (const auto& shard : m_shards)
        {
            if (shard.assembling && !shard.assembled)
            {
                assemblingInProgress = true;
                break;
            }
        }

        if (!assemblingInProgress)
            m_assembleTimer += dt;
        else
            m_assembleTimer = 0.0f;

        if (!assemblingInProgress && m_nextAssembleIndex < m_assembleOrder.size() && m_assembleTimer >= m_assembleInterval)
        {
            size_t shardIndex = m_assembleOrder[m_nextAssembleIndex++];
            if (shardIndex < m_shards.size())
            {
                m_shards[shardIndex].assembling = true;
            }
            m_assembleTimer = 0.0f;
        }

        for (auto& shard : m_shards)
        {
            if (!shard.assembling || shard.assembled)
                continue;

            DirectX::XMFLOAT3 targetPos{};
            DirectX::XMFLOAT3 targetRot{};
            DirectX::XMFLOAT3 targetScale{};
            if (!GetBindTargetWorldPose(targetPos, targetRot, targetScale))
                continue;

            auto* tr = world->GetComponent<TransformComponent>(shard.id);
            if (!tr)
                continue;

            DirectX::XMFLOAT3 delta{ targetPos.x - tr->position.x,
                                     targetPos.y - tr->position.y,
                                     targetPos.z - tr->position.z };
            float dist = Length(delta);
            float speed = ComputeAcceleratedSpeed(m_assembleMoveSpeed, m_assembleDistanceAccel, dist);
            bool arrived = MoveTowards(shard.id, targetPos, targetRot, targetScale, speed, dt);
            if (arrived)
            {
                shard.assembled = true;
                shard.assembling = false;
                if (m_bindTarget != InvalidEntityId)
                    world->SetParent(shard.id, m_bindTarget, false);
                if (auto* tr = world->GetComponent<TransformComponent>(shard.id))
                {
                    tr->position = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
                    tr->rotation = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
                    tr->scale = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
                    world->MarkTransformDirty(shard.id);
                }
            }
        }
    }

    void Gimmick::UpdateAssembleEye(float dt)
    {
        auto* world = GetWorld();
        if (!world || m_eye == InvalidEntityId)
            return;

        DirectX::XMFLOAT3 targetPos{};
        DirectX::XMFLOAT3 targetRot{};
        DirectX::XMFLOAT3 targetScale{};
        if (!GetBindTargetWorldPose(targetPos, targetRot, targetScale))
            return;

        if (!m_eyeArrived)
        {
            if (auto* tr = world->GetComponent<TransformComponent>(m_eye))
            {
                DirectX::XMFLOAT3 delta{ targetPos.x - tr->position.x,
                                         targetPos.y - tr->position.y,
                                         targetPos.z - tr->position.z };
                float dist = Length(delta);
                float speed = ComputeAcceleratedSpeed(m_eyeMoveSpeed, m_eyeDistanceAccel, dist);
                bool arrived = MoveTowards(m_eye, targetPos, targetRot, targetScale, speed, dt);
                if (arrived)
                {
                    m_eyeArrived = true;
                    if (m_bindTarget != InvalidEntityId)
                        world->SetParent(m_eye, m_bindTarget, false);
                    if (auto* eyeTr = world->GetComponent<TransformComponent>(m_eye))
                    {
                        eyeTr->position = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
                        eyeTr->rotation = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
                        eyeTr->scale = DirectX::XMFLOAT3(1.0f, 1.0f, 1.0f);
                        world->MarkTransformDirty(m_eye);
                    }
                }
            }
        }
        else
        {
            if (!m_tendonFading)
            {
                m_tendonFading = true;
                m_tendonTimer = 0.0f;
                SetVisible(m_tendon, true);
                SetMaterialAlpha(m_tendon, 0.0f);
            }

            m_tendonTimer += dt;
            float duration = std::max(0.001f, m_tendonVisibleDelay);
            float t = Clamp(m_tendonTimer / duration, 0.0f, 1.0f);
            float alpha = SmoothStep(t);
            SetMaterialAlpha(m_tendon, alpha);
            if (t >= 1.0f)
            {
                SetMaterialAlpha(m_tendon, 1.0f);
            }
        }
    }

    void Gimmick::ApplyBreakImpulse()
    {
        auto* world = GetWorld();
        if (!world)
            return;

        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_real_distribution<float> impulseDist(0.0f, std::max(0.0f, m_breakMaxImpulse));

        auto applyImpulse = [&](EntityId id, const char* label)
        {
            auto* rb = world->GetComponent<Phy_RigidBodyComponent>(id);
            if (!rb || !rb->physicsActorHandle)
                return;

            IRigidBody* body = rb->physicsActorHandle;
            if (!body || !body->IsValid() || !body->IsInWorld())
                return;

            if (auto* tr = world->GetComponent<TransformComponent>(id))
            {
                const float mass = body->GetMass();
                const float invMass = (mass > 0.0f) ? (1.0f / mass) : 0.0f;
                ALICE_LOG_INFO("[Gimmick] Break pre-impulse %s id=%llu scale=(%.4f, %.4f, %.4f) mass=%.6f invMass=%.3f kinematic=%d",
                    (label ? label : ""),
                    static_cast<unsigned long long>(id),
                    tr->scale.x, tr->scale.y, tr->scale.z,
                    mass, invMass,
                    body->IsKinematic() ? 1 : 0);
            }

            // 엔진 Sync 타이밍과 무관하게 즉시 dynamic 전환
            rb->isKinematic = false;
            rb->gravityEnabled = true;
            body->SetKinematic(false);
            body->SetGravityEnabled(true);
            ClearRigidBodyVelocity(id);
            XMFLOAT3 dir = RandomUnitVector(gen);
            float impulse = impulseDist(gen);
            Vec3 impulseVec(dir.x * impulse, dir.y * impulse, dir.z * impulse);
            body->AddImpulse(impulseVec);
            body->WakeUp();
        };


        for (const auto& shard : m_shards)
            applyImpulse(shard.id, shard.name.c_str());

        applyImpulse(m_eye, "Eye");
    }

    bool Gimmick::CanApplyBreakImpulse() const
    {
        auto* world = GetWorld();
        if (!world)
            return false;

        auto ready = [&](EntityId id) -> bool
        {
            auto* rb = world->GetComponent<Phy_RigidBodyComponent>(id);
            if (!rb || !rb->physicsActorHandle)
                return false;
            IRigidBody* body = rb->physicsActorHandle;
            return body && body->IsValid() && body->IsInWorld();
        };

        for (const auto& shard : m_shards)
        {
            if (!ready(shard.id))
                return false;
        }
        return ready(m_eye);
    }

    void Gimmick::ResetShardState()
    {
        for (auto& shard : m_shards)
        {
            shard.captured = false;
            shard.pulling = false;
            shard.assembling = false;
            shard.assembled = false;
            shard.orbitAngle = 0.0f;
            shard.orbitAxis = DirectX::XMFLOAT3(0.0f, 1.0f, 0.0f);
            shard.orbitBaseDir = DirectX::XMFLOAT3(1.0f, 0.0f, 0.0f);
            shard.orbitRadius = 0.0f;
            shard.pullTimer = 0.0f;
            shard.pullSpeed = 0.0f;
            shard.orbitAngularSpeed = 0.0f;
            shard.orbitAngularStartSpeed = 0.0f;
            shard.orbitBlendTimer = 0.0f;
            shard.orbitBlending = false;
        }
        m_magnetizeInitialized = false;
    }

    void Gimmick::SetEnabled(EntityId id, bool enabled)
    {
        auto* world = GetWorld();
        if (!world || id == InvalidEntityId)
            return;
        if (auto* tr = world->GetComponent<TransformComponent>(id))
        {
            tr->enabled = enabled;
            world->MarkTransformDirty(id);
        }
    }

    void Gimmick::SetVisible(EntityId id, bool visible)
    {
        auto* world = GetWorld();
        if (!world || id == InvalidEntityId)
            return;
        if (auto* tr = world->GetComponent<TransformComponent>(id))
        {
            tr->visible = visible;
        }
    }

    void Gimmick::SetMaterialAlpha(EntityId id, float alpha)
    {
        auto* world = GetWorld();
        if (!world || id == InvalidEntityId)
            return;

        MaterialComponent* mat = world->GetComponent<MaterialComponent>(id);
        if (!mat)
        {
            MaterialComponent& newMat = world->AddComponent<MaterialComponent>(id, DirectX::XMFLOAT3(0.7f, 0.7f, 0.7f));
            mat = &newMat;
        }

        const float clamped = Clamp(alpha, 0.0f, 1.0f);
        mat->Set_alpha(clamped);
        mat->Set_transparent(clamped < 0.999f);
    }

    void Gimmick::SetMaterialTransparent(EntityId id, bool transparent)
    {
        auto* world = GetWorld();
        if (!world || id == InvalidEntityId)
            return;
        MaterialComponent* mat = world->GetComponent<MaterialComponent>(id);
        if (!mat)
        {
            MaterialComponent& newMat = world->AddComponent<MaterialComponent>(id, DirectX::XMFLOAT3(0.7f, 0.7f, 0.7f));
            mat = &newMat;
        }
        mat->Set_transparent(transparent);
    }

    void Gimmick::SetColliderTrigger(EntityId id, bool trigger)
    {
        auto* world = GetWorld();
        if (!world || id == InvalidEntityId)
            return;

        if (auto* col = world->GetComponent<Phy_ColliderComponent>(id))
            col->isTrigger = trigger;
        if (auto* mc = world->GetComponent<Phy_MeshColliderComponent>(id))
            mc->isTrigger = trigger;
    }

    void Gimmick::SetIgnoreLayers(EntityId id, uint32_t mask)
    {
        if (mask == 0u)
            return;
        auto* world = GetWorld();
        if (!world || id == InvalidEntityId)
            return;

        if (auto* col = world->GetComponent<Phy_ColliderComponent>(id))
            col->ignoreLayers = mask;
        if (auto* mc = world->GetComponent<Phy_MeshColliderComponent>(id))
            mc->ignoreLayers = mask;
    }

    void Gimmick::AddIgnoreSelfLayer(EntityId id)
    {
        auto* world = GetWorld();
        if (!world || id == InvalidEntityId)
            return;

        if (auto* col = world->GetComponent<Phy_ColliderComponent>(id))
            col->ignoreLayers |= col->layerBits;
        if (auto* mc = world->GetComponent<Phy_MeshColliderComponent>(id))
            mc->ignoreLayers |= mc->layerBits;
    }

    void Gimmick::SetRigidBodyKinematic(EntityId id, bool kinematic, bool gravityEnabled)
    {
        auto* world = GetWorld();
        if (!world || id == InvalidEntityId)
            return;

        if (auto* rb = world->GetComponent<Phy_RigidBodyComponent>(id))
        {
            rb->isKinematic = kinematic;
            rb->gravityEnabled = gravityEnabled;
            rb->startAwake = true;
        }
    }

    void Gimmick::ClearRigidBodyVelocity(EntityId id)
    {
        auto* world = GetWorld();
        if (!world || id == InvalidEntityId)
            return;

        if (auto* rb = world->GetComponent<Phy_RigidBodyComponent>(id))
        {
            IRigidBody* body = rb->physicsActorHandle;
            if (!body || !body->IsValid())
                return;
            if (!body->IsInWorld())
                return;
            if (body->IsKinematic())
                return;

            body->SetLinearVelocity(Vec3(0.0f, 0.0f, 0.0f));
            body->SetAngularVelocity(Vec3(0.0f, 0.0f, 0.0f));
        }
    }

    void Gimmick::TeleportRigidBody(EntityId id)
    {
        auto* world = GetWorld();
        if (!world || id == InvalidEntityId)
            return;

        if (auto* rb = world->GetComponent<Phy_RigidBodyComponent>(id))
            rb->teleport = true;
    }

    bool Gimmick::MoveTowards(EntityId id, const DirectX::XMFLOAT3& targetPos,
                              const DirectX::XMFLOAT3& targetRot, const DirectX::XMFLOAT3& targetScale,
                              float speed, float dt)
    {
        auto* world = GetWorld();
        if (!world || id == InvalidEntityId)
            return true;

        auto* tr = world->GetComponent<TransformComponent>(id);
        if (!tr)
            return true;

        XMFLOAT3 delta{ targetPos.x - tr->position.x, targetPos.y - tr->position.y, targetPos.z - tr->position.z };
        float dist = Length(delta);
        if (dist <= m_arriveThreshold)
        {
            tr->position = targetPos;
            tr->rotation = targetRot;
            tr->scale = targetScale;
            TeleportRigidBody(id);
            world->MarkTransformDirty(id);
            return true;
        }

        float step = speed * dt;
        float t = (dist > 0.0f) ? std::min(1.0f, step / dist) : 1.0f;
        tr->position.x += delta.x * t;
        tr->position.y += delta.y * t;
        tr->position.z += delta.z * t;
        tr->rotation = targetRot;
        tr->scale = targetScale;

        TeleportRigidBody(id);
        world->MarkTransformDirty(id);
        return false;
    }

    void Gimmick::UpdateEyeFloat(float dt)
    {
        auto* world = GetWorld();
        if (!world || m_eye == InvalidEntityId)
            return;

        auto* tr = world->GetComponent<TransformComponent>(m_eye);
        if (!tr)
            return;

        DirectX::XMFLOAT3 eyeTargetPos{};
        DirectX::XMFLOAT3 eyeTargetRot = tr->rotation;
        DirectX::XMFLOAT3 eyeTargetScale = tr->scale;

        if (!m_eyeFloatAnchorValid)
        {
            m_eyeFloatAnchor = tr->position;
            m_eyeFloatAnchorValid = true;
        }

        const float floatHeight = 2.0f;
        const float bobAmplitude = 0.08f;
        const float bobSpeed = 2.0f;
        eyeTargetPos = m_eyeFloatAnchor;
        eyeTargetPos.y += floatHeight;
        eyeTargetPos.y += std::sin(m_phaseTime * bobSpeed) * bobAmplitude;

        MoveTowards(m_eye, eyeTargetPos, eyeTargetRot, eyeTargetScale, m_eyeFloatMoveSpeed, dt);
    }

    void Gimmick::UpdateOrbitingShards(float dt)
    {
        auto* world = GetWorld();
        if (!world || m_eye == InvalidEntityId)
            return;

        const XMFLOAT3 eyePos = GetEyeWorldPosition();

        for (auto& shard : m_shards)
        {
            if (!shard.captured || shard.assembling || shard.assembled)
                continue;

            const float radius = std::max(m_orbitMinRadius, shard.orbitRadius * m_orbitRadiusScale);
            XMFLOAT3 rotatedDir = RotateAroundAxis(shard.orbitBaseDir, shard.orbitAxis, shard.orbitAngle);
            XMFLOAT3 offset{ rotatedDir.x * radius, rotatedDir.y * radius, rotatedDir.z * radius };
            XMFLOAT3 targetPos{ eyePos.x + offset.x, eyePos.y + offset.y, eyePos.z + offset.z };

            if (auto* tr = world->GetComponent<TransformComponent>(shard.id))
            {
                if (shard.pulling)
                {
                    shard.pullTimer += dt;
                    float t = (shard.pullDuration > 0.0f) ? std::min(1.0f, shard.pullTimer / shard.pullDuration) : 1.0f;
                    float smoothT = SmoothStep(t);
                    tr->position.x = shard.pullStartPos.x + (targetPos.x - shard.pullStartPos.x) * smoothT;
                    tr->position.y = shard.pullStartPos.y + (targetPos.y - shard.pullStartPos.y) * smoothT;
                    tr->position.z = shard.pullStartPos.z + (targetPos.z - shard.pullStartPos.z) * smoothT;
                    if (t >= 1.0f)
                    {
                        shard.pulling = false;
                        shard.orbitBlending = true;
                        shard.orbitBlendTimer = 0.0f;

                        XMFLOAT3 toEye{ tr->position.x - eyePos.x, tr->position.y - eyePos.y, tr->position.z - eyePos.z };
                        float newRadius = Length(toEye);
                        shard.orbitRadius = std::max(m_orbitMinRadius, newRadius);
                        shard.orbitBaseDir = Normalize(toEye);
                        if (shard.orbitRadius > 0.001f)
                            shard.orbitAngularSpeed = std::max(0.0f, shard.pullSpeed / shard.orbitRadius);
                        else
                            shard.orbitAngularSpeed = m_orbitAngularSpeed;
                        shard.orbitAngularStartSpeed = shard.orbitAngularSpeed;
                    }
                }
                else
                {
                    if (shard.orbitAngularSpeed <= 0.0f)
                        shard.orbitAngularSpeed = m_orbitAngularSpeed;

                    if (shard.orbitBlending && m_orbitAngularBlendDuration > 0.0f)
                    {
                        shard.orbitBlendTimer += dt;
                        float blendT = shard.orbitBlendTimer / m_orbitAngularBlendDuration;
                        float smoothT = SmoothStep(blendT);
                        shard.orbitAngularSpeed = shard.orbitAngularStartSpeed +
                            (m_orbitAngularSpeed - shard.orbitAngularStartSpeed) * smoothT;
                        if (blendT >= 1.0f)
                        {
                            shard.orbitBlending = false;
                            shard.orbitAngularSpeed = m_orbitAngularSpeed;
                        }
                    }

                    shard.orbitAngle += shard.orbitAngularSpeed * dt;
                    tr->position = targetPos;
                }
                TeleportRigidBody(shard.id);
                world->MarkTransformDirty(shard.id);
            }
        }
    }

    DirectX::XMFLOAT3 Gimmick::GetEyeWorldPosition() const
    {
        auto* world = GetWorld();
        if (!world || m_eye == InvalidEntityId)
            return DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);

        if (auto* tr = world->GetComponent<TransformComponent>(m_eye))
        {
            return tr->position;
        }

        if (m_eyeFloatAnchorValid)
        {
            const float floatHeight = 2.0f;
            const float bobAmplitude = 0.08f;
            const float bobSpeed = 2.0f;
            DirectX::XMFLOAT3 pos = m_eyeFloatAnchor;
            pos.y += floatHeight;
            pos.y += std::sin(m_phaseTime * bobSpeed) * bobAmplitude;
            return pos;
        }

        return DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
    }

    bool Gimmick::GetBindTargetWorldPose(DirectX::XMFLOAT3& outPos,
                                         DirectX::XMFLOAT3& outRot,
                                         DirectX::XMFLOAT3& outScale) const
    {
        auto* world = GetWorld();
        if (!world)
            return false;

        const EntityId basis = (m_bindTarget != InvalidEntityId) ? m_bindTarget : m_core;
        if (basis == InvalidEntityId)
            return false;

        const XMMATRIX basisWorld = world->ComputeWorldMatrix(basis);
        return DecomposeMatrix(basisWorld, outPos, outRot, outScale);
    }

}
