#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdint>

#include <DirectXMath.h>
#include <DirectXCollision.h>

namespace Alice
{
    /// 간단한 3D 카메라 클래스입니다.
    /// - 위치, 타깃, 업 벡터를 기반으로 뷰 행렬을 계산합니다.
    /// - FOV, 종횡비, near/far 를 기반으로 투영 행렬을 계산합니다.
    class Camera
    {
    public:
        Camera() = default;

        // 카메라의 위치와 바라보는 타깃, 업 벡터를 설정합니다.
        void SetLookAt(const DirectX::XMFLOAT3& position, const DirectX::XMFLOAT3& target, const DirectX::XMFLOAT3& up);

        // 원근 투영 설정을 합니다.
        void SetPerspective(float fovYRadians, float aspectRatio, float nearPlane, float farPlane);

        // 현재 카메라 위치를 반환합니다.
        const DirectX::XMFLOAT3& GetPosition() const { return m_position; }
        const DirectX::XMFLOAT4& GetRotationQuat() const { return m_rotation; }
        DirectX::XMFLOAT3 GetRotation() const;
        const DirectX::XMFLOAT3& GetScale() const { return m_scale; }

        void SetPosition(const DirectX::XMFLOAT3& position);
        void SetRotation(const DirectX::XMFLOAT4& rotation);
        void SetScale(const DirectX::XMFLOAT3& scale);

        float GetFovYRadians() const { return m_fovYRadians; }              // FOV (라디안)를 반환합니다.
        float GetFovXRadians() const;                                       // 수평 FOV (라디안)를 반환합니다.
        float GetAspectRatio() const { return m_aspectRatio; }              // 종횡비를 반환합니다.
        float GetNearPlane() const { return m_nearPlane; }                  // near/far 평면을 반환합니다.
        float GetFarPlane()  const { return m_farPlane; }

        DirectX::XMMATRIX GetViewMatrix() const;                            // 뷰 행렬을 반환합니다.
        DirectX::XMMATRIX GetProjectionMatrix() const;                      // 투영 행렬을 반환합니다.
        DirectX::XMMATRIX GetViewProjectionMatrix() const;                  // 뷰-프로젝션 행렬을 반환합니다.

        /// 프러스텀 평면을 추출합니다. (좌,우,하,상,근,원)
        void GetFrustumPlanes(DirectX::XMFLOAT4 outPlanes[6]) const;

        /// 렌더링 최적화를 위해 월드 공간의 절두체(Frustum)를 반환합니다.
        /// 프러스텀 컬링에 사용됩니다.
        DirectX::BoundingFrustum GetWorldFrustum() const;

        /// 월드 좌표를 스크린 좌표로 변환합니다. (픽셀 기준)
        DirectX::XMFLOAT2 WorldToScreen(const DirectX::XMFLOAT3& worldPos,float viewportWidth, float viewportHeight) const;

        /// 스크린 좌표를 월드 레이로 변환합니다.
        /// 성공 시 true 반환, outOrigin/outDir에 결과가 저장됩니다.
        bool ScreenToWorldRay(float screenX, float screenY, float viewportWidth, float viewportHeight,
                              DirectX::XMFLOAT3& outOrigin,
                              DirectX::XMFLOAT3& outDir) const;

    private:
        DirectX::XMFLOAT3 m_position { 0.0f, 0.0f, -5.0f };
        DirectX::XMFLOAT4 m_rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
        DirectX::XMFLOAT3 m_scale{ 1.0f, 1.0f, 1.0f };

        DirectX::XMFLOAT3 m_target   { 0.0f, 0.0f,  0.0f };
        DirectX::XMFLOAT3 m_up       { 0.0f, 1.0f,  0.0f };

        float m_fovYRadians { DirectX::XM_PIDIV4 }; // 45도
        float m_aspectRatio { 16.0f / 9.0f };
        float m_nearPlane   { 0.1f };
        float m_farPlane    { 1000.0f };
    };
}


