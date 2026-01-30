#include "Core/CameraSystem.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>
#include <sstream>

#include <DirectXMath.h>

#include "Core/World.h"
#include "Core/InputSystem.h"
#include "Core/GameObject.h"
#include "Components/TransformComponent.h"
#include "Components/CameraComponent.h"
#include "Components/CameraFollowComponent.h"
#include "Components/CameraSpringArmComponent.h"
#include "Components/CameraLookAtComponent.h"
#include "Components/CameraShakeComponent.h"
#include "Components/CameraBlendComponent.h"
#include "Components/SkinnedMeshComponent.h"
#include "PhysX/IPhysicsWorld.h"

namespace Alice
{
    namespace
    {
        static float DegToRad(float deg) { return deg * (DirectX::XM_PI / 180.0f); }
        static float RadToDeg(float rad) { return rad * (180.0f / DirectX::XM_PI); }

        static float ExpSmooth(float damping, float dt)
        {
            if (damping <= 0.0f) return 1.0f;
            return 1.0f - std::exp(-damping * dt);
        }

        static DirectX::XMFLOAT3 LerpVec(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, float t)
        {
            return {
                a.x + (b.x - a.x) * t,
                a.y + (b.y - a.y) * t,
                a.z + (b.z - a.z) * t
            };
        }

        static DirectX::XMFLOAT3 DirectionToEuler(const DirectX::XMFLOAT3& dir)
        {
            const float yaw = std::atan2(dir.x, dir.z);
            const float distXZ = std::sqrt(dir.x * dir.x + dir.z * dir.z);
            const float pitch = -std::atan2(dir.y, distXZ);
            return DirectX::XMFLOAT3(pitch, yaw, 0.0f);
        }

        static DirectX::XMFLOAT4 EulerToQuaternion(const DirectX::XMFLOAT3& eulerRad)
        {
            const DirectX::XMVECTOR q = DirectX::XMQuaternionRotationRollPitchYawFromVector(DirectX::XMLoadFloat3(&eulerRad));
            DirectX::XMFLOAT4 out{};
            DirectX::XMStoreFloat4(&out, q);
            return out;
        }

        static DirectX::XMFLOAT3 QuaternionToEuler(const DirectX::XMFLOAT4& quat)
        {
            // DirectXMath 컨벤션에 맞게 수정: (pitch=X, yaw=Y, roll=Z)
            using namespace DirectX;

            const XMVECTOR q = XMLoadFloat4(&quat);

            // DirectX 기본: +Z forward, +Y up
            const XMVECTOR f = XMVector3Rotate(XMVectorSet(0, 0, 1, 0), q);
            const XMVECTOR u = XMVector3Rotate(XMVectorSet(0, 1, 0, 0), q);

            XMFLOAT3 f3{};
            XMStoreFloat3(&f3, f);

            const float yaw   = std::atan2(f3.x, f3.z);
            const float pitch = -std::atan2(f3.y, std::sqrt(f3.x * f3.x + f3.z * f3.z));

            // roll: (yaw,pitch)만으로 만든 기준 up(u0)과 실제 up(u)의 차이를 forward축 기준으로 측정
            XMVECTOR worldUp = XMVectorSet(0, 1, 0, 0);
            XMVECTOR r0 = XMVector3Cross(worldUp, f);
            if (XMVectorGetX(XMVector3LengthSq(r0)) < 1e-6f)
                r0 = XMVector3Cross(XMVectorSet(1, 0, 0, 0), f);

            r0 = XMVector3Normalize(r0);
            const XMVECTOR u0 = XMVector3Cross(f, r0);

            const float roll = std::atan2(
                XMVectorGetX(XMVector3Dot(r0, u)),
                XMVectorGetX(XMVector3Dot(u0, u))
            );

            return { pitch, yaw, roll };
        }

        static DirectX::XMFLOAT3 GetForward(float yawRad, float pitchRad)
        {
            // DirectionToEuler()와 부호 규칙을 맞춤:
            // - pitch가 +면 아래를 보는 상태(카메라는 위), pitch가 -면 위를 보는 상태(카메라는 아래)
            const float cosPitch = std::cos(pitchRad);
            return {
                std::sin(yawRad) * cosPitch,
                -std::sin(pitchRad),  // DirectionToEuler()와 부호 일치
                std::cos(yawRad) * cosPitch
            };
        }

