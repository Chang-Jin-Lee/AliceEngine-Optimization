#pragma once

// Windows API에서 제공하는 HWND 타입을 사용하기 위해 필요
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <cstdint>

namespace Alice
{
    /// 렌더 디바이스가 제공해야 하는 최소한의 인터페이스입니다.
    /// - Direct3D11, 이후 다른 렌더러(OpenGL, Vulkan 등)로도 교체 가능하도록 설계합니다.
    class IRenderDevice
    {
    public:
        virtual ~IRenderDevice() = default;

        /// 디바이스와 스왑체인, 기본 렌더 타깃을 초기화합니다.
        /// \param window 렌더링을 수행할 윈도우 핸들
        /// \param width  초기 백버퍼 너비
        /// \param height 초기 백버퍼 높이
        virtual bool Initialize(HWND window, std::uint32_t width, std::uint32_t height) = 0;

        /// 윈도우 크기가 변경되었을 때 호출됩니다.
        virtual void Resize(std::uint32_t width, std::uint32_t height) = 0;

        /// 한 프레임의 렌더링을 시작합니다.
        /// \param clearColor 렌더 타깃을 지울 색상 (RGBA)
        virtual void BeginFrame(const float clearColor[4]) = 0;

        /// 한 프레임의 렌더링을 종료하고 화면에 표시합니다.
        virtual void EndFrame() = 0;
    };
}


