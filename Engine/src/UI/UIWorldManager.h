#pragma once
#include <d2d1_3.h> //ID2D1Factory8,ID2D1DeviceContext7
#pragma comment(lib, "d2d1.lib")

#include <dxgi1_6.h> // IDXGIFactory7
#pragma comment(lib, "dxgi.lib")

#include <DirectXTex.h>
#include <d3d11.h>          // ID3D11Texture2D 및 핵심 인터페이스 정의
#include <dxgi.h>           // DXGI 관련 설정 (포맷, 스왑체인 등)
#include <d3dcompiler.h>    // 셰이더 컴파일이 필요한 경우
#include <filesystem>
#include <windows.h>
#include <wincodec.h>
#pragma comment(lib, "windowscodecs.lib")
#include <dwrite.h>
#pragma comment(lib, "dwrite.lib")

// 라이브러리 링크 (Pragma comment 방식)
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3dcompiler.lib")

#include <wrl/client.h>
#include <unordered_map>
#include <memory>
#include <string>



#include "Core/InputSystem.h"
#include "UIRenderStruct.h"
#include "UISceneManager.h"

// 전방 선언
struct IWICImagingFactory;

namespace Alice
{
    class ResourceManager;
}

    class UIWorldManager
    {
    public:
        UIWorldManager() = default;
        ~UIWorldManager() = default;
  
    private:
        UINT m_curWidth{ 0 }, m_curHeight{ 0 };

        UINT m_bindSlot{102}; //픽셀 셰이더 바인드 슬롯

        // gpu 관련 변수들
        Microsoft::WRL::ComPtr<ID3D11Texture2D> m_tex2D;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> m_shaderRV;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> m_RenderTV;
        Microsoft::WRL::ComPtr<ID3D11BlendState> m_pAlphaBlendState; // 알파 블렌드 상태

        //d2d 구조
        Microsoft::WRL::ComPtr<ID2D1Factory8>        m_d2DFactory;
        Microsoft::WRL::ComPtr<ID2D1Device7>          m_d2DDevice;
        Microsoft::WRL::ComPtr<ID2D1DeviceContext>   m_d2DdevCon;
        Microsoft::WRL::ComPtr<ID2D1Bitmap1>         m_d2dTargetBitmap;
        Microsoft::WRL::ComPtr<IDXGISurface>         m_dxgiSurface;

        // Text, image
        Microsoft::WRL::ComPtr<IDWriteFactory>       m_D3DWFactory;
        Microsoft::WRL::ComPtr<IWICImagingFactory>   m_wicFactory; // 이미지 파일 -> bitmap
        Microsoft::WRL::ComPtr<ID2D1SolidColorBrush> m_brush;

       
        ID3D11DeviceContext* m_devCon{ nullptr };
        
        UIRenderStruct m_RenderStruct;

        //Scene Manager 저장소 (씬 이름을 키로 사용)
        std::unordered_map<std::string, std::unique_ptr<UISceneManager>> sceneStorages;

        //Scene 정보
        std::string m_nowSceneName; // 현재 씬 이름

        //매니저
        UISceneManager* m_nowManager{ nullptr };

        //inputSystem
        Alice::InputSystem* m_inputSystem{ nullptr };

    public:
        ID3D11Device* m_d3dDev{ nullptr };
        void Initalize(ID3D11Device* pDev, ID3D11DeviceContext* pDevCon, UINT w, UINT h, Alice::InputSystem& tmpInput);
        void Update(UINT w, UINT h);
        void Render();

        //씬 매니저 선택/생성만 수행 (side-effect 없음)
        //주의: ChangeScene은 월드 데이터를 건드리지 않음. Clear는 LoadUI에서만 수행됨.
        //씬 전환 시 UI를 새로 로드하려면 LoadUI(sceneName)를 명시적으로 호출해야 함.
        void ChangeScene(const char* sceneName);

        //해당 Scene에 맞는 매니저를 반환함
        UISceneManager& GetManager() {return *m_nowManager;}

        //3D에 합성할 2D Tex 생성
        void Create2DTex(UINT w, UINT h);

        // UI 텍스처 SRV를 반환합니다 (3D 렌더러에서 합성용)
        ID3D11ShaderResourceView* GetUISRV() const { return m_shaderRV.Get(); }
        
        // UI 텍스처 RTV를 반환합니다 (D2D 렌더링 타겟용)
        ID3D11RenderTargetView* GetUIRTV() const { return m_RenderTV.Get(); }
        
        // 현재 UI 크기를 반환합니다
        UINT GetWidth() const { return m_curWidth; }
        UINT GetHeight() const { return m_curHeight; }
        
        // 렌더 구조체 참조 반환 (재초기화 시 사용)
        UIRenderStruct& GetRenderStruct() { return m_RenderStruct; }
        const UIRenderStruct& GetRenderStruct() const { return m_RenderStruct; }
        
        // UI 씬 파일 저장/로드 (ID 기반 직렬화)
        bool SaveUI(const std::filesystem::path& worldScenePath);
        bool LoadUI(const std::filesystem::path& worldScenePath, const Alice::ResourceManager* resources = nullptr);
        
        // 렌더러 준비 후 모든 UI 컴포넌트 재초기화 (초기화 순서 문제 해결)
        // renderStruct: 렌더러 포인터를 전파할 UIRenderStruct (nullptr이면 m_RenderStruct 사용)
        void ReinitializeAllUIComponents(UIRenderStruct* renderStruct = nullptr);
        
        // UI 리소스 강제 복구 (m_path는 있지만 m_texture가 null인 경우)
        void EnsureAllUIResources();
    };