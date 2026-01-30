#pragma once

#include <string>

#include "Core/IScript.h"
#include "Core/ScriptReflection.h"

namespace Alice
{
    /// 엔진 카메라 컴포넌트를 간단히 묶는 프리셋
    /// - 콘텐츠는 이 스크립트만 붙이고 값만 조절하면 됩니다.
    class CameraRigPreset : public IScript
    {
        ALICE_BODY(CameraRigPreset);

    public:
        void Awake() override;
        void Update(float deltaTime) override;

    private:
        // 카메라 목록 및 타깃
        ALICE_PROPERTY(std::string, m_cameraListCsv, "Camera1,Camera2,Camera3,Camera4,Camera5");
        ALICE_PROPERTY(std::string, m_followTargetName, "Rapi");
        ALICE_PROPERTY(std::string, m_lookAtTargetName, "Enemy");

        // 키 3/4/5 블렌드 시간
        ALICE_PROPERTY(float, m_blendTimeKey3, 0.6f);
        ALICE_PROPERTY(float, m_blendTimeKey4, 0.6f);
        ALICE_PROPERTY(float, m_blendTimeKey5, 0.8f);

        // 키 4: 쉐이크
        ALICE_PROPERTY(float, m_shakeAmplitudeKey4, 0.3f);
        ALICE_PROPERTY(float, m_shakeFrequencyKey4, 10.0f);
        ALICE_PROPERTY(float, m_shakeDurationKey4, 0.4f);
        ALICE_PROPERTY(float, m_shakeDecayKey4, 2.0f);

        // 키 5: 슬로우 모션
        ALICE_PROPERTY(float, m_slowTriggerTKey5, 0.5f);
        ALICE_PROPERTY(float, m_slowDurationKey5, 0.3f);
        ALICE_PROPERTY(float, m_slowTimeScaleKey5, 0.2f);

        // 스프링 암 줌
        ALICE_PROPERTY(float, m_springArmDistance, 2.0f);
        ALICE_PROPERTY(float, m_springArmMinDistance, 1.0f);
        ALICE_PROPERTY(float, m_springArmMaxDistance, 3.0f);
        ALICE_PROPERTY(float, m_springArmZoomSpeed, 0.01f);

        // 런타임에 계속 반영할지 여부
        ALICE_PROPERTY(bool, m_applyEveryFrame, false);
    };
}
