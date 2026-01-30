#include "ViewportPicker.h"

#include <algorithm>
#include <cfloat>
#include <cmath>

#include "Rendering/SkinnedMeshRegistry.h"
#include "3Dmodel/FbxModel.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/RectLightComponent.h"

using namespace DirectX;

namespace Alice
{
    namespace
    {
        struct Ray
        {
            XMFLOAT3 origin;    // 시작점
            XMFLOAT3 direction; // 정규화된 방향 벡터
        };

        // 로컬 공간에서의 레이 - AABB([-1,1]^3) 교차 테스트
        bool IntersectRayAABB(const Ray& ray,
                              const XMFLOAT3& min,
                              const XMFLOAT3& max,
                              float& outT)
        {
            XMVECTOR O = XMLoadFloat3(&ray.origin);
            XMVECTOR D = XMLoadFloat3(&ray.direction);

            XMVECTOR boxMin = XMLoadFloat3(&min);
            XMVECTOR boxMax = XMLoadFloat3(&max);

            // 슬랩(Slab) 방식
            XMVECTOR invD = XMVectorReciprocal(D);

            XMVECTOR t1 = XMVectorMultiply(XMVectorSubtract(boxMin, O), invD);
            XMVECTOR t2 = XMVectorMultiply(XMVectorSubtract(boxMax, O), invD);

            XMVECTOR tMinVec = XMVectorMin(t1, t2);
            XMVECTOR tMaxVec = XMVectorMax(t1, t2);

            float tMin = (std::max)((std::max)(XMVectorGetX(tMinVec), XMVectorGetY(tMinVec)),
                                    XMVectorGetZ(tMinVec));
            float tMax = (std::min)((std::min)(XMVectorGetX(tMaxVec), XMVectorGetY(tMaxVec)),
                                    XMVectorGetZ(tMaxVec));

            if (tMax < 0.0f || tMin > tMax)
                return false;

            outT = (tMin >= 0.0f) ? tMin : tMax;
            return outT >= 0.0f;
        }
    }

