#pragma once

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdint>
#include <wrl/client.h>
#include <d3d11.h>

#include "Rendering/D3D11/ID3D11RenderDevice.h"

namespace Alice
{
    /// Direct3D 11 기반 렌더 디바이스 구현체입니다.
    /// - ID3D11RenderDevice 인터페이스를 구현합니다.
    class D3D11RenderDevice final : public ID3D11RenderDevice
    {
    public:
        D3D11RenderDevice() = default;
        ~D3D11RenderDevice() override = default;

        bool Initialize(HWND window, std::uint32_t width, std::uint32_t height) override;
        void Resize(std::uint32_t width, std::uint32_t height) override;
        void BeginFrame(const float clearColor[4]) override;
        void EndFrame() override;

        ID3D11Device* GetDevice() override { return m_device.Get(); }
        ID3D11DeviceContext* GetImmediateContext() override { return m_immediateContext.Get(); }
        ID3D11RenderTargetView* GetBackBufferRTV() override { return m_renderTargetView.Get(); }
        ID3D11DepthStencilView* GetBackBufferDSV() override { return m_depthStencilView.Get(); }
        void TrimVideoMemory();
        bool IsHDRSupported(float& outMaxNits) const override;
        DXGI_FORMAT GetBackBufferFormat() const override { return m_backBufferFormat; }

    private:
        using ComPtr = Microsoft::WRL::ComPtr<IUnknown>;

        // 내부에서 사용할 실제 타입별 ComPtr 별칭
        Microsoft::WRL::ComPtr<ID3D11Device>           m_device;
        Microsoft::WRL::ComPtr<ID3D11DeviceContext>    m_immediateContext;
        Microsoft::WRL::ComPtr<IDXGISwapChain>         m_swapChain;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_renderTargetView;
        Microsoft::WRL::ComPtr<ID3D11Texture2D>        m_depthStencil;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilView> m_depthStencilView;
        Microsoft::WRL::ComPtr<ID3D11DepthStencilState> m_depthStencilState;
        Microsoft::WRL::ComPtr<ID3D11RasterizerState>  m_rasterizerState;

        std::uint32_t m_width  = 0;
        std::uint32_t m_height = 0;
        DXGI_FORMAT m_backBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM; // 기본값: LDR
        float m_maxHDRNits = 100.0f; // 기본값: SDR

        /// 백버퍼로부터 렌더 타깃 뷰를 생성합니다.
        bool CreateRenderTarget();

        /// 깊이/스텐실 버퍼와 뷰를 생성합니다.
        bool CreateDepthStencil(std::uint32_t width, std::uint32_t height);

        /// 현재 렌더 타깃 기준으로 뷰포트를 설정합니다.
        void SetupViewport();
    };
}


