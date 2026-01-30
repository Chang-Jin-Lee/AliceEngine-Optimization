#include "CameraDirector.h"
#include "Core/ScriptFactory.h"
#include "Core/GameObject.h"
#include "Core/World.h"
#include "Components/TransformComponent.h"
#include "Components/CameraComponent.h"
#include "Rendering/Camera.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <sstream>

namespace Alice
{
    REGISTER_SCRIPT(CameraDirector);

    namespace
    {
        static std::string Trim(const std::string& s)
        {
            std::size_t start = 0;
            while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
            std::size_t end = s.size();
            while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
            return s.substr(start, end - start);
        }
    }

    void CameraDirector::Awake()
    {
        m_cameraIds = ResolveCameraList();

        auto go = gameObject();
        if (!go.IsValid())
            return;

        if (m_autoSetPrimary)
        {
            auto* cam = go.GetComponent<CameraComponent>();
            if (!cam)
                cam = &go.AddComponent<CameraComponent>();
            cam->primary = true;
        }
    }

    void CameraDirector::LateUpdate(float deltaTime)
    {
        if (!m_isBlending || m_targetCameraId == InvalidEntityId)
            return;

        BlendSnapshot target{};
        if (!GetCameraSnapshot(m_targetCameraId, target))
        {
            m_isBlending = false;
            return;
        }

        m_blendElapsed += deltaTime;
        const float duration = (m_blendDuration > 0.0f) ? m_blendDuration : m_defaultBlendTime;
        const float t01 = (duration <= 0.0f) ? 1.0f : std::clamp(m_blendElapsed / duration, 0.0f, 1.0f);
        ApplyBlend(m_from, target, t01);

        if (t01 >= 1.0f)
        {
            m_isBlending = false;
        }
    }

    void CameraDirector::BlendTo(const std::string& cameraName, float duration)
    {
        auto world = GetWorld();
        if (!world)
            return;

        const std::string trimmed = Trim(cameraName);
        if (trimmed.empty())
            return;

        auto targetGo = world->FindGameObject(trimmed);
        if (!targetGo.IsValid())
            return;

        m_targetCameraId = targetGo.id();
        if (!GetCameraSnapshot(gameObject().id(), m_from))
            return;

        m_blendElapsed = 0.0f;
        m_blendDuration = (duration >= 0.0f) ? duration : m_defaultBlendTime;
        m_isBlending = true;

        if (m_blendDuration <= 0.0f)
        {
            BlendSnapshot target{};
            if (GetCameraSnapshot(m_targetCameraId, target))
                ApplyBlend(m_from, target, 1.0f);
            m_isBlending = false;
        }
    }

    void CameraDirector::SetCameraWithBlend(const std::string& cameraName, float duration)
    {
        BlendTo(cameraName, duration);
    }

    void CameraDirector::BlendToIndex(int index, float duration)
    {
        if (m_cameraIds.empty() || index < 0 || index >= static_cast<int>(m_cameraIds.size()))
            return;

        auto world = GetWorld();
        if (!world)
            return;

        m_targetCameraId = m_cameraIds[static_cast<std::size_t>(index)];
        if (!GetCameraSnapshot(gameObject().id(), m_from))
            return;

        m_blendElapsed = 0.0f;
        m_blendDuration = (duration >= 0.0f) ? duration : m_defaultBlendTime;
        m_isBlending = true;
    }

    void CameraDirector::RebuildCameraList()
    {
        m_cameraIds = ResolveCameraList();
    }

    std::vector<EntityId> CameraDirector::ResolveCameraList() const
    {
        std::vector<EntityId> result;
        auto world = GetWorld();
        if (!world)
            return result;

        std::stringstream ss(m_cameraListCsv);
        std::string item;
        while (std::getline(ss, item, ','))
        {
            const std::string name = Trim(item);
            if (name.empty())
                continue;
            auto go = world->FindGameObject(name);
            if (go.IsValid())
                result.push_back(go.id());
        }
        return result;
    }

