#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdint>
#include <unordered_map>
#include <string>
#include <wrl/client.h>
#include <d3d11.h>
#include <DirectXMath.h>

#include "Core/World.h"
#include "Components/TransformComponent.h"
#include "Rendering/D3D11/ID3D11RenderDevice.h"

namespace Alice
{
    /// 컴퓨트 셰이더 이펙트 시스템
    /// - ComputeEffectComponent를 가진 엔티티들에 컴퓨트 셰이더 이펙트를 적용합니다.
    class ComputeEffectSystem
    {
    public:
        explicit ComputeEffectSystem(ID3D11RenderDevice& renderDevice);
        ~ComputeEffectSystem();

        /// 셰이더, 버퍼 등 렌더링에 필요한 리소스를 생성합니다.
        bool Initialize(std::uint32_t width, std::uint32_t height);

        /// 뷰포트 크기가 변경되면 리소스도 함께 리사이즈합니다.
        void Resize(std::uint32_t width, std::uint32_t height);

        /// 컴퓨트 셰이더 이펙트를 실행합니다.
        /// \param world ECS 월드 (ComputeEffectComponent 조회)
        /// \param viewProj View * Projection 행렬
        /// \param cameraPos 카메라 월드 위치
        /// \param sceneDepthSRV Scene Depth SRV (depth test용, nullptr 가능)
        /// \param nearPlane 카메라 near plane (depth test 선형화용)
        /// \param farPlane 카메라 far plane (depth test 선형화용)
        /// \param dtSec 델타 타임 (초)
        void Execute(const World& world, const DirectX::XMMATRIX& viewProj, const DirectX::XMFLOAT3& cameraPos, ID3D11ShaderResourceView* sceneDepthSRV, float nearPlane, float farPlane, float dtSec);

        /// 파티클 출력 텍스처의 SRV를 반환합니다.
        ID3D11ShaderResourceView* GetOutputSRV() const { return m_outputSRV.Get(); }
        
        /// 이번 프레임에 활성화된 이펙트가 있었는지 반환합니다.
        bool HasActiveEffect() const;

        /// 파티클 개수를 설정합니다.
        void SetParticleCount(std::uint32_t particleCount);

        // (레거시) 전역 파티클 파라미터 설정 - 멀티 이미터 구조에서는 의미가 거의 없음
        // 컴포넌트별로 개별 설정하는 것을 권장
        void SetEmitterNormalized(float x, float y, float radius);
        void SetParticleColor(float r, float g, float b);
        void SetParticleSizePx(float sizePx);
        
        /// 등록된 파티클 셰이더 이름 목록을 반환합니다 (인스펙터 UI용)
        std::vector<std::string> GetRegisteredShaderNames() const;

    private:
        bool CreateComputeShaders();
        bool CreateConstantBuffer();
        bool CreateUnorderedAccessViews(std::uint32_t width, std::uint32_t height);
        bool CreatePointSampler();
        bool CreateDummyDepthTexture();

        // 프리셋별 리소스 생성
        bool EnsureParticlePool(const std::string& presetName, std::uint32_t particleCount);
        bool EnsureEmitterBufferCapacity(const std::string& presetName, std::uint32_t emitterCount);
        
        void UpdateConstantBuffer(std::uint32_t emitterCount);
        void DispatchClear(ID3D11ComputeShader* clearShader);
        void DispatchParticlesUpdate(ID3D11ComputeShader* updateShader, 
                                     ID3D11UnorderedAccessView* particleUAV,
                                     ID3D11ShaderResourceView* emitterSRV);
        void DispatchParticlesDraw(ID3D11ComputeShader* drawShader, 
                                  ID3D11ShaderResourceView* particleSRV,
                                  ID3D11ShaderResourceView* emitterSRV,
                                  ID3D11ShaderResourceView* sceneDepthSRV);
        void UnbindCS();
        
        // 파티클 셰이더 세트 등록 (타입별)
        bool RegisterParticleShaderSet(const std::string& name, 
                                       const char* clearCS, 
                                       const char* updateCS, 
                                       const char* drawCS);

    private:
        ID3D11RenderDevice& m_renderDevice;

        Microsoft::WRL::ComPtr<ID3D11Device>        m_device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;
        
        // 파티클 셰이더 세트 (타입별로 관리)
        struct ParticleShaderSet
        {
            Microsoft::WRL::ComPtr<ID3D11ComputeShader> clearShader;
            Microsoft::WRL::ComPtr<ID3D11ComputeShader> updateShader;
            Microsoft::WRL::ComPtr<ID3D11ComputeShader> drawShader;
        };

        // 프리셋별 파티클 풀
        struct ParticlePool
        {
            std::uint32_t count = 0;
            Microsoft::WRL::ComPtr<ID3D11Buffer>              buffer;
            Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> uav;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView>   srv;
        };

        // 프리셋별 이미터 버퍼
        struct EmitterBuffer
        {
            std::uint32_t capacity = 0;
            Microsoft::WRL::ComPtr<ID3D11Buffer>             buffer;
            Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srv;
        };

        // 프리셋별 런타임 리소스
        struct PresetRuntime
        {
            ParticleShaderSet shaders;
            ParticlePool      pool;
            EmitterBuffer      emitters;
        };

        std::unordered_map<std::string, PresetRuntime> m_presets;
        /// Clear용 셰이더. 모든 프리셋이 m_outputUAV를 공유하므로 시스템 전역 1회 Clear.
        Microsoft::WRL::ComPtr<ID3D11ComputeShader> m_clearShader;
        Microsoft::WRL::ComPtr<ID3D11Buffer>        m_constantBuffer;

        // 컴퓨트 셰이더 출력용 UAV
        Microsoft::WRL::ComPtr<ID3D11Texture2D>         m_outputTexture;
        Microsoft::WRL::ComPtr<ID3D11UnorderedAccessView> m_outputUAV;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_outputSRV;
        
        // Point Clamp Sampler State (depth 샘플링 전용 - 경계 보간 방지)
        Microsoft::WRL::ComPtr<ID3D11SamplerState> m_pointSampler;

        // 더미 depth 텍스처 (depthSRV가 nullptr일 때 사용)
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_dummyDepthTexture;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_dummyDepthSRV;

        // 전역 파티클 개수 (프리셋별 풀의 기본 크기)
        std::uint32_t m_particleCount = 65536;
        DirectX::XMMATRIX m_viewProj{};  // View * Projection 행렬
        DirectX::XMFLOAT3 m_cameraPos{};  // 카메라 월드 위치
        float m_nearPlane = 0.1f;        // 카메라 near plane (depth test 선형화용)
        float m_farPlane = 1000.0f;      // 카메라 far plane (depth test 선형화용)

        // 이펙트 활성화 상태 플래그
        bool m_hasActiveEffect = false;

        // 시간 관리
        float m_timeSec = 0.0f;
        float m_dtSec = 0.0f;

        std::uint32_t m_width  = 0;
        std::uint32_t m_height = 0;
    };
}
