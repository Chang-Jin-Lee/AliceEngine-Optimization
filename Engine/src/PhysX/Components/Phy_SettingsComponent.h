#pragma once
#include <DirectXMath.h>
#include <cstdint>
#include <array>
#include <string>

// opt-in 방식 이라고 하더라
// 표식 컴포넌트
// 해당 컴포넌트가 있는지 없는지로 물리를 사용하는지 안하는지 확인함

// 최대 32개 레이어 지원 (uint32_t bitmask 기반)
constexpr int MAX_PHYSICS_LAYERS = 32;

struct Phy_SettingsComponent
{
    bool enablePhysics = true;

    // Ground Plane (y=0) toggle
    bool enableGroundPlane = true;

    // Ground Plane material
    float groundStaticFriction = 0.5f;
    float groundDynamicFriction = 0.5f;
    float groundRestitution = 0.0f;

    // Ground Plane filtering
    uint32_t groundLayerBits = 1u << 0;
    uint32_t groundCollideMask = 0xFFFFFFFFu;
    uint32_t groundQueryMask = 0xFFFFFFFFu;
    uint32_t groundIgnoreLayers = 0u;
    bool groundIsTrigger = false;

    // Transform이 DirectX 기준이니까 gravity도 XMFLOAT3로
    DirectX::XMFLOAT3 gravity = { 0.0f, -9.81f, 0.0f };

    float fixedDt = 1.0f / 60.0f;
    int   maxSubsteps = 4;

    // 레이어 간 충돌 매트릭스 (32x32) - 콜라이더 충돌용
    // layerCollideMatrix[i][j] = true 이면 레이어 i와 레이어 j가 충돌함
    // 기본값: 모든 레이어가 서로 충돌 (true)
    std::array<std::array<bool, MAX_PHYSICS_LAYERS>, MAX_PHYSICS_LAYERS> layerCollideMatrix;
    
    // 레이어 간 쿼리 매트릭스 (32x32) - 쿼리용
    // layerQueryMatrix[i][j] = true 이면 레이어 i가 레이어 j를 쿼리할 수 있음
    // 기본값: 모든 레이어가 서로 쿼리 가능 (true)
    std::array<std::array<bool, MAX_PHYSICS_LAYERS>, MAX_PHYSICS_LAYERS> layerQueryMatrix;

    // 레이어 이름 (인스펙터에서 표시용)
    std::array<std::string, MAX_PHYSICS_LAYERS> layerNames;

    // 필터 매트릭스 변경 감지용 리비전 (에디터에서 매트릭스 변경 시 증가)
    uint32_t filterRevision = 0;

    Phy_SettingsComponent()
    {
        // 기본값: 모든 레이어가 서로 충돌/쿼리 가능
        for (int i = 0; i < MAX_PHYSICS_LAYERS; ++i)
        {
            for (int j = 0; j < MAX_PHYSICS_LAYERS; ++j)
            {
                layerCollideMatrix[i][j] = true;
                layerQueryMatrix[i][j] = true;
            }
        }

        // 기본 레이어 이름 설정
        for (int i = 0; i < MAX_PHYSICS_LAYERS; ++i)
        {
            layerNames[i] = "Layer_" + std::to_string(i);
        }
    }
};
