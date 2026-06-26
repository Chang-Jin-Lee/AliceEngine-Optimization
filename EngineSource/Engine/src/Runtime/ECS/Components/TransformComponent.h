#pragma once

#include <DirectXMath.h>
#include "Runtime/ECS/Entity.h"

// 트랜스폼
namespace Alice {
    struct TransformComponent 
    {
        // 위치, 회전(라디안), 스케일
        DirectX::XMFLOAT3 position{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 rotation{ 0.0f, 0.0f, 0.0f };
        DirectX::XMFLOAT3 scale{ 1.0f, 1.0f, 1.0f };
        bool enabled = true;
        bool visible = true; // 렌더링 on/off 전용
        
        // 부모 엔티티 ID (InvalidEntityId면 부모 없음)
        EntityId parent = InvalidEntityId;

        TransformComponent& SetPosition(float x, float y, float z) 
        {
            position = DirectX::XMFLOAT3(x, y, z);
            return *this;
        }

        TransformComponent& SetScale(float x, float y, float z) 
        {
            scale = DirectX::XMFLOAT3(x, y, z);
            return *this;
        }

        TransformComponent& SetRotation(float x, float y, float z) 
        {
            rotation = DirectX::XMFLOAT3(DirectX::XMConvertToRadians(x),
                DirectX::XMConvertToRadians(y),
                DirectX::XMConvertToRadians(z));
            return *this;
        }

        // XMFLOAT3 위치 설정
        TransformComponent& SetPosition(const DirectX::XMFLOAT3& pos)
        {
            position = pos;
            return *this;
        }

        // XMFLOAT3 스케일 설정
        TransformComponent& SetScale(const DirectX::XMFLOAT3& s)
        {
            scale = s;
            return *this;
        }

        TransformComponent& SetRotation(const DirectX::XMFLOAT3& rot)
        {
            rotation = DirectX::XMFLOAT3(
                DirectX::XMConvertToRadians(rot.x),
                DirectX::XMConvertToRadians(rot.y),
                DirectX::XMConvertToRadians(rot.z)
            );
            return *this;
        }

        // 쿼터니언(XMFLOAT4)은 오일러에서 라디안(XMFLOAT3) 변환 후 설정
        TransformComponent& SetRotation(const DirectX::XMFLOAT4& quat)
        {
            using namespace DirectX;

            XMVECTOR q = XMLoadFloat4(&quat);
            XMMATRIX R = XMMatrixRotationQuaternion(q);
            XMFLOAT4X4 m;
            XMStoreFloat4x4(&m, R);

            // Rotation Matrix -> Euler Angles (Pitch, Yaw, Roll) 추출
            float pitch = asinf(-m._32);
            float yaw = 0.0f;
            float roll = 0.0f;

            // 짐벌락 체크
            if (std::abs(m._32) > 0.9999f)
            {
                yaw = atan2f(-m._13, m._11);
                roll = 0.0f;
            }
            else
            {
                yaw = atan2f(m._31, m._33);
                roll = atan2f(m._12, m._22);
            }

            rotation = XMFLOAT3(pitch, yaw, roll);
            return *this;
        }
    };
}
