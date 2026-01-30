#include "Rendering/EffectSystem.h"
#include "Components/EffectComponent.h"
#include "Components/TransformComponent.h"

#include <d3dcompiler.h>
#include <cmath>
#include <Core/Logger.h>

using namespace DirectX;
using Microsoft::WRL::ComPtr;

namespace Alice
{
	namespace
	{
		// 이펙트용 셰이더
		const char* g_EffectVS = R"(
cbuffer CBPerEffect : register(b0)
{
    float4x4 gWorld;
    float4x4 gViewProj;
    float4   gColor;
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
    float4 worldPos = mul(float4(input.Position, 1.0f), gWorld);
    output.Position = mul(worldPos, gViewProj);
    output.Color    = gColor;
    return output;
}
)";

		const char* g_EffectPS = R"(
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

	EffectSystem::EffectSystem(ID3D11RenderDevice& renderDevice)
		: m_renderDevice(renderDevice)
	{
		m_device = m_renderDevice.GetDevice();
		m_context = m_renderDevice.GetImmediateContext();
	}

	bool EffectSystem::Initialize()
	{
		// [1] 진입 확인
		ALICE_LOG_INFO("EffectSystem::Initialize: Begin");

		if (!m_device || !m_context)
		{
			ALICE_LOG_ERRORF("EffectSystem::Initialize: Device or Context is null.");
			return false;
		}

		// [2] 쉐이더 생성 시도
		ALICE_LOG_INFO("EffectSystem::Initialize: Calling CreateShadersAndInputLayout...");

		// 가장 유력한 용의자 1: 여기서 쉐이더 컴파일 에러나면 예외 던지고 죽을 수 있음
		if (!CreateShadersAndInputLayout())
		{
			ALICE_LOG_ERRORF("EffectSystem::Initialize: CreateShadersAndInputLayout failed.");
			return false;
		}
		ALICE_LOG_INFO("EffectSystem::Initialize: Shaders Created.");

		// [3] 메쉬 생성 시도
		ALICE_LOG_INFO("EffectSystem::Initialize: Calling CreateCrescentMesh...");

		// 유력한 용의자 2: 버퍼 생성 실패
		if (!CreateCrescentMesh())
		{
			ALICE_LOG_ERRORF("EffectSystem::Initialize: CreateCrescentMesh failed.");
			return false;
		}
		ALICE_LOG_INFO("EffectSystem::Initialize: Mesh Created.");

		// [4] 상수 버퍼 생성 시도
		ALICE_LOG_INFO("EffectSystem::Initialize: Creating Constant Buffer...");
		D3D11_BUFFER_DESC desc = { sizeof(CBPerEffect), D3D11_USAGE_DEFAULT, D3D11_BIND_CONSTANT_BUFFER, 0, 0, 0 };

		HRESULT hr = m_device->CreateBuffer(&desc, nullptr, m_cbPerEffect.ReleaseAndGetAddressOf());
		if (FAILED(hr))
		{
			ALICE_LOG_ERRORF("EffectSystem::Initialize: m_device->CreateBuffer fail... HR=0x%08X", hr);
			return false;
		}

		// [5] 블렌드 스테이트 생성 시도
		ALICE_LOG_INFO("EffectSystem::Initialize: Creating Blend State...");
		D3D11_BLEND_DESC blendDesc = {};
		blendDesc.RenderTarget[0].BlendEnable = TRUE;
		blendDesc.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		blendDesc.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		blendDesc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		blendDesc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
		blendDesc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		blendDesc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;

		hr = m_device->CreateBlendState(&blendDesc, m_blendState.ReleaseAndGetAddressOf());
		if (FAILED(hr))
		{
			ALICE_LOG_ERRORF("EffectSystem::Initialize: m_device->CreateBlendState fail... HR=0x%08X", hr);
			return false;
		}

		// [6] 최종 성공
		ALICE_LOG_INFO("EffectSystem::Initialize: Success.");
		return true;
	}


