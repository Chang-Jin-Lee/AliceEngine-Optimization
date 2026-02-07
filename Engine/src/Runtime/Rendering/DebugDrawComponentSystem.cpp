#include "Runtime/Rendering/DebugDrawComponentSystem.h"
#include "Runtime/Rendering/DebugDrawSystem.h"

#include "Runtime/Rendering/Components/DebugDrawBoxComponent.h"
#include "Runtime/Rendering/Components/DecalComponent.h"
#include "Runtime/Audio/Components/SoundBoxComponent.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Rendering/Components/SkinnedAnimationComponent.h"
#include "Runtime/Rendering/Components/SkinnedMeshComponent.h"
#include "Runtime/Gameplay/Combat/WeaponTraceComponent.h"
#include "Runtime/Gameplay/Combat/AttackDriverComponent.h"
#include "Runtime/Gameplay/Animation/AdvancedAnimationComponent.h"
#include "Runtime/Rendering/SkinnedMeshRegistry.h"
#include "Runtime/Importing/FbxModel.h"
#include "Runtime/ECS/Components/IDComponent.h"
#include "Runtime/Physics/Components/Phy_ColliderComponent.h"
#include "Runtime/Physics/Components/Phy_CCTComponent.h"
#include "Runtime/Physics/Components/Phy_MeshColliderComponent.h"

#include <algorithm>
#include <vector>
#include <cmath>
#include <cstdint>
#include <string>
#include <memory>
#include <assimp/scene.h>

namespace Alice
{
	namespace
	{
        using namespace DirectX;

        void AddBoxLines(DebugDrawSystem& dbg, const DirectX::XMFLOAT3 corners[8], const DirectX::XMFLOAT4& col)
        {
            // bottom
            dbg.AddLine(corners[0], corners[1], col);
            dbg.AddLine(corners[1], corners[2], col);
            dbg.AddLine(corners[2], corners[3], col);
            dbg.AddLine(corners[3], corners[0], col);
            // top
            dbg.AddLine(corners[4], corners[5], col);
            dbg.AddLine(corners[5], corners[6], col);
            dbg.AddLine(corners[6], corners[7], col);
            dbg.AddLine(corners[7], corners[4], col);
            // sides
            dbg.AddLine(corners[0], corners[4], col);
            dbg.AddLine(corners[1], corners[5], col);
            dbg.AddLine(corners[2], corners[6], col);
            dbg.AddLine(corners[3], corners[7], col);
        }

        XMVECTOR EulerToQuaternion(const XMFLOAT3& euler)
        {
            return XMQuaternionRotationRollPitchYaw(euler.x, euler.y, euler.z);
        }

        XMVECTOR RotateVector(const XMVECTOR& v, const XMVECTOR& q)
        {
            return XMVector3Rotate(v, q);
        }

        void GetBoxCorners(const XMFLOAT3& center, const XMFLOAT3& halfExtents, const XMVECTOR& rot, XMFLOAT3 corners[8])
        {
            XMVECTOR pos = XMLoadFloat3(&center);

            XMFLOAT3 localCorners[8] = {
                { -halfExtents.x, -halfExtents.y, -halfExtents.z },
                {  halfExtents.x, -halfExtents.y, -halfExtents.z },
                {  halfExtents.x, -halfExtents.y,  halfExtents.z },
                { -halfExtents.x, -halfExtents.y,  halfExtents.z },
                { -halfExtents.x,  halfExtents.y, -halfExtents.z },
                {  halfExtents.x,  halfExtents.y, -halfExtents.z },
                {  halfExtents.x,  halfExtents.y,  halfExtents.z },
                { -halfExtents.x,  halfExtents.y,  halfExtents.z },
            };

            for (int i = 0; i < 8; ++i)
            {
                XMVECTOR local = XMLoadFloat3(&localCorners[i]);
                XMVECTOR rotated = RotateVector(local, rot);
                XMVECTOR world = XMVectorAdd(pos, rotated);
                XMStoreFloat3(&corners[i], world);
            }
        }

        void DrawBox(DebugDrawSystem& dbg, const XMFLOAT3& center, const XMFLOAT3& halfExtents, const XMVECTOR& rot, const XMFLOAT4& color)
        {
            XMFLOAT3 corners[8];
            GetBoxCorners(center, halfExtents, rot, corners);
            AddBoxLines(dbg, corners, color);
        }

        void DrawSphere(DebugDrawSystem& dbg, const XMFLOAT3& center, float radius, const XMFLOAT4& color)
        {
            const int segments = 16;
            const float angleStep = XM_2PI / segments;

            for (int i = 0; i < segments; ++i)
            {
                float a1 = i * angleStep;
                float a2 = (i + 1) * angleStep;
                XMFLOAT3 p1 = { center.x + radius * std::cos(a1), center.y + radius * std::sin(a1), center.z };
                XMFLOAT3 p2 = { center.x + radius * std::cos(a2), center.y + radius * std::sin(a2), center.z };
                dbg.AddLine(p1, p2, color);
            }

            for (int i = 0; i < segments; ++i)
            {
                float a1 = i * angleStep;
                float a2 = (i + 1) * angleStep;
                XMFLOAT3 p1 = { center.x + radius * std::cos(a1), center.y, center.z + radius * std::sin(a1) };
                XMFLOAT3 p2 = { center.x + radius * std::cos(a2), center.y, center.z + radius * std::sin(a2) };
                dbg.AddLine(p1, p2, color);
            }

            for (int i = 0; i < segments; ++i)
            {
                float a1 = i * angleStep;
                float a2 = (i + 1) * angleStep;
                XMFLOAT3 p1 = { center.x, center.y + radius * std::cos(a1), center.z + radius * std::sin(a1) };
                XMFLOAT3 p2 = { center.x, center.y + radius * std::cos(a2), center.z + radius * std::sin(a2) };
                dbg.AddLine(p1, p2, color);
            }
        }

