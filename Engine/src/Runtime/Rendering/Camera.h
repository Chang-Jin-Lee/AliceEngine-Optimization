#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdint>

#include <DirectXMath.h>
#include <DirectXCollision.h>

namespace Alice
{
    class Camera
    {
    public:
        Camera() = default;

        void SetLookAt(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& target, const DirectX::XMFLOAT3& up);
        void SetPerspective(float fovYRadians, float aspectRatio, float nearPlane, float farPlane);

        const DirectX::XMFLOAT3& GetPosition() const { return m_position; }
        const DirectX::XMFLOAT4& GetRotationQuat() const { return m_rotation; }
        DirectX::XMFLOAT3 GetRotation() const;
        const DirectX::XMFLOAT3& GetScale() const { return m_scale; }

        void SetPosition(const DirectX::XMFLOAT3& position);
        void SetRotation(const DirectX::XMFLOAT4& rotation);
        void SetScale(const DirectX::XMFLOAT3& scale);

        float GetFovYRadians() const { return m_fovYRadians; }
        float GetFovXRadians() const;
        float GetAspectRatio() const { return m_aspectRatio; }
        float GetNearPlane() const { return m_nearPlane; }
        float GetFarPlane()  const { return m_farPlane; }

        DirectX::XMMATRIX GetViewMatrix() const;
        DirectX::XMMATRIX GetProjectionMatrix() const;
        DirectX::XMMATRIX GetViewProjectionMatrix() const;

        void GetFrustumPlanes(DirectX::XMFLOAT4 outPlanes[6]) const;
        DirectX::BoundingFrustum GetWorldFrustum() const;

        DirectX::XMFLOAT2 WorldToScreen(const DirectX::XMFLOAT3& worldPos, float viewportWidth, float viewportHeight) const;
        bool ScreenToWorldRay(float screenX, float screenY, float viewportWidth, float viewportHeight,
                              DirectX::XMFLOAT3& outOrigin, DirectX::XMFLOAT3& outDir) const;

    private:
        void InvalidateView()  const { m_viewDirty = true; }
        void InvalidateProj()  const { m_projDirty = true; }
        void RebuildView()     const;
        void RebuildProj()     const;

        DirectX::XMFLOAT3 m_position { 0.0f, 0.0f, -5.0f };
        DirectX::XMFLOAT4 m_rotation { 0.0f, 0.0f, 0.0f, 1.0f };
        DirectX::XMFLOAT3 m_scale    { 1.0f, 1.0f, 1.0f };
        DirectX::XMFLOAT3 m_target   { 0.0f, 0.0f,  0.0f };
        DirectX::XMFLOAT3 m_up       { 0.0f, 1.0f,  0.0f };

        float m_fovYRadians { DirectX::XM_PIDIV4 };
        float m_aspectRatio { 16.0f / 9.0f };
        float m_nearPlane   { 0.1f };
        float m_farPlane    { 1000.0f };

        mutable DirectX::XMFLOAT4X4 m_cachedView {};
        mutable DirectX::XMFLOAT4X4 m_cachedProj {};
        mutable bool m_viewDirty { true };
        mutable bool m_projDirty { true };
    };
}
