#include "PhysicsDebug.h"
#include "Runtime/Physics/Components/Phy_ColliderComponent.h"
#include "Runtime/Physics/Components/Phy_RigidBodyComponent.h"
#include <Runtime/ECS/World.h>
#include <Runtime/Rendering/DebugDrawSystem.h>
#include <DirectXMath.h>
#include <cmath>

using namespace DirectX;

namespace Alice
{
    namespace PhysicsDebug
    {
        namespace
        {
            // 오일러 각도를 쿼터니언으로 변환
            // PhysicsSystem::ToQuat와 동일한 축 매핑: yaw=Y, pitch=X, roll=Z
            // CreateFromYawPitchRoll(yaw=Y, pitch=X, roll=Z) ↔ XMQuaternionRotationRollPitchYaw(pitch=X, yaw=Y, roll=Z)
            XMVECTOR EulerToQuaternion(const XMFLOAT3& euler)
            {
                return XMQuaternionRotationRollPitchYaw(euler.x, euler.y, euler.z);
            }

            // 쿼터니언으로 벡터 회전
            XMVECTOR RotateVector(const XMVECTOR& v, const XMVECTOR& q)
            {
                return XMVector3Rotate(v, q);
            }

            // 박스 8개 코너 계산
            void GetBoxCorners(const XMFLOAT3& center, const XMFLOAT3& halfExtents, const XMFLOAT3& rotation, XMFLOAT3 corners[8])
            {
                XMVECTOR pos = XMLoadFloat3(&center);
                XMVECTOR rot = EulerToQuaternion(rotation);
                XMVECTOR he = XMLoadFloat3(&halfExtents);

                // 로컬 공간의 8개 코너
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

                // 회전 및 이동 적용
                for (int i = 0; i < 8; ++i)
                {
                    XMVECTOR local = XMLoadFloat3(&localCorners[i]);
                    XMVECTOR rotated = RotateVector(local, rot);
                    XMVECTOR world = XMVectorAdd(pos, rotated);
                    XMStoreFloat3(&corners[i], world);
                }
            }

            // 박스 와이어프레임 그리기
            void DrawBox(DebugDrawSystem& dbg, const XMFLOAT3& center, const XMFLOAT3& halfExtents, const XMFLOAT3& rotation, const XMFLOAT4& color)
            {
                XMFLOAT3 corners[8];
                GetBoxCorners(center, halfExtents, rotation, corners);

                // bottom
                dbg.AddLine(corners[0], corners[1], color);
                dbg.AddLine(corners[1], corners[2], color);
                dbg.AddLine(corners[2], corners[3], color);
                dbg.AddLine(corners[3], corners[0], color);
                // top
                dbg.AddLine(corners[4], corners[5], color);
                dbg.AddLine(corners[5], corners[6], color);
                dbg.AddLine(corners[6], corners[7], color);
                dbg.AddLine(corners[7], corners[4], color);
                // sides
                dbg.AddLine(corners[0], corners[4], color);
                dbg.AddLine(corners[1], corners[5], color);
                dbg.AddLine(corners[2], corners[6], color);
                dbg.AddLine(corners[3], corners[7], color);
            }