        void DrawCapsule(DebugDrawSystem& dbg, const XMFLOAT3& center, float radius, float halfHeight, bool alignYAxis, const XMVECTOR& rot, const XMFLOAT4& color)
        {
            const int segments = 16;
            const float angleStep = XM_2PI / segments;

            XMVECTOR pos = XMLoadFloat3(&center);

            if (alignYAxis)
            {
                for (int i = 0; i < segments; ++i)
                {
                    float a1 = i * angleStep;
                    float a2 = (i + 1) * angleStep;
                    float y1 = halfHeight + radius * std::sin(a1);
                    float y2 = halfHeight + radius * std::sin(a2);
                    float r1 = radius * std::cos(a1);
                    float r2 = radius * std::cos(a2);

                    XMVECTOR p1 = XMVectorSet(r1, y1, 0.0f, 0.0f);
                    XMVECTOR p2 = XMVectorSet(r2, y2, 0.0f, 0.0f);
                    p1 = XMVectorAdd(pos, RotateVector(p1, rot));
                    p2 = XMVectorAdd(pos, RotateVector(p2, rot));
                    XMFLOAT3 f1, f2;
                    XMStoreFloat3(&f1, p1);
                    XMStoreFloat3(&f2, p2);
                    dbg.AddLine(f1, f2, color);
                }

                for (int i = 0; i < segments; ++i)
                {
                    float a1 = i * angleStep;
                    float a2 = (i + 1) * angleStep;
                    float y1 = -halfHeight - radius * std::sin(a1);
                    float y2 = -halfHeight - radius * std::sin(a2);
                    float r1 = radius * std::cos(a1);
                    float r2 = radius * std::cos(a2);

                    XMVECTOR p1 = XMVectorSet(r1, y1, 0.0f, 0.0f);
                    XMVECTOR p2 = XMVectorSet(r2, y2, 0.0f, 0.0f);
                    p1 = XMVectorAdd(pos, RotateVector(p1, rot));
                    p2 = XMVectorAdd(pos, RotateVector(p2, rot));
                    XMFLOAT3 f1, f2;
                    XMStoreFloat3(&f1, p1);
                    XMStoreFloat3(&f2, p2);
                    dbg.AddLine(f1, f2, color);
                }

                for (int i = 0; i < segments; ++i)
                {
                    float a1 = i * angleStep;
                    float a2 = (i + 1) * angleStep;
                    float r1 = radius * std::cos(a1);
                    float r2 = radius * std::cos(a2);
                    float z1 = radius * std::sin(a1);
                    float z2 = radius * std::sin(a2);

                    XMVECTOR p1 = XMVectorSet(r1, halfHeight, z1, 0.0f);
                    XMVECTOR p2 = XMVectorSet(r2, halfHeight, z2, 0.0f);
                    p1 = XMVectorAdd(pos, RotateVector(p1, rot));
                    p2 = XMVectorAdd(pos, RotateVector(p2, rot));
                    XMFLOAT3 f1, f2;
                    XMStoreFloat3(&f1, p1);
                    XMStoreFloat3(&f2, p2);
                    dbg.AddLine(f1, f2, color);

                    p1 = XMVectorSet(r1, -halfHeight, z1, 0.0f);
                    p2 = XMVectorSet(r2, -halfHeight, z2, 0.0f);
                    p1 = XMVectorAdd(pos, RotateVector(p1, rot));
                    p2 = XMVectorAdd(pos, RotateVector(p2, rot));
                    XMStoreFloat3(&f1, p1);
                    XMStoreFloat3(&f2, p2);
                    dbg.AddLine(f1, f2, color);
                }
            }
            else
            {
                for (int i = 0; i < segments; ++i)
                {
                    float a1 = i * angleStep;
                    float a2 = (i + 1) * angleStep;
                    float x1 = halfHeight + radius * std::sin(a1);
                    float x2 = halfHeight + radius * std::sin(a2);
                    float r1 = radius * std::cos(a1);
                    float r2 = radius * std::cos(a2);

                    XMVECTOR p1 = XMVectorSet(x1, r1, 0.0f, 0.0f);
                    XMVECTOR p2 = XMVectorSet(x2, r2, 0.0f, 0.0f);
                    p1 = XMVectorAdd(pos, RotateVector(p1, rot));
                    p2 = XMVectorAdd(pos, RotateVector(p2, rot));
                    XMFLOAT3 f1, f2;
                    XMStoreFloat3(&f1, p1);
                    XMStoreFloat3(&f2, p2);
                    dbg.AddLine(f1, f2, color);
                }

                for (int i = 0; i < segments; ++i)
                {
                    float a1 = i * angleStep;
                    float a2 = (i + 1) * angleStep;
                    float x1 = -halfHeight - radius * std::sin(a1);
                    float x2 = -halfHeight - radius * std::sin(a2);
                    float r1 = radius * std::cos(a1);
                    float r2 = radius * std::cos(a2);

                    XMVECTOR p1 = XMVectorSet(x1, r1, 0.0f, 0.0f);
                    XMVECTOR p2 = XMVectorSet(x2, r2, 0.0f, 0.0f);
                    p1 = XMVectorAdd(pos, RotateVector(p1, rot));
                    p2 = XMVectorAdd(pos, RotateVector(p2, rot));
                    XMFLOAT3 f1, f2;
                    XMStoreFloat3(&f1, p1);
                    XMStoreFloat3(&f2, p2);
                    dbg.AddLine(f1, f2, color);
                }

                for (int i = 0; i < segments; ++i)
                {
                    float a1 = i * angleStep;
                    float a2 = (i + 1) * angleStep;
                    float r1 = radius * std::cos(a1);
                    float r2 = radius * std::cos(a2);
                    float z1 = radius * std::sin(a1);
                    float z2 = radius * std::sin(a2);

                    XMVECTOR p1 = XMVectorSet(halfHeight, r1, z1, 0.0f);
                    XMVECTOR p2 = XMVectorSet(halfHeight, r2, z2, 0.0f);
                    p1 = XMVectorAdd(pos, RotateVector(p1, rot));
                    p2 = XMVectorAdd(pos, RotateVector(p2, rot));
                    XMFLOAT3 f1, f2;
                    XMStoreFloat3(&f1, p1);
                    XMStoreFloat3(&f2, p2);
                    dbg.AddLine(f1, f2, color);

                    p1 = XMVectorSet(-halfHeight, r1, z1, 0.0f);
                    p2 = XMVectorSet(-halfHeight, r2, z2, 0.0f);
                    p1 = XMVectorAdd(pos, RotateVector(p1, rot));
                    p2 = XMVectorAdd(pos, RotateVector(p2, rot));
                    XMStoreFloat3(&f1, p1);
                    XMStoreFloat3(&f2, p2);
                    dbg.AddLine(f1, f2, color);
                }
            }
        }

