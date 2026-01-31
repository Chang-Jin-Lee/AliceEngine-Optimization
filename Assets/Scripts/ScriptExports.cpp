#include "Core/IScript.h"
#include "Core/ScriptFactory.h"
#include "Core/Logger.h"

// 동적 스크립트 DLL이 내보내는 간단한 C API 입니다.
// - 엔진 쪽에서 GetProcAddress 로 이 함수들을 찾아서
//   ScriptFactory 에 연결합니다.

extern "C"
{
    __declspec(dllexport) int Alice_GetDynamicScriptCount()
    {
        auto names = Alice::ScriptFactory::GetRegisteredScriptNames();
        return static_cast<int>(names.size());
    }

    __declspec(dllexport) bool Alice_GetDynamicScriptName(int index, char* outName, int maxLen)
    {
        if (!outName || maxLen <= 0)
            return false;

        auto names = Alice::ScriptFactory::GetRegisteredScriptNames();
        if (index < 0 || index >= static_cast<int>(names.size()))
            return false;

        const std::string& n = names[static_cast<std::size_t>(index)];
#ifdef _MSC_VER
        strcpy_s(outName, static_cast<std::size_t>(maxLen), n.c_str());
#else
        std::strncpy(outName, n.c_str(), static_cast<std::size_t>(maxLen));
        outName[maxLen - 1] = '\0';
#endif
        return true;
    }

    __declspec(dllexport) Alice::IScript* Alice_CreateDynamicScript(const char* name)
    {
        if (!name) return nullptr;

        auto ptr = Alice::ScriptFactory::Create(name);
        return ptr.release();
    }
}



