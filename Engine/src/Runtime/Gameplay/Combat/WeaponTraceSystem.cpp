#include "Runtime/Gameplay/Combat/WeaponTraceSystem.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <DirectXMath.h>

#include "Runtime/ECS/World.h"
#include "Runtime/ECS/Components/IDComponent.h"
#include "Runtime/Gameplay/Combat/WeaponTraceComponent.h"
#include "Runtime/Gameplay/Combat/HurtboxComponent.h"
#include "Runtime/Gameplay/Combat/CombatPhysicsLayers.h"
#include "Runtime/Gameplay/Combat/CombatHitEvent.h"
#include "Runtime/Physics/IPhysicsWorld.h"
#include "Runtime/Foundation/Logger.h"

namespace Alice
{
	namespace
	{
		EntityId ResolveOwner(World& world, WeaponTraceComponent& trace, EntityId self)
		{
			if (trace.ownerCached != InvalidEntityId)
			{
				if (trace.ownerGuid == 0)
					return trace.ownerCached;

				if (const auto* idc = world.GetComponent<IDComponent>(trace.ownerCached))
				{
					if (idc->guid == trace.ownerGuid)
						return trace.ownerCached;
				}
			}

			if (trace.ownerGuid == 0)
				return self;

			EntityId resolved = world.FindEntityByGuid(trace.ownerGuid);
			if (resolved == InvalidEntityId)
				return InvalidEntityId;

			trace.ownerCached = resolved;
			return resolved;
		}

        EntityId ResolveTraceBasis(World& world, WeaponTraceComponent& trace, EntityId self)
        {
            if (trace.traceBasisGuid == 0)
            {
                trace.traceBasisCached = InvalidEntityId;
                return self;
            }

            if (trace.traceBasisCached != InvalidEntityId)
            {
                if (const auto* idc = world.GetComponent<IDComponent>(trace.traceBasisCached))
                {
                    if (idc->guid == trace.traceBasisGuid)
                        return trace.traceBasisCached;
                }
            }

            EntityId resolved = world.FindEntityByGuid(trace.traceBasisGuid);
            if (resolved == InvalidEntityId)
                return InvalidEntityId;

			trace.traceBasisCached = resolved;
			return resolved;
		}

		bool TryGetBasisPose(World& world, EntityId basis, DirectX::XMFLOAT3& outPos, DirectX::XMFLOAT4& outRot)
		{
			const DirectX::XMMATRIX worldMatrix = world.ComputeWorldMatrix(basis);
			DirectX::XMVECTOR s, r, t;
			if (!DirectX::XMMatrixDecompose(&s, &r, &t, worldMatrix))
				return false;

			DirectX::XMStoreFloat3(&outPos, t);
			DirectX::XMStoreFloat4(&outRot, r);
			return true;
		}

		DirectX::XMMATRIX BuildBasisWorldMatrix(const DirectX::XMFLOAT3& pos, const DirectX::XMFLOAT4& rot)
		{
			using namespace DirectX;
			const XMVECTOR q = XMLoadFloat4(&rot);
			const XMMATRIX R = XMMatrixRotationQuaternion(q);
			const XMMATRIX T = XMMatrixTranslation(pos.x, pos.y, pos.z);
			return R * T;
		}

		float ToShapePathPhase(float globalPhase)
		{
			return std::clamp(globalPhase, 0.0f, 1.0f);
		}

		bool ResolveShapeStepPathPhase(float globalPhase0,
		                               float globalPhase1,
		                               float& outPathPhase0,
		                               float& outPathPhase1)
		{
			outPathPhase0 = ToShapePathPhase(globalPhase0);
			outPathPhase1 = ToShapePathPhase(globalPhase1);
			return true;
		}

		DirectX::XMFLOAT3 ResolveShapeLocalPos(const WeaponTraceShape& shape, float phase)
		{
			if (!shape.pathEnabled)
				return shape.localPos;

			const float t = std::clamp(phase, 0.0f, 1.0f);
			switch (shape.pathMode)
			{
			case WeaponTracePathMode::QuadraticBezier:
			{
				const float omt = 1.0f - t;
				const float w0 = omt * omt;
				const float w1 = 2.0f * omt * t;
				const float w2 = t * t;
				return DirectX::XMFLOAT3(
					(w0 * shape.pathStartLocalPos.x) + (w1 * shape.pathControlLocalPos.x) + (w2 * shape.pathEndLocalPos.x),
					(w0 * shape.pathStartLocalPos.y) + (w1 * shape.pathControlLocalPos.y) + (w2 * shape.pathEndLocalPos.y),
					(w0 * shape.pathStartLocalPos.z) + (w1 * shape.pathControlLocalPos.z) + (w2 * shape.pathEndLocalPos.z));
			}
			case WeaponTracePathMode::Linear:
			default:
				return DirectX::XMFLOAT3(
					shape.pathStartLocalPos.x + ((shape.pathEndLocalPos.x - shape.pathStartLocalPos.x) * t),
					shape.pathStartLocalPos.y + ((shape.pathEndLocalPos.y - shape.pathStartLocalPos.y) * t),
					shape.pathStartLocalPos.z + ((shape.pathEndLocalPos.z - shape.pathStartLocalPos.z) * t));
			}
		}