        bool TryGetBasisPose(World& world, EntityId basis, XMFLOAT3& outPos, XMFLOAT4& outRot)
        {
            XMMATRIX worldMatrix = world.ComputeWorldMatrix(basis);
            XMVECTOR s, r, t;
            if (!XMMatrixDecompose(&s, &r, &t, worldMatrix))
                return false;
            XMStoreFloat3(&outPos, t);
            XMStoreFloat4(&outRot, r);
            return true;
        }

        XMMATRIX BuildBasisWorldMatrix(const XMFLOAT3& pos, const XMFLOAT4& rot)
        {
            XMVECTOR q = XMLoadFloat4(&rot);
            XMMATRIX R = XMMatrixRotationQuaternion(q);
            XMMATRIX T = XMMatrixTranslation(pos.x, pos.y, pos.z);
            return R * T;
        }

        float ToShapePathPhase(float globalPhase)
        {
            return std::clamp(globalPhase, 0.0f, 1.0f);
        }

        XMFLOAT3 ResolveShapePathLocalPos(const WeaponTraceShape& shape, float phase)
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
                return XMFLOAT3(
                    (w0 * shape.pathStartLocalPos.x) + (w1 * shape.pathControlLocalPos.x) + (w2 * shape.pathEndLocalPos.x),
                    (w0 * shape.pathStartLocalPos.y) + (w1 * shape.pathControlLocalPos.y) + (w2 * shape.pathEndLocalPos.y),
                    (w0 * shape.pathStartLocalPos.z) + (w1 * shape.pathControlLocalPos.z) + (w2 * shape.pathEndLocalPos.z));
            }
            case WeaponTracePathMode::Linear:
            default:
                return XMFLOAT3(
                    shape.pathStartLocalPos.x + ((shape.pathEndLocalPos.x - shape.pathStartLocalPos.x) * t),
                    shape.pathStartLocalPos.y + ((shape.pathEndLocalPos.y - shape.pathStartLocalPos.y) * t),
                    shape.pathStartLocalPos.z + ((shape.pathEndLocalPos.z - shape.pathStartLocalPos.z) * t));
            }
        }

        bool ComputeShapeWorldPose(const WeaponTraceShape& shape,
                                   const XMMATRIX& basisWorld,
                                   const XMFLOAT3& localPos,
                                   XMFLOAT3& outCenter,
                                   XMFLOAT4& outRot)
        {
            const float rx = XMConvertToRadians(shape.localRotDeg.x);
            const float ry = XMConvertToRadians(shape.localRotDeg.y);
            const float rz = XMConvertToRadians(shape.localRotDeg.z);
            const XMMATRIX R = XMMatrixRotationRollPitchYaw(rx, ry, rz);
            const XMMATRIX T = XMMatrixTranslation(localPos.x, localPos.y, localPos.z);
            const XMMATRIX local = R * T;
            const XMMATRIX world = local * basisWorld;
            XMVECTOR s, r, t;
            if (!XMMatrixDecompose(&s, &r, &t, world))
                return false;
            XMStoreFloat3(&outCenter, t);
            XMStoreFloat4(&outRot, r);
            return true;
        }

        bool ComputeShapeWorldPose(const WeaponTraceShape& shape, const XMMATRIX& basisWorld, XMFLOAT3& outCenter, XMFLOAT4& outRot)
        {
            return ComputeShapeWorldPose(shape, basisWorld, shape.localPos, outCenter, outRot);
        }

        float NormalizeClipPhase(double timeSec, float durationSec)
        {
            if (durationSec <= 0.0001f)
                return 0.0f;
            double wrapped = std::fmod(timeSec, static_cast<double>(durationSec));
            if (wrapped < 0.0)
                wrapped += static_cast<double>(durationSec);
            return std::clamp(static_cast<float>(wrapped / static_cast<double>(durationSec)), 0.0f, 1.0f);
        }

        bool ResolveClipNameAndDurationByIndex(const SkinnedMeshRegistry* registry,
                                               World& world,
                                               EntityId entityId,
                                               int clipIndex,
                                               std::string& outName,
                                               float& outDurationSec)
        {
            outName.clear();
            outDurationSec = 0.0f;
            if (!registry)
                return false;

            const auto* skinned = world.GetComponent<SkinnedMeshComponent>(entityId);
            if (!skinned || skinned->meshAssetPath.empty())
                return false;

            std::shared_ptr<SkinnedMeshGPU> mesh = registry->Find(skinned->meshAssetPath);
            if (!mesh || !mesh->sourceModel)
                return false;

            const aiScene* scene = mesh->sourceModel->GetScenePtr();
            const auto& names = mesh->sourceModel->GetAnimationNames();
            const int clipCount = scene ? static_cast<int>(scene->mNumAnimations) : static_cast<int>(names.size());
            if (clipCount <= 0)
                return false;

            const int idx = std::clamp(clipIndex, 0, clipCount - 1);
            if (idx < static_cast<int>(names.size()) && !names[static_cast<size_t>(idx)].empty())
                outName = names[static_cast<size_t>(idx)];

            if (outName.empty() && scene && static_cast<unsigned>(idx) < scene->mNumAnimations)
            {
                const aiAnimation* anim = scene->mAnimations[idx];
                if (anim && anim->mName.length > 0)
                    outName = anim->mName.C_Str();
            }
            if (outName.empty())
                outName = "Anim" + std::to_string(idx);

            outDurationSec = static_cast<float>(mesh->sourceModel->GetClipDurationSec(idx));
            return (outDurationSec > 0.0f);
        }

        bool ResolveClipDurationByName(const SkinnedMeshRegistry* registry,
                                       World& world,
                                       EntityId entityId,
                                       const std::string& clipName,
                                       float& outDurationSec)
        {
            outDurationSec = 0.0f;
            if (!registry || clipName.empty())
                return false;

            const auto* skinned = world.GetComponent<SkinnedMeshComponent>(entityId);
            if (!skinned || skinned->meshAssetPath.empty())
                return false;

            std::shared_ptr<SkinnedMeshGPU> mesh = registry->Find(skinned->meshAssetPath);
            if (!mesh || !mesh->sourceModel)
                return false;

            const aiScene* scene = mesh->sourceModel->GetScenePtr();
            const auto& names = mesh->sourceModel->GetAnimationNames();
            const std::size_t clipCount = scene ? static_cast<std::size_t>(scene->mNumAnimations) : names.size();
            if (clipCount == 0)
                return false;

            for (std::size_t i = 0; i < clipCount && i < names.size(); ++i)
            {
                if (names[i] == clipName)
                {
                    outDurationSec = static_cast<float>(mesh->sourceModel->GetClipDurationSec(static_cast<int>(i)));
                    return (outDurationSec > 0.0f);
                }
            }

            if (scene)
            {
                for (std::size_t i = 0; i < scene->mNumAnimations; ++i)
                {
                    const aiAnimation* anim = scene->mAnimations[i];
                    if (anim && anim->mName.length > 0 && clipName == anim->mName.C_Str())
                    {
                        outDurationSec = static_cast<float>(mesh->sourceModel->GetClipDurationSec(static_cast<int>(i)));
                        return (outDurationSec > 0.0f);
                    }
                }
            }

            return false;
        }

        bool ResolvePreviewClipTime(const SkinnedMeshRegistry* registry,
                                    World& world,
                                    EntityId entityId,
                                    std::string& outClipName,
                                    float& outTimeSec,
                                    float& outDurationSec)
        {
            outClipName.clear();
            outTimeSec = 0.0f;
            outDurationSec = 0.0f;

            // AdvancedAnimation drives real gameplay timing in combat scenes.
            // Use it first when present, then fall back to SkinnedAnimation-only preview.
            if (const auto* adv = world.GetComponent<AdvancedAnimationComponent>(entityId))
            {
                struct AdvCandidate
                {
                    const std::string* clip = nullptr;
                    float timeSec = 0.0f;
                };
                const AdvCandidate candidates[] = {
                    { &adv->base.clipA, adv->base.timeA },
                    { &adv->base.clipB, adv->base.timeB },
                    { &adv->upper.clipA, adv->upper.timeA },
                    { &adv->upper.clipB, adv->upper.timeB },
                    { &adv->additive.clip, adv->additive.time }
                };
                for (const auto& candidate : candidates)
                {
                    if (!candidate.clip || candidate.clip->empty())
                        continue;
                    float duration = 0.0f;
                    if (!ResolveClipDurationByName(registry, world, entityId, *candidate.clip, duration))
                        continue;
                    outClipName = *candidate.clip;
                    outTimeSec = candidate.timeSec;
                    outDurationSec = duration;
                    return true;
                }
            }

            if (const auto* skinnedAnim = world.GetComponent<SkinnedAnimationComponent>(entityId))
            {
                std::string clipName;
                float duration = 0.0f;
                if (ResolveClipNameAndDurationByIndex(registry, world, entityId, skinnedAnim->clipIndex, clipName, duration))
                {
                    outClipName = std::move(clipName);
                    outTimeSec = static_cast<float>(skinnedAnim->timeSec);
                    outDurationSec = duration;
                    return true;
                }
            }

            return false;
        }

        bool ResolveTracePreviewPhase(const SkinnedMeshRegistry* registry,
                                      World& world,
                                      EntityId traceEntity,
                                      const WeaponTraceComponent& trace,
                                      float& outPhase,
                                      EntityId* outOwnerEntity = nullptr,
                                      std::string* outClipName = nullptr,
                                      float* outClipDurationSec = nullptr)
        {
            outPhase = 0.0f;
            EntityId owner = traceEntity;
            if (trace.ownerGuid != 0)
            {
                const EntityId resolved = world.FindEntityByGuid(trace.ownerGuid);
                if (resolved != InvalidEntityId)
                    owner = resolved;
            }

            std::string currentClipName;
            float currentTimeSec = 0.0f;
            float currentDurationSec = 0.0f;
            if (!ResolvePreviewClipTime(registry, world, owner, currentClipName, currentTimeSec, currentDurationSec))
            {
                EntityId basis = traceEntity;
                if (trace.traceBasisGuid != 0)
                {
                    const EntityId resolvedBasis = world.FindEntityByGuid(trace.traceBasisGuid);
                    if (resolvedBasis != InvalidEntityId)
                        basis = resolvedBasis;
                }
                if (!ResolvePreviewClipTime(registry, world, basis, currentClipName, currentTimeSec, currentDurationSec))
                    return false;
            }

            outPhase = NormalizeClipPhase(currentTimeSec, currentDurationSec);
            if (outOwnerEntity)
                *outOwnerEntity = owner;
            if (outClipName)
                *outClipName = currentClipName;
            if (outClipDurationSec)
                *outClipDurationSec = currentDurationSec;
            return true;
        }

        bool IsAttackDriverClipMatchPreview(const AttackDriverClip& clip,
                                            const AdvancedAnimationComponent* adv,
                                            const std::string& currentClipName)
        {
            if (currentClipName.empty())
                return false;

            switch (clip.source)
            {
            case AttackDriverClipSource::Explicit:
                return !clip.clipName.empty() && (clip.clipName == currentClipName);
            case AttackDriverClipSource::BaseA:
                return adv && !adv->base.clipA.empty() && (adv->base.clipA == currentClipName);
            case AttackDriverClipSource::BaseB:
                return adv && !adv->base.clipB.empty() && (adv->base.clipB == currentClipName);
            case AttackDriverClipSource::UpperA:
                return adv && !adv->upper.clipA.empty() && (adv->upper.clipA == currentClipName);
            case AttackDriverClipSource::UpperB:
                return adv && !adv->upper.clipB.empty() && (adv->upper.clipB == currentClipName);
            case AttackDriverClipSource::Additive:
                return adv && !adv->additive.clip.empty() && (adv->additive.clip == currentClipName);
            default:
                return false;
            }
        }

        EntityId ResolveAttackDriverTraceSlotEntity(World& world,
                                                    const AttackDriverComponent& driver,
                                                    EntityId ownerEntity,
                                                    std::uint32_t slotIndex)
        {
            if (slotIndex == 0u)
            {
                if (driver.traceGuid == 0u)
                    return ownerEntity;
                return world.FindEntityByGuid(driver.traceGuid);
            }

            const std::uint32_t extraIndex = slotIndex - 1u;
            if (extraIndex >= driver.traceGuids.size())
                return InvalidEntityId;
            const std::uint64_t guid = driver.traceGuids[extraIndex];
            if (guid == 0u)
                return InvalidEntityId;
            return world.FindEntityByGuid(guid);
        }

        float ResolveWindowPhase(const std::vector<std::pair<float, float>>& mergedIntervals, float clipPhaseNorm)
        {
            if (mergedIntervals.empty())
                return std::clamp(clipPhaseNorm, 0.0f, 1.0f);

            const float phase = std::clamp(clipPhaseNorm, 0.0f, 1.0f);
            if (phase <= mergedIntervals.front().first)
                return 0.0f;

            for (size_t i = 0; i < mergedIntervals.size(); ++i)
            {
                const float start = mergedIntervals[i].first;
                const float end = mergedIntervals[i].second;
                const float len = std::max(0.0f, end - start);

                if (phase < start)
                    return 1.0f;
                if (phase <= end)
                {
                    if (len <= 0.0001f)
                        return 1.0f;
                    return std::clamp((phase - start) / len, 0.0f, 1.0f);
                }
            }

            return 1.0f;
        }

        bool ResolveAttackDriverPreviewWindowForTrace(World& world,
                                                      EntityId traceEntity,
                                                      EntityId ownerEntity,
                                                      const std::string& clipName,
                                                      float clipDurationSec,
                                                      float clipPhaseNorm,
                                                      bool& outActive,
                                                      float& outWindowPhase)
        {
            outActive = false;
            outWindowPhase = std::clamp(clipPhaseNorm, 0.0f, 1.0f);

            if (ownerEntity == InvalidEntityId || clipName.empty() || clipDurationSec <= 0.0001f)
                return false;

            const auto* driver = world.GetComponent<AttackDriverComponent>(ownerEntity);
            if (!driver)
                return false;

            const auto* adv = world.GetComponent<AdvancedAnimationComponent>(ownerEntity);
            const std::uint32_t slotCount = static_cast<std::uint32_t>(
                std::min<std::size_t>(std::size_t(1) + driver->traceGuids.size(), 32));
            if (slotCount == 0u)
                return false;

            std::uint32_t traceSlotMask = 0u;
            for (std::uint32_t slot = 0u; slot < slotCount; ++slot)
            {
                const EntityId slotEntity = ResolveAttackDriverTraceSlotEntity(world, *driver, ownerEntity, slot);
                if (slotEntity == InvalidEntityId || slotEntity != traceEntity)
                    continue;
                traceSlotMask |= (1u << slot);
            }
            if (traceSlotMask == 0u)
                return false;

            bool foundAnyClipForCurrentAnim = false;
            std::vector<std::pair<float, float>> intervals;
            intervals.reserve(driver->clips.size());
            for (const auto& clip : driver->clips)
            {
                if (!clip.enabled || clip.type != AttackDriverNotifyType::Attack)
                    continue;
                if (!IsAttackDriverClipMatchPreview(clip, adv, clipName))
                    continue;

                foundAnyClipForCurrentAnim = true;
                if (clip.traceSlotMask != 0u && (clip.traceSlotMask & traceSlotMask) == 0u)
                    continue;

                float startSec = std::max(0.0f, clip.startTimeSec);
                float endSec = std::max(0.0f, clip.endTimeSec);
                if (endSec < startSec)
                    std::swap(startSec, endSec);

                float startNorm = std::clamp(startSec / clipDurationSec, 0.0f, 1.0f);
                float endNorm = std::clamp(endSec / clipDurationSec, 0.0f, 1.0f);
                if (endNorm < startNorm)
                    std::swap(startNorm, endNorm);
                intervals.emplace_back(startNorm, endNorm);
            }

            if (!foundAnyClipForCurrentAnim)
                return false;

            if (intervals.empty())
            {
                outActive = false;
                outWindowPhase = 0.0f;
                return true;
            }

            std::sort(intervals.begin(), intervals.end(), [](const auto& a, const auto& b) {
                if (a.first != b.first)
                    return a.first < b.first;
                return a.second < b.second;
            });

            std::vector<std::pair<float, float>> merged;
            merged.reserve(intervals.size());
            for (const auto& span : intervals)
            {
                if (merged.empty() || span.first > (merged.back().second + 0.0001f))
                {
                    merged.push_back(span);
                }
                else
                {
                    merged.back().second = std::max(merged.back().second, span.second);
                }
            }

            const float phase = std::clamp(clipPhaseNorm, 0.0f, 1.0f);
            for (const auto& span : merged)
            {
                if (phase >= span.first && phase <= span.second)
                {
                    outActive = true;
                    break;
                }
            }

            outWindowPhase = ResolveWindowPhase(merged, phase);
            return true;
        }
    }

    void DebugDrawComponentSystem::Build(World& world,
                                         DebugDrawSystem* overlay,
                                         DebugDrawSystem* depth,
                                         EntityId selectedEntity,
                                         bool debugEnabled,
                                         bool editorMode,
                                         const SkinnedMeshRegistry* skinnedRegistry)
    {
        if (!overlay && !depth) return;

        // SoundBox -> DebugDrawBox 자동 연결 및 동기화
        {
            std::vector<EntityId> toAdd;
            toAdd.reserve(world.GetComponents<SoundBoxComponent>().size());

            for (const auto& [entityId, sb] : world.GetComponents<SoundBoxComponent>())
            {
                if (!world.GetComponent<DebugDrawBoxComponent>(entityId))
                {
                    toAdd.push_back(entityId);
                }
            }

            for (EntityId id : toAdd)
            {
                world.AddComponent<DebugDrawBoxComponent>(id);
            }

            for (const auto& [entityId, sb] : world.GetComponents<SoundBoxComponent>())
            {
                auto* dbg = world.GetComponent<DebugDrawBoxComponent>(entityId);
                if (!dbg) continue;
                dbg->boundsMin = sb.boundsMin;
                dbg->boundsMax = sb.boundsMax;
                dbg->enabled = editorMode ? true : sb.debugDraw;
                dbg->depthTest = false;

                if (editorMode)
                {
                    dbg->color = (entityId == selectedEntity)
                        ? DirectX::XMFLOAT4(1.0f, 0.2f, 0.2f, 1.0f)
                        : DirectX::XMFLOAT4(0.3f, 0.7f, 1.0f, 1.0f);
                }
            }
        }

        for (const auto& [entityId, box] : world.GetComponents<DebugDrawBoxComponent>())
        {
            if (!box.enabled) continue;

            const bool isSoundBox = (world.GetComponent<SoundBoxComponent>(entityId) != nullptr);
            if (!debugEnabled && !isSoundBox) continue;

            DebugDrawSystem* target = (box.depthTest && depth) ? depth : (overlay ? overlay : depth);
            if (!target) continue;

            const TransformComponent* tr = world.GetComponent<TransformComponent>(entityId);
            if (tr && !tr->enabled) continue;
            const DirectX::XMFLOAT3 pos = tr ? tr->position : DirectX::XMFLOAT3(0, 0, 0);
            const DirectX::XMFLOAT3 scale = tr ? tr->scale : DirectX::XMFLOAT3(1, 1, 1);

            const float minX = (std::min)(box.boundsMin.x * scale.x, box.boundsMax.x * scale.x);
            const float maxX = (std::max)(box.boundsMin.x * scale.x, box.boundsMax.x * scale.x);
            const float minY = (std::min)(box.boundsMin.y * scale.y, box.boundsMax.y * scale.y);
            const float maxY = (std::max)(box.boundsMin.y * scale.y, box.boundsMax.y * scale.y);
            const float minZ = (std::min)(box.boundsMin.z * scale.z, box.boundsMax.z * scale.z);
            const float maxZ = (std::max)(box.boundsMin.z * scale.z, box.boundsMax.z * scale.z);

            DirectX::XMFLOAT3 mn{ minX + pos.x, minY + pos.y, minZ + pos.z };
            DirectX::XMFLOAT3 mx{ maxX + pos.x, maxY + pos.y, maxZ + pos.z };

            DirectX::XMFLOAT3 corners[8] = {
                { mn.x, mn.y, mn.z }, { mx.x, mn.y, mn.z }, { mx.x, mn.y, mx.z }, { mn.x, mn.y, mx.z },
                { mn.x, mx.y, mn.z }, { mx.x, mx.y, mn.z }, { mx.x, mx.y, mx.z }, { mn.x, mx.y, mx.z }
            };

            AddBoxLines(*target, corners, box.color);
        }

        if (!debugEnabled)
            return;

        DebugDrawSystem* target = overlay ? overlay : depth;
        if (!target)
            return;

        // Decal volume debug draw
        for (const auto& [entityId, decal] : world.GetComponents<DecalComponent>())
        {
            if (!decal.enabled) continue;
            if (decal.albedoTexturePath.empty()) continue;
            if (decal.opacity <= 0.0f) continue;

            const TransformComponent* tr = world.GetComponent<TransformComponent>(entityId);
            if (!tr || !tr->enabled || !tr->visible) continue;

            XMMATRIX worldM = world.ComputeWorldMatrix(entityId);
            XMVECTOR s, r, t;
            if (!XMMatrixDecompose(&s, &r, &t, worldM))
                continue;

            XMFLOAT3 center{};
            XMStoreFloat3(&center, t);
            XMFLOAT3 halfExtents{};
            XMStoreFloat3(&halfExtents, s);
            halfExtents.x = std::abs(halfExtents.x);
            halfExtents.y = std::abs(halfExtents.y);
            halfExtents.z = std::abs(halfExtents.z);

            const XMFLOAT4 color = (entityId == selectedEntity)
                ? XMFLOAT4(1.0f, 0.4f, 0.1f, 1.0f)
                : XMFLOAT4(decal.color.x, decal.color.y, decal.color.z, 1.0f);

            DrawBox(*target, center, halfExtents, r, color);
        }

        // Collider debug draw
        for (const auto& [entityId, collider] : world.GetComponents<Phy_ColliderComponent>())
        {
            if (!collider.debugDraw)
                continue;

            const TransformComponent* tr = world.GetComponent<TransformComponent>(entityId);
            if (!tr || !tr->enabled) continue;

            XMFLOAT3 scale = tr->scale;
            scale.x = std::abs(scale.x);
            scale.y = std::abs(scale.y);
            scale.z = std::abs(scale.z);

            const XMFLOAT4 color = collider.isTrigger
                ? XMFLOAT4(0.9f, 0.35f, 0.05f, 1.0f)
                : XMFLOAT4(0.35f, 0.6f, 1.0f, 1.0f);
            XMVECTOR rot = EulerToQuaternion(tr->rotation);

            XMFLOAT3 localOffset = collider.offset;
            localOffset.x *= scale.x;
            localOffset.y *= scale.y;
            localOffset.z *= scale.z;
            XMVECTOR offsetV = RotateVector(XMLoadFloat3(&localOffset), rot);
            XMFLOAT3 center = tr->position;
            center.x += XMVectorGetX(offsetV);
            center.y += XMVectorGetY(offsetV);
            center.z += XMVectorGetZ(offsetV);

            if (collider.type == ColliderType::Box)
            {
                XMFLOAT3 he = collider.halfExtents;
                he.x *= scale.x;
                he.y *= scale.y;
                he.z *= scale.z;
                DrawBox(*target, center, he, rot, color);
            }
            else if (collider.type == ColliderType::Sphere)
            {
                float sMax = std::max({ scale.x, scale.y, scale.z });
                float radius = collider.radius * sMax;
                DrawSphere(*target, center, radius, color);
            }
            else if (collider.type == ColliderType::Capsule)
            {
                float radius = 0.0f;
                float halfHeight = 0.0f;
                if (collider.capsuleAlignYAxis)
                {
                    float radial = std::max(scale.x, scale.z);
                    radius = collider.capsuleRadius * radial;
                    halfHeight = collider.capsuleHalfHeight * scale.y;
                }
                else
                {
                    float radial = std::max(scale.y, scale.z);
                    radius = collider.capsuleRadius * radial;
                    halfHeight = collider.capsuleHalfHeight * scale.x;
                }
                DrawCapsule(*target, center, radius, halfHeight, collider.capsuleAlignYAxis, rot, color);
            }
        }

        // Mesh collider debug draw (approximate box)
        for (const auto& [entityId, meshCollider] : world.GetComponents<Phy_MeshColliderComponent>())
        {
            if (!meshCollider.debugDraw)
                continue;

            const TransformComponent* tr = world.GetComponent<TransformComponent>(entityId);
            if (!tr || !tr->enabled) continue;

            XMFLOAT3 scale = tr->scale;
            scale.x = std::abs(scale.x);
            scale.y = std::abs(scale.y);
            scale.z = std::abs(scale.z);

            XMFLOAT3 he{ 0.5f * scale.x, 0.5f * scale.y, 0.5f * scale.z };
            XMVECTOR rot = EulerToQuaternion(tr->rotation);
            const XMFLOAT4 color = meshCollider.isTrigger
                ? XMFLOAT4(0.95f, 0.25f, 0.45f, 1.0f)
                : XMFLOAT4(0.45f, 0.9f, 0.9f, 1.0f);

            DrawBox(*target, tr->position, he, rot, color);
        }

        // CCT debug draw
        for (const auto& [entityId, cct] : world.GetComponents<Phy_CCTComponent>())
        {
            const TransformComponent* tr = world.GetComponent<TransformComponent>(entityId);
            if (!tr || !tr->enabled) continue;

            XMFLOAT3 scale = tr->scale;
            scale.x = std::abs(scale.x);
            scale.y = std::abs(scale.y);
            scale.z = std::abs(scale.z);

            const float radial = std::max(scale.x, scale.z);
            const float radius = cct.radius * radial;
            const float halfHeight = cct.halfHeight * scale.y;

            XMFLOAT3 center = tr->position;
            center.y += (halfHeight + radius);

            const XMFLOAT4 color(0.95f, 0.85f, 0.2f, 1.0f);
            DrawCapsule(*target, center, radius, halfHeight, true, XMQuaternionIdentity(), color);
        }

        // WeaponTrace shapes debug draw
        for (const auto& [entityId, trace] : world.GetComponents<WeaponTraceComponent>())
        {
            if (!trace.debugDraw || trace.shapes.empty())
                continue;

            EntityId basis = entityId;
            if (trace.traceBasisGuid != 0)
            {
                EntityId resolved = world.FindEntityByGuid(trace.traceBasisGuid);
                if (resolved == InvalidEntityId)
                    continue;
                basis = resolved;
            }

            XMFLOAT3 basisPos{};
            XMFLOAT4 basisRot{};
            if (!TryGetBasisPose(world, basis, basisPos, basisRot))
                continue;

            const XMMATRIX basisWorld = BuildBasisWorldMatrix(basisPos, basisRot);
            const XMFLOAT4 inactiveColor = (entityId == selectedEntity)
                ? XMFLOAT4(0.2f, 0.9f, 0.2f, 1.0f)
                : XMFLOAT4(0.1f, 0.7f, 0.1f, 1.0f);
            const XMFLOAT4 activeColor = (entityId == selectedEntity)
                ? XMFLOAT4(0.2f, 0.4f, 1.0f, 1.0f)
                : XMFLOAT4(0.1f, 0.4f, 0.9f, 1.0f);
            const XMFLOAT4 hitColor = (entityId == selectedEntity)
                ? XMFLOAT4(1.0f, 0.2f, 0.2f, 1.0f)
                : XMFLOAT4(0.9f, 0.1f, 0.1f, 1.0f);
            const XMFLOAT4 pathStartColor = XMFLOAT4(1.0f, 0.85f, 0.1f, 1.0f);
            const XMFLOAT4 pathEndColor = XMFLOAT4(0.15f, 1.0f, 0.95f, 1.0f);
            const XMFLOAT4 pathGridColor = XMFLOAT4(0.8f, 0.85f, 1.0f, 1.0f);
            const XMFLOAT4 sweepLineColor = XMFLOAT4(1.0f, 0.6f, 0.1f, 1.0f);
            const XMFLOAT4 sweepStartColor = XMFLOAT4(1.0f, 0.45f, 0.45f, 1.0f);
            const XMFLOAT4 sweepEndColor = XMFLOAT4(0.35f, 1.0f, 0.75f, 1.0f);
            float traceGlobalPhase = (trace.activeWindowDurationSec > 0.0f)
                ? std::clamp(trace.activeElapsedSec / trace.activeWindowDurationSec, 0.0f, 1.0f)
                : (trace.active ? 1.0f : 0.0f);
            bool hasPreviewPhase = false;
            EntityId previewOwner = InvalidEntityId;
            std::string previewClipName;
            float previewClipDurationSec = 0.0f;
            if (editorMode)
            {
                hasPreviewPhase = ResolveTracePreviewPhase(
                    skinnedRegistry,
                    world,
                    entityId,
                    trace,
                    traceGlobalPhase,
                    &previewOwner,
                    &previewClipName,
                    &previewClipDurationSec);
            }
            const bool useEditorPreview = editorMode && hasPreviewPhase;
            bool hasAttackDriverPreviewWindow = false;
            bool attackDriverPreviewActive = false;
            float attackDriverPreviewPhase = traceGlobalPhase;
            if (useEditorPreview)
            {
                hasAttackDriverPreviewWindow = ResolveAttackDriverPreviewWindowForTrace(
                    world,
                    entityId,
                    previewOwner,
                    previewClipName,
                    previewClipDurationSec,
                    traceGlobalPhase,
                    attackDriverPreviewActive,
                    attackDriverPreviewPhase);
            }

            const bool hasHit = trace.active && !trace.hitVictims.empty();
            const float markerRadius = std::max(0.005f, trace.debugPathMarkerRadius);
            const std::uint32_t gridSteps = std::max(1u, trace.debugPathGridSteps);

            for (size_t shapeIndex = 0; shapeIndex < trace.shapes.size(); ++shapeIndex)
            {
                const auto& shape = trace.shapes[shapeIndex];
                if (!shape.enabled)
                    continue;

                const bool shapeActiveNow = useEditorPreview
                    ? (hasAttackDriverPreviewWindow ? attackDriverPreviewActive : trace.active)
                    : trace.active;
                const XMFLOAT4 shapeColor = hasHit ? hitColor : (shapeActiveNow ? activeColor : inactiveColor);

                XMFLOAT3 center{};
                XMFLOAT4 rot{};
                const float previewPhase = hasAttackDriverPreviewWindow ? attackDriverPreviewPhase : traceGlobalPhase;
                const float shapePathPhase = ToShapePathPhase(previewPhase);
                const XMFLOAT3 localPos = ResolveShapePathLocalPos(shape, shapePathPhase);
                if (!ComputeShapeWorldPose(shape, basisWorld, localPos, center, rot))
                    continue;

                XMVECTOR rotQ = XMLoadFloat4(&rot);
                if (shape.type == WeaponTraceShapeType::Sphere)
                {
                    DrawSphere(*target, center, shape.radius, shapeColor);
                }
                else if (shape.type == WeaponTraceShapeType::Capsule)
                {
                    DrawCapsule(*target, center, shape.radius, shape.capsuleHalfHeight, true, rotQ, shapeColor);
                }
                else if (shape.type == WeaponTraceShapeType::Box)
                {
                    DrawBox(*target, center, shape.boxHalfExtents, rotQ, shapeColor);
                }

                // Editor clip-time preview should be deterministic and not mixed with runtime sweep-cache.
                if (!useEditorPreview && trace.active && !trace.debugSweepSegments.empty())
                {
                    for (const auto& seg : trace.debugSweepSegments)
                    {
                        if (seg.shapeIndex != static_cast<std::uint32_t>(shapeIndex))
                            continue;

                        target->AddLine(seg.startCenterWS, seg.endCenterWS, sweepLineColor);

                        const XMVECTOR startRotQ = XMLoadFloat4(&seg.startRotWS);
                        const XMVECTOR endRotQ = XMLoadFloat4(&seg.endRotWS);
                        if (shape.type == WeaponTraceShapeType::Sphere)
                        {
                            DrawSphere(*target, seg.startCenterWS, shape.radius, sweepStartColor);
                            DrawSphere(*target, seg.endCenterWS, shape.radius, sweepEndColor);
                        }
                        else if (shape.type == WeaponTraceShapeType::Capsule)
                        {
                            DrawCapsule(*target, seg.startCenterWS, shape.radius, shape.capsuleHalfHeight, true, startRotQ, sweepStartColor);
                            DrawCapsule(*target, seg.endCenterWS, shape.radius, shape.capsuleHalfHeight, true, endRotQ, sweepEndColor);
                        }
                        else if (shape.type == WeaponTraceShapeType::Box)
                        {
                            DrawBox(*target, seg.startCenterWS, shape.boxHalfExtents, startRotQ, sweepStartColor);
                            DrawBox(*target, seg.endCenterWS, shape.boxHalfExtents, endRotQ, sweepEndColor);
                        }
                    }
                }

                if (trace.debugPathGuide && shape.pathEnabled)
                {
                    XMFLOAT3 pathStartCenter{};
                    XMFLOAT4 pathStartRot{};
                    XMFLOAT3 pathEndCenter{};
                    XMFLOAT4 pathEndRot{};
                    if (!ComputeShapeWorldPose(shape, basisWorld, shape.pathStartLocalPos, pathStartCenter, pathStartRot))
                        continue;
                    if (!ComputeShapeWorldPose(shape, basisWorld, shape.pathEndLocalPos, pathEndCenter, pathEndRot))
                        continue;

                    DrawSphere(*target, pathStartCenter, markerRadius, pathStartColor);
                    DrawSphere(*target, pathEndCenter, markerRadius, pathEndColor);
                    XMFLOAT3 prevPathCenter = pathStartCenter;

                    for (std::uint32_t s = 1; s <= gridSteps; ++s)
                    {
                        const float t = static_cast<float>(s) / static_cast<float>(gridSteps);
                        XMFLOAT3 pathCenter{};
                        XMFLOAT4 pathRot{};
                        const XMFLOAT3 pathLocalPos = ResolveShapePathLocalPos(shape, t);
                        if (ComputeShapeWorldPose(shape, basisWorld, pathLocalPos, pathCenter, pathRot))
                        {
                            target->AddLine(prevPathCenter, pathCenter, pathGridColor);
                            if (s < gridSteps)
                                DrawSphere(*target, pathCenter, markerRadius * 0.5f, pathGridColor);
                            prevPathCenter = pathCenter;
                        }
                    }
                }
            }
        }
    }
}
