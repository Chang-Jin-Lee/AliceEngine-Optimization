#include "SocketFollower.h"

#include <DirectXMath.h>

#include "Core/ScriptFactory.h"
#include "Core/Logger.h"
#include "Core/GameObject.h"

namespace Alice
{
    REGISTER_SCRIPT(SocketFollower);

    namespace
    {
        DirectX::XMFLOAT3 QuaternionToEulerXYZ(const DirectX::XMVECTOR& q)
        {
            DirectX::XMFLOAT4 qf;
            DirectX::XMStoreFloat4(&qf, q);

            const float sinp = 2.0f * (qf.w * qf.x + qf.y * qf.z);
            const float cosp = 1.0f - 2.0f * (qf.x * qf.x + qf.y * qf.y);
            const float pitch = std::atan2(sinp, cosp);

            float siny = 2.0f * (qf.w * qf.y - qf.z * qf.x);
            siny = std::clamp(siny, -1.0f, 1.0f);
            const float yaw = std::asin(siny);

            const float sinr = 2.0f * (qf.w * qf.z + qf.x * qf.y);
            const float cosr = 1.0f - 2.0f * (qf.y * qf.y + qf.z * qf.z);
            const float roll = std::atan2(sinr, cosr);

            return DirectX::XMFLOAT3(pitch, yaw, roll);
        }
    }

    void SocketFollower::Start()
    {
        // 대상 찾기
        auto* world = GetWorld();
        if (!world)
            return;

        GameObject target = world->FindGameObject(targetName);
        if (!target.IsValid())
        {
            ALICE_LOG_WARN("[SocketFollower] target not found: %s", targetName.c_str());
            return;
        }
        m_targetId = target.id();
    }

    void SocketFollower::Update(float /*deltaTime*/)
    {
        if (m_targetId == InvalidEntityId)
            return;

        auto* world = GetWorld();
        if (!world)
            return;

        GameObject target(world, m_targetId, nullptr);
        auto graph = target.GetAnimGraph();
        if (!graph.IsValid())
            return;

        DirectX::XMFLOAT4X4 socketWorld{};
        if (!graph.TryGetSocketWorld(socketName.c_str(), socketWorld))
            return;

        // 월드 행렬 -> SRT 분해
        DirectX::XMMATRIX m = DirectX::XMLoadFloat4x4(&socketWorld);
        DirectX::XMVECTOR s, r, t;
        if (!DirectX::XMMatrixDecompose(&s, &r, &t, m))
            return;

        auto* tr = GetTransform();
        if (!tr)
            return;

        DirectX::XMStoreFloat3(&tr->position, t);

        if (followScale)
            DirectX::XMStoreFloat3(&tr->scale, s);

        if (followRotation)
            tr->rotation = QuaternionToEulerXYZ(r);
    }
}