		DirectX::XMMATRIX BuildShapeLocalMatrix(const WeaponTraceShape& shape, const DirectX::XMFLOAT3& localPos)
		{
			using namespace DirectX;
			const float rx = XMConvertToRadians(shape.localRotDeg.x);
			const float ry = XMConvertToRadians(shape.localRotDeg.y);
			const float rz = XMConvertToRadians(shape.localRotDeg.z);
			const XMMATRIX R = XMMatrixRotationRollPitchYaw(rx, ry, rz);
			const XMMATRIX T = XMMatrixTranslation(localPos.x, localPos.y, localPos.z);
			return R * T;
		}

		bool ComputeShapeWorldPose(const WeaponTraceShape& shape, const DirectX::XMMATRIX& basisWorld, float phase,
			DirectX::XMFLOAT3& outCenter, DirectX::XMFLOAT4& outRot)
		{
			using namespace DirectX;
			const DirectX::XMFLOAT3 localPos = ResolveShapeLocalPos(shape, phase);
			const XMMATRIX local = BuildShapeLocalMatrix(shape, localPos);
			const XMMATRIX world = local * basisWorld;
			XMVECTOR s, r, t;
			if (!XMMatrixDecompose(&s, &r, &t, world))
				return false;

			XMStoreFloat3(&outCenter, t);
			XMStoreFloat4(&outRot, r);
			return true;
		}
	}

	void WeaponTraceSystem::Update(World& world, float dtSec, std::vector<CombatHitEvent>* outHits)
	{
		auto* physics = world.GetPhysicsWorld();
		if (!physics)
			return;

		auto&& traces = world.GetComponents<WeaponTraceComponent>(); // & -> &&�� �ٲ�
		if (traces.empty())
			return;

		using namespace DirectX;

		for (auto&& [eid, trace] : traces)
		{
			trace.debugSweepSegments.clear();
			if (!trace.active)
			{
				trace.hasPrevBasis = false;
				trace.hasPrevShapes = false;
				trace.prevCentersWS.clear();
				trace.prevRotsWS.clear();
				trace.hitVictims.clear();
				trace.activeElapsedSec = 0.0f;
				trace.activeWindowDurationSec = 0.0f;
				continue;
			}

			const float frameStartElapsedSec = trace.activeElapsedSec;
			const float safeDtSec = std::max(0.0f, dtSec);
			trace.activeElapsedSec = std::max(0.0f, trace.activeElapsedSec + safeDtSec);
			const float frameEndElapsedSec = trace.activeElapsedSec;
			auto ResolvePhase = [&](float elapsedSec) -> float {
				if (trace.activeWindowDurationSec <= 0.0f)
					return 1.0f;
				return std::clamp(elapsedSec / trace.activeWindowDurationSec, 0.0f, 1.0f);
			};
			const float framePhaseStart = ResolvePhase(frameStartElapsedSec);
			const float framePhaseEnd = ResolvePhase(frameEndElapsedSec);

			const EntityId owner = ResolveOwner(world, trace, eid);
			const EntityId basis = ResolveTraceBasis(world, trace, eid);
			if (owner == InvalidEntityId || basis == InvalidEntityId)
				continue;

			if (trace.lastAttackInstanceId != trace.attackInstanceId)
			{
				trace.hitVictims.clear();
				trace.lastAttackInstanceId = trace.attackInstanceId;
			}

			const size_t shapeCount = trace.shapes.size();
			if (trace.prevCentersWS.size() != shapeCount || trace.prevRotsWS.size() != shapeCount)
			{
				trace.prevCentersWS.assign(shapeCount, DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f));
				trace.prevRotsWS.assign(shapeCount, DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f));
				trace.hasPrevShapes = false;
			}
			if (trace.debugDraw)
			{
				trace.debugSweepSegments.reserve(shapeCount * std::max(1u, trace.subSteps));
			}

			DirectX::XMFLOAT3 currBasisPos{};
			DirectX::XMFLOAT4 currBasisRot{};
			if (!TryGetBasisPose(world, basis, currBasisPos, currBasisRot))
				continue;

