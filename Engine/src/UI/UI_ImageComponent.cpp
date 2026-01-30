#include "UI_ImageComponent.h"
#include "UIBase.h"
#include "Core/ResourceManager.h"
#include "Core/Logger.h"
#include <cwchar>
#include <filesystem>

void UI_ImageComponent::Render()
{
    
    // ============================================================================
    // 4) 디버그 로그 추가 (문제 가시화)
    // ============================================================================
    if (!m_UIRenderStruct)
    {
        ALICE_LOG_WARN("[UI_ImageComponent::Render] Render skipped: renderer not ready (m_UIRenderStruct is null)");
        return;
    }
    
    if (!Owner)
    {
        ALICE_LOG_WARN("[UI_ImageComponent::Render] Render skipped: Owner is null");
        return;
    }
    
    auto tmpTransform = Owner->GetTransform();
    const D2D1::Matrix3x2F& tmpMat = tmpTransform.ConVertD2DPos();
    
   

    // 소스 영역 계산 (이미지 내부의 어느 영역을 그릴 것인가)
    m_srcRect = D2D1::RectF(
        m_srcPos.x - SrcWidthHeight.x,
        m_srcPos.y - SrcWidthHeight.y,
        m_srcPos.x + SrcWidthHeight.x,
        m_srcPos.y + SrcWidthHeight.y
    );
    
    

    m_UIRenderStruct->m_d2DdevCon->SetTransform(tmpMat);

    // 이미지가 없거나 Index가 0이면 기본 브러시로 사각형 출력
    if (m_path == L"" || !m_texture)
    {
        if (!m_path.empty() && !m_texture)
        {
            // m_path는 있지만 m_texture가 null인 경우 - 리소스 복구 시도
            EnsureResource();
        }

        m_UIRenderStruct->m_d2DdevCon->FillRectangle(m_rect, m_UIRenderStruct->m_brush.Get());
    }
    else
    {
        // 1장만 로드된 m_texture를 바로 사용
        m_UIRenderStruct->m_d2DdevCon->DrawBitmap(
            m_texture.Get(),
            m_rect,
            1.0f,
            D2D1_BITMAP_INTERPOLATION_MODE_LINEAR,
            m_srcRect
        );
        ALICE_LOG_INFO("[UI_ImageComponent::Render] DrawBitmap completed");
    }
    
}

UINT UI_ImageComponent::SetImagePath(const std::wstring& path)
{
    if (!m_UIRenderStruct)
    {
        ALICE_LOG_WARN("[UI_ImageComponent::SetImagePath] m_UIRenderStruct is null!");
        return 0;
    }

    if (!Owner)
    {
        ALICE_LOG_WARN("[UI_ImageComponent::SetImagePath] Owner is null!");
        return 0;
    }

    //ALICE_LOG_INFO("[UI_ImageComponent::SetImagePath] Called with path: %ls", path.c_str());
    m_path = path;
    
    // 경로 처리: 절대 경로인 경우 그대로 사용
    std::filesystem::path inputPath(path);
    

    //bool isAbsolute = false;
    //size_t pathLen = path.length();
    
    std::filesystem::path resolvedPath;
    

    resolvedPath = inputPath;
    ALICE_LOG_INFO("[UI_ImageComponent::SetImagePath] Absolute path detected, using as-is: %ls", resolvedPath.wstring().c_str());


    // WIC API는 wide string을 요구하므로 그대로 사용
    std::wstring resolvedPathW = resolvedPath.wstring();
    
    Microsoft::WRL::ComPtr<IWICBitmapDecoder> decoder;
    Microsoft::WRL::ComPtr<IWICBitmapFrameDecode> frame;
    Microsoft::WRL::ComPtr<IWICFormatConverter> converter;

    // WIC 디코더 생성 (해석된 경로 사용)
    HRESULT hr = m_UIRenderStruct->m_wicImageFactory.Get()->CreateDecoderFromFilename(
        resolvedPathW.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnLoad, decoder.GetAddressOf());


    //ALICE_LOG_INFO("WIC final wide path='%ls' len=%zu", resolvedPathW.c_str(), resolvedPathW.size());

    if (FAILED(hr))
    {
        ALICE_LOG_WARN("[UI_ImageComponent::SetImagePath] CreateDecoderFromFilename failed: %ls (resolved: %ls, HRESULT=0x%08X)", 
            path.c_str(), resolvedPathW.c_str(), hr);
        return 0;
    }

    hr = decoder->GetFrame(0, frame.GetAddressOf());
    if (FAILED(hr)) return 0;

    UINT imgWidth = 0, imgHeight = 0;
    frame->GetSize(&imgWidth, &imgHeight);

    // 사이즈 및 중점 설정
    auto& tmpTransform = Owner->GetTransform();
    m_size = { (float)imgWidth, (float)imgHeight };
    tmpTransform.m_size = m_size;
    m_srcPos = { (float)imgWidth / 2.0f, (float)imgHeight / 2.0f };
    SrcWidthHeight = m_srcPos;

    // 포맷 컨버터 초기화 (32bppPBGRA 필수)
    hr = m_UIRenderStruct->m_wicImageFactory.Get()->CreateFormatConverter(converter.GetAddressOf());
    if (FAILED(hr)) return 0;

    hr = converter->Initialize(
        frame.Get(),
        GUID_WICPixelFormat32bppPBGRA,
        WICBitmapDitherTypeNone,
        nullptr,
        0.0f,
        WICBitmapPaletteTypeCustom);
    if (FAILED(hr)) return 0;

    D2D1_BITMAP_PROPERTIES1 bmpProps = D2D1::BitmapProperties1(
        D2D1_BITMAP_OPTIONS_NONE,
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));

    // 기존 텍스처 초기화 후 새 비트맵 생성
    m_texture.Reset();
    hr = m_UIRenderStruct->m_d2DdevCon->CreateBitmapFromWicBitmap(converter.Get(), &bmpProps, &m_texture);

    if (SUCCEEDED(hr)) {
        m_nowIndex = 1; // 로드 성공 시 1번 인덱스로 간주
    }

    return m_nowIndex;
}


