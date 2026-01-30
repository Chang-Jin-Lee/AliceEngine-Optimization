#include "UIWorldManager.h"
#include <d2d1_3.h> //ID2D1Factory8,ID2D1DeviceContext7
#pragma comment(lib, "d2d1.lib")

#include <dxgi1_6.h> // IDXGIFactory7
#pragma comment(lib, "dxgi.lib")



#include <dxgi1_2.h>
#include <d2d1_1.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <string>
#include <stdexcept>




#define IMGUI_IMPL_API 
#include "imgui.h"


#include "Core/Helper.h"
#include "Core/JsonRttr.h"
#include "Core/Logger.h"
#include "Core/ResourceManager.h"
#include "UIRenderStruct.h"
#include "UISceneManager.h"
#include "UIBase.h"
#include "UITransform.h"
#include "UI_ImageComponent.h"
#include "UI_ScriptComponent.h"
#include "UIImage.h"
#include <filesystem>
#include <fstream>
#include <functional>
#include <unordered_map>



//UIWorldManager::~UIWorldManager()
//{
//    m_nowManager = nullptr;
//
//    sceneStorages.clear();
//
//    if (m_d2DdevCon)
//    {
//        //드로잉 종료 보장
//        m_d2DdevCon->SetTarget(nullptr);
//        m_d2DdevCon->Flush();
//    }
//
//    //D2D 리소스 (자식 → 부모 순)
//    m_d2dTargetBitmap.Reset();
//    m_brush.Reset();
//
//    //DXGI 리소스
//    m_dxgiSurface.Reset();
//
//    //D2D Core
//    m_d2DdevCon.Reset();
//    m_d2DDevice.Reset();
//    m_d2DFactory.Reset();
//
//    //리소스
//    m_shaderRV.Reset();
//    m_RenderTV.Reset();
//    m_tex2D.Reset();
//
//    //WIC
//    m_wicFactory.Reset();
//}

// inputSystem은 추후에 싱글톤인 경우 SceneManager에서 변경하기
void UIWorldManager::Initalize(ID3D11Device* pDev, ID3D11DeviceContext* pDevCon, UINT w, UINT h, Alice::InputSystem& tmpInput)
{
	ALICE_LOG_INFO("[UIWorld] Initialize Start");

	m_d3dDev = pDev;
	m_devCon = pDevCon;

	// 1. 3D Texture 생성 (여기서 만드는 텍스처 포맷이 B8G8R8A8 인지 확인 필요)
	ALICE_LOG_INFO("[UIWorld] Calling Create2DTex...");
	Create2DTex(w, h);
	ALICE_LOG_INFO("[UIWorld] Create2DTex Success");

	HRESULT hr = S_OK;

	// 2. D2D Factory 생성
	ALICE_LOG_INFO("[UIWorld] Creating D2D Factory...");
	D2D1_FACTORY_OPTIONS options = {};
	// 디버그 레이어 활성화 (렌더독 등에서 도움됨)
#if defined(_DEBUG)
	options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif

	hr = D2D1CreateFactory(
		D2D1_FACTORY_TYPE_SINGLE_THREADED,
		__uuidof(ID2D1Factory8),
		&options,
		reinterpret_cast<void**>(m_d2DFactory.GetAddressOf())
	);
	if (FAILED(hr)) { ALICE_LOG_ERRORF("[UIWorld] D2D1CreateFactory Failed. HR=0x%08X", hr); return; }

	// 3. DXGI Device 가져오기
	ALICE_LOG_INFO("[UIWorld] QueryInterface DXGI Device...");
	Microsoft::WRL::ComPtr<IDXGIDevice> dxgiDevice;
	hr = m_d3dDev->QueryInterface(IID_PPV_ARGS(dxgiDevice.GetAddressOf()));
	if (FAILED(hr)) { ALICE_LOG_ERRORF("[UIWorld] QI DXGI Failed. HR=0x%08X", hr); return; }

	// 4. D2D Device 생성
	ALICE_LOG_INFO("[UIWorld] Creating D2D Device...");
	hr = m_d2DFactory->CreateDevice((dxgiDevice.Get()), m_d2DDevice.GetAddressOf());
	if (FAILED(hr)) { ALICE_LOG_ERRORF("[UIWorld] CreateDevice Failed. HR=0x%08X", hr); return; }

	// 5. D2D Device Context 생성
	ALICE_LOG_INFO("[UIWorld] Creating D2D Device Context...");
	hr = m_d2DDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, m_d2DdevCon.GetAddressOf());
	if (FAILED(hr)) { ALICE_LOG_ERRORF("[UIWorld] CreateDeviceContext Failed. HR=0x%08X", hr); return; }

	// 6. DWrite Factory
	ALICE_LOG_INFO("[UIWorld] Creating DWrite Factory...");
	hr = DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory),
		reinterpret_cast<IUnknown**>(m_D3DWFactory.GetAddressOf()));
	if (FAILED(hr)) { ALICE_LOG_ERRORF("[UIWorld] DWriteCreateFactory Failed. HR=0x%08X", hr); return; }

	// 7. Brush 생성
	ALICE_LOG_INFO("[UIWorld] Creating Brush...");
	hr = m_d2DdevCon->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::DeepSkyBlue, 0.5f), &m_brush);
	if (FAILED(hr)) { ALICE_LOG_ERRORF("[UIWorld] CreateSolidColorBrush Failed. HR=0x%08X", hr); return; }

	// 8. Texture -> DXGI Surface 변환
	ALICE_LOG_INFO("[UIWorld] Texture As DXGI Surface...");
	if (!m_tex2D) { ALICE_LOG_ERRORF("[UIWorld] m_tex2D is NULL! Check Create2DTex."); return; }

	hr = m_tex2D.As(&m_dxgiSurface);
	if (FAILED(hr)) { ALICE_LOG_ERRORF("[UIWorld] m_tex2D.As(DXGISurface) Failed. HR=0x%08X (Format mismatch?)", hr); return; }

	// 9. WIC Factory (COM 초기화 필수)
	ALICE_LOG_INFO("[UIWorld] Creating WIC Factory...");
	hr = CoCreateInstance(
		CLSID_WICImagingFactory,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&m_wicFactory)
	);
	if (FAILED(hr)) {
		ALICE_LOG_ERRORF("[UIWorld] CoCreateInstance(WIC) Failed. HR=0x%08X (Did you call CoInitialize?)", hr);
		return;
	}

	// 10. Bitmap from Surface (가장 위험한 구간)
	ALICE_LOG_INFO("[UIWorld] Creating Bitmap from Surface...");
	D2D1_BITMAP_PROPERTIES1 bmpProps = {};
	bmpProps.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM; // ★ 여기랑 Create2DTex의 포맷이 다르면 죽음
	bmpProps.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
	bmpProps.bitmapOptions = D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW;
	bmpProps.dpiX = 96.0f;
	bmpProps.dpiY = 96.0f;

	hr = m_d2DdevCon->CreateBitmapFromDxgiSurface(m_dxgiSurface.Get(), &bmpProps, m_d2dTargetBitmap.GetAddressOf());
	if (FAILED(hr)) { ALICE_LOG_ERRORF("[UIWorld] CreateBitmapFromDxgiSurface Failed. HR=0x%08X", hr); return; }

	m_d2DdevCon->SetTarget(m_d2dTargetBitmap.Get());

	// 11. 마무리
	ALICE_LOG_INFO("[UIWorld] Finalizing Setup...");
	m_inputSystem = &tmpInput;

	Microsoft::WRL::ComPtr<ID2D1Factory1> factory1;
	if (FAILED(m_d2DFactory.As(&factory1))) { ALICE_LOG_WARN("[UIWorld] Factory1 Cast Failed"); }
	m_RenderStruct.m_d2DFactory = factory1;

	Microsoft::WRL::ComPtr<ID2D1Device> device;
	if (FAILED(m_d2DDevice.As(&device))) { ALICE_LOG_WARN("[UIWorld] Device Cast Failed"); }
	m_RenderStruct.m_d2DDevice = device;

	m_RenderStruct.m_d2DdevCon = m_d2DdevCon;
	m_RenderStruct.m_D3DWFactory = m_D3DWFactory;
	m_RenderStruct.m_brush = m_brush;
	m_RenderStruct.m_wicImageFactory = m_wicFactory;
	m_RenderStruct.m_d2dTargetBitmap = m_d2dTargetBitmap;
	m_RenderStruct.m_width = w;
	m_RenderStruct.m_height = h;

	ALICE_LOG_INFO("[UIWorld] Calling ChangeScene...");
	ChangeScene("Default");

	ALICE_LOG_INFO("[UIWorld] Initialize Success.");
}