			std::vector<DirectX::XMFLOAT3> currCenters(shapeCount);
			std::vector<DirectX::XMFLOAT4> currRots(shapeCount);
			const DirectX::XMMATRIX currBasisWorld = BuildBasisWorldMatrix(currBasisPos, currBasisRot);
			for (size_t i = 0; i < shapeCount; ++i)
			{
				const float shapePhase = ToShapePathPhase(framePhaseEnd);
				if (!ComputeShapeWorldPose(trace.shapes[i], currBasisWorld, shapePhase, currCenters[i], currRots[i]))
				{
					currCenters[i] = (trace.hasPrevShapes && i < trace.prevCentersWS.size())
						? trace.prevCentersWS[i]
						: DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
					currRots[i] = (trace.hasPrevShapes && i < trace.prevRotsWS.size())
						? trace.prevRotsWS[i]
						: DirectX::XMFLOAT4(0.0f, 0.0f, 0.0f, 1.0f);
				}
			}

            if (!trace.hasPrevBasis || !trace.hasPrevShapes)
            {
                trace.prevBasisPos = currBasisPos;
                trace.prevBasisRot = currBasisRot;
                trace.prevCentersWS = currCenters;
                trace.prevRotsWS = currRots;
                trace.hasPrevBasis = true;
                trace.hasPrevShapes = true;
            }

			const uint32_t targetLayerBits = (trace.targetLayerBits != 0u)
				? trace.targetLayerBits
				: ((trace.teamId == 0) ? CombatPhysicsLayers::EnemyHurtboxBit : CombatPhysicsLayers::PlayerHurtboxBit);
			const uint32_t queryLayerBits = (trace.queryLayerBits != 0u)
				? trace.queryLayerBits
				: CombatPhysicsLayers::AttackQueryLayerBit(trace.teamId);

			SceneQueryFilter filter{};
			filter.layerMask = targetLayerBits;
			filter.queryMask = queryLayerBits;
			filter.hitTriggers = true;

            std::vector<SweepHit> hits;
            hits.reserve(32);
            std::vector<OverlapHit> overlaps;
            overlaps.reserve(32);

			const uint32_t steps = std::max(1u, trace.subSteps);
			const DirectX::XMVECTOR prevBasisPosV = XMLoadFloat3(&trace.prevBasisPos);
			const DirectX::XMVECTOR currBasisPosV = XMLoadFloat3(&currBasisPos);
			const DirectX::XMVECTOR prevBasisRotV = XMLoadFloat4(&trace.prevBasisRot);
			const DirectX::XMVECTOR currBasisRotV = XMLoadFloat4(&currBasisRot);

