#include "CameraBossIntroProduction.h"

#include <algorithm>
#include <cmath>

#include <DirectXMath.h>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Input/InputTypes.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Rendering/Components/CameraComponent.h"
#include "Runtime/Rendering/Components/CameraBlendComponent.h"
#include "Runtime/Rendering/Components/CameraFollowComponent.h"
#include "Runtime/Rendering/Components/CameraLookAtComponent.h"
#include "Runtime/Rendering/Components/CameraSpringArmComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(CameraBossIntroProduction);

    void CameraBossIntroProduction::Start()
    {
        m_autoStartInitialized = false;
        m_autoStartPending = false;
        m_autoStartElapsedSec = 0.0f;
    }

    void CameraBossIntroProduction::Update(float deltaTime)
    {
        InitializeAutoStartOnce();

        auto go = gameObject();
        if (!go.IsValid())
            return;

        if (m_autoStartPending && !m_sequenceRunning)
        {
            m_autoStartElapsedSec += std::max(0.0f, deltaTime);
            if (m_autoStartElapsedSec >= std::max(0.0f, Get_m_autoStartDelaySec()))
            {
                const bool wasRunning = m_sequenceRunning;
                StartSequence();
                if (!wasRunning && m_sequenceRunning)
                    m_autoStartPending = false;
            }
        }

        if (Get_m_enableHotkey())
        {
            if (auto* input = Input())
            {
                if (input->GetKeyDown(KeyCode::Alpha9))
                {
                    if (!m_sequenceRunning || Get_m_restartOnKey())
                        StartSequence();
                    if (m_sequenceRunning)
                        m_autoStartPending = false;
                }
            }
        }

        if (!m_sequenceRunning)
            return;

        if (UpdateBlend(deltaTime))
            AdvanceAfterBlend();
    }

    void CameraBossIntroProduction::OnDisable()
    {
        RestoreIfNeeded();
    }

    void CameraBossIntroProduction::OnDestroy()
    {
        RestoreIfNeeded();
    }

    void CameraBossIntroProduction::InitializeAutoStartOnce()
    {
        if (m_autoStartInitialized)
            return;

        m_autoStartPending = Get_m_autoStartOnBegin();
        m_autoStartElapsedSec = 0.0f;
        m_autoStartInitialized = true;
    }

    void CameraBossIntroProduction::StartSequence()
    {
        DirectX::XMFLOAT3 playerPos{};
        DirectX::XMFLOAT3 bossPos{};
        if (!ResolveActorPositions(playerPos, bossPos))
            return;

        SetGameplayCameraControl(false);

        m_phase1StartPose = BuildShoulderPose(playerPos, bossPos, 0.0f, Get_m_phase1FovStartDeg());
        m_phase1EndPose = BuildShoulderPose(playerPos, bossPos, Get_m_phase1MoveTowardBoss(), Get_m_phase1FovEndDeg());

        m_phase2StartPose = BuildBossUpperPose(
            playerPos,
            bossPos,
            Get_m_phase2SideOffsetStart(),
            Get_m_phase2DistanceStart(),
            Get_m_phase2TowardBossStart(),
            Get_m_phase1FovEndDeg());

        m_phase2EndPose = BuildBossUpperPose(
            playerPos,
            bossPos,
            Get_m_phase2SideOffsetEnd(),
            Get_m_phase2DistanceEnd(),
            Get_m_phase2TowardBossEnd(),
            Get_m_phase2FovEndDeg());

        m_sequenceRunning = true;
        m_phase = Phase::ShoulderPush;

        ApplyPose(m_phase1StartPose);
        BeginBlend(
            m_phase1StartPose,
            m_phase1EndPose,
            ResolveDuration(Get_m_phase1Duration(), Get_m_phase1Speed()),
            Get_m_phase1UseSmoothStep());
    }

    void CameraBossIntroProduction::AdvanceAfterBlend()
    {
        if (!m_sequenceRunning)
            return;

        switch (m_phase)
        {
        case Phase::ShoulderPush:
            m_phase = Phase::BossUpperSweep;
            ApplyPose(m_phase2StartPose);
            BeginBlend(
                m_phase2StartPose,
                m_phase2EndPose,
                ResolveDuration(Get_m_phase2Duration(), Get_m_phase2Speed()),
                Get_m_phase2UseSmoothStep());
            break;

        case Phase::BossUpperSweep:
            ApplyPose(m_phase2EndPose);
            m_sequenceRunning = false;
            m_phase = Phase::None;
            m_blend.active = false;
            if (Get_m_restoreGameplayCameraOnFinish())
                SetGameplayCameraControl(true);
            break;

        case Phase::None:
        default:
            break;
        }
    }

    void CameraBossIntroProduction::BeginBlend(const Pose& from, const Pose& to, float duration, bool useSmoothStep)
    {
        m_blend.active = true;
        m_blend.from = from;
        m_blend.to = to;
        m_blend.duration = std::max(0.0f, duration);
        m_blend.elapsed = 0.0f;
        m_blend.useSmoothStep = useSmoothStep;
    }

    bool CameraBossIntroProduction::UpdateBlend(float deltaTime)
    {
        if (!m_blend.active)
            return true;

        m_blend.elapsed += std::max(0.0f, deltaTime);

        const float duration = std::max(m_blend.duration, 0.0001f);
        float t = std::clamp(m_blend.elapsed / duration, 0.0f, 1.0f);
        if (m_blend.useSmoothStep)
            t = ApplySmoothStep(t);

        Pose blended{};
        blended.position = LerpVec(m_blend.from.position, m_blend.to.position, t);
        blended.fovDeg = m_blend.from.fovDeg + (m_blend.to.fovDeg - m_blend.from.fovDeg) * t;

        const DirectX::XMFLOAT4 qFrom = EulerToQuaternion(m_blend.from.rotationRad);
        const DirectX::XMFLOAT4 qTo = EulerToQuaternion(m_blend.to.rotationRad);
        DirectX::XMFLOAT4 qOut{};
        DirectX::XMStoreFloat4(
            &qOut,
            DirectX::XMQuaternionSlerp(
                DirectX::XMLoadFloat4(&qFrom),
                DirectX::XMLoadFloat4(&qTo),
                t));
        blended.rotationRad = QuaternionToEuler(qOut);

        ApplyPose(blended);

        if (m_blend.elapsed >= duration)
        {
            m_blend.active = false;
            ApplyPose(m_blend.to);
            return true;
        }
        return false;
    }

    void CameraBossIntroProduction::ApplyPose(const Pose& pose)
    {
        auto* world = GetWorld();
        if (!world)
            return;

        EntityId outputId = InvalidEntityId;
        if (!TryResolveOutputCamera(outputId))
            return;

        auto* tr = world->GetComponent<TransformComponent>(outputId);
        auto* cam = world->GetComponent<CameraComponent>(outputId);
        if (!tr || !cam || !tr->enabled)
            return;

        tr->position = pose.position;
        tr->rotation = pose.rotationRad;

        cam->SetFov(std::max(1.0f, pose.fovDeg));
    }

    CameraBossIntroProduction::Pose CameraBossIntroProduction::BuildShoulderPose(
        const DirectX::XMFLOAT3& playerPos,
        const DirectX::XMFLOAT3& bossPos,
        float towardBossOffset,
        float fovDeg) const
    {
        const DirectX::XMFLOAT3 up{ 0.0f, 1.0f, 0.0f };
        const DirectX::XMFLOAT3 toBoss = NormalizeSafe(SubVec(bossPos, playerPos), DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f));
        const DirectX::XMFLOAT3 right = NormalizeSafe(CrossVec(up, toBoss), DirectX::XMFLOAT3(1.0f, 0.0f, 0.0f));
        const DirectX::XMFLOAT3 left = MulVec(right, -1.0f);

        const DirectX::XMFLOAT3 shoulderBase = AddVec(
            playerPos,
            AddVec(
                MulVec(up, Get_m_shoulderHeight()),
                AddVec(
                    MulVec(toBoss, -Get_m_shoulderBackDistance()),
                    MulVec(left, Get_m_shoulderLeftOffset()))));

        const DirectX::XMFLOAT3 cameraPos = AddVec(shoulderBase, MulVec(toBoss, towardBossOffset));
        const DirectX::XMFLOAT3 lookTarget = AddVec(bossPos, MulVec(up, Get_m_phase1LookHeight()));
        return MakeLookPose(cameraPos, lookTarget, fovDeg);
    }

    CameraBossIntroProduction::Pose CameraBossIntroProduction::BuildBossUpperPose(
        const DirectX::XMFLOAT3& playerPos,
        const DirectX::XMFLOAT3& bossPos,
        float sideOffset,
        float distanceFromBoss,
        float towardBossOffset,
        float fovDeg) const
    {
        const DirectX::XMFLOAT3 up{ 0.0f, 1.0f, 0.0f };
        const DirectX::XMFLOAT3 toPlayer = NormalizeSafe(SubVec(playerPos, bossPos), DirectX::XMFLOAT3(0.0f, 0.0f, -1.0f));
        const DirectX::XMFLOAT3 toBoss = MulVec(toPlayer, -1.0f);
        const DirectX::XMFLOAT3 side = NormalizeSafe(CrossVec(up, toPlayer), DirectX::XMFLOAT3(1.0f, 0.0f, 0.0f));

        const DirectX::XMFLOAT3 lookTarget = AddVec(bossPos, MulVec(up, Get_m_phase2LookHeight()));
        const DirectX::XMFLOAT3 cameraPos = AddVec(
            lookTarget,
            AddVec(
                MulVec(toPlayer, distanceFromBoss),
                AddVec(
                    MulVec(side, sideOffset),
                    AddVec(
                        MulVec(toBoss, towardBossOffset),
                        MulVec(up, Get_m_phase2CameraLift())))));

        return MakeLookPose(cameraPos, lookTarget, fovDeg);
    }

    CameraBossIntroProduction::Pose CameraBossIntroProduction::MakeLookPose(
        const DirectX::XMFLOAT3& cameraPos,
        const DirectX::XMFLOAT3& lookTarget,
        float fovDeg) const
    {
        Pose out{};
        out.position = cameraPos;
        out.rotationRad = DirectionToEuler(SubVec(lookTarget, cameraPos));
        out.fovDeg = std::max(1.0f, fovDeg);
        return out;
    }

    bool CameraBossIntroProduction::TryResolveOutputCamera(EntityId& outId) const
    {
        outId = InvalidEntityId;

        auto* world = GetWorld();
        if (!world || Get_m_outputCameraName().empty())
            return false;

        auto outputGo = world->FindGameObject(Get_m_outputCameraName());
        if (!outputGo.IsValid())
            return false;

        outId = outputGo.id();
        return true;
    }

    bool CameraBossIntroProduction::TryGetEntityPositionByName(const std::string& name, DirectX::XMFLOAT3& outPos) const
    {
        auto* world = GetWorld();
        if (!world || name.empty())
            return false;

        auto go = world->FindGameObject(name);
        if (!go.IsValid())
            return false;

        const auto* tr = world->GetComponent<TransformComponent>(go.id());
        if (!tr || !tr->enabled)
            return false;

        outPos = tr->position;
        return true;
    }

    bool CameraBossIntroProduction::ResolveActorPositions(DirectX::XMFLOAT3& outPlayerPos, DirectX::XMFLOAT3& outBossPos) const
    {
        if (!TryGetEntityPositionByName(Get_m_playerName(), outPlayerPos))
            return false;
        if (!TryGetEntityPositionByName(Get_m_bossName(), outBossPos))
            return false;
        return true;
    }

    void CameraBossIntroProduction::SetGameplayCameraControl(bool enable)
    {
        auto* world = GetWorld();
        if (!world)
            return;

        EntityId outputId = InvalidEntityId;
        if (!TryResolveOutputCamera(outputId))
            return;

        auto* tr = world->GetComponent<TransformComponent>(outputId);
        auto* follow = world->GetComponent<CameraFollowComponent>(outputId);
        auto* lookAt = world->GetComponent<CameraLookAtComponent>(outputId);
        auto* springArm = world->GetComponent<CameraSpringArmComponent>(outputId);
        auto* blend = world->GetComponent<CameraBlendComponent>(outputId);

        if (!enable)
        {
            if (m_controlsOverridden)
                return;

            m_hasFollow = (follow != nullptr);
            m_hasLookAt = (lookAt != nullptr);
            m_hasSpringArm = (springArm != nullptr);

            if (m_hasFollow)
                m_savedFollowEnabled = follow->enabled;
            if (m_hasLookAt)
                m_savedLookAtEnabled = lookAt->enabled;
            if (m_hasSpringArm)
                m_savedSpringArmEnabled = springArm->enabled;

            if (m_hasFollow && Get_m_disableFollowDuringProduction())
                follow->enabled = false;
            if (m_hasLookAt && Get_m_disableLookAtDuringProduction())
                lookAt->enabled = false;
            if (m_hasSpringArm && Get_m_disableSpringArmDuringProduction())
                springArm->enabled = false;

            if (blend)
            {
                blend->active = false;
                blend->needsSnapshot = false;
            }

            m_controlsOverridden = true;
            return;
        }

        if (!m_controlsOverridden)
            return;

        if (m_hasFollow && follow)
        {
            follow->enabled = m_savedFollowEnabled;
            if (tr)
            {
                follow->initialized = false;
                follow->smoothedPosition = tr->position;
                follow->smoothedRotation = tr->rotation;
                follow->yawDeg = DirectX::XMConvertToDegrees(tr->rotation.y);
                follow->pitchDeg = DirectX::XMConvertToDegrees(tr->rotation.x);
            }
        }

        if (m_hasLookAt && lookAt)
            lookAt->enabled = m_savedLookAtEnabled;

        if (m_hasSpringArm && springArm)
            springArm->enabled = m_savedSpringArmEnabled;

        m_controlsOverridden = false;
    }

    void CameraBossIntroProduction::RestoreIfNeeded()
    {
        m_sequenceRunning = false;
        m_phase = Phase::None;
        m_blend.active = false;

        if (Get_m_restoreGameplayCameraOnFinish())
            SetGameplayCameraControl(true);
    }

    DirectX::XMFLOAT3 CameraBossIntroProduction::AddVec(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b)
    {
        return { a.x + b.x, a.y + b.y, a.z + b.z };
    }

    DirectX::XMFLOAT3 CameraBossIntroProduction::SubVec(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b)
    {
        return { a.x - b.x, a.y - b.y, a.z - b.z };
    }

    DirectX::XMFLOAT3 CameraBossIntroProduction::MulVec(const DirectX::XMFLOAT3& v, float s)
    {
        return { v.x * s, v.y * s, v.z * s };
    }

    DirectX::XMFLOAT3 CameraBossIntroProduction::LerpVec(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, float t)
    {
        return {
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t
        };
    }

    DirectX::XMFLOAT3 CameraBossIntroProduction::NormalizeSafe(
        const DirectX::XMFLOAT3& v,
        const DirectX::XMFLOAT3& fallback)
    {
        const float lenSq = v.x * v.x + v.y * v.y + v.z * v.z;
        if (lenSq <= 1e-8f)
            return fallback;

        const float invLen = 1.0f / std::sqrt(lenSq);
        return { v.x * invLen, v.y * invLen, v.z * invLen };
    }

    DirectX::XMFLOAT3 CameraBossIntroProduction::CrossVec(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b)
    {
        return {
            a.y * b.z - a.z * b.y,
            a.z * b.x - a.x * b.z,
            a.x * b.y - a.y * b.x
        };
    }

    DirectX::XMFLOAT3 CameraBossIntroProduction::DirectionToEuler(const DirectX::XMFLOAT3& dir)
    {
        const DirectX::XMFLOAT3 n = NormalizeSafe(dir, DirectX::XMFLOAT3(0.0f, 0.0f, 1.0f));
        const float yaw = std::atan2(n.x, n.z);
        const float pitch = -std::atan2(n.y, std::sqrt(n.x * n.x + n.z * n.z));
        return { pitch, yaw, 0.0f };
    }

    DirectX::XMFLOAT4 CameraBossIntroProduction::EulerToQuaternion(const DirectX::XMFLOAT3& eulerRad)
    {
        const DirectX::XMVECTOR q = DirectX::XMQuaternionRotationRollPitchYawFromVector(DirectX::XMLoadFloat3(&eulerRad));
        DirectX::XMFLOAT4 out{};
        DirectX::XMStoreFloat4(&out, q);
        return out;
    }

    DirectX::XMFLOAT3 CameraBossIntroProduction::QuaternionToEuler(const DirectX::XMFLOAT4& quat)
    {
        const DirectX::XMVECTOR q = DirectX::XMLoadFloat4(&quat);

        const DirectX::XMVECTOR f = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 0, 1, 0), q);
        const DirectX::XMVECTOR u = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 1, 0, 0), q);

        DirectX::XMFLOAT3 f3{};
        DirectX::XMStoreFloat3(&f3, f);

        const float yaw = std::atan2(f3.x, f3.z);
        const float pitch = -std::atan2(f3.y, std::sqrt(f3.x * f3.x + f3.z * f3.z));

        DirectX::XMVECTOR worldUp = DirectX::XMVectorSet(0, 1, 0, 0);
        DirectX::XMVECTOR r0 = DirectX::XMVector3Cross(worldUp, f);
        if (DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(r0)) < 1e-6f)
            r0 = DirectX::XMVector3Cross(DirectX::XMVectorSet(1, 0, 0, 0), f);

        r0 = DirectX::XMVector3Normalize(r0);
        const DirectX::XMVECTOR u0 = DirectX::XMVector3Cross(f, r0);

        const float roll = std::atan2(
            DirectX::XMVectorGetX(DirectX::XMVector3Dot(r0, u)),
            DirectX::XMVectorGetX(DirectX::XMVector3Dot(u0, u))
        );

        return { pitch, yaw, roll };
    }

    float CameraBossIntroProduction::ApplySmoothStep(float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    float CameraBossIntroProduction::ResolveDuration(float baseDuration, float speedMultiplier)
    {
        const float safeDuration = std::max(0.0f, baseDuration);
        const float safeSpeed = std::max(0.001f, speedMultiplier);
        return safeDuration / safeSpeed;
    }
}

