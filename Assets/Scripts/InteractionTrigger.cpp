#include "InteractionTrigger.h"

#include <algorithm>
#include <cmath>

#include "Runtime/ECS/World.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Physics/IPhysicsWorld.h"
#include "Runtime/Physics/Components/Phy_ColliderComponent.h"
#include "Runtime/Scripting/ScriptFactory.h"

#include "Combat/C_CombatSessionComponent.h"

#include <DirectXMath.h>

namespace Alice
{
    REGISTER_SCRIPT(InteractionTrigger);

    namespace
    {
        bool DecomposeWorldPose(World* world, EntityId id,
                                DirectX::XMFLOAT3& outPos,
                                DirectX::XMFLOAT3& outScale,
                                Quat& outRot)
        {
            if (!world || id == InvalidEntityId)
                return false;

            using namespace DirectX;
            const XMMATRIX m = world->ComputeWorldMatrix(id);
            XMVECTOR s{};
            XMVECTOR r{};
            XMVECTOR t{};
            if (!XMMatrixDecompose(&s, &r, &t, m))
                return false;

            XMStoreFloat3(&outPos, t);
            XMStoreFloat3(&outScale, s);
            XMFLOAT4 rotQ{};
            XMStoreFloat4(&rotQ, r);
            outRot = Quat(rotQ.x, rotQ.y, rotQ.z, rotQ.w);
            return true;
        }

        DirectX::XMFLOAT3 AbsScale(const DirectX::XMFLOAT3& v)
        {
            return DirectX::XMFLOAT3(std::abs(v.x), std::abs(v.y), std::abs(v.z));
        }

        Vec3 RotateOffset(const DirectX::XMFLOAT3& localOffset, const Quat& rot)
        {
            using DirectX::SimpleMath::Vector3;
            const Vector3 v(localOffset.x, localOffset.y, localOffset.z);
            const Vector3 rotated = Vector3::Transform(v, rot);
            return Vec3(rotated.x, rotated.y, rotated.z);
        }
    }

    void InteractionTrigger::Start()
    {
        m_used = false;
        m_lastEnabled = false;
        m_warnedMissingCollider = false;
        m_warnedMissingSession = false;
        m_warnedMissingPlayer = false;
    }

    void InteractionTrigger::OnDisable()
    {
        auto* world = GetWorld();
        if (!world)
            return;

        if (auto* session = ResolveCombatSession(world))
        {
            ApplyInteractionEnabled(session, false);
        }
    }

    void InteractionTrigger::Update(float /*deltaTime*/)
    {
        auto* world = GetWorld();
        if (!world)
            return;

        auto* session = ResolveCombatSession(world);
        if (!session)
        {
            if (!m_warnedMissingSession && Get_m_enableLogs())
            {
                ALICE_LOG_WARN("[InteractionTrigger] Combat session not found. name=%s", Get_m_combatSessionName().c_str());
                m_warnedMissingSession = true;
            }
            return;
        }

        const EntityId playerId = ResolvePlayerId(world);
        if (playerId == InvalidEntityId)
        {
            if (!m_warnedMissingPlayer && Get_m_enableLogs())
            {
                ALICE_LOG_WARN("[InteractionTrigger] Player not found. name=%s guid=%llu",
                    Get_m_playerName().c_str(),
                    static_cast<unsigned long long>(Get_m_playerGuid()));
                m_warnedMissingPlayer = true;
            }
            ApplyInteractionEnabled(session, false);
            return;
        }

        if (m_used)
        {
            ApplyInteractionEnabled(session, false);
            return;
        }

        const bool inside = IsPlayerInside(world, playerId);
        ApplyInteractionEnabled(session, inside);

        if (inside && Get_m_oneShot())
        {
            if (session->GetPlayerState() == Combat::ActionState::Interaction)
            {
                m_used = true;
                ApplyInteractionEnabled(session, false);
            }
        }
    }

    EntityId InteractionTrigger::ResolvePlayerId(World* world)
    {
        if (!world)
            return InvalidEntityId;

        if (m_cachedPlayerId != InvalidEntityId &&
            world->IsEntityValid(m_cachedPlayerId, m_cachedPlayerGen))
        {
            return m_cachedPlayerId;
        }

        m_cachedPlayerId = InvalidEntityId;
        m_cachedPlayerGen = 0;

        if (Get_m_playerGuid() != 0)
        {
            m_cachedPlayerId = world->FindEntityByGuid(Get_m_playerGuid());
        }

        if (m_cachedPlayerId == InvalidEntityId && !Get_m_playerName().empty())
        {
            GameObject go = world->FindGameObject(Get_m_playerName());
            if (go.IsValid())
                m_cachedPlayerId = go.id();
        }

        if (m_cachedPlayerId != InvalidEntityId)
            m_cachedPlayerGen = world->GetEntityGeneration(m_cachedPlayerId);

        return m_cachedPlayerId;
    }

