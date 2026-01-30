#pragma once

#include "Rendering/IRenderDevice.h"

// DirectX 11 전방 선언 (헤더 의존성 최소화)
struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11RenderTargetView;
struct ID3D11DepthStencilView;

namespace Alice
{
    /// Direct3D11 전용 렌더 디바이스 인터페이스입니다.
    /// - IRenderDevice 위에 DX11 타입을 노출하기 위한 얇은 계층입니다.
    struct ID3D11RenderDevice : public IRenderDevice
    {
        virtual ~ID3D11RenderDevice() = default;

        /// 내부 D3D11 디바이스 포인터를 반환합니다.
        virtual ID3D11Device* GetDevice() = 0;

        /// 내부 D3D11 디바이스 컨텍스트 포인터를 반환합니다.
        virtual ID3D11DeviceContext* GetImmediateContext() = 0;

        /// 기본 백버퍼 렌더 타깃 뷰를 반환합니다.
        /// - 렌더 타깃을 임시로 다른 텍스처로 바꿨다가, 다시 기본 백버퍼로 복원할 때 사용합니다.
        virtual ID3D11RenderTargetView* GetBackBufferRTV() = 0;

        /// 기본 깊이 스텐실 뷰를 반환합니다.
        virtual ID3D11DepthStencilView* GetBackBufferDSV() = 0;

		/// 비디오 메모리를 트림(해제)합니다.
		virtual void TrimVideoMemory() = 0;

        /// HDR 지원 여부를 확인하고 최대 밝기(nits)를 반환합니다.
        /// @param outMaxNits 모니터 최대 밝기 (nits)
        /// @return HDR 지원 여부
        virtual bool IsHDRSupported(float& outMaxNits) const = 0;

        /// 백버퍼 포맷을 반환합니다.
        /// @return DXGI_FORMAT_R8G8B8A8_UNORM (LDR) 또는 DXGI_FORMAT_R10G10B10A2_UNORM (HDR)
        virtual DXGI_FORMAT GetBackBufferFormat() const = 0;
    };
}