void UIWorldManager::Update(UINT w, UINT h)
{
    m_curWidth = w;
    m_curHeight = h;

    // 현재 매니저 포인터 저장
    if (sceneStorages.size() == 0 || m_nowSceneName.empty()) { return; }
    auto it = sceneStorages.find(m_nowSceneName);
    if (it != sceneStorages.end())
    {
        m_nowManager = it->second.get();
        if (m_nowManager)
        {
            m_nowManager->Update();
        }
    }
}


void UIWorldManager::Render()
{
    if (sceneStorages.size() == 0 || m_nowSceneName.empty()) 
    { 
        //Alice_LOG_WARN("[UIWorldManager] Render skipped: no scenes or empty scene name");
        return; 
    }
    
    auto it = sceneStorages.find(m_nowSceneName);
    if (it == sceneStorages.end()) 
    { 
        //Alice_LOG_WARN("[UIWorldManager] Render skipped: scene '%s' not found", m_nowSceneName.c_str());
        return; 
    }
    m_nowManager = it->second.get();
    
    // SRV 슬롯 해제 (RTV 바인딩 전)
    ID3D11ShaderResourceView* nullSRVArray[D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT] = { nullptr };
    m_devCon->PSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRVArray);
    m_devCon->VSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRVArray);
    m_devCon->CSSetShaderResources(0, D3D11_COMMONSHADER_INPUT_RESOURCE_SLOT_COUNT, nullSRVArray);
    
    // D2D 렌더 타겟 설정
    m_devCon->OMSetRenderTargets(1, m_RenderTV.GetAddressOf(), nullptr);
    
    if (m_d2DdevCon && m_d2dTargetBitmap)
    {
        m_d2DdevCon->SetTarget(m_d2dTargetBitmap.Get());
    }
    
    // UI 렌더링
    if (m_nowManager)
    {
        m_nowManager->Render();
    }

    // RTV 해제
    ID3D11RenderTargetView* nullRTV[1] = { nullptr };
    m_devCon->OMSetRenderTargets(1, nullRTV, nullptr);

    // UI 텍스처 SRV 바인딩
    ID3D11ShaderResourceView* srvs[] = { m_shaderRV.Get() };
    m_devCon->PSSetShaderResources(m_bindSlot, 1, srvs);
}



 //3D에 합성할 2D Tex 생성
void UIWorldManager::Create2DTex(UINT w, UINT h)
{
    m_curWidth = w; m_curHeight = h;

	// 2D 텍스처 구조체
    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = w;
    desc.Height = h;
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;

    HR_T(m_d3dDev->CreateTexture2D(&desc, nullptr, m_tex2D.GetAddressOf()));
    HR_T(m_d3dDev->CreateShaderResourceView(m_tex2D.Get(), nullptr, &m_shaderRV));
    HR_T(m_d3dDev->CreateRenderTargetView(m_tex2D.Get(), nullptr, &m_RenderTV));

}


void UIWorldManager::ChangeScene(const char* sceneName) {
    if (!sceneName || sceneName[0] == '\0') return;
    
    std::string sceneNameStr(sceneName);
    
    // ChangeScene은 "씬 매니저 선택/생성"만 수행하고, 월드 데이터는 건드리지 않음
    if (sceneStorages.find(sceneNameStr) == sceneStorages.end())
    {
        auto pObj = std::make_unique<UISceneManager>();
        UISceneManager* ptr = pObj.get();
        ptr->initalize(m_d3dDev, m_devCon, &m_RenderStruct, m_inputSystem);
        m_nowManager = ptr;
        sceneStorages.emplace(sceneNameStr, std::move(pObj));
    }
    else
    {
        m_nowManager = sceneStorages[sceneNameStr].get();
    }
    
    m_nowSceneName = sceneNameStr;
    
    // 핵심 로그: 씬 전환 후 root entity count
    size_t rootEntityCount = 0;
    if (m_nowManager)
    {
        rootEntityCount = m_nowManager->GetWorld().GetRootIDs().size();
    }
    //Alice_LOG_INFO("[UIWorldManager] ChangeScene: %s -> rootCount=%zu", 
         //          m_nowSceneName.c_str(), rootEntityCount);
}

