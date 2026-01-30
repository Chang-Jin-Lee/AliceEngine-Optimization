#pragma once

namespace Alice
{
    /// AliceScripts.dll 을 로드하여 동적 스크립트 함수 포인터를 등록합니다.
    /// - dllName 은 보통 "AliceScripts.dll" 입니다.
    bool ScriptHotReload_Load(const wchar_t* dllName = L"AliceScripts.dll");

    /// 이미 로드된 DLL 을 언로드 한 뒤 다시 로드합니다.
    bool ScriptHotReload_Reload(const wchar_t* dllName = L"AliceScripts.dll");

    /// DLL 을 언로드하고, 동적 스크립트 함수 포인터를 모두 해제합니다.
    void ScriptHotReload_Unload();
}



