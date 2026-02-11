#pragma once

#include <DirectXMath.h>
#include "Runtime/Rendering/RenderTypes.h"
#include <algorithm>

namespace Alice
{
    /// Unreal Engine 스타일의 Post Process Settings 구조체
    /// 각 항목마다 override 플래그를 가져서, 볼륨이 해당 항목에 관여하는지 제어합니다.
    struct PostProcessSettings
    {
        // ==== Exposure ====
        bool bOverride_Exposure = false;
        float exposure = 0.0f;  // 기본값: 0 = 1.0배
        
        bool bOverride_MaxHDRNits = false;
        float maxHDRNits = 1000.0f;  // HDR 모니터 최대 밝기 (nits)

        // ==== Color Grading ====
        bool bOverride_ColorGradingSaturation = false;
        DirectX::XMFLOAT3 saturation = { 1.0f, 1.0f, 1.0f };  // 기본값: 변화 없음

        bool bOverride_ColorGradingContrast = false;
        DirectX::XMFLOAT3 contrast = { 1.0f, 1.0f, 1.0f };  // 기본값: 변화 없음

        bool bOverride_ColorGradingGamma = false;
        DirectX::XMFLOAT3 gamma = { 1.0f, 1.0f, 1.0f };  // 기본값: 변화 없음

        bool bOverride_ColorGradingGain = false;
        DirectX::XMFLOAT3 gain = { 1.0f, 1.0f, 1.0f };  // 기본값: 변화 없음

        // ==== Bloom ====
        bool bOverride_BloomThreshold = false;
        float bloomThreshold = 1.0f;

        bool bOverride_BloomKnee = false;
        float bloomKnee = 0.5f;

        bool bOverride_BloomIntensity = false;
        float bloomIntensity = 0.0f;

        bool bOverride_BloomGaussianIntensity = false;
        float bloomGaussianIntensity = 1.0f;

        bool bOverride_BloomRadius = false;
        float bloomRadius = 1.0f;

        bool bOverride_BloomDownsample = false;
        int bloomDownsample = 2;

        // ==== Impact Blur ====
        // impactBlurIntensity: 0 = 비활성, 값이 클수록 강함
        bool bOverride_ImpactBlurIntensity = false;
        float impactBlurIntensity = 0.0f;

        // impactBlurRadius: 화면 정규화 반경(0~1 권장)
        bool bOverride_ImpactBlurRadius = false;
        float impactBlurRadius = 0.25f;

        // impactBlurCenterX/Y: 화면 중심점(UV, 0~1)
        bool bOverride_ImpactBlurCenterX = false;
        float impactBlurCenterX = 0.5f;

        bool bOverride_ImpactBlurCenterY = false;
        float impactBlurCenterY = 0.5f;

        // ==== Screen Split (HalfCut) ====
        // splitAmount: 분할선 기준 위/아래 절반이 이동하는 UV 오프셋 강도 (0 = 비활성)
        bool bOverride_SplitAmount = false;
        float splitAmount = 0.0f;

        // splitAngleDeg: 분할선 각도(도). 0이면 수평 분할, 양수는 반시계 회전.
        bool bOverride_SplitAngleDeg = false;
        float splitAngleDeg = 0.0f;

        // splitLineOffset: 분할선 위치 오프셋(정규화 공간). 0이면 화면 중심.
        bool bOverride_SplitLineOffset = false;
        float splitLineOffset = 0.0f;

        // splitFeather: 분할 경계부 블렌딩 폭(정규화 공간).
        bool bOverride_SplitFeather = false;
        float splitFeather = 0.001f;

        // splitFxIntensity: 절단선 수학 기반 하이라이트 강도 (0 = 비활성)
        bool bOverride_SplitFxIntensity = false;
        float splitFxIntensity = 0.0f;

        // splitFxWidth: 절단선 주변 영향 폭(정규화 공간)
        bool bOverride_SplitFxWidth = false;
        float splitFxWidth = 0.01f;