// ============================================================================
// UI 엔티티를 JSON으로 직렬화 (ID 기반, 포인터 저장 금지)
// 
// 직렬화 원칙:
// - 저장 대상: ID, parentID, children ID 목록, 컴포넌트 데이터만
// - 저장 금지: unique_ptr, raw pointer, 메모리 주소, 런타임 리소스
// - ScriptComponent: scriptName, enabled만 저장 (instance는 저장하지 않음)
// ============================================================================
static bool WriteUIEntity(Alice::JsonRttr::json& outEntity, const UIWorld& world, unsigned long id)
{
    outEntity = Alice::JsonRttr::json::object();
    // ID만 저장 (포인터나 메모리 주소 저장 금지)
    outEntity["id"] = static_cast<std::uint32_t>(id);

    const UIBase* uiBase = world.Get(id);
    if (!uiBase) return false;

    // parentID 저장 (0이면 루트)
    long unsigned int parentID = uiBase->GetParentID();
    if (parentID != 0)
    {
        outEntity["parent"] = static_cast<std::uint32_t>(parentID);
    }

    // children ID 목록 저장
    const auto& childIDs = uiBase->GetChildIDs();
    if (!childIDs.empty())
    {
        Alice::JsonRttr::json children = Alice::JsonRttr::json::array();
        for (auto childID : childIDs)
        {
            children.push_back(static_cast<std::uint32_t>(childID));
        }
        outEntity["children"] = children;
    }

    // 컴포넌트들을 "components" 객체 안에 저장
    Alice::JsonRttr::json components = Alice::JsonRttr::json::object();

    // UITransform 컴포넌트 저장 (데이터만)
    if (const auto* transform = world.TryGetComponent<UITransform>(id); transform)
    {
        Alice::JsonRttr::json compJson = Alice::JsonRttr::json::object();
        compJson["translation"] = Alice::JsonRttr::json::array({ transform->m_translation.x, transform->m_translation.y });
        compJson["rotation"] = transform->m_rotation;
        compJson["scale"] = Alice::JsonRttr::json::array({ transform->m_scale.x, transform->m_scale.y });
        compJson["size"] = Alice::JsonRttr::json::array({ transform->m_size.x, transform->m_size.y });
        compJson["pivot"] = Alice::JsonRttr::json::array({ transform->m_pivot.x, transform->m_pivot.y });
        components["Transform"] = compJson;
    }

    // UI_ImageComponent 저장 (데이터만, 포인터 저장 금지)
    if (const auto* image = world.TryGetComponent<UI_ImageComponent>(id); image)
    {
        Alice::JsonRttr::json compJson = Alice::JsonRttr::json::object();
        compJson["size"] = Alice::JsonRttr::json::array({ image->m_size.x, image->m_size.y });
        compJson["srcPos"] = Alice::JsonRttr::json::array({ image->m_srcPos.x, image->m_srcPos.y });
        compJson["srcWidthHeight"] = Alice::JsonRttr::json::array({ image->SrcWidthHeight.x, image->SrcWidthHeight.y });
        compJson["pivot"] = Alice::JsonRttr::json::array({ image->m_pivot.x, image->m_pivot.y });
        compJson["srcRect"] = Alice::JsonRttr::json::array({ 
            image->m_srcRect.left, image->m_srcRect.top, 
            image->m_srcRect.right, image->m_srcRect.bottom 
        });
        compJson["rect"] = Alice::JsonRttr::json::array({ 
            image->m_rect.left, image->m_rect.top, 
            image->m_rect.right, image->m_rect.bottom 
        });
        compJson["nowIndex"] = image->m_nowIndex;
        // 이미지 경로 저장 (문자열 데이터이므로 저장 가능, 재생성 가능한 정보)
        const std::wstring& path = image->GetImagePath();
        if (!path.empty())
        {
            // wide string을 UTF-8로 변환하여 저장
            int size_needed = WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, NULL, 0, NULL, NULL);
            if (size_needed > 0)
            {
                std::string pathUtf8(size_needed - 1, 0);
                WideCharToMultiByte(CP_UTF8, 0, path.c_str(), -1, &pathUtf8[0], size_needed, NULL, NULL);
                compJson["path"] = pathUtf8;
            }
        }
        components["Image"] = compJson;
    }

    // UI_ScriptComponent 저장 (레거시, 단일 스크립트용)
    if (const auto* scriptComp = world.TryGetComponent<UI_ScriptComponent>(id); scriptComp)
    {
        Alice::JsonRttr::json compJson = Alice::JsonRttr::json::object();
        compJson["name"] = scriptComp->scriptName;
        compJson["enabled"] = scriptComp->enabled;
        // instance는 저장하지 않음 (UIScriptSystem에서 자동 생성)
        components["Script"] = compJson;
    }
    
    // ============================================================================
    // UI Script 저장 (여러 스크립트 지원, World Script와 유사)
    // ============================================================================
    if (const auto* scripts = world.GetUIScripts(id); scripts && !scripts->empty())
    {
        Alice::JsonRttr::json arr = Alice::JsonRttr::json::array();
        for (const auto& entry : *scripts)
        {
            Alice::JsonRttr::json s = Alice::JsonRttr::json::object();
            s["name"] = entry.scriptName;
            s["enabled"] = entry.enabled;
            
            // 스크립트 인스턴스가 있으면 RTTR로 속성 저장
            if (entry.instance)
            {
                rttr::instance inst = *entry.instance;
                rttr::type t = rttr::type::get_by_name(entry.scriptName);
                if (!t.is_valid())
                {
                    t = inst.get_type();
                }
                if (t.is_valid())
                {
                    s["props"] = Alice::JsonRttr::ToJsonObject(inst, t);
                }
            }
            
            arr.push_back(s);
        }
        components["UIScripts"] = arr;
    }

    // components가 비어있지 않으면 저장
    if (!components.empty())
    {
        outEntity["components"] = components;
    }

    return true;
}