	void EffectSystem::Render(const World& world, const Camera& camera)
	{
		if (!m_vertexShader || !m_pixelShader || !m_inputLayout || !m_vertexBuffer || !m_indexBuffer) return;

		XMMATRIX view = camera.GetViewMatrix();
		XMMATRIX proj = camera.GetProjectionMatrix();
		XMMATRIX viewProj = view * proj;

		// 파이프라인 설정
		UINT stride = sizeof(EffectVertex), offset = 0;
		ID3D11Buffer* vb = m_vertexBuffer.Get();

		m_context->IASetVertexBuffers(0, 1, &vb, &stride, &offset);
		m_context->IASetIndexBuffer(m_indexBuffer.Get(), DXGI_FORMAT_R16_UINT, 0);
		m_context->IASetInputLayout(m_inputLayout.Get());
		m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

		m_context->VSSetShader(m_vertexShader.Get(), nullptr, 0);
		m_context->VSSetConstantBuffers(0, 1, m_cbPerEffect.GetAddressOf());
		m_context->PSSetShader(m_pixelShader.Get(), nullptr, 0);

		// 알파 블렌딩 활성화
		float blendFactor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
		m_context->OMSetBlendState(m_blendState.Get(), blendFactor, 0xffffffff);

		// EffectComponent를 가진 모든 엔티티 렌더링
		const auto& effects = world.GetComponents<EffectComponent>();
		for (const auto& [entityId, effectComp] : effects)
		{
			if (!effectComp.enabled) continue;

			const TransformComponent* transform = world.GetComponent<TransformComponent>(entityId);
			if (!transform || !transform->enabled || !transform->visible) continue;

			// 월드 행렬 계산 (S * R * T)
			XMVECTOR S = XMLoadFloat3(&transform->scale);
			XMVECTOR R = XMLoadFloat3(&transform->rotation);
			XMVECTOR T = XMLoadFloat3(&transform->position);
			XMMATRIX worldM = XMMatrixScalingFromVector(S) * XMMatrixRotationRollPitchYawFromVector(R) * XMMatrixTranslationFromVector(T);

			// 크기 스케일 적용 (EffectComponent의 size 속성 사용)
			XMMATRIX scaleM = XMMatrixScaling(effectComp.size, effectComp.size, effectComp.size);
			worldM = worldM * scaleM;

			// 상수 버퍼 업데이트
			CBPerEffect cb;
			cb.world = XMMatrixTranspose(worldM);
			cb.viewProj = XMMatrixTranspose(viewProj);
			cb.color = DirectX::SimpleMath::Vector4(effectComp.color.x, effectComp.color.y, effectComp.color.z, effectComp.alpha);
			m_context->UpdateSubresource(m_cbPerEffect.Get(), 0, nullptr, &cb, 0, 0);

			m_context->DrawIndexed(m_indexCount, 0, 0);
		}

		// 블렌드 스테이트 복원
		m_context->OMSetBlendState(nullptr, nullptr, 0xffffffff);
	}

	bool EffectSystem::CreateShadersAndInputLayout()
	{
		ComPtr<ID3DBlob> vsBlob, psBlob;

		// 1. VS 컴파일 및 생성
		if (FAILED(D3DCompile(g_EffectVS, std::strlen(g_EffectVS), nullptr, nullptr, nullptr, "main", "vs_5_0", 0, 0, vsBlob.GetAddressOf(), nullptr))) return false;
		if (FAILED(m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, m_vertexShader.ReleaseAndGetAddressOf()))) return false;

