#pragma once

#include <windows.h>   // UINT
#include <d2d1_1.h>    // ID2D1Factory1, ID2D1DeviceContext, ID2D1Bitmap1, ID2D1SolidColorBrush
#include <dwrite.h>    // IDWriteFactory
#include <wincodec.h>
#include <wrl/client.h>
#include <string>
#include <stdexcept>
using Microsoft::WRL::ComPtr;

struct UIRenderStruct
{
    ComPtr<ID2D1Factory1>        m_d2DFactory;
    ComPtr<ID2D1Device>          m_d2DDevice;
    ComPtr<ID2D1DeviceContext>   m_d2DdevCon;
    ComPtr<IDWriteFactory>       m_D3DWFactory;
    ComPtr<ID2D1SolidColorBrush> m_brush;
    ComPtr<IWICImagingFactory>   m_wicImageFactory;
    ComPtr<ID2D1Bitmap1>         m_d2dTargetBitmap;
    UINT m_width{ 0 };
    UINT m_height{ 0 };
};