#pragma once

#include <string>
#include <vector>

#include <DirectXMath.h>

#include "Core/IScript.h"
#include "Core/ScriptReflection.h"
#include "Core/Entity.h"

namespace Alice
{
    /// 여러 카메라 엔티티를 블렌딩하여 출력 카메라로 합성하는 디렉터 스크립트
    /// - 이 스크립트가 붙은 카메라 엔티티가 "출력 카메라"입니다.
    /// - m_cameraListCsv에 지정된 카메라들을 대상으로 전환합니다.
    class CameraDirector : public IScript
    {
        ALICE_BODY(CameraDirector);

    public:
        void Awake() override;
        void LateUpdate(float deltaTime) override;

        /// 카메라 이름으로 블렌딩 전환 (duration <= 0 이면 즉시 컷)
        void BlendTo(const std::string& cameraName, float duration = -1.0f);
        ALICE_FUNC(BlendTo);

        /// 언리얼의 SetCameraWithBlend와 동일한 의미의 래퍼
        void SetCameraWithBlend(const std::string& cameraName, float duration = -1.0f);
        ALICE_FUNC(SetCameraWithBlend);

        /// 인덱스로 블렌딩 전환 (m_cameraListCsv 기준)
        void BlendToIndex(int index, float duration = -1.0f);
        ALICE_FUNC(BlendToIndex);

        /// 카메라 리스트를 다시 스캔합니다.
        void RebuildCameraList();
        ALICE_FUNC(RebuildCameraList);

    private:
        struct BlendSnapshot
        {
            DirectX::XMFLOAT3 position{};
            DirectX::XMFLOAT3 rotation{};
            float fovY = DirectX::XM_PIDIV4;
            float nearPlane = 0.1f;
            float farPlane = 5000.0f;
        };

        static DirectX::XMFLOAT4 EulerToQuaternion(const DirectX::XMFLOAT3& eulerRad);
        static DirectX::XMFLOAT3 QuaternionToEuler(const DirectX::XMFLOAT4& quat);
        static float SmoothStep(float t);

        void ApplyBlend(const BlendSnapshot& from,
                        const BlendSnapshot& to,
                        float t01);

        bool GetCameraSnapshot(EntityId id, BlendSnapshot& out) const;

        std::vector<EntityId> ResolveCameraList() const;

    private:
        // 카메라 이름 목록 (CSV: "CamA,CamB,CamC")
        ALICE_PROPERTY(std::string, m_cameraListCsv, "");
        ALICE_PROPERTY(float, m_defaultBlendTime, 0.5f);
        ALICE_PROPERTY(bool, m_autoSetPrimary, true);

        // 런타임 상태
        std::vector<EntityId> m_cameraIds;
        EntityId m_targetCameraId = InvalidEntityId;
        BlendSnapshot m_from;
        float m_blendElapsed = 0.0f;
        float m_blendDuration = 0.0f;
        bool m_isBlending = false;
    };
}
