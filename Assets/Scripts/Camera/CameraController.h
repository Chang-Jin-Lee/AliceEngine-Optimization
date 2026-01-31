#pragma once
#include <string>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    // 콘텐츠용 카메라 컨트롤러:
    // - 입력/연출 트리거는 여기서만 처리
    // - CameraSystem은 컴포넌트 실행만 담당
    class CameraController : public IScript
    {
        ALICE_BODY(CameraController);

    public:
        void Awake() override;
        void Start() override;
        void Update(float deltaTime) override;

    private:
        bool m_preview = false;
        bool m_savedFollowEnabled = true;
        bool m_savedLookAtEnabled = false;

        std::string GetCameraNameFromCsv(const std::string& csv, int idx) const;

        void SetPreview(bool on);

        void TriggerCut(const std::string& camName);
        void TriggerBlend(const std::string& camName, float duration, bool useCurve = true);
        void TriggerShake(float amp, float freq, float dur, float decay);

        void ToggleLookAt();
        void UpdateOrbit();
        void UpdateZoom();

        // SpaceBar 쉐이크 파라미터 (에디터에서 조절 가능)
        ALICE_PROPERTY(float, m_shakeAmplitude, 0.5f)       // 흔들림 강도
        ALICE_PROPERTY(float, m_shakeFrequency, 20.0f)      // 흔들림 빈도
        ALICE_PROPERTY(float, m_shakeDuration, 2.2f)        // 흔들림 지속 시간
        ALICE_PROPERTY(float, m_shakeDecay, 2.0f)           // 흔들림 감쇠 계수
        ALICE_PROPERTY(float, m_sholderOffset, 4.0f)        // 흔들림 감쇠 계수
    };
}
