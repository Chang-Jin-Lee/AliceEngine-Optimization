#include "Runtime/Rendering/Camera.h"
#include "Runtime/Rendering/CullingTuning.h"
#include "Runtime/Rendering/Metrics/LegacyPathFlags.h"

#include <cmath>
#include <algorithm>

using namespace DirectX;

namespace Alice
{
    void Camera::RebuildView() const
    {
        XMMATRIX v = XMMatrixLookAtLH(XMLoadFloat3(&m_position), XMLoadFloat3(&m_target), XMLoadFloat3(&m_up));
        XMStoreFloat4x4(&m_cachedView, v);
        m_viewDirty = false;
    }

    void Camera::RebuildProj() const
    {
        XMMATRIX p = XMMatrixPerspectiveFovLH(m_fovYRadians, std::max(0.1f, m_aspectRatio), m_nearPlane, m_farPlane);
        XMStoreFloat4x4(&m_cachedProj, p);
        m_projDirty = false;
    }

    void Camera::SetLookAt(const XMFLOAT3& position, const XMFLOAT3& target, const XMFLOAT3& up)
    {
        m_position = position;
        m_target   = target;
        m_up       = up;
        XMMATRIX view = XMMatrixLookAtLH(XMLoadFloat3(&m_position), XMLoadFloat3(&m_target), XMLoadFloat3(&m_up));
        XMMATRIX world = XMMatrixInverse(nullptr, view);
        XMStoreFloat4(&m_rotation, XMQuaternionRotationMatrix(world));
        InvalidateView();
    }

    void Camera::SetPerspective(float fovYRadians, float aspectRatio, float nearPlane, float farPlane)
    {
        m_fovYRadians = fovYRadians;
        m_aspectRatio = aspectRatio;
        m_nearPlane   = nearPlane;
        m_farPlane    = farPlane;
        InvalidateProj();
    }

    void Camera::SetPosition(const DirectX::XMFLOAT3& position)
    {
        m_position = position;
        InvalidateView();
    }