		// 2. PS 컴파일 및 생성
		if (FAILED(D3DCompile(g_EffectPS, std::strlen(g_EffectPS), nullptr, nullptr, nullptr, "main", "ps_5_0", 0, 0, psBlob.GetAddressOf(), nullptr))) return false;
		if (FAILED(m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, m_pixelShader.ReleaseAndGetAddressOf()))) return false;

		// 3. Input Layout 생성
		D3D11_INPUT_ELEMENT_DESC desc[] = {
			{ "POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT,    0, 0,                            D3D11_INPUT_PER_VERTEX_DATA, 0 },
			{ "COLOR",    0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, D3D11_APPEND_ALIGNED_ELEMENT, D3D11_INPUT_PER_VERTEX_DATA, 0 },
		};

		if (FAILED(m_device->CreateInputLayout(desc, (UINT)std::size(desc), vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), m_inputLayout.ReleaseAndGetAddressOf()))) return false;

		return true;
	}

	bool EffectSystem::CreateCrescentMesh()
	{
		// 반달 모양 메시 생성
		// 외부 반원과 내부 반원을 이용하여 반달 모양 생성
		const int segmentCount = 32; // 반원의 세그먼트 수 (더 많으면 더 부드러움)
		const float outerRadius = 1.0f; // 외부 반지름
		const float innerRadius = 0.5f; // 내부 반지름 (반달의 두께 결정)

		std::vector<EffectVertex> vertices;
		std::vector<uint16_t> indices;

		// 중심점 추가 (선택적 - 반달의 양 끝점을 연결)
		// 반달은 외부 호와 내부 호로 구성

		// 외부 호와 내부 호의 버텍스 생성 (XZ 평면에 반원)
		// 각도 범위: -90도 ~ 90도 (왼쪽에서 오른쪽으로)
		for (int i = 0; i <= segmentCount; ++i)
		{
			float angle = (i / float(segmentCount) - 0.5f) * XM_PI; // -PI/2 ~ PI/2
			float cosA = std::cos(angle);
			float sinA = std::sin(angle);

			// 외부 점
			XMFLOAT3 outerPos(outerRadius * cosA, 0.0f, outerRadius * sinA);
			// 내부 점
			XMFLOAT3 innerPos(innerRadius * cosA, 0.0f, innerRadius * sinA);

			// 색상은 상수 버퍼로 전달되므로 여기서는 기본값 사용
			XMFLOAT4 color(1.0f, 1.0f, 1.0f, 1.0f);

			vertices.push_back({ outerPos, color });
			vertices.push_back({ innerPos, color });
		}

		// 인덱스 생성 (삼각형 스트립 방식)
		for (int i = 0; i < segmentCount; ++i)
		{
			int base = i * 2;
			// 첫 번째 삼각형
			indices.push_back(base);
			indices.push_back(base + 1);
			indices.push_back(base + 2);
			// 두 번째 삼각형
			indices.push_back(base + 1);
			indices.push_back(base + 3);
			indices.push_back(base + 2);
		}

		m_vertexCount = (UINT)vertices.size();
		m_indexCount = (UINT)indices.size();

		// Vertex Buffer 생성
		D3D11_BUFFER_DESC vbDesc = {};
		vbDesc.ByteWidth = (UINT)(sizeof(EffectVertex) * vertices.size());
		vbDesc.Usage = D3D11_USAGE_DEFAULT;
		vbDesc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
		D3D11_SUBRESOURCE_DATA vbData = {};
		vbData.pSysMem = vertices.data();
		if (FAILED(m_device->CreateBuffer(&vbDesc, &vbData, m_vertexBuffer.ReleaseAndGetAddressOf()))) return false;

		// Index Buffer 생성
		D3D11_BUFFER_DESC ibDesc = {};
		ibDesc.ByteWidth = (UINT)(sizeof(uint16_t) * indices.size());
		ibDesc.Usage = D3D11_USAGE_DEFAULT;
		ibDesc.BindFlags = D3D11_BIND_INDEX_BUFFER;
		D3D11_SUBRESOURCE_DATA ibData = {};
		ibData.pSysMem = indices.data();
		if (FAILED(m_device->CreateBuffer(&ibDesc, &ibData, m_indexBuffer.ReleaseAndGetAddressOf()))) return false;

		return true;
	}
}