    bool CameraDirector::GetCameraSnapshot(EntityId id, BlendSnapshot& out) const
    {
        auto world = GetWorld();
        if (!world || id == InvalidEntityId)
            return false;

        const auto* tr = world->GetComponent<TransformComponent>(id);
        const auto* cam = world->GetComponent<CameraComponent>(id);
        if (!tr || !cam)
            return false;

        out.position = tr->position;
        out.rotation = tr->rotation;
        // Camera 객체에서 FOV, Near, Far 가져오기
        const Camera& camera = cam->GetCamera();
        out.fovY = camera.GetFovYRadians();
        out.nearPlane = camera.GetNearPlane();
        out.farPlane = camera.GetFarPlane();
        return true;
    }

    DirectX::XMFLOAT4 CameraDirector::EulerToQuaternion(const DirectX::XMFLOAT3& eulerRad)
    {
        const DirectX::XMVECTOR q = DirectX::XMQuaternionRotationRollPitchYawFromVector(DirectX::XMLoadFloat3(&eulerRad));
        DirectX::XMFLOAT4 out{};
        DirectX::XMStoreFloat4(&out, q);
        return out;
    }

    DirectX::XMFLOAT3 CameraDirector::QuaternionToEuler(const DirectX::XMFLOAT4& quat)
    {
        // yaw/pitch/roll 변환 (Left-handed, Yaw-Pitch-Roll)
        const float x = quat.x;
        const float y = quat.y;
        const float z = quat.z;
        const float w = quat.w;

        const float sinr_cosp = 2.0f * (w * x + y * z);
        const float cosr_cosp = 1.0f - 2.0f * (x * x + y * y);
        const float roll = std::atan2(sinr_cosp, cosr_cosp);

        const float sinp = 2.0f * (w * y - z * x);
        float pitch;
        if (std::abs(sinp) >= 1.0f)
            pitch = std::copysign(DirectX::XM_PIDIV2, sinp);
        else
            pitch = std::asin(sinp);

        const float siny_cosp = 2.0f * (w * z + x * y);
        const float cosy_cosp = 1.0f - 2.0f * (y * y + z * z);
        const float yaw = std::atan2(siny_cosp, cosy_cosp);

        return DirectX::XMFLOAT3(pitch, yaw, roll);
    }

    float CameraDirector::SmoothStep(float t)
    {
        t = std::clamp(t, 0.0f, 1.0f);
        return t * t * (3.0f - 2.0f * t);
    }

    void CameraDirector::ApplyBlend(const BlendSnapshot& from,
                                    const BlendSnapshot& to,
                                    float t01)
    {
        auto go = gameObject();
        if (!go.IsValid())
            return;

        auto* tr = go.GetComponent<TransformComponent>();
        auto* cam = go.GetComponent<CameraComponent>();
        if (!tr || !cam)
            return;

        const float t = SmoothStep(t01);

        tr->position.x = from.position.x + (to.position.x - from.position.x) * t;
        tr->position.y = from.position.y + (to.position.y - from.position.y) * t;
        tr->position.z = from.position.z + (to.position.z - from.position.z) * t;

        const DirectX::XMFLOAT4 qFrom = EulerToQuaternion(from.rotation);
        const DirectX::XMFLOAT4 qTo = EulerToQuaternion(to.rotation);
        const DirectX::XMVECTOR qA = DirectX::XMLoadFloat4(&qFrom);
        const DirectX::XMVECTOR qB = DirectX::XMLoadFloat4(&qTo);
        DirectX::XMFLOAT4 qOut{};
        DirectX::XMStoreFloat4(&qOut, DirectX::XMQuaternionSlerp(qA, qB, t));
        tr->rotation = QuaternionToEuler(qOut);

        // Camera 객체에 FOV, Near, Far 설정
        Camera& camera = cam->GetCamera();
        float fov = from.fovY + (to.fovY - from.fovY) * t;
        float nearP = from.nearPlane + (to.nearPlane - from.nearPlane) * t;
        float farP = from.farPlane + (to.farPlane - from.farPlane) * t;
        camera.SetPerspective(fov, camera.GetAspectRatio(), nearP, farP);
    }
}