            // 구 와이어프레임 그리기 (원으로 근사)
            void DrawSphere(DebugDrawSystem& dbg, const XMFLOAT3& center, float radius, const XMFLOAT4& color)
            {
                const int segments = 16;
                const float angleStep = XM_2PI / segments;

                // XY 평면 원
                for (int i = 0; i < segments; ++i)
                {
                    float a1 = i * angleStep;
                    float a2 = (i + 1) * angleStep;
                    XMFLOAT3 p1 = { center.x + radius * std::cos(a1), center.y + radius * std::sin(a1), center.z };
                    XMFLOAT3 p2 = { center.x + radius * std::cos(a2), center.y + radius * std::sin(a2), center.z };
                    dbg.AddLine(p1, p2, color);
                }

                // XZ 평면 원
                for (int i = 0; i < segments; ++i)
                {
                    float a1 = i * angleStep;
                    float a2 = (i + 1) * angleStep;
                    XMFLOAT3 p1 = { center.x + radius * std::cos(a1), center.y, center.z + radius * std::sin(a1) };
                    XMFLOAT3 p2 = { center.x + radius * std::cos(a2), center.y, center.z + radius * std::sin(a2) };
                    dbg.AddLine(p1, p2, color);
                }

                // YZ 평면 원
                for (int i = 0; i < segments; ++i)
                {
                    float a1 = i * angleStep;
                    float a2 = (i + 1) * angleStep;
                    XMFLOAT3 p1 = { center.x, center.y + radius * std::cos(a1), center.z + radius * std::sin(a1) };
                    XMFLOAT3 p2 = { center.x, center.y + radius * std::cos(a2), center.z + radius * std::sin(a2) };
                    dbg.AddLine(p1, p2, color);
                }
            }

