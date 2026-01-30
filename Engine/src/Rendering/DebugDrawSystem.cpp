#include "Rendering/DebugDrawSystem.h"

#include <d3dcompiler.h>
#include <Core/Logger.h>
#include <cmath>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace Alice
{
    namespace
    {
        // 아주 단순한 컬러 라인 전용 셰이더입니다.
        const char* g_DebugLineVS = R"(
cbuffer CBViewProj : register(b0)
{
    float4x4 gViewProj;
};

struct VSInput
{
    float3 Position : POSITION;
    float4 Color    : COLOR0;
};

struct VSOutput
{
    float4 Position : SV_POSITION;
    float4 Color    : COLOR0;
};

VSOutput main(VSInput input)
{
    VSOutput output;
    float4 worldPos = float4(input.Position, 1.0f);
    output.Position = mul(worldPos, gViewProj);
    output.Color    = input.Color;
    return output;
}
)";

        const char* g_DebugLinePS = R"(
struct PSInput
{
    float4 Position : SV_POSITION;
    float4 Color    : COLOR0;
};

float4 main(PSInput input) : SV_TARGET
{
    return input.Color;
}
)";
    }

    DebugDrawSystem::DebugDrawSystem(ID3D11RenderDevice& renderDevice)
        : m_renderDevice(renderDevice)
    {
        m_device  = m_renderDevice.GetDevice();
        m_context = m_renderDevice.GetImmediateContext();
    }

	bool DebugDrawSystem::Initialize()
	{
		ALICE_LOG_INFO("[DDS] init begin. dev=%p ctx=%p", m_device.Get(), m_context.Get());

		if (!m_device) { ALICE_LOG_ERRORF("[DDS] device null"); return false; }
		if (!m_context) { ALICE_LOG_ERRORF("[DDS] context null"); return false; }

		if (!CreateShadersAndInputLayout())
		{
			ALICE_LOG_ERRORF("[DDS] CreateShadersAndInputLayout failed");
			return false;
		}

		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = sizeof(CBViewProj);
		desc.Usage = D3D11_USAGE_DEFAULT;
		desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

		HRESULT hr = m_device->CreateBuffer(&desc, nullptr, m_cbViewProj.ReleaseAndGetAddressOf());
		if (FAILED(hr))
		{
			ALICE_LOG_ERRORF("[DDS] CreateBuffer(CBViewProj) failed hr=0x%08X size=%u", (unsigned)hr, (unsigned)sizeof(CBViewProj));
			return false;
		}

		ALICE_LOG_INFO("[DDS] init success");
		return true;
	}


    void DebugDrawSystem::Clear()
    {
        m_lineVertices.clear();
        m_triVertices.clear();
    }

    void DebugDrawSystem::AddLine(const XMFLOAT3& from, const XMFLOAT3& to, const XMFLOAT4& color)
    {
        m_lineVertices.push_back({ from, color });
        m_lineVertices.push_back({ to,   color });
    }

    void DebugDrawSystem::AddTriangle(const XMFLOAT3& a, const XMFLOAT3& b, const XMFLOAT3& c, const XMFLOAT4& color)
    {
        m_triVertices.push_back({ a, color });
        m_triVertices.push_back({ b, color });
        m_triVertices.push_back({ c, color });
    }

    void DebugDrawSystem::AddCylinder(const XMFLOAT3& from, const XMFLOAT3& to, float radius, const XMFLOAT4& color, int segments)
    {
        if (segments < 3 || radius <= 0.0f) return;

        XMVECTOR p0 = XMLoadFloat3(&from);
        XMVECTOR p1 = XMLoadFloat3(&to);
        XMVECTOR axis = p1 - p0;
        float len = XMVectorGetX(XMVector3Length(axis));
        if (len <= 0.0001f) return;

        XMVECTOR dir = XMVector3Normalize(axis);
        XMVECTOR up = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
        if (fabsf(XMVectorGetX(XMVector3Dot(dir, up))) > 0.99f)
        {
            up = XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
        }

        XMVECTOR right = XMVector3Normalize(XMVector3Cross(up, dir));
        XMVECTOR up2 = XMVector3Normalize(XMVector3Cross(dir, right));

        const float step = DirectX::XM_2PI / static_cast<float>(segments);

        for (int i = 0; i < segments; ++i)
        {
            float a0 = step * static_cast<float>(i);
            float a1 = step * static_cast<float>(i + 1);

            XMVECTOR off0 = right * cosf(a0) + up2 * sinf(a0);
            XMVECTOR off1 = right * cosf(a1) + up2 * sinf(a1);

            XMVECTOR v00 = p0 + off0 * radius;
            XMVECTOR v01 = p0 + off1 * radius;
            XMVECTOR v10 = p1 + off0 * radius;
            XMVECTOR v11 = p1 + off1 * radius;

            XMFLOAT3 f00, f01, f10, f11, f0, f1;
            XMStoreFloat3(&f00, v00);
            XMStoreFloat3(&f01, v01);
            XMStoreFloat3(&f10, v10);
            XMStoreFloat3(&f11, v11);
            XMStoreFloat3(&f0, p0);
            XMStoreFloat3(&f1, p1);

            // Side
            AddTriangle(f00, f10, f11, color);
            AddTriangle(f00, f11, f01, color);

            // Caps (simple)
            AddTriangle(f0, f01, f00, color);
            AddTriangle(f1, f10, f11, color);
        }
    }

    void DebugDrawSystem::Render(const Camera& camera)
    {
        if ((!m_vertexShader || !m_pixelShader || !m_inputLayout) ||
            (m_lineVertices.empty() && m_triVertices.empty()))
        {
            return;
        }

        // View-Proj 행렬 업데이트
        CBViewProj cb = { XMMatrixTranspose(camera.GetViewMatrix() * camera.GetProjectionMatrix()) };
        m_context->UpdateSubresource(m_cbViewProj.Get(), 0, nullptr, &cb, 0, 0);

        m_context->IASetInputLayout(m_inputLayout.Get());
        m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
        m_context->VSSetConstantBuffers(0, 1, m_cbViewProj.GetAddressOf());
        m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);

        auto DrawList = [&](const std::vector<DebugVertex>& vertices, D3D11_PRIMITIVE_TOPOLOGY topo)
        {
            if (vertices.empty()) return;
            if (!EnsureVertexBufferSize(vertices.size())) return;

            D3D11_MAPPED_SUBRESOURCE mapped{};
            if (FAILED(m_context->Map(m_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) return;
            std::memcpy(mapped.pData, vertices.data(), vertices.size() * sizeof(DebugVertex));
            m_context->Unmap(m_vertexBuffer.Get(), 0);

            UINT stride = sizeof(DebugVertex), offset = 0;
            ID3D11Buffer* vb = m_vertexBuffer.Get();
            m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
            m_context->IASetPrimitiveTopology(topo);

            m_context->Draw((UINT)vertices.size(), 0);
        };

        DrawList(m_lineVertices, D3D11_PRIMITIVE_TOPOLOGY_LINELIST);
        DrawList(m_triVertices, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    }

    bool DebugDrawSystem::CreateShadersAndInputLayout()
    {
        ComPtr<ID3DBlob> vsBlob, psBlob;

        // 1. VS 컴파일 및 생성
        if (FAILED(D3DCompile(g_DebugLineVS, std::strlen(g_DebugLineVS), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), nullptr))) return false;
        if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_vertexShader.ReleaseAndGetAddressOf()))) return false;

        // 2. PS 컴파일 및 생성
        if (FAILED(D3DCompile(g_DebugLinePS, std::strlen(g_DebugLinePS), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), nullptr))) return false;
        if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_pixelShader.ReleaseAndGetAddressOf()))) return false;

        // 3. Input Layout 생성 (오프셋 자동 정렬 사용)
        D3D11_INPUT_ELEMENT_DESC desc[] = {
            { "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,                            D3D11_INPUT_PER_VERTEX_DATA, 0 },
            { "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
        };

        if (FAILED(m_device->CreateInputLayout(desc, (UINT)std::size(desc), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_inputLayout.ReleaseAndGetAddressOf()))) return false;

        return true;
    }

    bool DebugDrawSystem::EnsureVertexBufferSize(std::size_t vertexCount)
    {
        if (vertexCount == 0 || (m_vertexBuffer && vertexCount <= m_vertexCapacity)) return true;

        m_vertexBuffer.Reset();
        m_vertexCapacity = vertexCount;

        // 동적 버퍼 생성 (Dynamic Usage, CPU Write)
        D3D11_BUFFER_DESC desc = { (UINT)(sizeof(DebugVertex) * vertexCount), D3D11_USAGE_DYNAMIC, D3D11_BIND_VERTEX_BUFFER, D3D11_CPU_ACCESS_WRITE, 0, 0 };
        if (FAILED(m_device->CreateBuffer(&desc, nullptr, m_vertexBuffer.ReleaseAndGetAddressOf()))) return false;

        return true;
    }
}