void UI_ImageComponent::Initalize(UIRenderStruct& UIRenderStruct) {
    m_UIRenderStruct = &UIRenderStruct;
    // 초기 rect 계산 (Owner가 있을 경우)
    if (Owner)
    {
        auto tmpTransform = Owner->GetTransform();
        m_size = tmpTransform.m_size;
        m_pivot = tmpTransform.m_pivot;
        CalRect();
    }
}

void UI_ImageComponent::Update() {
    auto tmpTransform = Owner->GetTransform();
    m_size = tmpTransform.m_size;
    m_pivot = tmpTransform.m_pivot;
    CalRect();
    Owner->m_rect = m_rect; // 오너에 갱신된 m_rect 값 추출
}

void UI_ImageComponent::CalRect()
{
    // 피벗을 기준으로 로컬 좌표계 상의 사각형 영역 계산
    float px = m_size.x * m_pivot.x;
    float py = m_size.y * m_pivot.y;
    m_rect = D2D1::RectF(-px, -py, m_size.x - px, m_size.y - py);
}

// ============================================================================
// UI 리소스 강제 복구 (m_path는 있지만 m_texture가 null인 경우), 커밋용 로그 주석처리
// ============================================================================
bool UI_ImageComponent::EnsureResource()
{
    // 렌더러가 준비되지 않은 경우
    if (!m_UIRenderStruct)
    {
        //ALICE_LOG_WARN("[UI_ImageComponent::EnsureResource] m_UIRenderStruct is null, cannot recover");
        return false;
    }
    
    // Owner가 없는 경우
    if (!Owner)
    {
        //ALICE_LOG_WARN("[UI_ImageComponent::EnsureResource] Owner is null, cannot recover");
        return false;
    }
    
    // 경로가 없는 경우
    if (m_path.empty())
    {
        //ALICE_LOG_INFO("[UI_ImageComponent::EnsureResource] m_path is empty, nothing to recover");
        return false;
    }
    
    // 텍스처가 이미 있는 경우
    if (HasTexture())
    {
        //ALICE_LOG_INFO("[UI_ImageComponent::EnsureResource] Texture already exists, no recovery needed");
        return true;
    }
    
    // 리소스 복구 시도
    //ALICE_LOG_INFO("[UI_ImageComponent::EnsureResource] Recovering UI Image resource: path=%S", m_path.c_str());
    UINT result = SetImagePath(m_path);
    
    if (result > 0)
    {
        //ALICE_LOG_INFO("[UI_ImageComponent::EnsureResource] UI Image recovered successfully: path=%S", m_path.c_str());
        return true;
    }
    else
    {
        //ALICE_LOG_WARN("[UI_ImageComponent::EnsureResource] UI Image recovery failed: path=%S", m_path.c_str());
        return false;
    }
}