        // splitFxSpeed: 절단선 방향 흐름 속도
        bool bOverride_SplitFxSpeed = false;
        float splitFxSpeed = 30.0f;

        // splitFxTimeSec: 절단선 이펙트 시간(초)
        bool bOverride_SplitFxTimeSec = false;
        float splitFxTimeSec = 0.0f;

        // splitFxColorA/B: 절단선 색상(핵심/보조)
        bool bOverride_SplitFxColorA = false;
        DirectX::XMFLOAT3 splitFxColorA = { 2.8f, 1.2f, 0.45f };

        bool bOverride_SplitFxColorB = false;
        DirectX::XMFLOAT3 splitFxColorB = { 0.10f, 1.05f, 0.95f };

        /// 기본 설정으로 초기화 (모든 override = false)
        PostProcessSettings() = default;

        /// PostProcessParams에서 기본값 복사 (override는 false로 유지)
        static PostProcessSettings FromDefaults()
        {
            PostProcessSettings settings;
            settings.exposure = 0.0f;
            settings.maxHDRNits = 1000.0f;
            settings.saturation = DirectX::XMFLOAT3(
                ColorGradingLimits::SaturationDefault,
                ColorGradingLimits::SaturationDefault,
                ColorGradingLimits::SaturationDefault
            );
            settings.contrast = DirectX::XMFLOAT3(
                ColorGradingLimits::ContrastDefault,
                ColorGradingLimits::ContrastDefault,
                ColorGradingLimits::ContrastDefault
            );
            settings.gamma = DirectX::XMFLOAT3(
                ColorGradingLimits::GammaDefault,
                ColorGradingLimits::GammaDefault,
                ColorGradingLimits::GammaDefault
            );
            settings.gain = DirectX::XMFLOAT3(
                ColorGradingLimits::GainDefault,
                ColorGradingLimits::GainDefault,
                ColorGradingLimits::GainDefault
            );
            settings.bloomThreshold = 1.0f;
            settings.bloomKnee = 0.5f;
            settings.bloomIntensity = 0.0f;
            settings.bloomGaussianIntensity = 1.0f;
            settings.bloomRadius = 1.0f;
            settings.bloomDownsample = 2;
            settings.impactBlurIntensity = 0.0f;
            settings.impactBlurRadius = 0.25f;
            settings.impactBlurCenterX = 0.5f;
            settings.impactBlurCenterY = 0.5f;
            settings.splitAmount = 0.0f;
            settings.splitAngleDeg = 0.0f;
            settings.splitLineOffset = 0.0f;
            settings.splitFeather = 0.001f;
            settings.splitFxIntensity = 0.0f;
            settings.splitFxWidth = 0.01f;
            settings.splitFxSpeed = 30.0f;
            settings.splitFxTimeSec = 0.0f;
            settings.splitFxColorA = { 2.8f, 1.2f, 0.45f };
            settings.splitFxColorB = { 0.10f, 1.05f, 0.95f };
            return settings;
        }
    };

    /// PostProcessSettings 블렌딩 헬퍼 함수
    namespace PostProcessBlend
    {
        /// float 값 블렌딩: lerp(Final, Volume, weight)
        /// weight에 비례하여 점진적으로 블렌딩 (UE 스타일)
        inline void BlendFloat(float& final, float volume, float weight, bool override)
        {
            if (override && weight > 0.0f)
            {
                // lerp: final = final + (volume - final) * weight
                final = final + (volume - final) * weight;
            }
        }

        /// float3 값 블렌딩: lerp(Final, Volume, weight)
        /// weight에 비례하여 점진적으로 블렌딩 (UE 스타일)
        inline void BlendFloat3(DirectX::XMFLOAT3& final, const DirectX::XMFLOAT3& volume, float weight, bool override)
        {
            if (override && weight > 0.0f)
            {
                // lerp: final = final + (volume - final) * weight
                final.x = final.x + (volume.x - final.x) * weight;
                final.y = final.y + (volume.y - final.y) * weight;
                final.z = final.z + (volume.z - final.z) * weight;
            }
        }

