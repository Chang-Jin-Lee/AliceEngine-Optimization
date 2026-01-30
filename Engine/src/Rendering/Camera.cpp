#include "Rendering/Camera.h"

#include <cmath>
#include <algorithm>

using namespace DirectX;

namespace Alice
{
    void Camera::SetLookAt(const XMFLOAT3& position,
                           const XMFLOAT3& target,
                           const XMFLOAT3& up)
    {
        m_position = position;
        m_target   = target;
        m_up       = up;
        // m_rotation(쿼터니언)을 업데이트
        XMMATRIX view = XMMatrixLookAtLH(XMLoadFloat3(&m_position), XMLoadFloat3(&m_target), XMLoadFloat3(&m_up));
        XMMATRIX world = XMMatrixInverse(nullptr, view);
        XMStoreFloat4(&m_rotation, XMQuaternionRotationMatrix(world));
    }

    void Camera::SetPerspective(float fovYRadians,
                                float aspectRatio,
                                float nearPlane,
                                float farPlane)
    {
        m_fovYRadians = fovYRadians;
        m_aspectRatio = aspectRatio;
        m_nearPlane   = nearPlane;
        m_farPlane    = farPlane;
    }

    XMMATRIX Camera::GetViewMatrix() const
    {
        XMVECTOR eye    = XMLoadFloat3(&m_position);
        XMVECTOR target = XMLoadFloat3(&m_target);
        XMVECTOR up     = XMLoadFloat3(&m_up);

        return XMMatrixLookAtLH(eye, target, up);
    }

    XMMATRIX Camera::GetProjectionMatrix() const
    {
        return XMMatrixPerspectiveFovLH(m_fovYRadians, std::max(0.1f, m_aspectRatio), m_nearPlane, m_farPlane);
    }
    DirectX::XMFLOAT3 Camera::GetRotation() const
    {
        // 쿼터니언에서 회전 행렬로 변환
        XMMATRIX R = XMMatrixRotationQuaternion(XMLoadFloat4(&m_rotation));
        XMFLOAT4X4 m;
        XMStoreFloat4x4(&m, R);

        // 회전 행렬에서 오일러 각(Pitch, Yaw, Roll) 추출 (라디안)
        float pitch = asinf(-m._32);
        float yaw = 0.0f;
        float roll = 0.0f;

        // 짐벌락(Gimbal Lock) 체크 (Pitch가 수직에 가까울 때)
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

        return DirectX::XMFLOAT3(pitch, yaw, roll);
    }

    void Camera::SetPosition(const DirectX::XMFLOAT3& position)
    {
        m_position = position;
    }

