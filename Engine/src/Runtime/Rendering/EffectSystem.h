#pragma once

#include <vector>
#include <wrl/client.h>
#include <d3d11.h>
#include <DirectXMath.h>

#include "Runtime/Rendering/Camera.h"
#include "Runtime/Rendering/D3D11/ID3D11RenderDevice.h"
#include "Runtime/ECS/World.h"

namespace Alice
{
	/// 이펙트 시스템 (반달 모양 이펙트 렌더링)
	class EffectSystem
	{
	public:
		explicit EffectSystem(ID3D11RenderDevice& renderDevice);
		~EffectSystem() = default;

		/// 셰이더, 버퍼 등을 초기화합니다.
		bool Initialize();

		/// World에서 EffectComponent를 가진 모든 엔티티를 렌더링합니다.
		void Render(const World& world, const Camera& camera);

	private:
		struct EffectVertex
		{
			DirectX::XMFLOAT3 position;
			DirectX::XMFLOAT4 color;
		};

		struct CBPerEffect
		{
			DirectX::XMMATRIX world;
			DirectX::XMMATRIX viewProj;
			DirectX::SimpleMath::Vector4 color;
		};

		bool CreateShadersAndInputLayout();
		bool CreateCrescentMesh(); // 반달 모양 메시 생성

	private:
		ID3D11RenderDevice& m_renderDevice;

		Microsoft::WRL::ComPtr<ID3D11Device>        m_device;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;

		Microsoft::WRL::ComPtr<ID3D11Buffer>        m_vertexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer>        m_indexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer>        m_cbPerEffect;
		Microsoft::WRL::ComPtr<ID3D11VertexShader>  m_vertexShader;
		Microsoft::WRL::ComPtr<ID3D11PixelShader>   m_pixelShader;
		Microsoft::WRL::ComPtr<ID3D11InputLayout>   m_inputLayout;
		Microsoft::WRL::ComPtr<ID3D11BlendState>     m_blendState; // 알파 블렌딩용

		UINT m_vertexCount{ 0 };
		UINT m_indexCount{ 0 };
	};
}