			for (uint32_t step = 0; step < steps; ++step)
			{
				const float t0 = static_cast<float>(step) / static_cast<float>(steps);
				const float t1 = static_cast<float>(step + 1) / static_cast<float>(steps);

				DirectX::XMFLOAT3 basisStartPos{};
				DirectX::XMFLOAT3 basisEndPos{};
				DirectX::XMFLOAT4 basisStartRot{};
				DirectX::XMFLOAT4 basisEndRot{};

				DirectX::XMStoreFloat3(&basisStartPos, DirectX::XMVectorLerp(prevBasisPosV, currBasisPosV, t0));
				DirectX::XMStoreFloat3(&basisEndPos, DirectX::XMVectorLerp(prevBasisPosV, currBasisPosV, t1));
				DirectX::XMStoreFloat4(&basisStartRot, DirectX::XMQuaternionSlerp(prevBasisRotV, currBasisRotV, t0));
				DirectX::XMStoreFloat4(&basisEndRot, DirectX::XMQuaternionSlerp(prevBasisRotV, currBasisRotV, t1));

				const DirectX::XMMATRIX basisStartWorld = BuildBasisWorldMatrix(basisStartPos, basisStartRot);
				const DirectX::XMMATRIX basisEndWorld = BuildBasisWorldMatrix(basisEndPos, basisEndRot);
				const float phase0 = framePhaseStart + ((framePhaseEnd - framePhaseStart) * t0);
				const float phase1 = framePhaseStart + ((framePhaseEnd - framePhaseStart) * t1);

				for (size_t i = 0; i < shapeCount; ++i)
				{
					const WeaponTraceShape& shape = trace.shapes[i];
					if (!shape.enabled)
						continue;
					float shapePhase0 = 0.0f;
					float shapePhase1 = 0.0f;
					if (!ResolveShapeStepPathPhase(phase0, phase1, shapePhase0, shapePhase1))
						continue;

					DirectX::XMFLOAT3 startCenter{};
					DirectX::XMFLOAT4 startRot{};
					DirectX::XMFLOAT3 endCenter{};
					DirectX::XMFLOAT4 endRot{};

					if (!ComputeShapeWorldPose(shape, basisStartWorld, shapePhase0, startCenter, startRot))
						continue;
					if (!ComputeShapeWorldPose(shape, basisEndWorld, shapePhase1, endCenter, endRot))
						continue;
					if (trace.debugDraw)
					{
						WeaponTraceDebugSweepSegment dbgSeg{};
						dbgSeg.shapeIndex = static_cast<std::uint32_t>(i);
						dbgSeg.startCenterWS = startCenter;
						dbgSeg.startRotWS = startRot;
						dbgSeg.endCenterWS = endCenter;
						dbgSeg.endRotWS = endRot;
						trace.debugSweepSegments.push_back(dbgSeg);
					}

                    const char* typeName = (shape.type == WeaponTraceShapeType::Sphere)
                        ? "Sphere"
                        : (shape.type == WeaponTraceShapeType::Capsule ? "Capsule" : "Box");

                    auto ProcessHit = [&](void* userData,
                                          const DirectX::XMFLOAT3& hitPosWS,
                                          const DirectX::XMFLOAT3& hitNormalWS,
                                          uint32_t shapeIndex,
                                          bool hasSweepFraction,
                                          float sweepFraction)
                    {
                        if (!userData)
                            return;

                        EntityId hitEntity = world.ExtractEntityIdFromUserData(userData);
                        if (hitEntity == InvalidEntityId)
                            return;

                        auto* hurt = world.GetComponent<HurtboxComponent>(hitEntity);
                        if (!hurt)
                            return;

                        if (hurt->teamId == trace.teamId)
                            return;

							const std::uint64_t victimGuid = (hurt->ownerGuid != 0)
								? hurt->ownerGuid
								: static_cast<std::uint64_t>(hitEntity);

                        const EntityId victimOwner = (hurt->ownerGuid != 0)
                            ? world.FindEntityByGuid(hurt->ownerGuid)
                            : (hurt->ownerCached != InvalidEntityId ? hurt->ownerCached : hitEntity);

                        auto hitIt = trace.hitVictims.find(victimGuid);
                        if (hitIt != trace.hitVictims.end())
                        {
                            return;
                        }

							trace.hitVictims.insert(victimGuid);

                        if (outHits)
                        {
                            // SFX/VFX hook (raw hit detection):
                            // This is the earliest point with hitPosWS/hitNormalWS.
                            // Prefer spawning impact effects in CombatSession after resolve
                            // (Hit vs Guard vs Parry vs GuardBreak), but use this for
                            // generic debug, sparks, or tracer impacts if needed.
                            if (trace.debugDraw)
                            {
                                ALICE_LOG_INFO("[WeaponTrace] Hit attacker=%llu victim=%llu hurtbox=%llu part=%u dmg=%.2f attackId=%u shape=%s type=%s layerMask=0x%08X queryMask=0x%08X pos=(%.2f,%.2f,%.2f)",
                                    static_cast<unsigned long long>(owner),
                                    static_cast<unsigned long long>(victimOwner),
                                    static_cast<unsigned long long>(hitEntity),
                                    hurt->part,
                                    trace.baseDamage * hurt->damageScale,
                                    trace.attackInstanceId,
                                    shape.name.c_str(),
                                    typeName,
                                    targetLayerBits,
                                    queryLayerBits,
                                    hitPosWS.x, hitPosWS.y, hitPosWS.z);
                            }

                            CombatHitEvent ev{};
                            ev.attackerOwner = owner;
                            ev.victimOwner = victimOwner;
                            ev.hurtboxEntity = hitEntity;
                            ev.part = hurt->part;
                            ev.attackInstanceId = trace.attackInstanceId;
                            ev.subShapeIndex = shapeIndex;
                            ev.damage = trace.baseDamage * hurt->damageScale;
                            ev.guardDurabilityCost = trace.guardDurabilityCost;
                            ev.guardLockSec = trace.guardLockSec;
                            ev.parryLockSec = trace.parryLockSec;
                            ev.parryGroggyGain = trace.parryGroggyGain;
                            ev.guardBreakWeakSec = trace.guardBreakWeakSec;
                            ev.guardBreakPushbackSpeed = trace.guardBreakPushbackSpeed;
                            ev.guardBreakPushbackDuration = trace.guardBreakPushbackDuration;
                            ev.debugLog = trace.debugDraw;
                            ev.sweepFraction = sweepFraction;
                            ev.hasSweepFraction = hasSweepFraction;
                            ev.hitPosWS = hitPosWS;
                            ev.hitNormalWS = hitNormalWS;
                            outHits->push_back(ev);
                        }
                    };

                    const DirectX::XMFLOAT3 delta{ endCenter.x - startCenter.x, endCenter.y - startCenter.y, endCenter.z - startCenter.z };
                    const float dist = std::sqrt(delta.x * delta.x + delta.y * delta.y + delta.z * delta.z);
                    if (dist < 0.0001f)
                    {
                        overlaps.clear();
                        uint32_t hitCount = 0;
                        if (shape.type == WeaponTraceShapeType::Sphere)
                        {
                            const Vec3 center(endCenter.x, endCenter.y, endCenter.z);
                            hitCount = physics->OverlapSphereQ(center, shape.radius, overlaps, filter, 64);
                        }
                        else if (shape.type == WeaponTraceShapeType::Capsule)
                        {
                            const Vec3 center(endCenter.x, endCenter.y, endCenter.z);
                            const Quat rot(endRot.x, endRot.y, endRot.z, endRot.w);
                            hitCount = physics->OverlapCapsuleQ(center, rot, shape.radius, shape.capsuleHalfHeight, overlaps, filter, 64, true);
                        }
                        else if (shape.type == WeaponTraceShapeType::Box)
                        {
                            const Vec3 center(endCenter.x, endCenter.y, endCenter.z);
                            const Quat rot(endRot.x, endRot.y, endRot.z, endRot.w);
                            const Vec3 halfExtents(shape.boxHalfExtents.x, shape.boxHalfExtents.y, shape.boxHalfExtents.z);
                            hitCount = physics->OverlapBoxQ(center, rot, halfExtents, overlaps, filter, 64);
                        }

                        if (hitCount > 0)
                        {
                            const DirectX::XMFLOAT3 hitPosWS = endCenter;
                            const DirectX::XMFLOAT3 hitNormalWS{ 0.0f, 1.0f, 0.0f };
                            for (uint32_t h = 0; h < hitCount && h < overlaps.size(); ++h)
                                ProcessHit(overlaps[h].userData, hitPosWS, hitNormalWS,
                                    static_cast<uint32_t>(i), false, 0.0f);
                        }
                        continue;
                    }

                    const Vec3 origin(startCenter.x, startCenter.y, startCenter.z);
                    const Vec3 dir(delta.x / dist, delta.y / dist, delta.z / dist);

                    hits.clear();
                    uint32_t hitCount = 0;
                    if (shape.type == WeaponTraceShapeType::Sphere)
                    {
                        hitCount = physics->SweepSphereAllQ(origin, shape.radius, dir, dist, hits, filter);
                    }
                    else if (shape.type == WeaponTraceShapeType::Capsule)
                    {
                        const Quat rot(startRot.x, startRot.y, startRot.z, startRot.w);
                        hitCount = physics->SweepCapsuleAllQ(origin, rot, shape.radius, shape.capsuleHalfHeight, dir, dist, hits, filter, 64, true);
                    }
                    else if (shape.type == WeaponTraceShapeType::Box)
                    {
                        const Quat rot(startRot.x, startRot.y, startRot.z, startRot.w);
                        const Vec3 halfExtents(shape.boxHalfExtents.x, shape.boxHalfExtents.y, shape.boxHalfExtents.z);
                        hitCount = physics->SweepBoxAllQ(origin, rot, halfExtents, dir, dist, hits, filter);
                    }

                    if (hitCount == 0)
                        continue;

                    const float stepScale = 1.0f / static_cast<float>(steps);
                    for (uint32_t h = 0; h < hitCount && h < hits.size(); ++h)
                    {
                        const auto& hit = hits[h];
                        const float localFraction = (dist > 0.0f) ? (hit.distance / dist) : 0.0f;
                        const float sweepFraction = t0 + (std::clamp(localFraction, 0.0f, 1.0f) * stepScale);
                        ProcessHit(hit.userData,
                            DirectX::XMFLOAT3(hit.position.x, hit.position.y, hit.position.z),
                            DirectX::XMFLOAT3(hit.normal.x, hit.normal.y, hit.normal.z),
                            static_cast<uint32_t>(i), true, sweepFraction);
                    }
                }
            }

			trace.prevBasisPos = currBasisPos;
			trace.prevBasisRot = currBasisRot;
			trace.prevCentersWS = currCenters;
			trace.prevRotsWS = currRots;
			trace.hasPrevBasis = true;
			trace.hasPrevShapes = true;
		}
	}
}