    C_CombatSessionComponent* InteractionTrigger::ResolveCombatSession(World* world)
    {
        if (!world)
            return nullptr;

        if (m_cachedSessionId != InvalidEntityId &&
            world->IsEntityValid(m_cachedSessionId, m_cachedSessionGen))
        {
            return world->GetComponent<C_CombatSessionComponent>(m_cachedSessionId);
        }

        m_cachedSessionId = InvalidEntityId;
        m_cachedSessionGen = 0;

        if (!Get_m_combatSessionName().empty())
        {
            GameObject go = world->FindGameObject(Get_m_combatSessionName());
            if (go.IsValid())
            {
                if (auto* session = go.GetComponent<C_CombatSessionComponent>())
                {
                    m_cachedSessionId = go.id();
                    m_cachedSessionGen = world->GetEntityGeneration(m_cachedSessionId);
                    return session;
                }
            }
        }

        const auto& scripts = world->GetAllScriptsInWorld();
        for (const auto& [id, list] : scripts)
        {
            for (const auto& scriptComp : list)
            {
                if (!scriptComp.instance)
                    continue;
                auto* session = dynamic_cast<C_CombatSessionComponent*>(scriptComp.instance.get());
                if (!session)
                    continue;
                m_cachedSessionId = id;
                m_cachedSessionGen = world->GetEntityGeneration(id);
                return session;
            }
        }

        return nullptr;
    }

    bool InteractionTrigger::IsPlayerInside(World* world, EntityId playerId)
    {
        if (!world || playerId == InvalidEntityId)
            return false;

        auto* physics = world->GetPhysicsWorld();
        if (!physics)
            return false;

        auto* collider = world->GetComponent<Phy_ColliderComponent>(GetOwnerId());
        if (!collider)
        {
            if (!m_warnedMissingCollider && Get_m_enableLogs())
            {
                ALICE_LOG_WARN("[InteractionTrigger] Missing Phy_ColliderComponent on trigger entity.");
                m_warnedMissingCollider = true;
            }
            return false;
        }

        DirectX::XMFLOAT3 worldPos{};
        DirectX::XMFLOAT3 worldScale{};
        Quat worldRot{};
        if (!DecomposeWorldPose(world, GetOwnerId(), worldPos, worldScale, worldRot))
            return false;

        const DirectX::XMFLOAT3 absScale = AbsScale(worldScale);
        DirectX::XMFLOAT3 localOffset = collider->offset;
        localOffset.x *= absScale.x;
        localOffset.y *= absScale.y;
        localOffset.z *= absScale.z;

        const Vec3 offsetWS = RotateOffset(localOffset, worldRot);
        const Vec3 center(
            worldPos.x + offsetWS.x,
            worldPos.y + offsetWS.y,
            worldPos.z + offsetWS.z);

        SceneQueryFilter filter{};
        filter.layerMask = 0xFFFFFFFFu;
        filter.queryMask = 0xFFFFFFFFu;
        filter.hitTriggers = true;
        if (collider->physicsActorHandle)
        {
            filter.ignoreNativeActor = collider->physicsActorHandle->GetNativeActor();
            filter.ignoreUserData = collider->physicsActorHandle->GetUserData();
        }

        std::vector<OverlapHit> hits;
        hits.reserve(16);
        uint32_t hitCount = 0;

        switch (collider->type)
        {
        case ColliderType::Box:
        {
            Vec3 he(
                collider->halfExtents.x * absScale.x,
                collider->halfExtents.y * absScale.y,
                collider->halfExtents.z * absScale.z);
            hitCount = physics->OverlapBoxQ(center, worldRot, he, hits, filter, 16);
            break;
        }
        case ColliderType::Sphere:
        {
            const float sMax = std::max({ absScale.x, absScale.y, absScale.z });
            const float radius = collider->radius * sMax;
            hitCount = physics->OverlapSphereQ(center, radius, hits, filter, 16);
            break;
        }
        case ColliderType::Capsule:
        {
            float radius = 0.0f;
            float halfHeight = 0.0f;
            if (collider->capsuleAlignYAxis)
            {
                const float radial = std::max(absScale.x, absScale.z);
                radius = collider->capsuleRadius * radial;
                halfHeight = collider->capsuleHalfHeight * absScale.y;
            }
            else
            {
                const float radial = std::max(absScale.y, absScale.z);
                radius = collider->capsuleRadius * radial;
                halfHeight = collider->capsuleHalfHeight * absScale.x;
            }
            hitCount = physics->OverlapCapsuleQ(center, worldRot, radius, halfHeight,
                                                hits, filter, 16, collider->capsuleAlignYAxis);
            break;
        }
        default:
            return false;
        }

        if (hitCount == 0)
            return false;

        for (const auto& hit : hits)
        {
            const EntityId hitId = world->ExtractEntityIdFromUserData(hit.userData);
            if (hitId == playerId)
                return true;
        }

        return false;
    }

    void InteractionTrigger::ApplyInteractionEnabled(C_CombatSessionComponent* session, bool enabled)
    {
        if (!session)
            return;

        if (m_lastEnabled == enabled)
            return;

        session->Set_m_playerInteractionEnabled(enabled);
        m_lastEnabled = enabled;
    }
}