    void Camera::SetRotation(const DirectX::XMFLOAT4& rotation)
    {
        m_rotation = rotation;

        // Rotation이 바뀌었으므로 Target, Up 벡터 동기화
        XMVECTOR q = XMLoadFloat4(&m_rotation);

        // 기본 전방(Z+), 상방(Y+) 벡터를 회전
        XMVECTOR forward = XMVector3Rotate(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), q);
        XMVECTOR up = XMVector3Rotate(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), q);
        XMVECTOR pos = XMLoadFloat3(&m_position);

        XMStoreFloat3(&m_target, pos + forward); // Target = Pos + Forward
        XMStoreFloat3(&m_up, up);
    }

    void Camera::SetScale(const DirectX::XMFLOAT3& scale)
    {
        m_scale = scale;
    }

    float Camera::GetFovXRadians() const
    {
        return 2.0f * std::atan(std::tan(m_fovYRadians * 0.5f) * m_aspectRatio);
    }

    XMMATRIX Camera::GetViewProjectionMatrix() const
    {
        return GetViewMatrix() * GetProjectionMatrix();
    }

    void Camera::GetFrustumPlanes(DirectX::XMFLOAT4 outPlanes[6]) const
    {
        if (!outPlanes) return;

        const XMMATRIX vp = GetViewProjectionMatrix();
        XMFLOAT4X4 m{};
        XMStoreFloat4x4(&m, vp);

        // 좌, 우, 하, 상, 근, 원 (LH 기준)
        XMVECTOR planes[6];
        planes[0] = XMPlaneNormalize(XMVectorSet(m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41)); // Left
        planes[1] = XMPlaneNormalize(XMVectorSet(m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41)); // Right
        planes[2] = XMPlaneNormalize(XMVectorSet(m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42)); // Bottom
        planes[3] = XMPlaneNormalize(XMVectorSet(m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42)); // Top
        planes[4] = XMPlaneNormalize(XMVectorSet(m._13,         m._23,         m._33,         m._43));          // Near
        planes[5] = XMPlaneNormalize(XMVectorSet(m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43)); // Far

        for (int i = 0; i < 6; ++i)
        {
            XMStoreFloat4(&outPlanes[i], planes[i]);
        }
    }

    DirectX::XMFLOAT2 Camera::WorldToScreen(const DirectX::XMFLOAT3& worldPos,
                                            float viewportWidth,
                                            float viewportHeight) const
    {
        const XMMATRIX vp = GetViewProjectionMatrix();
        const XMVECTOR p = XMLoadFloat3(&worldPos);
        XMVECTOR clip = XMVector3TransformCoord(p, vp);

        const float ndcX = XMVectorGetX(clip);
        const float ndcY = XMVectorGetY(clip);

        const float screenX = (ndcX * 0.5f + 0.5f) * viewportWidth;
        const float screenY = (1.0f - (ndcY * 0.5f + 0.5f)) * viewportHeight;

        return DirectX::XMFLOAT2(screenX, screenY);
    }

    bool Camera::ScreenToWorldRay(float screenX,
                                  float screenY,
                                  float viewportWidth,
                                  float viewportHeight,
                                  DirectX::XMFLOAT3& outOrigin,
                                  DirectX::XMFLOAT3& outDir) const
    {
        if (viewportWidth <= 0.0f || viewportHeight <= 0.0f)
            return false;

        const float ndcX = (screenX / viewportWidth) * 2.0f - 1.0f;
        const float ndcY = 1.0f - (screenY / viewportHeight) * 2.0f;

        const XMMATRIX invView = XMMatrixInverse(nullptr, GetViewMatrix());
        const XMMATRIX invProj = XMMatrixInverse(nullptr, GetProjectionMatrix());

        XMVECTOR nearPoint = XMVectorSet(ndcX, ndcY, 0.0f, 1.0f);
        XMVECTOR farPoint  = XMVectorSet(ndcX, ndcY, 1.0f, 1.0f);

        nearPoint = XMVector3TransformCoord(nearPoint, invProj);
        farPoint  = XMVector3TransformCoord(farPoint, invProj);

        nearPoint = XMVector3TransformCoord(nearPoint, invView);
        farPoint  = XMVector3TransformCoord(farPoint, invView);

        XMVECTOR dir = XMVector3Normalize(farPoint - nearPoint);
        XMStoreFloat3(&outOrigin, nearPoint);
        XMStoreFloat3(&outDir, dir);
        return true;
    }

    BoundingFrustum Camera::GetWorldFrustum() const
    {
        // 투영 행렬로 기본 프러스텀 생성 (뷰 공간 기준)
        BoundingFrustum frustum;

        // 각도 조절. 실제 눈에 보이는 FOV(m_fovYRadians)보다 1.3배(30%) 더 넓게 잡습니다.
        float cullingFov = m_fovYRadians * 1.3f;

        // 179도(약 3.124 라디안)를 넘지 않도록 제한 (180도 이상은 투영 행렬 생성 불가)
        if (cullingFov > XM_PI - 0.02f) cullingFov = XM_PI - 0.02f;

        float safeAspectRatio = m_aspectRatio;
        if (safeAspectRatio < 0.001f)
            safeAspectRatio = 1.0f;

        // 넓어진 각도로 임시 투영 행렬 생성
        XMMATRIX cullProj = XMMatrixPerspectiveFovLH(cullingFov, safeAspectRatio, m_nearPlane, m_farPlane);

        // 프러스텀 생성
        BoundingFrustum::CreateFromMatrix(frustum, cullProj);

        // 월드 공간으로 변환
        XMMATRIX invView = XMMatrixInverse(nullptr, GetViewMatrix());
        frustum.Transform(frustum, invView);

        return frustum;
    }
}