    EntityId ViewportPicker::Pick(const World& world,
                                  const Camera& camera,
                                  const SkinnedMeshRegistry* skinnedRegistry,
                                  float u,
                                  float v) const
    {
        // 1) NDC 좌표 (-1~1) 변환
        const float ndcX = 2.0f * u - 1.0f;
        const float ndcY = 1.0f - 2.0f * v;

        XMMATRIX view       = camera.GetViewMatrix();
        XMMATRIX projection = camera.GetProjectionMatrix();
        XMMATRIX viewProj    = XMMatrixMultiply(view, projection);
        XMMATRIX invViewProj = XMMatrixInverse(nullptr, viewProj);

        // 2) 클립 공간 → 월드 공간
        XMVECTOR nearPoint = XMVectorSet(ndcX, ndcY, 0.0f, 1.0f);
        XMVECTOR farPoint  = XMVectorSet(ndcX, ndcY, 1.0f, 1.0f);

        nearPoint = XMVector3TransformCoord(nearPoint, invViewProj);
        farPoint  = XMVector3TransformCoord(farPoint,  invViewProj);

        XMVECTOR dirWorld = XMVector3Normalize(XMVectorSubtract(farPoint, nearPoint));

        // 레이의 시작점: 오쏘그래픽은 nearPlane 위의 픽셀 위치, 퍼스펙티브도 nearPoint 사용 (Camera::ScreenToWorldRay와 동일)
        XMVECTOR originWorld = nearPoint;

        Ray rayWorld {};
        XMStoreFloat3(&rayWorld.origin,    originWorld);
        XMStoreFloat3(&rayWorld.direction, dirWorld);

        const auto& transforms = world.GetComponents<TransformComponent>();
        if (transforms.empty())
            return InvalidEntityId;

        float   nearestDist = FLT_MAX;
        EntityId hitEntity  = InvalidEntityId;

        // 오브젝트별로: 월드 행렬의 역행렬을 사용해 레이를 로컬 공간으로 변환 후,
        // 로컬 AABB에 대한 교차를 검사합니다.
        const XMFLOAT3 defaultBoxMin(-1.0f, -1.0f, -1.0f);  // SkinnedMesh bounds 폴백
        const XMFLOAT3 defaultBoxMax( 1.0f,  1.0f,  1.0f);
        const XMFLOAT3 lightBoxMin(-0.1f, -0.1f, -0.1f);     // 라이트용 작은 큐브
        const XMFLOAT3 lightBoxMax( 0.1f,  0.1f,  0.1f);

        for (const auto& [entityId, transform] : transforms)
        {
            // Transform.enabled == false는 스킵
            if (!transform.enabled || !transform.visible)
                continue;

            // 피킹 가능한 컴포넌트가 있는지 확인
            bool hasPickableComponent = false;
            XMFLOAT3 boxMin = defaultBoxMin;
            XMFLOAT3 boxMax = defaultBoxMax;

            // 1) SkinnedMeshComponent: 메시 bounds 사용
            if (skinnedRegistry)
            {
                if (const auto* skinned = world.GetComponent<SkinnedMeshComponent>(entityId))
                {
                    hasPickableComponent = true;
                    if (!skinned->meshAssetPath.empty())
                    {
                        auto mesh = skinnedRegistry->Find(skinned->meshAssetPath);
                        if (mesh && mesh->sourceModel)
                        {
                            XMFLOAT3 mn{}, mx{};
                            if (mesh->sourceModel->GetLocalBounds(mn, mx))
                            {
                                boxMin = mn;
                                boxMax = mx;
                            }
                        }
                    }
                }
            }

            // 2) 라이트 컴포넌트들: 작은 큐브 bounds 사용 (렌더 가능)
            if (world.GetComponent<PointLightComponent>(entityId) ||
                world.GetComponent<SpotLightComponent>(entityId) ||
                world.GetComponent<RectLightComponent>(entityId))
            {
                hasPickableComponent = true;
                boxMin = lightBoxMin;
                boxMax = lightBoxMax;
            }

            // 피킹 가능한 컴포넌트가 없으면 스킵
            if (!hasPickableComponent)
                continue;

            // 부모-자식 계층 포함 월드 행렬 (렌더링과 동일) — 직접 S*R*T 금지
            XMMATRIX worldM    = world.ComputeWorldMatrix(entityId);
            XMMATRIX invWorldM = XMMatrixInverse(nullptr, worldM);

            // 레이를 로컬 공간으로 변환
            // 레이 길이는 카메라 farPlane 기반 (큰 씬/스케일에서도 정확한 피킹)
            float rayLength = camera.GetFarPlane();
            XMVECTOR originLocal = XMVector3TransformCoord(originWorld, invWorldM);
            XMVECTOR endWorld    = XMVectorAdd(originWorld, XMVectorScale(dirWorld, rayLength));
            XMVECTOR endLocal    = XMVector3TransformCoord(endWorld, invWorldM);
            XMVECTOR dirLocal    = XMVector3Normalize(XMVectorSubtract(endLocal, originLocal));

            Ray rayLocal {};
            XMStoreFloat3(&rayLocal.origin,    originLocal);
            XMStoreFloat3(&rayLocal.direction, dirLocal);

            float tLocal = 0.0f;
            if (!IntersectRayAABB(rayLocal, boxMin, boxMax, tLocal))
                continue;

            // 로컬 히트 포인트 → 월드 좌표
            XMVECTOR hitLocal = XMVectorAdd(originLocal, XMVectorScale(dirLocal, tLocal));
            XMVECTOR hitWorld = XMVector3TransformCoord(hitLocal, worldM);

            // 카메라 기준 거리 계산
            float dist = XMVectorGetX(XMVector3Length(XMVectorSubtract(hitWorld, originWorld)));

            if (dist < nearestDist)
            {
                nearestDist = dist;
                hitEntity   = entityId;
            }
        }

        return hitEntity;
    }
}