        static float AngleDeltaRad(float a, float b)
        {
            float d = a - b;
            while (d > DirectX::XM_PI) d -= DirectX::XM_2PI;
            while (d < -DirectX::XM_PI) d += DirectX::XM_2PI;
            return d;
        }

        static std::string Trim(const std::string& s)
        {
            std::size_t start = 0;
            while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
            std::size_t end = s.size();
            while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
            return s.substr(start, end - start);
        }

        static std::vector<EntityId> ResolveCameraList(World& world, const std::string& csv)
        {
            std::vector<EntityId> result;
            std::stringstream ss(csv);
            std::string item;
            while (std::getline(ss, item, ','))
            {
                const std::string name = Trim(item);
                if (name.empty())
                    continue;
                auto go = world.FindGameObject(name);
                if (go.IsValid())
                    result.push_back(go.id());
            }
            return result;
        }

        static EntityId FindPrimaryCamera(World& world)
        {
            EntityId camId = InvalidEntityId;
            for (const auto& [id, cam] : world.GetComponents<CameraComponent>())
            {
                if (cam.GetPrimary()) { camId = id; break; }
                if (camId == InvalidEntityId) camId = id;
            }
            return camId;
        }

        // 타겟 카메라의 속성을 안전하게 가져오는 함수
        static bool FetchTargetCameraInfo(World& world, EntityId id,
                                          DirectX::XMFLOAT3& outPos, DirectX::XMFLOAT3& outRot,
                                          float& outFov, float& outNear, float& outFar)
        {
            if (id == InvalidEntityId) return false;
            auto* tr = world.GetComponent<TransformComponent>(id);
            auto* cam = world.GetComponent<CameraComponent>(id);
            if (!tr || !cam || !tr->enabled) return false;

            outPos = tr->position;
            outRot = tr->rotation; // Euler Radian
            
            // CameraComponent 내부 객체에서 정보 가져옴
            const Camera& c = cam->GetCamera();
            outFov = c.GetFovYRadians();
            outNear = c.GetNearPlane();
            outFar = c.GetFarPlane();
            return true;
        }

        static bool GetCameraSnapshot(World& world, EntityId id,
                                      DirectX::XMFLOAT3& outPos,
                                      DirectX::XMFLOAT3& outRot,
                                      float& outFovY,
                                      float& outNear,
                                      float& outFar)
        {
            return FetchTargetCameraInfo(world, id, outPos, outRot, outFovY, outNear, outFar);
        }

        static void ApplyCameraSnapshot(World& world, EntityId id,
                                        const DirectX::XMFLOAT3& pos,
                                        const DirectX::XMFLOAT3& rot,
                                        float fovY,
                                        float nearPlane,
                                        float farPlane)
        {
            if (id == InvalidEntityId) return;
            auto* tr = world.GetComponent<TransformComponent>(id);
            auto* cam = world.GetComponent<CameraComponent>(id);
            if (!tr || !cam || !tr->enabled) return;
            tr->position = pos;
            tr->rotation = rot;
            // Camera 객체에 FOV, Near, Far 설정
            Camera& camera = cam->GetCamera();
            camera.SetPerspective(fovY, camera.GetAspectRatio(), nearPlane, farPlane);
        }
    }