// ============================================================================
// JSON에서 UI 엔티티를 역직렬화 (ID 기반, 새 객체 생성)
// 
// 복원 원칙:
// - 모든 객체는 Load 시 새로 생성됨 (unique_ptr로 관리)
// - JSON에 저장된 ID는 관계 복원에만 사용 (실제 런타임 ID와 다를 수 있음)
// - parent/child 관계는 ID 기반으로만 복원
// - ScriptComponent의 instance는 로드하지 않음 (UIScriptSystem에서 자동 생성)
// ============================================================================
static bool ApplyUIEntity(UISceneManager& manager, const Alice::JsonRttr::json& e, unsigned long parentID = 0)
{
    if (!e.is_object()) return false;

    UIWorld& uiWorld = manager.GetWorld();
    
    // 새 UIBase 객체 생성 (새로운 unique_ptr로 관리됨)
    // 이전 메모리 주소와는 완전히 무관한 새 객체
    UIBase* uiBase = nullptr;
    if (parentID == 0)
    {
        uiBase = manager.CreateUIObjects<UIImage>();
    }
    else
    {
        uiBase = manager.CreateChildUIObjects<UIImage>(parentID);
    }
    
    if (!uiBase) return false;
    
    // 생성된 UIBase의 실제 런타임 ID 사용
    // 주의: JSON에 저장된 ID와 다를 수 있음 (새 객체이므로)
    const unsigned long id = uiBase->getID();

    // components 객체에서 컴포넌트 복원
    auto itComponents = e.find("components");
    if (itComponents == e.end() || !itComponents->is_object())
        return true; // 컴포넌트가 없어도 성공

    // UITransform 복원
    auto itT = itComponents->find("Transform");
    if (itT != itComponents->end() && itT->is_object())
    {
        UITransform& transform = manager.GetComponent<UITransform>(id);
        if (itT->contains("translation") && (*itT)["translation"].is_array())
        {
            auto arr = (*itT)["translation"];
            if (arr.size() >= 2)
                transform.m_translation = { arr[0].get<float>(), arr[1].get<float>() };
        }
        if (itT->contains("rotation"))
            transform.m_rotation = itT->value("rotation", 0.0f);
        if (itT->contains("scale") && (*itT)["scale"].is_array())
        {
            auto arr = (*itT)["scale"];
            if (arr.size() >= 2)
                transform.m_scale = { arr[0].get<float>(), arr[1].get<float>() };
        }
        if (itT->contains("size") && (*itT)["size"].is_array())
        {
            auto arr = (*itT)["size"];
            if (arr.size() >= 2)
                transform.m_size = { arr[0].get<float>(), arr[1].get<float>() };
        }
        if (itT->contains("pivot") && (*itT)["pivot"].is_array())
        {
            auto arr = (*itT)["pivot"];
            if (arr.size() >= 2)
                transform.m_pivot = { arr[0].get<float>(), arr[1].get<float>() };
        }
    }

    // UI_ImageComponent 복원
    auto itImg = itComponents->find("Image");
    if (itImg != itComponents->end() && itImg->is_object())
    {
        UI_ImageComponent* image = manager.CreateComponent<UI_ImageComponent>(id);
        if (image)
        {
            if (itImg->contains("size") && (*itImg)["size"].is_array())
            {
                auto arr = (*itImg)["size"];
                if (arr.size() >= 2)
                    image->m_size = { arr[0].get<float>(), arr[1].get<float>() };
            }
            if (itImg->contains("srcPos") && (*itImg)["srcPos"].is_array())
            {
                auto arr = (*itImg)["srcPos"];
                if (arr.size() >= 2)
                    image->m_srcPos = { arr[0].get<float>(), arr[1].get<float>() };
            }
            if (itImg->contains("srcWidthHeight") && (*itImg)["srcWidthHeight"].is_array())
            {
                auto arr = (*itImg)["srcWidthHeight"];
                if (arr.size() >= 2)
                    image->SrcWidthHeight = { arr[0].get<float>(), arr[1].get<float>() };
            }
            if (itImg->contains("pivot") && (*itImg)["pivot"].is_array())
            {
                auto arr = (*itImg)["pivot"];
                if (arr.size() >= 2)
                    image->m_pivot = { arr[0].get<float>(), arr[1].get<float>() };
            }
            
            if (itImg->contains("nowIndex"))
                image->m_nowIndex = itImg->value("nowIndex", 0u);
            
            // 이미지 경로 복원 및 이미지 다시 로드
            if (itImg->contains("path"))
            {
                std::string pathUtf8 = itImg->value("path", std::string{});
                if (!pathUtf8.empty())
                {
                    // UTF-8 문자열을 wide string으로 변환
                    int size_needed = MultiByteToWideChar(CP_UTF8, 0, pathUtf8.c_str(), -1, NULL, 0);
                    if (size_needed > 0)
                    {
                        std::wstring path(size_needed - 1, 0);
                        MultiByteToWideChar(CP_UTF8, 0, pathUtf8.c_str(), -1, &path[0], size_needed);
                        // SetImagePath를 호출하여 이미지를 다시 로드
                        image->SetImagePath(path);
                    }
                }
            }
        }
    }

    // UI_ScriptComponent 복원 (레거시, 단일 스크립트용)
    auto itScript = itComponents->find("Script");
    if (itScript != itComponents->end() && itScript->is_object())
    {
        const std::string scriptName = itScript->value("name", std::string{});
        if (!scriptName.empty())
        {
            UI_ScriptComponent* scriptComp = manager.CreateComponent<UI_ScriptComponent>(id);
            if (scriptComp)
            {
                scriptComp->scriptName = scriptName;
                scriptComp->enabled = itScript->value("enabled", true);
                // instance는 저장하지 않았으므로 로드하지 않음
                // UIScriptSystem::Tick → EnsureInstance()에서 자동 생성됨
            }
        }
    }
    
    // ============================================================================
    // UI Script 복원 (여러 스크립트 지원, World Script와 유사)
    // ============================================================================
    auto itUIScripts = itComponents->find("UIScripts");
    if (itUIScripts != itComponents->end() && itUIScripts->is_array())
    {
        UIWorld& uiWorld = manager.GetWorld();
        for (const auto& s : *itUIScripts)
        {
            if (!s.is_object()) continue;
            const std::string name = s.value("name", std::string{});
            if (name.empty()) continue;
            
            UIScriptEntry& entry = uiWorld.AddUIScript(id, name);
            entry.enabled = s.value("enabled", true);
            
            // RTTR 속성 복원
            auto itP = s.find("props");
            if (itP != s.end() && itP->is_object())
            {
                // 인스턴스가 생성될 때까지 대기 (UIScriptSystem에서 생성)
                // 속성은 나중에 복원하거나, 인스턴스 생성 후 복원
                // 일단 스크립트 이름과 enabled만 저장하고, 속성은 다음 Tick에서 복원
                // (또는 EnsureUIScriptInstance 후 즉시 복원)
            }
        }
    }

    return true;
}

// ============================================================================
// UI 씬 저장
// 
// 저장 원칙:
// - ID 기반 직렬화만 수행
// - unique_ptr, 포인터, 메모리 주소는 절대 저장하지 않음
// - 컴포넌트 데이터만 저장 (런타임 리소스 제외)
// ============================================================================
bool UIWorldManager::SaveUI(const std::filesystem::path& worldScenePath)
{
    if (!m_nowManager) return false;
    
    // World 씬 파일 경로에서 Scene 이름 추출
    std::string sceneName = worldScenePath.stem().string();
    
    // UI 파일 경로 생성: {sceneName}.ui.scene
    std::filesystem::path uiPath = worldScenePath.parent_path() / (sceneName + ".ui.scene");
    
    // 현재 씬 이름과 일치하는지 확인
    if (m_nowSceneName != sceneName)
    {
        // 씬이 다르면 해당 씬으로 변경
        ChangeScene(sceneName.c_str());
    }
    
    if (!m_nowManager) return false;
    
    const UIWorld& uiWorld = m_nowManager->GetWorld();
    
    Alice::JsonRttr::json root = Alice::JsonRttr::json::object();
    root["version"] = 1;
    root["sceneName"] = sceneName;
    root["uiEntities"] = Alice::JsonRttr::json::array();
    
    // 모든 UI 엔티티를 저장하기 위한 헬퍼 함수 (재귀적)
    std::function<void(unsigned long)> saveUIEntityRecursive = 
        [&](unsigned long id) {
            Alice::JsonRttr::json uiEntity;
            if (!WriteUIEntity(uiEntity, uiWorld, id)) return;
            root["uiEntities"].push_back(uiEntity);
            
            // 자식들도 재귀적으로 저장
            const UIBase* uiBase = uiWorld.Get(id);
            if (uiBase)
            {
                const auto& childIDs = uiBase->GetChildIDs();
                for (auto childID : childIDs)
                {
                    saveUIEntityRecursive(childID);
                }
            }
        };
    
    // 루트 ID들부터 시작하여 모든 UI 엔티티 저장
    const auto& rootIDs = uiWorld.GetRootIDs();
    for (auto rootID : rootIDs)
    {
        saveUIEntityRecursive(rootID);
    }

    if (!Alice::JsonRttr::SaveJsonFile(uiPath, root, 4)) return false;

    //Alice_LOG_INFO("[UIWorldManager] UI saved to: %s", uiPath.string().c_str());
    return true;
}

