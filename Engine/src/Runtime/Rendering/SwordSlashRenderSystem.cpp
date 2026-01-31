#include "Runtime/Rendering/SwordSlashRenderSystem.h"
#include "../../Assets/Scripts/SwordSlashEffect.h"

#include <d3dcompiler.h>
#include <cmath>
#include <algorithm>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace Alice
{
	namespace
	{
		// 검기 스플라인 렌더링용 셰이더
		const char* g_SwordSlashVS = R"(
cbuffer CBPerSwordSlash : register(b0)
{
    float4x4 gViewProj;
    float3   gColor;
    float    gThickness;
};

struct VSInput
{
    float3 Position : POSITION;
    float  Alpha    : COLOR0;
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
    output.Color = float4(gColor, input.Alpha);
    return output;
}
)";

		const char* g_SwordSlashPS = R"(
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

	SwordSlashRenderSystem::SwordSlashRenderSystem(ID3D11RenderDevice& renderDevice)
		: m_renderDevice(renderDevice)
	{
		m_device = m_renderDevice.GetDevice();
		m_context = m_renderDevice.GetImmediateContext();
	}

	bool SwordSlashRenderSystem::Initialize()
	{
		if (!m_device || !m_context) return false;
		if (!CreateShadersAndInputLayout()) return false;

		// PerSwordSlash 상수 버퍼 생성
		D3D11_BUFFER_DESC desc = { sizeof(CBPerSwordSlash), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };
		if (FAILED(m_device->CreateBuffer(&desc, nullptr, m_cbPerSwordSlash.ReleaseAndGetAddressOf()))) return false;

		// 알파 블렌딩용 블렌드 스테이트 생성
		D3D11_BLEND_DESC blendDesc = {};
		blendDesc.RenderTarget[0].BlendEnable = TRUE;
		blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(m_device->CreateBlendState(&blendDesc, m_blendState.ReleaseAndGetAddressOf()))) return false;

		return true;
	}

	void SwordSlashRenderSystem::Render(const World& world, const Camera& camera)
	{
		if (!m_vertexShader || !m_pixelShader || !m_inputLayout) return;

		XMMATRIX view = camera.GetViewMatrix();
		XMMATRIX proj = camera.GetProjectionMatrix();
		XMMATRIX viewProj = view * proj;

		// 파이프라인 설정
		m_context->IASetInputLayout(m_inputLayout.Get());
		m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP);
		m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
		m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);

		// 알파 블렌딩 활성화
		float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		m_context->OMSetBlendState(m_blendState.Get(), blendFactor, 0xffffffff);

		// 모든 엔티티에서 SwordSlashEffect 스크립트 찾기
		const auto& allScripts = world.GetAllScriptsInWorld();
		for (const auto& [entityId, scriptList] : allScripts)
		{
			// TrailEffect 스크립트 찾기
			TrailEffect* slashEffect = nullptr;
			for (const auto& scriptComp : scriptList)
			{
				if (scriptComp.instance)
				{
					slashEffect = dynamic_cast<TrailEffect*>(scriptComp.instance.get());
					if (slashEffect) break;
				}
			}

			if (!slashEffect || !slashEffect->IsActive()) continue;

			// 스플라인 점들 가져오기
			const auto& splinePoints = slashEffect->GetSplinePoints();
			if (splinePoints.empty()) continue;

			// 현재 알파 값 가져오기
			float alpha = slashEffect->GetCurrentAlpha();

			// 버텍스 데이터 생성
			std::vector<SplineVertex> vertices;
			vertices.reserve(splinePoints.size());
			for (const auto& point : splinePoints)
			{
				SplineVertex v;
				v.position = point;
				v.alpha = alpha;
				vertices.push_back(v);
			}

			if (vertices.empty()) continue;

			// Vertex Buffer 업데이트
			if (!EnsureVertexBufferSize(vertices.size())) continue;

			D3D11_MAPPED_SUBRESOURCE mapped;
			if (FAILED(m_context->Map(m_vertexBuffer.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) continue;
			std::memcpy(mapped.pData, vertices.data(), vertices.size() * sizeof(SplineVertex));
			m_context->Unmap(m_vertexBuffer.Get(), 0);

			// 상수 버퍼 업데이트
			CBPerSwordSlash cb;
			cb.viewProj = XMMatrixTranspose(viewProj);
			cb.color = slashEffect->Get_m_color();
			cb.thickness = slashEffect->Get_m_thickness();
			m_context->UpdateSubresource(m_cbPerSwordSlash.Get(), 0, nullptr, &cb, 0, 0);

			// Vertex Buffer 바인딩
			UINT stride = sizeof(SplineVertex), offset = 0;
			ID3D11Buffer* vb = m_vertexBuffer.Get();
			m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
			m_context->VSSetConstantBuffers(0, 1, m_cbPerSwordSlash.GetAddressOf());

			// 렌더링
			m_context->Draw((UINT)vertices.size(), 0);
		}

		// 블렌드 스테이트 복원
		m_context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
	}

	bool SwordSlashRenderSystem::CreateShadersAndInputLayout()
	{
		ComPtr<ID3DBlob> vsBlob, psBlob;

		// 1. VS 컴파일 및 생성
		if (FAILED(D3DCompile(g_SwordSlashVS, std::strlen(g_SwordSlashVS), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), nullptr))) return false;
		if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_vertexShader.ReleaseAndGetAddressOf()))) return false;

		// 2. PS 컴파일 및 생성
		if (FAILED(D3DCompile(g_SwordSlashPS, std::strlen(g_SwordSlashPS), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), nullptr))) return false;
		if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_pixelShader.ReleaseAndGetAddressOf()))) return false;

		// 3. Input Layout 생성
		D3D11_INPUT_ELEMENT_DESC desc[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,                            D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR",    0, DXGI_FORMAT_R32_FLOAT,         0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};

		if (FAILED(m_device->CreateInputLayout(desc, (UINT)std::size(desc), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_inputLayout.ReleaseAndGetAddressOf()))) return false;

		return true;
	}

	bool SwordSlashRenderSystem::EnsureVertexBufferSize(std::size_t vertexCount)
	{
		if (vertexCount <= m_vertexCapacity) return true;

		m_vertexBuffer.Reset();

		D3D11_BUFFER_DESC desc = {};
		desc.ByteWidth = (UINT)(vertexCount * sizeof(SplineVertex));
		desc.Usage = D3D11_USAGE_DYNAMIC;
		desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

		if (FAILED(m_device->CreateBuffer(&desc, nullptr, m_vertexBuffer.ReleaseAndGetAddressOf()))) return false;

		m_vertexCapacity = vertexCount;
		return true;
	}
}
