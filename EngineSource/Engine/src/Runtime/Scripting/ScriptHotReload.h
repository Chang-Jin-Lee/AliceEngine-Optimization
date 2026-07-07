#pragma once

namespace Alice
{
    class World;
    class ResourceManager;

    /// 스크립트 DLL에 엔진 서비스(World/ResourceManager)를 전달합니다.
    /// - DLL이 이미 로드되어 있으면 즉시 바인딩을 시도합니다.
    void ScriptHotReload_SetServices(World* world, ResourceManager* resources);

    /// AliceScripts.dll 을 로드하여 동적 스크립트 함수 포인터를 등록합니다.
    /// - dllName 은 보통 "AliceScripts.dll" 입니다.
    bool ScriptHotReload_Load(const wchar_t* dllName = L"AliceScripts.dll");

    /// 이미 로드된 DLL 을 언로드 한 뒤 다시 로드합니다.
    bool ScriptHotReload_Reload(const wchar_t* dllName = L"AliceScripts.dll");

    /// DLL 을 언로드하고, 동적 스크립트 함수 포인터를 모두 해제합니다.
    /// - 살아있는 스크립트 인스턴스가 있으면 언로드를 거부하고 false 를 반환합니다.
    bool ScriptHotReload_Unload();

    /// 현재 스크립트 DLL이 로드되어 있는지 여부
    bool ScriptHotReload_IsLoaded();
}