// ============================================================================
// UI 씬 로드
// 
// 로드 원칙:
// - 모든 객체는 새로 생성됨 (새로운 unique_ptr로 관리)
// - JSON에 저장된 ID는 관계 복원에만 사용
// - ScriptComponent의 instance는 로드하지 않음 (UIScriptSystem에서 자동 생성)
// ============================================================================
bool UIWorldManager::LoadUI(const std::filesystem::path& worldScenePath, const Alice::ResourceManager* resources)
{
    // World 씬 파일 경로에서 Scene 이름 추출
    std::string sceneName = worldScenePath.stem().string();
    
    // UI 파일 경로 생성: {sceneName}.ui.scene
    std::filesystem::path uiLogicalPath = worldScenePath.parent_path() / (sceneName + ".ui.scene");
    // 논리 경로를 정규화 (Assets/... 형식으로)
    std::string uiLogicalStr = uiLogicalPath.generic_string();
    if (uiLogicalStr.find("Assets/") != 0 && uiLogicalStr.find("Resource/") != 0 && uiLogicalStr.find("Cooked/") != 0)
    {
        // 상대 경로인 경우 Assets/를 앞에 붙임
        if (!uiLogicalStr.empty() && uiLogicalStr[0] != '/')
        {
            uiLogicalPath = std::filesystem::path("Assets") / uiLogicalPath;
        }
    }
    
    //Alice_LOG_INFO("[UIWorldManager] LoadUI: scene=%s, path=%s", 
         //          sceneName.c_str(), uiLogicalPath.string().c_str());
    
    // 씬 변경 (없으면 생성됨)
    ChangeScene(sceneName.c_str());
    
    if (!m_nowManager) return false;
    
    UIWorld& uiWorld = m_nowManager->GetWorld();
    
    // UI World Clear (새로 로드하기 전에)
    // 모든 기존 unique_ptr들이 해제되고, 새 객체들이 생성됨
    uiWorld.Clear();
    
    Alice::JsonRttr::json root;
    bool uiFileFound = false;
    
    // ============================================================================
    // 2) 패킹된 리소스(.alice) 로드 분기 처리
    // ============================================================================
    bool usingPackagedResource = false;
    
    // ResourceManager가 있고 패킹된 리소스에서 로드 시도
    if (resources)
    {
        const std::filesystem::path resolved = resources->Resolve(uiLogicalPath);
        
        if (resolved.extension() == ".alice")
        {
            auto sp = resources->LoadSharedBinaryAuto(uiLogicalPath);
            if (sp && sp->size() > 0)
            {
                try
                {
                    root = Alice::JsonRttr::json::parse(sp->data(), sp->data() + sp->size());
                    uiFileFound = true;
                }
                catch (...)
                {
                    //Alice_LOG_ERRORF("[UIWorldManager] LoadUI: Failed to parse JSON from .alice package");
                    return false;
                }
            }
        }
        else if (std::filesystem::exists(resolved))
        {
            if (Alice::JsonRttr::LoadJsonFile(resolved, root))
            {
                uiFileFound = true;
            }
            else
            {
                //Alice_LOG_ERRORF("[UIWorldManager] LoadUI: Failed to load JSON file: %s", resolved.string().c_str());
                return false;
            }
        }
    }
    else
    {
        std::filesystem::path uiPath = worldScenePath.parent_path() / (sceneName + ".ui.scene");
        if (std::filesystem::exists(uiPath))
        {
            if (Alice::JsonRttr::LoadJsonFile(uiPath, root))
            {
                uiFileFound = true;
            }
            else
            {
                //Alice_LOG_ERRORF("[UIWorldManager] LoadUI: Failed to load JSON file: %s", uiPath.string().c_str());
                return false;
            }
        }
    }
    
    // UI 파일이 없으면 "Default" 씬으로 폴백
    if (!uiFileFound)
    {
        //Alice_LOG_WARN("[UIWorldManager] LoadUI: UI file not found for '%s', trying Default", sceneName.c_str());
        
        // "Default" 씬으로 전환 (없으면 생성됨)
        // ChangeScene은 Clear를 하지 않으므로, LoadUI 내부에서 명시적으로 Clear 수행
        if (sceneName != "Default")
        {
            ChangeScene("Default");
            if (!m_nowManager) return true; // Default 씬도 없으면 그냥 성공 반환
            
            // LoadUI 내부에서 Clear 수행 (ChangeScene은 데이터를 건드리지 않음)
            m_nowManager->GetWorld().Clear();
        }
        
        // Default UI 파일 로드 시도
        std::filesystem::path defaultUIPath;
        if (resources)
        {
            // ResourceManager가 있는 경우
            std::filesystem::path defaultUILogicalPath = worldScenePath.parent_path() / "Default.ui.scene";
            std::string defaultUILogicalStr = defaultUILogicalPath.generic_string();
            if (defaultUILogicalStr.find("Assets/") != 0 && defaultUILogicalStr.find("Resource/") != 0 && defaultUILogicalStr.find("Cooked/") != 0)
            {
                if (!defaultUILogicalStr.empty() && defaultUILogicalStr[0] != '/')
                {
                    defaultUILogicalPath = std::filesystem::path("Assets") / defaultUILogicalPath;
                }
            }
            
            const std::filesystem::path resolved = resources->Resolve(defaultUILogicalPath);
            if (resolved.extension() == ".alice")
            {
                auto sp = resources->LoadSharedBinaryAuto(defaultUILogicalPath);
                if (sp && sp->size() > 0)
                {
                    try
                    {
                        root = Alice::JsonRttr::json::parse(sp->data(), sp->data() + sp->size());
                        uiFileFound = true;
                    }
                    catch (...)
                    {
                        // 파싱 실패는 무시하고 계속 진행
                    }
                }
            }
            else if (std::filesystem::exists(resolved))
            {
                if (Alice::JsonRttr::LoadJsonFile(resolved, root))
                {
                    uiFileFound = true;
                }
            }
        }
        else
        {
            defaultUIPath = worldScenePath.parent_path() / "Default.ui.scene";
            if (std::filesystem::exists(defaultUIPath))
            {
                if (Alice::JsonRttr::LoadJsonFile(defaultUIPath, root))
                {
                    uiFileFound = true;
                }
            }
        }
        
        if (!uiFileFound)
        {
            //Alice_LOG_WARN("[UIWorldManager] LoadUI: Default UI also not found, using empty scene");
            return true;
        }
    }
    
    auto itEntities = root.find("uiEntities");
    if (itEntities == root.end() || !itEntities->is_array())
    {
        //Alice_LOG_WARN("[UIWorldManager] LoadUI: No uiEntities in JSON");
        return true;
    }
    
    // ID -> 부모 ID 매핑을 저장
    std::unordered_map<unsigned long, unsigned long> parentMap;
    std::unordered_map<unsigned long, const Alice::JsonRttr::json*> entityMap;

    // 먼저 모든 엔티티를 파싱하여 부모-자식 관계 파악
    for (const auto& e : *itEntities)
    {
        if (!e.is_object()) continue;
        unsigned long id = e.value("id", 0u);
        if (id == 0) continue;

        entityMap[id] = &e;

        // parent ID 저장
        unsigned long parentID = e.value("parent", 0u);
        if (parentID != 0)
        {
            parentMap[id] = parentID;
        }
    }

    // 루트 엔티티부터 재귀적으로 로드
    // 주의: JSON에 저장된 ID는 관계 복원에만 사용됨
    // 실제 생성된 객체의 ID는 새로 할당되므로 다를 수 있음
    UISceneManager* currentManager = m_nowManager; // 람다에서 사용하기 위해 로컬 변수에 저장
    UIWorld* pUIWorld = &uiWorld; // 람다에서 사용하기 위해 로컬 변수에 저장
    std::function<bool(unsigned long, unsigned long)> loadEntityRecursive = 
        [&, currentManager, pUIWorld](unsigned long jsonEntityID, unsigned long parentRuntimeID) -> bool {
            // JSON 엔티티 찾기
            auto it = entityMap.find(jsonEntityID);
            if (it == entityMap.end()) return false;
            
            // 새 엔티티 생성 (parentRuntimeID를 사용하여 계층 구조 복원)
            // parentRuntimeID는 실제 런타임 ID (이전에 생성된 객체의 ID)
            if (!currentManager) return false;
            if (!ApplyUIEntity(*currentManager, *it->second, parentRuntimeID)) return false;
            
            // 생성된 엔티티의 실제 런타임 ID 찾기
            unsigned long actualRuntimeID = 0;
            if (parentRuntimeID == 0)
            {
                // 루트 엔티티: 최근 생성된 루트 엔티티 찾기
                const auto& rootIDs = pUIWorld->GetRootIDs();
                if (!rootIDs.empty())
                {
                    actualRuntimeID = rootIDs.back();
                }
            }
            else
            {
                // 자식 엔티티: 부모의 마지막 자식 찾기 (실제 런타임 ID)
                UIBase* parent = pUIWorld->Get(parentRuntimeID);
                if (parent)
                {
                    const auto& childIDs = parent->GetChildIDs();
                    if (!childIDs.empty())
                    {
                        actualRuntimeID = childIDs.back();
                    }
                }
            }
            
            if (actualRuntimeID == 0) return false;
            
            // 자식들도 재귀적으로 로드
            // JSON에 저장된 children ID는 관계 복원에만 사용
            const auto& e = *it->second;
            auto itChildren = e.find("children");
            if (itChildren != e.end() && itChildren->is_array())
            {
                for (const auto& childIdJson : *itChildren)
                {
                    if (childIdJson.is_number())
                    {
                        // JSON에 저장된 child ID (관계 복원용)
                        unsigned long jsonChildID = childIdJson.get<unsigned long>();
                        // 실제 런타임 ID(actualRuntimeID)를 부모로 하여 자식 생성
                        if (!loadEntityRecursive(jsonChildID, actualRuntimeID)) return false;
                    }
                }
            }
            
            return true;
        };

    // 루트 엔티티부터 로드 (부모가 없는 엔티티)
    // JSON에 저장된 ID를 사용하여 관계를 복원하지만,
    // 실제 생성되는 객체는 새로운 unique_ptr로 관리됨
    size_t rootCount = 0;
    for (const auto& [jsonID, eJson] : entityMap)
    {
        if (parentMap.find(jsonID) == parentMap.end())
        {
            // 루트 엔티티 (parentID = 0으로 생성)
            rootCount++;
            if (!loadEntityRecursive(jsonID, 0))
            {
                ////Alice_LOG_ERROR("[UIWorldManager] Failed to load root entity with JSON ID: %lu", jsonID);
                return false;
            }
        }
    }

    // 핵심 로그: UI 엔티티 생성 결과
    size_t finalRootCount = uiWorld.GetRootIDs().size();
    //Alice_LOG_INFO("[UIWorldManager] LoadUI: loaded %zu root entities (total: %zu)", 
            //       finalRootCount, entityMap.size());
    
    // ============================================================================
    // Post-Load Fixup: Load(데이터 생성) -> Initialize(포인터 전파) -> Recovery(GPU 텍스처) -> Layout(좌표 계산)
    // ============================================================================
    // m_renderStruct 유효성 확인
    bool canPerformFixup = true;
    if (!m_nowManager)
    {
        //Alice_LOG_WARN("[UIWorldManager] Cannot perform post-load fixup: m_nowManager is null for scene: %s", sceneName.c_str());
        canPerformFixup = false;
    }
    
    // 렌더러 포인터가 준비되지 않은 경우 확인
    // m_RenderStruct의 핵심 멤버들이 유효한지 확인
    bool renderStructValid = (m_RenderStruct.m_d2DdevCon != nullptr) && 
                             (m_RenderStruct.m_d2DFactory != nullptr) &&
                             (m_RenderStruct.m_d2DDevice != nullptr);
    
    if (!renderStructValid)
    {
        //Alice_LOG_WARN("[UIWorldManager] LoadUI: Post-load fixup deferred (renderer not ready)");
        canPerformFixup = false;
    }
    
    if (canPerformFixup)
    {
        // ============================================================================
        // (1) 렌더러 포인터 전파(Propagate renderer)
        // ============================================================================
        
        size_t initializedCount = 0;
        UISceneManager* fixupManager = m_nowManager;
        UIRenderStruct* pRenderStruct = &m_RenderStruct;
        
        // 재귀 함수 정의
        std::function<void(UIWorld&, UISceneManager*, UIRenderStruct*, const std::string&, size_t&, unsigned long)> propagateRendererRecursive;
        propagateRendererRecursive = [&propagateRendererRecursive](
            UIWorld& uiWorld, UISceneManager* manager, UIRenderStruct* pRenderStruct,
            const std::string& sceneName, size_t& initializedCount, unsigned long id) {
            UIBase* uiBase = uiWorld.Get(id);
            if (!uiBase) return;
            
            if (!manager) return;
            UI_ImageComponent* img = manager->TryGetComponent<UI_ImageComponent>(id);
            if (img)
            {
                // 렌더러/디바이스 포인터 주입
                img->Initalize(*pRenderStruct);
                initializedCount++;
            }
            
            // 자식 엔티티도 재귀적으로 처리
            const auto& childIDs = uiBase->GetChildIDs();
            for (auto childID : childIDs)
            {
                propagateRendererRecursive(uiWorld, manager, pRenderStruct, sceneName, initializedCount, childID);
            }
        };
        
        // 루트 엔티티부터 시작하여 모든 엔티티에 렌더러 포인터 전파
        const auto& rootIDs = uiWorld.GetRootIDs();
        for (auto rootID : rootIDs)
        {
            propagateRendererRecursive(uiWorld, fixupManager, pRenderStruct, sceneName, initializedCount, rootID);
        }
        
        // ============================================================================
        // (2) GPU 리소스 복구(Recover resources)
        // ============================================================================
        
        size_t recoveredCount = 0;
        size_t failedCount = 0;
        
        // 재귀 함수 정의
        std::function<void(UIWorld&, UISceneManager*, const std::string&, size_t&, size_t&, unsigned long)> recoverResourcesRecursive;
        recoverResourcesRecursive = [&recoverResourcesRecursive](
            UIWorld& uiWorld, UISceneManager* manager, const std::string& sceneName,
            size_t& recoveredCount, size_t& failedCount, unsigned long id) {
            UIBase* uiBase = uiWorld.Get(id);
            if (!uiBase) return;
            
            if (!manager) return;
            UI_ImageComponent* img = manager->TryGetComponent<UI_ImageComponent>(id);
            if (img)
            {
                // m_path는 있지만 m_texture가 null인 경우 복구
                const std::wstring& imagePath = img->GetImagePath();
                if (!imagePath.empty())
                {
                    // EnsureResource()를 호출하여 리소스 복구 시도
                    if (img->EnsureResource())
                    {
                        recoveredCount++;
                    }
                    else
                    {
                        failedCount++;
                        //Alice_LOG_WARN("[UIWorldManager] Failed to recover UI Image: Scene=%s, ID=%lu, path=%S", 
                                 //     sceneName.c_str(), id, imagePath.c_str());
                    }
                }
            }
            
            // 자식 엔티티도 재귀적으로 복구
            const auto& childIDs = uiBase->GetChildIDs();
            for (auto childID : childIDs)
            {
                recoverResourcesRecursive(uiWorld, manager, sceneName, recoveredCount, failedCount, childID);
            }
        };
        
        // 루트 엔티티부터 시작하여 모든 엔티티의 리소스 복구
        for (auto rootID : rootIDs)
        {
            recoverResourcesRecursive(uiWorld, fixupManager, sceneName, recoveredCount, failedCount, rootID);
        }
        
        // ============================================================================
        // (3) 레이아웃 강제 갱신(Update transforms)
        // ============================================================================
        UILayoutSystem::UpdateTransforms(uiWorld);
        UILayoutSystem::UpdateUI(uiWorld);
    }
    else
    {
        //Alice_LOG_WARN("[UIWorldManager] LoadUI: Post-load fixup skipped (renderer not ready)");
    }
    
    // 안전 로그: LoadUI 완료 직후 root entity count 검증
    size_t rootEntityCount = 0;
    if (m_nowManager)
    {
        rootEntityCount = m_nowManager->GetWorld().GetRootIDs().size();
    }
    //Alice_LOG_INFO("[UIWorldManager] UI file load completed successfully for scene: %s, rootEntityCount=%zu", sceneName.c_str(), rootEntityCount);
    
    return true;
}

