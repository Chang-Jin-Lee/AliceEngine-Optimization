#pragma once

#include <vector>
#include <wrl/client.h>
#include <d3d11.h>
#include <DirectXMath.h>
#include <string>

#include "Rendering/Camera.h"
#include "Rendering/D3D11/ID3D11RenderDevice.h"
#include "Core/World.h"

namespace Alice
{
	class ResourceManager;
	
	/// 검기 이펙트 시스템 (트레일 리본 렌더링)
	class TrailEffectRenderSystem
	{
	public:
		TrailEffectRenderSystem() = default;
		explicit TrailEffectRenderSystem(ID3D11RenderDevice& renderDevice);
		~TrailEffectRenderSystem() = default;

		/// 셰이더, 버퍼 등을 초기화합니다.
		bool Initialize();

		/// 리소스 매니저를 주입합니다.
		void SetResourceManager(ResourceManager* resources) { m_resources = resources; }

		/// World에서 SwordEffectComponent를 가진 모든 엔티티를 렌더링합니다.
		void Render(const World& world, const Camera& camera);

	private:
		struct TrailVertex
		{
			DirectX::XMFLOAT3 position;
			DirectX::XMFLOAT2 texCoord;
			float birthTime;
		};

		struct CBPerSwordEffectVS
		{
			DirectX::XMMATRIX viewProj;     // View * Projection 행렬
			DirectX::XMFLOAT3 cameraPos;    // 카메라 위치 (월드 좌표)
			float padding0;
			float currentTime;
			float fadeDuration;
			float width;                    // 트레일의 기본 폭
			float padding1;
		};


		struct CBPerSwordEffectPS
		{
			DirectX::XMFLOAT3 color;
			float fadeDuration;
			float width;                   // 트레일의 기본 폭
			float padding0;
			float padding1;
			float padding2;
		};

		bool CreateShadersAndInputLayout();
		bool EnsureVertexBufferSize(std::size_t vertexCount);
		bool LoadTexture();
		bool CreateDefaultTexture(); // 텍스처 로드 실패 시 기본 1x1 흰색 텍스처 생성

	private:
		ID3D11RenderDevice& m_renderDevice;
		ResourceManager* m_resources{ nullptr };

		Microsoft::WRL::ComPtr<ID3D11Device>        m_device;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;

		Microsoft::WRL::ComPtr<ID3D11Buffer>        m_vertexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer>        m_cbPerSwordEffectVS;
		Microsoft::WRL::ComPtr<ID3D11Buffer>        m_cbPerSwordEffectPS;
		Microsoft::WRL::ComPtr<ID3D11VertexShader>  m_vertexShader;
		Microsoft::WRL::ComPtr<ID3D11PixelShader>   m_pixelShader;
		Microsoft::WRL::ComPtr<ID3D11InputLayout>   m_inputLayout;
		Microsoft::WRL::ComPtr<ID3D11BlendState>     m_blendState; // 알파 블렌딩용
		Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_textureSRV; // Hanako.png 텍스처
		Microsoft::WRL::ComPtr<ID3D11SamplerState> m_samplerState;

		std::size_t m_vertexCapacity{ 0 };
	};
}