        /// int 값 블렌딩: lerp(Final, Volume, weight)
        inline void BlendInt(int& final, int volume, float weight, bool override)
        {
            if (override && weight > 0.0f)
            {
                float finalF = static_cast<float>(final);
                float volumeF = static_cast<float>(volume);
                finalF = finalF + (volumeF - finalF) * weight;
                final = static_cast<int>(finalF + 0.5f);  // 반올림
            }
        }

        /// PostProcessSettings 전체 블렌딩 (maxHDRNits 포함)
        inline void BlendSettings(PostProcessSettings& final, const PostProcessSettings& volume, float weight)
        {
            weight = std::clamp(weight, 0.0f, 1.0f);
            BlendFloat(final.exposure, volume.exposure, weight, volume.bOverride_Exposure);
            BlendFloat(final.maxHDRNits, volume.maxHDRNits, weight, volume.bOverride_MaxHDRNits);
            BlendFloat3(final.saturation, volume.saturation, weight, volume.bOverride_ColorGradingSaturation);
            BlendFloat3(final.contrast, volume.contrast, weight, volume.bOverride_ColorGradingContrast);
            BlendFloat3(final.gamma, volume.gamma, weight, volume.bOverride_ColorGradingGamma);
            BlendFloat3(final.gain, volume.gain, weight, volume.bOverride_ColorGradingGain);
            BlendFloat(final.bloomThreshold, volume.bloomThreshold, weight, volume.bOverride_BloomThreshold);
            BlendFloat(final.bloomKnee, volume.bloomKnee, weight, volume.bOverride_BloomKnee);
            BlendFloat(final.bloomIntensity, volume.bloomIntensity, weight, volume.bOverride_BloomIntensity);
            BlendFloat(final.bloomGaussianIntensity, volume.bloomGaussianIntensity, weight, volume.bOverride_BloomGaussianIntensity);
            BlendFloat(final.bloomRadius, volume.bloomRadius, weight, volume.bOverride_BloomRadius);
            BlendInt(final.bloomDownsample, volume.bloomDownsample, weight, volume.bOverride_BloomDownsample);
            BlendFloat(final.impactBlurIntensity, volume.impactBlurIntensity, weight, volume.bOverride_ImpactBlurIntensity);
            BlendFloat(final.impactBlurRadius, volume.impactBlurRadius, weight, volume.bOverride_ImpactBlurRadius);
            BlendFloat(final.impactBlurCenterX, volume.impactBlurCenterX, weight, volume.bOverride_ImpactBlurCenterX);
            BlendFloat(final.impactBlurCenterY, volume.impactBlurCenterY, weight, volume.bOverride_ImpactBlurCenterY);
            BlendFloat(final.splitAmount, volume.splitAmount, weight, volume.bOverride_SplitAmount);
            BlendFloat(final.splitAngleDeg, volume.splitAngleDeg, weight, volume.bOverride_SplitAngleDeg);
            BlendFloat(final.splitLineOffset, volume.splitLineOffset, weight, volume.bOverride_SplitLineOffset);
            BlendFloat(final.splitFeather, volume.splitFeather, weight, volume.bOverride_SplitFeather);
            BlendFloat(final.splitFxIntensity, volume.splitFxIntensity, weight, volume.bOverride_SplitFxIntensity);
            BlendFloat(final.splitFxWidth, volume.splitFxWidth, weight, volume.bOverride_SplitFxWidth);
            BlendFloat(final.splitFxSpeed, volume.splitFxSpeed, weight, volume.bOverride_SplitFxSpeed);
            BlendFloat(final.splitFxTimeSec, volume.splitFxTimeSec, weight, volume.bOverride_SplitFxTimeSec);
            BlendFloat3(final.splitFxColorA, volume.splitFxColorA, weight, volume.bOverride_SplitFxColorA);
            BlendFloat3(final.splitFxColorB, volume.splitFxColorB, weight, volume.bOverride_SplitFxColorB);
        }

    }
}
