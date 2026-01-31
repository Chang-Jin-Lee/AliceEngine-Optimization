#pragma once

#include <vector>
#include <wrl/client.h>
#include <d3d11.h>
#include <DirectXMath.h>

#include "Runtime/Rendering/Camera.h"
#include "Runtime/Rendering/D3D11/ID3D11RenderDevice.h"

namespace Alice
{
    /// 아주 단순한 DebugDraw 시스템입니다.
    /// - 선(line)과 단색 면(triangle)을 그립니다.
    /// - ECS/World 에 의존하지 않습니다.
    class DebugDrawSystem
    {
    public:
        explicit DebugDrawSystem(ID3D11RenderDevice& renderDevice);
        ~DebugDrawSystem() = default;

        /// 셰이더, 버퍼 등을 초기화합니다.
        bool Initialize();

        /// 모든 디버그 라인을 비웁니다.
        void Clear();

        /// 월드 공간에서 선을 추가합니다.
        void AddLine(const DirectX::XMFLOAT3& from,
                     const DirectX::XMFLOAT3& to,
                     const DirectX::XMFLOAT4& color);

        /// 월드 공간에서 원통을 추가합니다 (단색).
        void AddCylinder(const DirectX::XMFLOAT3& from,
                         const DirectX::XMFLOAT3& to,
                         float radius,
                         const DirectX::XMFLOAT4& color,
                         int segments = 12);

        /// 카메라 기준으로 모든 디버그 라인을 렌더링합니다.
        void Render(const Camera& camera);

    private:
        struct DebugVertex
        {
            DirectX::XMFLOAT3 position;
            DirectX::XMFLOAT4 color;
        };

        struct CBViewProj
        {
            DirectX::XMMATRIX viewProj;
        };

        bool CreateShadersAndInputLayout();
        bool EnsureVertexBufferSize(std::size_t vertexCount);
        void AddTriangle(const DirectX::XMFLOAT3& a,
                         const DirectX::XMFLOAT3& b,
                         const DirectX::XMFLOAT3& c,
                         const DirectX::XMFLOAT4& color);

    private:
        ID3D11RenderDevice& m_renderDevice;

        Microsoft::WRL::ComPtr<ID3D11Device>        m_device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;

        Microsoft::WRL::ComPtr<ID3D11Buffer>        m_vertexBuffer;
        Microsoft::WRL::ComPtr<ID3D11Buffer>        m_cbViewProj;
        Microsoft::WRL::ComPtr<ID3D11VertexShader>  m_vertexShader;
        Microsoft::WRL::ComPtr<ID3D11PixelShader>   m_pixelShader;
        Microsoft::WRL::ComPtr<ID3D11InputLayout>   m_inputLayout;

        std::vector<DebugVertex> m_lineVertices;
        std::vector<DebugVertex> m_triVertices;
        std::size_t              m_vertexCapacity { 0 };
    };
}



