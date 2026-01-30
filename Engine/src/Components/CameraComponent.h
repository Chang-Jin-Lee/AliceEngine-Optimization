#pragma once
#include <DirectXMath.h>
#include "Rendering/Camera.h"

namespace Alice {
    /// 씬 내 카메라(유니티의 Main Camera 느낌)
    /// - 게임 모드에서는 "첫번째(primary 우선)" 카메라 엔티티를 따라
    /// Camera(view/proj)를 갱신합니다.
    /// - 위치/회전/스케일은 TransformComponent에서 관리하며,
    ///   CameraSystem에서 매 프레임 동기화됩니다.
    class CameraComponent 
    {
    public:
        CameraComponent() = default;
        virtual ~CameraComponent() = default;

        // 렌더링용 저수준 카메라 객체
        // (위치/회전은 System에서 TransformComponent 값으로 매 프레임 덮어씌움)
        Camera& GetCamera() { return m_camera; }
        const Camera& GetCamera() const { return m_camera; }

        // RTTR 연동을 위한 Getter/Setter
        bool GetPrimary() const { return primary; }
        void SetPrimary(bool val) { primary = val; }

        float GetFov() const { return DirectX::XMConvertToDegrees(m_camera.GetFovYRadians()); }
        void SetFov(float deg) 
        { 
            m_camera.SetPerspective(DirectX::XMConvertToRadians(deg), m_camera.GetAspectRatio(), m_camera.GetNearPlane(), m_camera.GetFarPlane()); 
        }

        float GetNear() const { return m_camera.GetNearPlane(); }
        void SetNear(float val) 
        { 
            m_camera.SetPerspective(m_camera.GetFovYRadians(), m_camera.GetAspectRatio(), val, m_camera.GetFarPlane()); 
        }

        float GetFar() const { return m_camera.GetFarPlane(); }
        void SetFar(float val) 
        { 
            m_camera.SetPerspective(m_camera.GetFovYRadians(), m_camera.GetAspectRatio(), m_camera.GetNearPlane(), val); 
        }

        // 뷰포트 오버라이드 옵션
        bool  useAspectOverride{ false };
        float aspectOverride{ 0.0f };
        bool  primary{ true };

    private:
        Camera m_camera;
    };
}