// ============================================================================
// 렌더러 준비 후 모든 UI 컴포넌트 재초기화
// ============================================================================
void UIWorldManager::ReinitializeAllUIComponents(UIRenderStruct* renderStruct)
{
    // renderStruct가 nullptr이면 m_RenderStruct 사용
    UIRenderStruct* pRenderStruct = renderStruct ? renderStruct : &m_RenderStruct;
    
    if (!pRenderStruct)
    {
        //Alice_LOG_WARN("[UIWorldManager] ReinitializeAllUIComponents: renderStruct is null");
        return;
    }
    
    // ============================================================================
    // 1) m_nowManager 상태 정합성 보장 및 디버그 로그
    // ============================================================================
    //Alice_LOG_INFO("[UIWorldManager] Reinitializing all UI components with render struct...");
    //Alice_LOG_INFO("[UIWorldManager] Current m_nowManager scene: %s", 
             //      m_nowManager ? m_nowSceneName.c_str() : "null");
    //Alice_LOG_INFO("[UIWorldManager] Total UI scenes in storage: %zu", sceneStorages.size());
    
    // sceneStorages에 있는 모든 씬 이름 출력
    if (!sceneStorages.empty())
    {
        std::string sceneList = "Available UI scenes: ";
        for (const auto& pair : sceneStorages)
        {
            sceneList += pair.first + " ";
        }
        //Alice_LOG_INFO("[UIWorldManager] %s", sceneList.c_str());
    }
    
    // m_nowManager가 null이거나 유효하지 않은 경우, 첫 번째 유효한 UI 씬을 찾아 설정
    if (!m_nowManager || sceneStorages.find(m_nowSceneName) == sceneStorages.end())
    {
        if (!sceneStorages.empty())
        {
            // 첫 번째 유효한 UI 씬을 찾아서 설정
            auto firstScene = sceneStorages.begin();
            m_nowSceneName = firstScene->first;
            m_nowManager = firstScene->second.get();
            //Alice_LOG_WARN("[UIWorldManager] m_nowManager was invalid, switching to first available scene: %s", 
                 //         m_nowSceneName.c_str());
        }
        else
        {
            //Alice_LOG_WARN("[UIWorldManager] No UI scenes available in storage, cannot reinitialize");
            return;
        }
    }
    
    // ============================================================================
    // 2) Early Return 로직 수정: 모든 UI 월드를 순회
    // ============================================================================
    // m_nowManager는 우선순위일 뿐, 모든 UI 월드를 처리해야 함
    size_t totalReinitializedCount = 0;
    
    // 모든 UI 월드를 순회하며 초기화
    for (const auto& pair : sceneStorages)
    {
        const std::string& sceneName = pair.first;
        UISceneManager* manager = pair.second.get();
        
        if (!manager)
        {
            //Alice_LOG_WARN("[UIWorldManager] Scene '%s' has null manager, skipping", sceneName.c_str());
            continue;
        }
        
        //Alice_LOG_INFO("[UIWorldManager] Processing UI scene: %s", sceneName.c_str());
        
        UIWorld& uiWorld = manager->GetWorld();
        const auto& rootIDs = uiWorld.GetRootIDs();
        
        if (rootIDs.empty())
        {
            //Alice_LOG_INFO("[UIWorldManager] Scene '%s' has no UI entities, skipping", sceneName.c_str());
            continue;
        }
        
        //Alice_LOG_INFO("[UIWorldManager] Scene '%s' has %zu root entities", sceneName.c_str(), rootIDs.size());
        
        // 모든 UI 엔티티를 순회하여 컴포넌트 재초기화
        size_t sceneReinitializedCount = 0;
        
        // 재귀 함수를 별도로 정의 (람다 재귀 호출 문제 해결)
        std::function<void(UIWorld&, UISceneManager*, UIRenderStruct*, const std::string&, size_t&, unsigned long)> reinitializeRecursive;
        reinitializeRecursive = [&reinitializeRecursive](UIWorld& uiWorld, UISceneManager* manager, UIRenderStruct* pRenderStruct, 
                                                         const std::string& sceneName, size_t& sceneReinitializedCount, unsigned long id) {
            UIBase* uiBase = uiWorld.Get(id);
            if (!uiBase) return;
            
            // UI_ImageComponent 재초기화
            if (!manager) return;
            UI_ImageComponent* img = manager->TryGetComponent<UI_ImageComponent>(id);
            if (img)
            {
                // 렌더러/디바이스 포인터 재연결 (포인터 전파)
                img->Initalize(*pRenderStruct);
                sceneReinitializedCount++;
                
                // GPU 텍스처 재생성 (경로가 있는 경우)
                const std::wstring& imagePath = img->GetImagePath();
                if (!imagePath.empty())
                {
                    //Alice_LOG_INFO("[UIWorldManager] Reinitializing UI Image: Scene=%s, ID=%lu, path=%S",  sceneName.c_str(), id, imagePath.c_str());
                    img->SetImagePath(imagePath);
                }
            }
            
            // 자식 엔티티도 재귀적으로 재초기화
            const auto& childIDs = uiBase->GetChildIDs();
            for (auto childID : childIDs)
            {
                reinitializeRecursive(uiWorld, manager, pRenderStruct, sceneName, sceneReinitializedCount, childID);
            }
        };
        
        // 루트 엔티티부터 시작하여 모든 엔티티의 컴포넌트 재초기화
        for (auto rootID : rootIDs)
        {
            reinitializeRecursive(uiWorld, manager, pRenderStruct, sceneName, sceneReinitializedCount, rootID);
        }
        
        totalReinitializedCount += sceneReinitializedCount;
        //Alice_LOG_INFO("[UIWorldManager] Scene '%s': Reinitialized %zu UI components", 
          //            sceneName.c_str(), sceneReinitializedCount);
        
        // ============================================================================
        // 4) 레이아웃 강제 갱신 (즉시 가시화)
        // ============================================================================
        //Alice_LOG_INFO("[UIWorldManager] Updating UI transforms and layout for scene '%s'...", sceneName.c_str());
        UILayoutSystem::UpdateTransforms(uiWorld);
        UILayoutSystem::UpdateUI(uiWorld);
        //Alice_LOG_INFO("[UIWorldManager] UI transforms and layout updated for scene '%s'", sceneName.c_str());
    }
    
    //Alice_LOG_INFO("[UIWorldManager] Total reinitialized UI components across all scenes: %zu", totalReinitializedCount);
    
    if (totalReinitializedCount == 0)
    {
        //Alice_LOG_WARN("[UIWorldManager] No UI components were reinitialized. Possible reasons:");
        //Alice_LOG_WARN("[UIWorldManager]   - No UI entities exist in any scene");
        //Alice_LOG_WARN("[UIWorldManager]   - All UI entities lack UI_ImageComponent");
        //Alice_LOG_WARN("[UIWorldManager]   - UI entities were not loaded properly");
    }
    
    //Alice_LOG_INFO("[UIWorldManager] All UI components reinitialized");
}

