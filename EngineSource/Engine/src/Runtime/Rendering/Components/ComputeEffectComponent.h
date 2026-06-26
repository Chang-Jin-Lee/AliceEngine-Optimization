#pragma once

#include <DirectXMath.h>
#include <string>

namespace Alice {
    enum class ParticleSimulationSpace
    {
        World = 0,
        Local = 1
    };

    /// 컴퓨트 셰이더 이펙트 컴포넌트
    /// - 엔티티에 컴퓨트 셰이더 파티클 이펙트를 적용하기 위한 컴포넌트
    struct ComputeEffectComponent 
    {
        bool enabled{ true };                          // 이펙트 활성화 여부
        std::string shaderName{ "Particle" };          // 사용할 컴퓨트 셰이더 이름 (기본값: "Particle")
        
        // === Main Module (Unity-style) ===
        ParticleSimulationSpace simulationSpace{ ParticleSimulationSpace::World };
        DirectX::XMFLOAT3 localOffset{ 0.0f, 0.0f, 0.0f }; // Transform 기준 로컬 오프셋
        float lifeMin{ 0.8f };                        // 최소 수명
        float lifeMax{ 2.4f };                        // 최대 수명
        float startSpeed{ 1.0f };                     // 시작 속도 배율
        float sizePx{ 6.0f };                          // 파티클 크기 (픽셀)
        DirectX::XMFLOAT3 color{ 1.0f, 1.0f, 0.0f };    // 파티클 색상
        float intensity{ 1.0f };                       // 이펙트 강도

        // === Emission Module (단순화) ===
        float spawnRate{ 1.0f };                       // 리스폰 비율 (0..1, 1=항상 스폰)
        bool emitNewParticles{ true };                 // 신규 파티클 방출 on/off (기존 파티클은 자연 소멸)
        bool loop{ true };                              // true면 무한 방출, false면 duration 동안만 방출
        float emissionDurationSec{ 0.25f };            // loop=false일 때 방출 시간(초), <=0이면 1프레임 방출

        // === Shape Module ===
        float radius{ 0.5f };                          // 이미터 반경

        // === Forces Module ===
        DirectX::XMFLOAT3 gravity{ 0.0f, -0.65f, 0.0f }; // 중력
        float drag{ 0.12f };                          // 드래그 계수

        // === Renderer Module ===
        bool depthTest{ true };                        // Depth 테스트 사용 여부
        float depthBiasMeters{ 0.05f };                // Depth bias (미터 단위, 기본 5cm)
    };
}