    void CameraSystem::Update(World& world, InputSystem& input, float deltaTime)
    {
        // 1. [동기화] TransformComponent -> Camera Object
        // (스크립트나 다른 시스템이 Transform을 움직였을 때 Camera 뷰 행렬 갱신)
        world.UpdateTransformMatrices();

        for (auto [id, camComp] : world.GetComponents<CameraComponent>())
        {
            auto* tc = world.GetComponent<TransformComponent>(id);
            if (!tc || !tc->enabled) continue;

            // TransformComponent는 오일러 회전을 가짐 (내부적으로 Euler 저장)
            // Camera 객체에 동기화
            Camera& camera = camComp.GetCamera();
            camera.SetPosition(tc->position);
            
            // Transform의 오일러 회전을 쿼터니언으로 변환해서 카메라에 전달
            DirectX::XMVECTOR q = DirectX::XMQuaternionRotationRollPitchYaw(tc->rotation.x, tc->rotation.y, tc->rotation.z);
            DirectX::XMFLOAT4 quat;
            DirectX::XMStoreFloat4(&quat, q);
            camera.SetRotation(quat);

            camera.SetScale(tc->scale);
        }

        const EntityId outputId = FindPrimaryCamera(world);
        if (outputId == InvalidEntityId)
            return;

        auto* outputTr = world.GetComponent<TransformComponent>(outputId);
        auto* outputCam = world.GetComponent<CameraComponent>(outputId);
        if (!outputTr || !outputCam || !outputTr->enabled)
            return;

        auto* blendComp = world.GetComponent<CameraBlendComponent>(outputId);
        auto* shakeComp = world.GetComponent<CameraShakeComponent>(outputId);
        auto* followComp = world.GetComponent<CameraFollowComponent>(outputId);
        auto* springComp = world.GetComponent<CameraSpringArmComponent>(outputId);
        auto* lookAtComp = world.GetComponent<CameraLookAtComponent>(outputId);

        // --- 입력 처리 (System에서 담당) ---
        if (followComp && followComp->enabled && followComp->enableInput)
        {
            // Ctrl 키로 마우스 잠금 토글
            if (input.IsKeyPressed(DirectX::Keyboard::Keys::LeftControl) || 
                input.IsKeyPressed(DirectX::Keyboard::Keys::RightControl))
            {
                followComp->mouseLocked = !followComp->mouseLocked;
                
                if (followComp->mouseLocked)
                {
                    // 게임 모드: 커서 숨김 + 가둠
                    input.SetCursorVisible(false);
                    input.SetCursorLocked(true);
                }
                else
                {
                    // UI 모드: 커서 보임 + 풀기
                    input.SetCursorVisible(true);
                    input.SetCursorLocked(false);
                }
            }

            // LockOn 상태이고 매뉴얼 조작 불가능하면 스킵
            bool skipInput = (followComp->lockOnActive && !followComp->allowManualOrbitInLockOn);
            if (!skipInput && followComp->mouseLocked)
            {
                // 마우스 잠금 상태에서 바로 회전 (드래그 조건 제거)
                float dx = static_cast<float>(input.GetMouseDelta().x);
                float dy = static_cast<float>(input.GetMouseDelta().y);
                followComp->yawDeg -= dx * followComp->sensitivity;
                followComp->pitchDeg -= dy * followComp->sensitivity;
                followComp->pitchDeg = std::clamp(followComp->pitchDeg, followComp->pitchMinDeg, followComp->pitchMaxDeg);
            }
        }
        
        if (springComp && springComp->enabled && springComp->enableZoom)
        {
            float wheel = input.GetMouseScrollDelta();
            if (std::abs(wheel) > 0.001f)
            {
                springComp->desiredDistance -= wheel * springComp->zoomSpeed;
                springComp->desiredDistance = std::clamp(springComp->desiredDistance, springComp->minDistance, springComp->maxDistance);
            }
        }

        // --- 블렌드 로직 ---
        bool blending = (blendComp && blendComp->active);
        
        if (blending)
        {
            // 스냅샷 캡처 (블렌드 시작 시점)
            if (blendComp->needsSnapshot)
            {
                // 현재 상태를 Source로 저장
                const Camera& cam = outputCam->GetCamera();
                blendComp->sourcePosition = outputTr->position;
                blendComp->sourceRotation = outputTr->rotation;
                blendComp->sourceFovY = cam.GetFovYRadians();
                blendComp->sourceNear = cam.GetNearPlane();
                blendComp->sourceFar  = cam.GetFarPlane();
                
                blendComp->elapsed = 0.0f;
                blendComp->slowTriggered = false;
                blendComp->slowElapsed = 0.0f;
                blendComp->needsSnapshot = false; // 처리 완료

                // 타겟 ID 확인 (이름으로 찾기)
                if (blendComp->targetId == InvalidEntityId && !blendComp->targetName.empty())
                {
                    auto go = world.FindGameObject(blendComp->targetName);
                    if (go.IsValid()) blendComp->targetId = go.id();
                }
            }

            // 슬로우 모션 등 시간 조절
            float dt = deltaTime;
            if (blendComp->slowDuration > 0.0f && !blendComp->slowTriggered)
            {
                const float t0 = (blendComp->duration > 0.0f)
                    ? (blendComp->elapsed / blendComp->duration)
                    : 1.0f;
                if (t0 >= blendComp->slowTriggerT)
                {
                    blendComp->slowTriggered = true;
                    blendComp->slowElapsed = 0.0f;
                }
            }
            if (blendComp->slowTriggered && blendComp->slowDuration > 0.0f)
            {
                blendComp->slowElapsed += dt;
                if (blendComp->slowElapsed < blendComp->slowDuration)
                    dt *= std::max(0.01f, blendComp->slowTimeScale);
            }

            blendComp->elapsed += dt;
            float duration = std::max(blendComp->duration, 0.0001f);
            float t = std::clamp(blendComp->elapsed / duration, 0.0f, 1.0f);
            
            // Curve 사용 여부에 따라 보간 방식 결정
            if (blendComp->useSmoothStep)
                t = t * t * (3.0f - 2.0f * t);

            // 타겟 정보 가져오기
            DirectX::XMFLOAT3 tPos, tRot;
            float tFov, tNear, tFar;
            if (FetchTargetCameraInfo(world, blendComp->targetId, tPos, tRot, tFov, tNear, tFar))
            {
                // 보간 적용
                outputTr->position = LerpVec(blendComp->sourcePosition, tPos, t);
                
                DirectX::XMVECTOR qA = DirectX::XMQuaternionRotationRollPitchYaw(blendComp->sourceRotation.x, blendComp->sourceRotation.y, blendComp->sourceRotation.z);
                DirectX::XMVECTOR qB = DirectX::XMQuaternionRotationRollPitchYaw(tRot.x, tRot.y, tRot.z);
                DirectX::XMVECTOR qOut = DirectX::XMQuaternionSlerp(qA, qB, t);
                
                // 쿼터니언 -> 오일러 변환 후 Transform에 저장
                DirectX::XMFLOAT4 qf; DirectX::XMStoreFloat4(&qf, qOut);
                outputTr->rotation = QuaternionToEuler(qf);

                // 카메라 렌즈 설정 (FOV 등)
                float fov = blendComp->sourceFovY + (tFov - blendComp->sourceFovY) * t;
                float nr  = blendComp->sourceNear + (tNear - blendComp->sourceNear) * t;
                float fr  = blendComp->sourceFar  + (tFar - blendComp->sourceFar) * t;
                
                Camera& cam = outputCam->GetCamera();
                cam.SetPerspective(fov, cam.GetAspectRatio(), nr, fr);
            }

            // 블렌드 종료 시 처리
            if (t >= 1.0f)
            {
                blendComp->active = false; // 블렌드 종료

                // FollowComponent 내부 상태 동기화
                // 블렌드가 끝난 위치를 FollowComponent의 '현재 위치'로 갱신해 주어야
                // 다음 프레임에 Follow가 켜질 때 과거 위치로 튀지 않습니다.
                if (followComp)
                {
                    followComp->smoothedPosition = outputTr->position;
                    followComp->smoothedRotation = outputTr->rotation;
                    
                    // Yaw/Pitch도 현재 회전값에 맞춰 갱신 (쿼터니언->오일러 변환된 값 사용)
                    followComp->yawDeg   = RadToDeg(outputTr->rotation.y);
                    followComp->pitchDeg = RadToDeg(outputTr->rotation.x);
                }
                // SpringArm 거리도 필요하다면 여기서 타겟과의 거리를 계산해 갱신할 수 있습니다.
            }
        }

        // 변수 재정의 제거 (위에서 선언한 blending 변수 갱신)
        blending = (blendComp && blendComp->active);
        const bool externalLook = (!blending && lookAtComp && lookAtComp->enabled);

        // === 팔로우 처리 (블렌드 중이 아닐 때) ===
        if (!blending && followComp && followComp->enabled)
        {
            float dt = deltaTime * std::max(0.0f, followComp->cameraTimeScale);

            // 입력 처리는 Script(CameraController)가 담당

            // 타깃 찾기
            EntityId targetId = InvalidEntityId;
            if (!followComp->targetName.empty())
            {
                auto go = world.FindGameObject(followComp->targetName);
                if (go.IsValid()) targetId = go.id();
            }
            if (targetId == InvalidEntityId)
            {
                for (const auto& [id, skinned] : world.GetComponents<SkinnedMeshComponent>())
                {
                    if (!skinned.meshAssetPath.empty())
                    {
                        targetId = id;
                        break;
                    }
                }
            }
            if (targetId == InvalidEntityId)
                goto FollowDone;

            const auto* targetTr = world.GetComponent<TransformComponent>(targetId);
            if (!targetTr || !targetTr->enabled)
                goto FollowDone;

            // 모드 거리/시야각
            float modeDistance = followComp->baseDistance;
            float modeFovDeg = followComp->exploreFovDeg;
            switch (followComp->mode)
            {
            case 1: modeDistance = followComp->combatDistance; modeFovDeg = followComp->combatFovDeg; break;
            case 2: modeDistance = followComp->lockOnDistance; modeFovDeg = followComp->lockOnFovDeg; break;
            case 3: modeDistance = followComp->aimDistance; modeFovDeg = followComp->aimFovDeg; break;
            case 4: modeDistance = followComp->bossIntroDistance; modeFovDeg = followComp->bossIntroFovDeg; break;
            case 5: modeDistance = followComp->deathDistance; modeFovDeg = followComp->deathFovDeg; break;
            default: break;
            }

            if (springComp && springComp->enabled)
            {
                if (!springComp->initialized)
                {
                    springComp->distance = std::clamp(modeDistance, springComp->minDistance, springComp->maxDistance);
                    springComp->desiredDistance = springComp->distance;
                    springComp->initialized = true;
                }

                if (!springComp->enableZoom)
                {
                    springComp->desiredDistance = std::clamp(modeDistance, springComp->minDistance, springComp->maxDistance);
                }

                const float dAlpha = ExpSmooth(springComp->distanceDamping, dt);
                springComp->distance = springComp->distance + (springComp->desiredDistance - springComp->distance) * dAlpha;
            }

            if (!followComp->initialized)
            {
                followComp->smoothedPosition = outputTr->position;
                followComp->smoothedRotation = outputTr->rotation;
                followComp->initialized = true;
                
                // 초기화 시 마우스 잠금 및 커서 숨김
                if (followComp->mouseLocked)
                {
                    input.SetCursorVisible(false);
                    input.SetCursorLocked(true);
                }
            }

            DirectX::XMFLOAT3 pivot = {
                targetTr->position.x,
                targetTr->position.y + followComp->heightOffset,
                targetTr->position.z
            };

            const float yawRad = DegToRad(followComp->yawDeg);
            const DirectX::XMFLOAT3 right = { std::cos(yawRad), 0.0f, -std::sin(yawRad) };
            pivot.x += right.x * followComp->shoulderOffset * followComp->shoulderSide;
            pivot.z += right.z * followComp->shoulderOffset * followComp->shoulderSide;

            DirectX::XMFLOAT3 desiredForward = GetForward(yawRad, DegToRad(followComp->pitchDeg));
            if (followComp->lockOnActive && followComp->lockOnTargetId != InvalidEntityId)
            {
                if (const auto* lockTr = world.GetComponent<TransformComponent>(followComp->lockOnTargetId);
                    lockTr && lockTr->enabled)
                {
                    const DirectX::XMFLOAT3 to = {
                        lockTr->position.x - pivot.x,
                        lockTr->position.y - pivot.y,
                        lockTr->position.z - pivot.z
                    };
                    const float len = std::sqrt(to.x * to.x + to.y * to.y + to.z * to.z);
                    if (len > 0.001f)
                        desiredForward = { to.x / len, to.y / len, to.z / len };
                }
            }

            const float dist = (springComp && springComp->enabled) ? springComp->distance : modeDistance;
            DirectX::XMFLOAT3 desiredPos = {
                pivot.x - desiredForward.x * dist,
                pivot.y - desiredForward.y * dist,
                pivot.z - desiredForward.z * dist
            };

            // 스프링 암 충돌
            if (springComp && springComp->enabled && springComp->enableCollision)
            {
                if (auto* physics = world.GetPhysicsWorld())
                {
                    const Vec3 origin(pivot.x, pivot.y, pivot.z);
                    const Vec3 dir(desiredPos.x - pivot.x, desiredPos.y - pivot.y, desiredPos.z - pivot.z);
                    const float maxDist = std::sqrt(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
                    if (maxDist > 0.001f)
                    {
                        const Vec3 dirN = dir / maxDist;

                        // TODO(PhysX): 카메라 전용 queryMask/layerMask를 추가해 사용하세요.
                        const uint32_t layerMask = 0xFFFFFFFFu;
                        const uint32_t queryMask = 0xFFFFFFFFu;

                        bool hitFound = false;
                        float hitDistance = maxDist;

                        if (springComp->probeRadius > 0.001f)
                        {
                            SweepHit hit{};
                            hitFound = physics->SweepSphere(origin, springComp->probeRadius, dirN, maxDist, hit, layerMask, queryMask, false);
                            if (hitFound) hitDistance = hit.distance;
                        }
                        else
                        {
                            RaycastHit hit{};
                            hitFound = physics->RaycastEx(origin, dirN, maxDist, hit, layerMask, queryMask, false);
                            if (hitFound) hitDistance = hit.distance;
                        }

                        if (hitFound)
                        {
                            const float minDist = springComp ? springComp->minDistance : followComp->minDistance;
                            const float adjusted = std::max(minDist, hitDistance - springComp->probePadding);
                            desiredPos.x = pivot.x + dirN.x * adjusted;
                            desiredPos.y = pivot.y + dirN.y * adjusted;
                            desiredPos.z = pivot.z + dirN.z * adjusted;
                        }
                    }
                }
            }

            if (springComp && desiredPos.y < springComp->minHeight)
                desiredPos.y = springComp->minHeight;

            const DirectX::XMFLOAT3 toTarget = {
                pivot.x - desiredPos.x,
                pivot.y - desiredPos.y,
                pivot.z - desiredPos.z
            };
            const DirectX::XMFLOAT3 desiredRot = DirectionToEuler(toTarget);

            const float posAlpha = ExpSmooth(followComp->positionDamping, dt);
            float rotDamping = followComp->rotationDamping;
            const float deltaYaw = std::abs(RadToDeg(AngleDeltaRad(desiredRot.y, followComp->smoothedRotation.y)));
            if (deltaYaw > followComp->fastTurnYawThresholdDeg)
                rotDamping *= followComp->fastTurnMultiplier;
            const float rotAlpha = ExpSmooth(rotDamping, dt);

            followComp->smoothedPosition = LerpVec(followComp->smoothedPosition, desiredPos, posAlpha);

            outputTr->position = followComp->smoothedPosition;

            // LookAt이 활성화되어 있으면 회전은 LookAt이 담당하므로 Follow는 회전을 업데이트하지 않음
            if (!externalLook)
            {
                const DirectX::XMFLOAT4 qFrom = EulerToQuaternion(followComp->smoothedRotation);
                const DirectX::XMFLOAT4 qTo = EulerToQuaternion(desiredRot);
                DirectX::XMFLOAT4 qOut{};
                DirectX::XMStoreFloat4(&qOut, DirectX::XMQuaternionSlerp(DirectX::XMLoadFloat4(&qFrom),
                                                                         DirectX::XMLoadFloat4(&qTo),
                                                                         rotAlpha));
                followComp->smoothedRotation = QuaternionToEuler(qOut);

                outputTr->rotation = followComp->smoothedRotation;
                followComp->pitchDeg = RadToDeg(followComp->smoothedRotation.x);
                followComp->yawDeg = RadToDeg(followComp->smoothedRotation.y);
            }

            const float fovAlpha = ExpSmooth(followComp->fovDamping, dt);
            const float targetFovRad = DegToRad(modeFovDeg);
            Camera& camera = outputCam->GetCamera();
            float currentFov = camera.GetFovYRadians();
            float newFov = currentFov + (targetFovRad - currentFov) * fovAlpha;
            camera.SetPerspective(newFov, camera.GetAspectRatio(), camera.GetNearPlane(), camera.GetFarPlane());
        }
    FollowDone:

        // === 룩앳 처리 ===
        if (!blending && lookAtComp && lookAtComp->enabled)
        {
            auto go = world.FindGameObject(lookAtComp->targetName);
            if (go.IsValid())
            {
                if (const auto* targetTr = world.GetComponent<TransformComponent>(go.id());
                    targetTr && targetTr->enabled)
                {
                    const DirectX::XMFLOAT3 to = {
                        targetTr->position.x - outputTr->position.x,
                        targetTr->position.y - outputTr->position.y,
                        targetTr->position.z - outputTr->position.z
                    };
                    const DirectX::XMFLOAT3 desiredRot = DirectionToEuler(to);
                    const float rotAlpha = ExpSmooth(lookAtComp->rotationDamping, deltaTime);

                    const DirectX::XMFLOAT4 qFrom = EulerToQuaternion(outputTr->rotation);
                    const DirectX::XMFLOAT4 qTo = EulerToQuaternion(desiredRot);
                    DirectX::XMFLOAT4 qOut{};
                    DirectX::XMStoreFloat4(&qOut, DirectX::XMQuaternionSlerp(DirectX::XMLoadFloat4(&qFrom),
                                                                             DirectX::XMLoadFloat4(&qTo),
                                                                             rotAlpha));
                    outputTr->rotation = QuaternionToEuler(qOut);
                }
            }
        }

        // LookAt이 최종 회전을 결정했으니 Follow 내부 상태도 그걸로 맞춤. 다음 프레임에서 걸리는거 방지함
        if (!blending && followComp && followComp->enabled && lookAtComp && lookAtComp->enabled)
        {
            followComp->smoothedRotation = outputTr->rotation;
            followComp->yawDeg = RadToDeg(outputTr->rotation.y);
            followComp->pitchDeg = RadToDeg(outputTr->rotation.x);
        }

        // === 쉐이크 처리 ===
        // 이전 프레임 오프셋을 먼저 빼고, 이번 오프셋을 더한다(누적/드리프트 방지) 
        // 그 흔들리면서 트랜스폼 자체가 변해버려서 뒤로 밀리는 현상을 제거하기 위함
        if (shakeComp && shakeComp->enabled)
        {
            // 1) 항상 이전 프레임 오프셋 제거
            outputTr->position.x -= shakeComp->prevOffset.x;
            outputTr->position.y -= shakeComp->prevOffset.y;
            outputTr->position.z -= shakeComp->prevOffset.z;
            shakeComp->prevOffset = {};

            // 2) 이번 프레임 오프셋 계산/적용
            if (shakeComp->amplitude > 0.0f && shakeComp->duration > 0.0f)
            {
                shakeComp->elapsed += deltaTime;

                const float t01 = std::clamp(shakeComp->elapsed / shakeComp->duration, 0.0f, 1.0f);
                const float amp = shakeComp->amplitude * std::exp(-shakeComp->decay * t01);
                const float freq = shakeComp->frequency;

                DirectX::XMFLOAT3 offset{};
                offset.x = std::sin(shakeComp->elapsed * freq * 1.1f) * amp;
                offset.y = std::sin(shakeComp->elapsed * freq * 1.7f + 1.5f) * amp * 0.6f;
                offset.z = std::cos(shakeComp->elapsed * freq * 1.3f + 0.7f) * amp;

                outputTr->position.x += offset.x;
                outputTr->position.y += offset.y;
                outputTr->position.z += offset.z;

                shakeComp->prevOffset = offset;

                // 3) 종료 시 이번 프레임 오프셋도 즉시 제거(다음 프레임에 남지 않게)
                if (t01 >= 1.0f)
                {
                    outputTr->position.x -= shakeComp->prevOffset.x;
                    outputTr->position.y -= shakeComp->prevOffset.y;
                    outputTr->position.z -= shakeComp->prevOffset.z;
                    shakeComp->prevOffset = {};

                    shakeComp->duration = 0.0f;
                    shakeComp->amplitude = 0.0f;
                    shakeComp->elapsed = 0.0f;
                }
            }
        }
    }
}
