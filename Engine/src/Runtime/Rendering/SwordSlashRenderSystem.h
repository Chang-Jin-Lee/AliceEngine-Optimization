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
	/// 쉐이더 기반 검기 렌더링 시스템
	/// - TrailEffect 스크립트와 연동
	/// - Catmull-Rom 스플라인을 라인 스트립으로 렌더링
	class SwordSlashRenderSystem
	{
	public:
		explicit SwordSlashRenderSystem(ID3D11RenderDevice& renderDevice);
		~SwordSlashRenderSystem() = default;

		/// 셰이더, 버퍼 등을 초기화합니다.
		bool Initialize();

		/// World에서 SwordSlashEffect를 가진 모든 엔티티를 렌더링합니다.
		void Render(const World& world, const Camera& camera);

	private:
		struct SplineVertex
		{
			DirectX::XMFLOAT3 position;
			float alpha;
		};

		struct CBPerSwordSlash
		{
			DirectX::XMMATRIX viewProj;
			DirectX::XMFLOAT3 color;
			float thickness;
		};

		bool CreateShadersAndInputLayout();
		bool EnsureVertexBufferSize(std::size_t vertexCount);

	private:
		ID3D11RenderDevice& m_renderDevice;

		Microsoft::WRL::ComPtr<ID3D11Device>        m_device;
		Microsoft::WRL::ComPtr<ID3D11DeviceContext> m_context;

		Microsoft::WRL::ComPtr<ID3D11Buffer>        m_vertexBuffer;
		Microsoft::WRL::ComPtr<ID3D11Buffer>        m_cbPerSwordSlash;
		Microsoft::WRL::ComPtr<ID3D11VertexShader>  m_vertexShader;
		Microsoft::WRL::ComPtr<ID3D11PixelShader>   m_pixelShader;
		Microsoft::WRL::ComPtr<ID3D11InputLayout>   m_inputLayout;
		Microsoft::WRL::ComPtr<ID3D11BlendState>     m_blendState;

		std::size_t m_vertexCapacity{ 0 };
	};
}
