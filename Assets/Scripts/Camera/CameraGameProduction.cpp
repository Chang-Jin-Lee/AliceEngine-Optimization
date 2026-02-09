#include "CameraGameProduction.h"

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
    REGISTER_SCRIPT(CameraGameProduction);

    void CameraGameProduction::Awake()
    {
        auto* world = GetWorld();
        if (!world)
            return;

        EntityId outputId = InvalidEntityId;
        if (!TryResolveOutputCamera(outputId))
            return;

        if (!world->GetComponent<CameraBlendComponent>(outputId))
            world->AddComponent<CameraBlendComponent>(outputId);
    }

    void CameraGameProduction::OnDisable()
    {
        RestoreIfNeeded();
    }

    void CameraGameProduction::OnDestroy()
    {
        RestoreIfNeeded();
    }

    void CameraGameProduction::Update(float deltaTime)
    {
        auto go = gameObject();
        auto* input = Input();
        if (!go.IsValid() || !input)
            return;

        if (Get_m_enableHotkey() && input->GetKeyDown(KeyCode::Alpha8))
        {
            if (!m_sequenceRunning || Get_m_restartOnKey())
                StartSequence();
        }

        if (!m_sequenceRunning)
            return;

        if (UpdateBlend(deltaTime))
            AdvanceAfterBlend();
    }

    void CameraGameProduction::StartSequence()
    {
        EntityId outputId = InvalidEntityId;
        if (!TryResolveOutputCamera(outputId))
            return;

        SetGameplayCameraControl(false);

        m_sequenceRunning = true;
        m_phase = Phase::Blend12;

        const CameraPose cam1 = GetCameraPose(1);
        const CameraPose cam2 = GetCameraPose(2);

        ApplyPose(cam1);
        BeginBlend(cam1, cam2, std::max(0.0f, Get_m_blendDuration12()), Get_m_useSmoothStep12());
    }

    void CameraGameProduction::AdvanceAfterBlend()
    {
        if (!m_sequenceRunning)
            return;

        switch (m_phase)
        {
        case Phase::Blend12:
            m_phase = Phase::Blend34;
            ApplyPose(GetCameraPose(3));
            BeginBlend(GetCameraPose(3), GetCameraPose(4), std::max(0.0f, Get_m_blendDuration34()), Get_m_useSmoothStep34());
            break;

        case Phase::Blend34:
            m_phase = Phase::Blend56;
            ApplyPose(GetCameraPose(5));
            BeginBlend(GetCameraPose(5), GetCameraPose(6), std::max(0.0f, Get_m_blendDuration56()), Get_m_useSmoothStep56());
            break;

        case Phase::Blend56:
            m_phase = Phase::Blend78;
            ApplyPose(GetCameraPose(7));
            BeginBlend(GetCameraPose(7), GetCameraPose(8), std::max(0.0f, Get_m_blendDuration78()), Get_m_useSmoothStep78());
            break;

        case Phase::Blend78:
            m_phase = Phase::Blend910;
            ApplyPose(GetCameraPose(9));
            BeginBlend(GetCameraPose(9), GetCameraPose(10), std::max(0.0f, Get_m_blendDuration910()), Get_m_useSmoothStep910());
            break;

        case Phase::Blend910:
            ApplyPose(GetCameraPose(10));
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

    void CameraGameProduction::BeginBlend(const CameraPose& from, const CameraPose& to, float duration, bool useSmoothStep)
    {
        m_blend.active = true;
        m_blend.from = from;
        m_blend.to = to;
        m_blend.duration = std::max(0.0f, duration);
        m_blend.elapsed = 0.0f;
        m_blend.useSmoothStep = useSmoothStep;
    }

    bool CameraGameProduction::UpdateBlend(float deltaTime)
    {
        if (!m_blend.active)
            return true;

        m_blend.elapsed += std::max(0.0f, deltaTime);

        const float duration = std::max(m_blend.duration, 0.0001f);
        float t = std::clamp(m_blend.elapsed / duration, 0.0f, 1.0f);
        if (m_blend.useSmoothStep)
            t = ApplySmoothStep(t);

        CameraPose blended{};
        blended.position = LerpVec(m_blend.from.position, m_blend.to.position, t);
        blended.scale = LerpVec(m_blend.from.scale, m_blend.to.scale, t);

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

    void CameraGameProduction::ApplyPose(const CameraPose& pose)
    {
        auto* world = GetWorld();
        if (!world)
            return;

        EntityId outputId = InvalidEntityId;
        if (!TryResolveOutputCamera(outputId))
            return;

        auto* tr = world->GetComponent<TransformComponent>(outputId);
        if (!tr || !tr->enabled)
            return;

        tr->position = pose.position;
        tr->rotation = pose.rotationRad;
        tr->scale = pose.scale;
    }

    bool CameraGameProduction::TryResolveOutputCamera(EntityId& outId) const
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

    bool CameraGameProduction::TryGetEntityPose(EntityId entityId, CameraPose& outPose) const
    {
        auto* world = GetWorld();
        if (!world || entityId == InvalidEntityId)
            return false;

        auto* tr = world->GetComponent<TransformComponent>(entityId);
        if (!tr || !tr->enabled)
            return false;

        outPose.position = tr->position;
        outPose.rotationRad = tr->rotation;
        outPose.scale = tr->scale;
        return true;
    }

    std::string CameraGameProduction::GetSceneCameraNameByIndex(int index) const
    {
        switch (index)
        {
        case 1: return Get_m_camera1Name();
        case 2: return Get_m_camera2Name();
        case 3: return Get_m_camera3Name();
        case 4: return Get_m_camera4Name();
        case 5: return Get_m_camera5Name();
        case 6: return Get_m_camera6Name();
        case 7: return Get_m_camera7Name();
        case 8: return Get_m_camera8Name();
        case 9: return Get_m_camera9Name();
        case 10: return Get_m_camera10Name();
        default: return {};
        }
    }

    bool CameraGameProduction::TryGetSceneCameraPose(int index, CameraPose& outPose) const
    {
        if (!Get_m_useSceneCameraEntities())
            return false;

        auto* world = GetWorld();
        if (!world)
            return false;

        const std::string cameraName = GetSceneCameraNameByIndex(index);
        if (cameraName.empty())
            return false;

        auto go = world->FindGameObject(cameraName);
        if (!go.IsValid())
            return false;
        if (!go.GetComponent<CameraComponent>())
            return false;

        return TryGetEntityPose(go.id(), outPose);
    }

    CameraGameProduction::CameraPose CameraGameProduction::GetPropertyPose(int index) const
    {
        CameraPose pose{};

        switch (index)
        {
        case 1:
            pose.position = Get_m_camera1Position();
            pose.rotationRad = DegreesToRadians(Get_m_camera1RotationDeg());
            pose.scale = Get_m_camera1Scale();
            break;
        case 2:
            pose.position = Get_m_camera2Position();
            pose.rotationRad = DegreesToRadians(Get_m_camera2RotationDeg());
            pose.scale = Get_m_camera2Scale();
            break;
        case 3:
            pose.position = Get_m_camera3Position();
            pose.rotationRad = DegreesToRadians(Get_m_camera3RotationDeg());
            pose.scale = Get_m_camera3Scale();
            break;
        case 4:
            pose.position = Get_m_camera4Position();
            pose.rotationRad = DegreesToRadians(Get_m_camera4RotationDeg());
            pose.scale = Get_m_camera4Scale();
            break;
        case 5:
            pose.position = Get_m_camera5Position();
            pose.rotationRad = DegreesToRadians(Get_m_camera5RotationDeg());
            pose.scale = Get_m_camera5Scale();
            break;
        case 6:
            pose.position = Get_m_camera6Position();
            pose.rotationRad = DegreesToRadians(Get_m_camera6RotationDeg());
            pose.scale = Get_m_camera6Scale();
            break;
        case 7:
            pose.position = Get_m_camera7Position();
            pose.rotationRad = DegreesToRadians(Get_m_camera7RotationDeg());
            pose.scale = Get_m_camera7Scale();
            break;
        case 8:
            pose.position = Get_m_camera8Position();
            pose.rotationRad = DegreesToRadians(Get_m_camera8RotationDeg());
            pose.scale = Get_m_camera8Scale();
            break;
        case 9:
            pose.position = Get_m_camera9Position();
            pose.rotationRad = DegreesToRadians(Get_m_camera9RotationDeg());
            pose.scale = Get_m_camera9Scale();
            break;
        case 10:
        default:
            pose.position = Get_m_camera10Position();
            pose.rotationRad = DegreesToRadians(Get_m_camera10RotationDeg());
            pose.scale = Get_m_camera10Scale();
            break;
        }

        return pose;
    }

    CameraGameProduction::CameraPose CameraGameProduction::GetOutputCameraCurrentPose() const
    {
        CameraPose pose{};

        EntityId outputId = InvalidEntityId;
        if (TryResolveOutputCamera(outputId) && TryGetEntityPose(outputId, pose))
            return pose;

        auto hostGo = gameObject();
        if (auto* tr = hostGo.GetComponent<TransformComponent>())
        {
            pose.position = tr->position;
            pose.rotationRad = tr->rotation;
            pose.scale = tr->scale;
        }
        return pose;
    }

    CameraGameProduction::CameraPose CameraGameProduction::GetCameraPose(int index)
    {
        CameraPose pose{};
        if (TryGetSceneCameraPose(index, pose))
            return pose;
        if (Get_m_fallbackToPropertyPose())
            return GetPropertyPose(index);
        return GetOutputCameraCurrentPose();
    }

    void CameraGameProduction::SetGameplayCameraControl(bool enable)
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

    void CameraGameProduction::RestoreIfNeeded()
    {
        m_sequenceRunning = false;
        m_phase = Phase::None;
        m_blend.active = false;

        if (Get_m_restoreGameplayCameraOnFinish())
            SetGameplayCameraControl(true);
    }

    DirectX::XMFLOAT3 CameraGameProduction::DegreesToRadians(const DirectX::XMFLOAT3& deg)
    {
        return {
            DirectX::XMConvertToRadians(deg.x),
            DirectX::XMConvertToRadians(deg.y),
            DirectX::XMConvertToRadians(deg.z)
        };
    }

    DirectX::XMFLOAT3 CameraGameProduction::LerpVec(const DirectX::XMFLOAT3& a, const DirectX::XMFLOAT3& b, float t)
    {
        return {
            a.x + (b.x - a.x) * t,
            a.y + (b.y - a.y) * t,
            a.z + (b.z - a.z) * t
        };
    }

    DirectX::XMFLOAT4 CameraGameProduction::EulerToQuaternion(const DirectX::XMFLOAT3& eulerRad)
    {
        const DirectX::XMVECTOR q = DirectX::XMQuaternionRotationRollPitchYawFromVector(DirectX::XMLoadFloat3(&eulerRad));
        DirectX::XMFLOAT4 out{};
        DirectX::XMStoreFloat4(&out, q);
        return out;
    }

    DirectX::XMFLOAT3 CameraGameProduction::QuaternionToEuler(const DirectX::XMFLOAT4& quat)
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

    float CameraGameProduction::ApplySmoothStep(float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }
}