            // 캡슐 와이어프레임 그리기 (Y축 정렬 기준)
            void DrawCapsule(DebugDrawSystem& dbg, const XMFLOAT3& center, float radius, float halfHeight, bool alignYAxis, const XMFLOAT3& rotation, const XMFLOAT4& color)
            {
                const int segments = 16;
                const float angleStep = XM_2PI / segments;

                XMVECTOR pos = XMLoadFloat3(&center);
                XMVECTOR rot = EulerToQuaternion(rotation);

                if (alignYAxis)
                {
                    // 상단 반구 (Y+)
                    for (int i = 0; i < segments; ++i)
                    {
                        float a1 = i * angleStep;
                        float a2 = (i + 1) * angleStep;
                        float y1 = halfHeight + radius * std::sin(a1);
                        float y2 = halfHeight + radius * std::sin(a2);
                        float r1 = radius * std::cos(a1);
                        float r2 = radius * std::cos(a2);

                        // XY 평면 원
                        XMVECTOR p1 = XMVectorSet(r1, y1, 0.0f, 0.0f);
                        XMVECTOR p2 = XMVectorSet(r2, y2, 0.0f, 0.0f);
                        p1 = XMVectorAdd(pos, RotateVector(p1, rot));
                        p2 = XMVectorAdd(pos, RotateVector(p2, rot));
                        XMFLOAT3 f1, f2;
                        XMStoreFloat3(&f1, p1);
                        XMStoreFloat3(&f2, p2);
                        dbg.AddLine(f1, f2, color);
                    }

                    // 하단 반구 (Y-)
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

                    // 실린더 부분 (상단/하단 원)
                    for (int i = 0; i < segments; ++i)
                    {
                        float a1 = i * angleStep;
                        float a2 = (i + 1) * angleStep;
                        float r1 = radius * std::cos(a1);
                        float r2 = radius * std::cos(a2);
                        float z1 = radius * std::sin(a1);
                        float z2 = radius * std::sin(a2);

                        // 상단 원
                        XMVECTOR p1 = XMVectorSet(r1, halfHeight, z1, 0.0f);
                        XMVECTOR p2 = XMVectorSet(r2, halfHeight, z2, 0.0f);
                        p1 = XMVectorAdd(pos, RotateVector(p1, rot));
                        p2 = XMVectorAdd(pos, RotateVector(p2, rot));
                        XMFLOAT3 f1, f2;
                        XMStoreFloat3(&f1, p1);
                        XMStoreFloat3(&f2, p2);
                        dbg.AddLine(f1, f2, color);

                        // 하단 원
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
                    // X축 정렬 (PhysX 기본): 원을 x=±halfHeight에서 yz로 그림
                    // 상단 반구 (X+)
                    for (int i = 0; i < segments; ++i)
                    {
                        float a1 = i * angleStep;
                        float a2 = (i + 1) * angleStep;
                        float x1 = halfHeight + radius * std::sin(a1);
                        float x2 = halfHeight + radius * std::sin(a2);
                        float r1 = radius * std::cos(a1);
                        float r2 = radius * std::cos(a2);

                        // YZ 평면 원
                        XMVECTOR p1 = XMVectorSet(x1, r1, 0.0f, 0.0f);
                        XMVECTOR p2 = XMVectorSet(x2, r2, 0.0f, 0.0f);
                        p1 = XMVectorAdd(pos, RotateVector(p1, rot));
                        p2 = XMVectorAdd(pos, RotateVector(p2, rot));
                        XMFLOAT3 f1, f2;
                        XMStoreFloat3(&f1, p1);
                        XMStoreFloat3(&f2, p2);
                        dbg.AddLine(f1, f2, color);
                    }

                    // 하단 반구 (X-)
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

                    // 실린더 부분 (상단/하단 원)
                    for (int i = 0; i < segments; ++i)
                    {
                        float a1 = i * angleStep;
                        float a2 = (i + 1) * angleStep;
                        float r1 = radius * std::cos(a1);
                        float r2 = radius * std::cos(a2);
                        float z1 = radius * std::sin(a1);
                        float z2 = radius * std::sin(a2);

                        // 상단 원
                        XMVECTOR p1 = XMVectorSet(halfHeight, r1, z1, 0.0f);
                        XMVECTOR p2 = XMVectorSet(halfHeight, r2, z2, 0.0f);
                        p1 = XMVectorAdd(pos, RotateVector(p1, rot));
                        p2 = XMVectorAdd(pos, RotateVector(p2, rot));
                        XMFLOAT3 f1, f2;
                        XMStoreFloat3(&f1, p1);
                        XMStoreFloat3(&f2, p2);
                        dbg.AddLine(f1, f2, color);

                        // 하단 원
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
        }

        void DrawColliders(World& world, DebugDrawSystem& debugDraw)
        {
            auto colliders = world.GetComponents<Phy_ColliderComponent>();
            for (const auto& [entityId, collider] : colliders)
            {
                auto* transform = world.GetComponent<TransformComponent>(entityId);
                if (!transform || !transform->enabled) continue;

                // Scale 반영
                XMFLOAT3 scale = transform->scale;
                scale.x = std::abs(scale.x);
                scale.y = std::abs(scale.y);
                scale.z = std::abs(scale.z);
                XMFLOAT3 offset = collider.offset;
                offset.x *= scale.x;
                offset.y *= scale.y;
                offset.z *= scale.z;
                XMVECTOR pos = XMLoadFloat3(&transform->position);
                XMVECTOR rot = EulerToQuaternion(transform->rotation);
                XMVECTOR off = XMLoadFloat3(&offset);
                XMVECTOR centerV = XMVectorAdd(pos, RotateVector(off, rot));
                XMFLOAT3 center{};
                XMStoreFloat3(&center, centerV);

                // 색상 결정 (Trigger는 다른 색상)
                XMFLOAT4 color = collider.isTrigger 
                    ? XMFLOAT4(1.0f, 1.0f, 0.0f, 1.0f)  // 노란색 (Trigger)
                    : XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f); // 초록색 (일반)

                switch (collider.type)
                {
                case ColliderType::Box:
                {
                    XMFLOAT3 he = collider.halfExtents;
                    he.x *= scale.x;
                    he.y *= scale.y;
                    he.z *= scale.z;
                    DrawBox(debugDraw, center, he, transform->rotation, color);
                    break;
                }
                case ColliderType::Sphere:
                {
                    float sMax = std::max({ scale.x, scale.y, scale.z });
                    float radius = collider.radius * sMax;
                    DrawSphere(debugDraw, center, radius, color);
                    break;
                }
                case ColliderType::Capsule:
                {
                    float radius, halfHeight;
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
                    DrawCapsule(debugDraw, center, radius, halfHeight, collider.capsuleAlignYAxis, transform->rotation, color);
                    break;
                }
                }
            }
        }
    }
}
