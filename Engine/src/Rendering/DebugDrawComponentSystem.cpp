#include "Rendering/DebugDrawComponentSystem.h"
#include "Rendering/DebugDrawSystem.h"

#include "Components/DebugDrawBoxComponent.h"
#include "Components/SoundBoxComponent.h"
#include "Components/TransformComponent.h"
#include "Components/WeaponTraceComponent.h"
#include "Components/IDComponent.h"
#include "PhysX/Components/Phy_ColliderComponent.h"
#include "PhysX/Components/Phy_CCTComponent.h"
#include "PhysX/Components/Phy_MeshColliderComponent.h"

#include <algorithm>
#include <vector>
#include <cmath>

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

        bool ComputeShapeWorldPose(const WeaponTraceShape& shape, const XMMATRIX& basisWorld, XMFLOAT3& outCenter, XMFLOAT4& outRot)
        {
            const float rx = XMConvertToRadians(shape.localRotDeg.x);
            const float ry = XMConvertToRadians(shape.localRotDeg.y);
            const float rz = XMConvertToRadians(shape.localRotDeg.z);
            const XMMATRIX R = XMMatrixRotationRollPitchYaw(rx, ry, rz);
            const XMMATRIX T = XMMatrixTranslation(shape.localPos.x, shape.localPos.y, shape.localPos.z);
            const XMMATRIX local = R * T;
            const XMMATRIX world = local * basisWorld;
            XMVECTOR s, r, t;
            if (!XMMatrixDecompose(&s, &r, &t, world))
                return false;
            XMStoreFloat3(&outCenter, t);
            XMStoreFloat4(&outRot, r);
            return true;
        }
    }

    void DebugDrawComponentSystem::Build(World& world,
                                         DebugDrawSystem* overlay,
                                         DebugDrawSystem* depth,
                                         EntityId selectedEntity,
                                         bool debugEnabled,
                                         bool editorMode)
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

            const bool hasHit = trace.active && !trace.hitVictims.empty();
            const XMFLOAT4 color = hasHit ? hitColor : (trace.active ? activeColor : inactiveColor);

            for (const auto& shape : trace.shapes)
            {
                if (!shape.enabled)
                    continue;

                XMFLOAT3 center{};
                XMFLOAT4 rot{};
                if (!ComputeShapeWorldPose(shape, basisWorld, center, rot))
                    continue;

                XMVECTOR rotQ = XMLoadFloat4(&rot);
                if (shape.type == WeaponTraceShapeType::Sphere)
                {
                    DrawSphere(*target, center, shape.radius, color);
                }
                else if (shape.type == WeaponTraceShapeType::Capsule)
                {
                    DrawCapsule(*target, center, shape.radius, shape.capsuleHalfHeight, true, rotQ, color);
                }
                else if (shape.type == WeaponTraceShapeType::Box)
                {
                    DrawBox(*target, center, shape.boxHalfExtents, rotQ, color);
                }
            }
        }
    }
}
