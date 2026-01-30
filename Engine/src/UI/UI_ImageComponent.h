#pragma once
#include "IUIComponent.h"

#include <vector>
#include <string>
#include <windows.h>
#include <wincodec.h>     // IWICImagingFactory
#include <d2d1_1.h>       // ID2D1DeviceContext, ID2D1Bitmap
#include <d2d1helper.h>
#include <wrl/client.h>   // ComPtr 사용 시
#include <dwrite.h>      // (이미 쓰고 있으면)
#include <DirectXMath.h>
#include "UIRenderstruct.h"
#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "windowscodecs.lib")
#include "UITransform.h"

using namespace Microsoft::WRL;
using namespace DirectX;

class UI_ImageComponent : public IUIComponent
{
private:
    ComPtr<ID2D1Bitmap1> m_texture; // 단일 텍스처로 변경
    std::wstring m_path{L""};            // 단일 경로
    
    // 텍스처 존재 여부 확인 (EnsureResource에서 사용)
    bool HasTexture() const { return m_texture != nullptr; }
    

public:
    // 복사/이동 방지 (컴포넌트는 unique_ptr로 관리되므로 복사/이동 불필요)
    UI_ImageComponent() = default;
    ~UI_ImageComponent() = default;
    UI_ImageComponent(const UI_ImageComponent&) = delete;
    UI_ImageComponent& operator=(const UI_ImageComponent&) = delete;
    UI_ImageComponent(UI_ImageComponent&&) = delete;
    UI_ImageComponent& operator=(UI_ImageComponent&&) = delete;
    
public:
    XMFLOAT2 m_size{ 100, 100 };
    XMFLOAT2 m_srcPos{ 50, 50 };
    XMFLOAT2 SrcWidthHeight{ 100, 100 };
    XMFLOAT2 m_pivot{ 0.5f, 0.5f };
    UINT m_nowIndex = 0;            // 0이면 기본 사각형, 1이면 이미지 출력
    D2D1_RECT_F m_srcRect = D2D1::RectF(0, 0, 100, 100);
    D2D1_RECT_F m_rect = D2D1::RectF(0, 0, 100, 100);

    UIRenderStruct* m_UIRenderStruct{ nullptr };

    void Initalize(UIRenderStruct& UIRenderStruct);

    void Update() override;

    void Render(); // Index 인자 제거
    UINT SetImagePath(const std::wstring& path);
    const std::wstring& GetImagePath() const { return m_path; }
    void CalRect();
    
    // 리소스 강제 복구 (m_path는 있지만 m_texture가 null인 경우)
    bool EnsureResource();

};