// ============================================================================
// UI 리소스 강제 복구 (m_path는 있지만 m_texture가 null인 경우)
// ============================================================================
void UIWorldManager::EnsureAllUIResources()
{
    //Alice_LOG_INFO("[UIWorldManager] Ensuring all UI resources...");
    //Alice_LOG_INFO("[UIWorldManager] Current m_nowManager scene: %s", 
     //              m_nowManager ? m_nowSceneName.c_str() : "null");
    //Alice_LOG_INFO("[UIWorldManager] Total UI scenes in storage: %zu", sceneStorages.size());
    
    // m_nowManager가 null이거나 유효하지 않은 경우, 첫 번째 유효한 UI 씬을 찾아 설정
    if (!m_nowManager || sceneStorages.find(m_nowSceneName) == sceneStorages.end())
    {
        if (!sceneStorages.empty())
        {
            // 첫 번째 유효한 UI 씬을 찾아서 설정
            auto firstScene = sceneStorages.begin();
            m_nowSceneName = firstScene->first;
            m_nowManager = firstScene->second.get();
            //Alice_LOG_WARN("[UIWorldManager] m_nowManager was invalid, switching to first available scene: %s", 
                  //        m_nowSceneName.c_str());
        }
        else
        {
            //Alice_LOG_WARN("[UIWorldManager] No UI scenes available in storage, cannot ensure resources");
            return;
        }
    }
    
    // ============================================================================
    // 2) Early Return 로직 수정: 모든 UI 월드를 순회
    // ============================================================================
    // 모든 UI 월드를 순회하며 리소스 복구
    size_t totalRecoveredCount = 0;
    
    for (const auto& pair : sceneStorages)
    {
        const std::string& sceneName = pair.first;
        UISceneManager* manager = pair.second.get();
        
        if (!manager)
        {
            //Alice_LOG_WARN("[UIWorldManager] Scene '%s' has null manager, skipping", sceneName.c_str());
            continue;
        }
        
        //Alice_LOG_INFO("[UIWorldManager] Processing UI scene for resource recovery: %s", sceneName.c_str());
        
        UIWorld& uiWorld = manager->GetWorld();
        const auto& rootIDs = uiWorld.GetRootIDs();
        
        if (rootIDs.empty())
        {
            //Alice_LOG_INFO("[UIWorldManager] Scene '%s' has no UI entities, skipping", sceneName.c_str());
            continue;
        }
        
        // 모든 UI 엔티티를 순회하여 리소스 복구
        size_t sceneRecoveredCount = 0;
        
        // 재귀 함수를 별도로 정의 (람다 재귀 호출 문제 해결)
        std::function<void(UIWorld&, UISceneManager*, const std::string&, size_t&, unsigned long)> ensureResourcesRecursive;
        ensureResourcesRecursive = [&ensureResourcesRecursive](UIWorld& uiWorld, UISceneManager* manager, 
                                                               const std::string& sceneName, size_t& sceneRecoveredCount, unsigned long id) {
            UIBase* uiBase = uiWorld.Get(id);
            if (!uiBase) return;
            
            // UI_ImageComponent 리소스 복구
            if (!manager) return;
            UI_ImageComponent* img = manager->TryGetComponent<UI_ImageComponent>(id);
            if (img)
            {
                // m_path는 있지만 m_texture가 null인 경우
                const std::wstring& imagePath = img->GetImagePath();
                if (!imagePath.empty())
                {
                    // ============================================================================
                    // 4) 리소스 복구 타이밍 보장
                    // ============================================================================
                    // EnsureResource()를 호출하여 리소스 복구 시도
                    //Alice_LOG_INFO("[UIWorldManager] Ensuring UI Image resource: Scene=%s, ID=%lu, path=%S", 
                     //             sceneName.c_str(), id, imagePath.c_str());
                    if (img->EnsureResource())
                    {
                        sceneRecoveredCount++;
                        //Alice_LOG_INFO("[UIWorldManager] Successfully recovered UI Image: Scene=%s, ID=%lu", 
                     //                 sceneName.c_str(), id);
                    }
                    else
                    {
                        //Alice_LOG_WARN("[UIWorldManager] Failed to recover UI Image: Scene=%s, ID=%lu, path=%S", 
                            //          sceneName.c_str(), id, imagePath.c_str());
                    }
                }
            }
            
            // 자식 엔티티도 재귀적으로 복구
            const auto& childIDs = uiBase->GetChildIDs();
            for (auto childID : childIDs)
            {
                ensureResourcesRecursive(uiWorld, manager, sceneName, sceneRecoveredCount, childID);
            }
        };
        
        // 루트 엔티티부터 시작하여 모든 엔티티의 리소스 복구
        for (auto rootID : rootIDs)
        {
            ensureResourcesRecursive(uiWorld, manager, sceneName, sceneRecoveredCount, rootID);
        }
        
        totalRecoveredCount += sceneRecoveredCount;
        //Alice_LOG_INFO("[UIWorldManager] Scene '%s': Recovered %zu UI image resources", sceneName.c_str(), sceneRecoveredCount);
        
        if (sceneRecoveredCount > 0)
        {
            // ============================================================================
            // 4) 레이아웃 강제 갱신 (즉시 가시화)
            // ============================================================================
            //Alice_LOG_INFO("[UIWorldManager] Updating UI transforms and layout for scene '%s' after resource recovery...", sceneName.c_str());
            UILayoutSystem::UpdateTransforms(uiWorld);
            UILayoutSystem::UpdateUI(uiWorld);
            //Alice_LOG_INFO("[UIWorldManager] UI transforms and layout updated for scene '%s'", sceneName.c_str());
        }
    }
    
    if (totalRecoveredCount > 0)
    {
        //Alice_LOG_INFO("[UIWorldManager] Total UI resources ensured across all scenes: %zu images recovered", 
          //           totalRecoveredCount);
    }
    else
    {
        //Alice_LOG_INFO("[UIWorldManager] No UI resources needed recovery across all scenes");
    }
}