    void Camera::SetRotation(const DirectX::XMFLOAT4& rotation)
    {
        m_rotation = rotation;

        XMVECTOR q = XMLoadFloat4(&m_rotation);
        XMVECTOR forward = XMVector3Rotate(XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f), q);
        XMVECTOR up      = XMVector3Rotate(XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f), q);
        XMVECTOR pos     = XMLoadFloat3(&m_position);
        XMStoreFloat3(&m_target, pos + forward);
        XMStoreFloat3(&m_up, up);
        InvalidateView();
    }

    void Camera::SetScale(const DirectX::XMFLOAT3& scale)
    {
        m_scale = scale;
    }

    XMMATRIX Camera::GetViewMatrix() const
    {
        // OPTIMIZATION_REPORT P07: the legacy path rebuilt this matrix at every call.
        if (LegacyPathFlags::Get().noCameraMatrixCache)
            return XMMatrixLookAtLH(XMLoadFloat3(&m_position), XMLoadFloat3(&m_target), XMLoadFloat3(&m_up));
        if (m_viewDirty) RebuildView();
        return XMLoadFloat4x4(&m_cachedView);
    }

    XMMATRIX Camera::GetProjectionMatrix() const
    {
        // OPTIMIZATION_REPORT P07: the legacy path rebuilt this matrix at every call.
        if (LegacyPathFlags::Get().noCameraMatrixCache)
            return XMMatrixPerspectiveFovLH(
                m_fovYRadians, std::max(0.1f, m_aspectRatio), m_nearPlane, m_farPlane);
        if (m_projDirty) RebuildProj();
        return XMLoadFloat4x4(&m_cachedProj);
    }

    XMMATRIX Camera::GetViewProjectionMatrix() const
    {
        return GetViewMatrix() * GetProjectionMatrix();
    }

    DirectX::XMFLOAT3 Camera::GetRotation() const
    {
        XMMATRIX R = XMMatrixRotationQuaternion(XMLoadFloat4(&m_rotation));
        XMFLOAT4X4 m;
        XMStoreFloat4x4(&m, R);

        float pitch = asinf(-m._32);
        float yaw = 0.0f, roll = 0.0f;

        if (std::abs(m._32) > 0.9999f)
        {
            yaw  = atan2f(-m._13, m._11);
            roll = 0.0f;
        }
        else
        {
            yaw  = atan2f(m._31, m._33);
            roll = atan2f(m._12, m._22);
        }
        return DirectX::XMFLOAT3(pitch, yaw, roll);
    }

    float Camera::GetFovXRadians() const
    {
        return 2.0f * std::atan(std::tan(m_fovYRadians * 0.5f) * m_aspectRatio);
    }

    void Camera::GetFrustumPlanes(DirectX::XMFLOAT4 outPlanes[6]) const
    {
        if (!outPlanes) return;

        const XMMATRIX vp = GetViewProjectionMatrix();
        XMFLOAT4X4 m{};
        XMStoreFloat4x4(&m, vp);

        XMVECTOR planes[6];
        planes[0] = XMPlaneNormalize(XMVectorSet(m._14 + m._11, m._24 + m._21, m._34 + m._31, m._44 + m._41));
        planes[1] = XMPlaneNormalize(XMVectorSet(m._14 - m._11, m._24 - m._21, m._34 - m._31, m._44 - m._41));
        planes[2] = XMPlaneNormalize(XMVectorSet(m._14 + m._12, m._24 + m._22, m._34 + m._32, m._44 + m._42));
        planes[3] = XMPlaneNormalize(XMVectorSet(m._14 - m._12, m._24 - m._22, m._34 - m._32, m._44 - m._42));
        planes[4] = XMPlaneNormalize(XMVectorSet(m._13,         m._23,         m._33,         m._43));
        planes[5] = XMPlaneNormalize(XMVectorSet(m._14 - m._13, m._24 - m._23, m._34 - m._33, m._44 - m._43));

        for (int i = 0; i < 6; ++i)
            XMStoreFloat4(&outPlanes[i], planes[i]);
    }

    DirectX::XMFLOAT2 Camera::WorldToScreen(const DirectX::XMFLOAT3& worldPos,
                                            float viewportWidth, float viewportHeight) const
    {
        const XMMATRIX vp  = GetViewProjectionMatrix();
        const XMVECTOR p   = XMLoadFloat3(&worldPos);
        XMVECTOR clip      = XMVector3TransformCoord(p, vp);

        const float ndcX   = XMVectorGetX(clip);
        const float ndcY   = XMVectorGetY(clip);
        return DirectX::XMFLOAT2((ndcX * 0.5f + 0.5f) * viewportWidth,
                                 (1.0f - (ndcY * 0.5f + 0.5f)) * viewportHeight);
    }

    bool Camera::ScreenToWorldRay(float screenX, float screenY,
                                  float viewportWidth, float viewportHeight,
                                  DirectX::XMFLOAT3& outOrigin, DirectX::XMFLOAT3& outDir) const
    {
        if (viewportWidth <= 0.0f || viewportHeight <= 0.0f) return false;

        const float ndcX = (screenX / viewportWidth)  * 2.0f - 1.0f;
        const float ndcY = 1.0f - (screenY / viewportHeight) * 2.0f;

        const XMMATRIX invView = XMMatrixInverse(nullptr, GetViewMatrix());
        const XMMATRIX invProj = XMMatrixInverse(nullptr, GetProjectionMatrix());

        XMVECTOR nearPoint = XMVectorSet(ndcX, ndcY, 0.0f, 1.0f);
        XMVECTOR farPoint  = XMVectorSet(ndcX, ndcY, 1.0f, 1.0f);

        nearPoint = XMVector3TransformCoord(nearPoint, invProj);
        farPoint  = XMVector3TransformCoord(farPoint,  invProj);
        nearPoint = XMVector3TransformCoord(nearPoint, invView);
        farPoint  = XMVector3TransformCoord(farPoint,  invView);

        XMVECTOR dir = XMVector3Normalize(farPoint - nearPoint);
        XMStoreFloat3(&outOrigin, nearPoint);
        XMStoreFloat3(&outDir, dir);
        return true;
    }

    BoundingFrustum Camera::GetWorldFrustum() const
    {
        float cullingFov = m_fovYRadians * CullingTuning::FrustumFovScale;
        if (cullingFov > XM_PI - CullingTuning::FrustumFovClampEpsilon)
            cullingFov = XM_PI - CullingTuning::FrustumFovClampEpsilon;

        float safeAspect = m_aspectRatio < 0.001f ? 1.0f : m_aspectRatio;

        XMMATRIX cullProj = XMMatrixPerspectiveFovLH(cullingFov, safeAspect, m_nearPlane, m_farPlane);

        BoundingFrustum frustum;
        BoundingFrustum::CreateFromMatrix(frustum, cullProj);
        frustum.Transform(frustum, XMMatrixInverse(nullptr, GetViewMatrix()));
        return frustum;
    }
}
