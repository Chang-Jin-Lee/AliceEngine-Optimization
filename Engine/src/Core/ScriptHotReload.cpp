#include "Core/ScriptHotReload.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <filesystem>
#include <string>

#include "Core/ScriptFactory.h"
#include "Core/Logger.h"

namespace Alice
{
    namespace
    {
        HMODULE g_ScriptModule = nullptr;

        std::filesystem::path GetExecutableDirectory()
        {
            wchar_t pathW[MAX_PATH] = {};
            const DWORD len = ::GetModuleFileNameW(nullptr, pathW, MAX_PATH);
            if (len == 0 || len == MAX_PATH)
            {
                return std::filesystem::current_path();
            }

            std::filesystem::path exePath(pathW);
            return exePath.parent_path();
        }

        bool LoadInternal(const wchar_t* dllName)
        {
            if (!dllName)
                return false;

            // 이전에 로드된 모듈이 있다면 먼저 언로드
            if (g_ScriptModule)
            {
                ScriptHotReload_Unload();
            }

            const auto exeDir = GetExecutableDirectory();
            const auto dllPath = exeDir / dllName;

            HMODULE mod = ::LoadLibraryW(dllPath.c_str());
            if (!mod)
            {
                ALICE_LOG_WARN("ScriptHotReload: failed to load DLL \"%ls\"", dllPath.c_str());
                SetDynamicScriptFunctions(nullptr, nullptr, nullptr);
                return false;
            }

            auto getCount = reinterpret_cast<DynamicScriptCountFunc>(
                ::GetProcAddress(mod, "Alice_GetDynamicScriptCount"));
            auto getName  = reinterpret_cast<DynamicScriptGetNameFunc>(
                ::GetProcAddress(mod, "Alice_GetDynamicScriptName"));
            auto createFn = reinterpret_cast<DynamicScriptCreateFunc>(
                ::GetProcAddress(mod, "Alice_CreateDynamicScript"));

            if (!getCount || !getName || !createFn)
            {
                ALICE_LOG_WARN("ScriptHotReload: DLL \"%ls\" is missing required world script exports", dllPath.c_str());
                ::FreeLibrary(mod);
                SetDynamicScriptFunctions(nullptr, nullptr, nullptr);
                return false;
            }

            g_ScriptModule = mod;
            SetDynamicScriptFunctions(createFn, getCount, getName);

            ALICE_LOG_INFO("ScriptHotReload: loaded \"%ls\"", dllPath.c_str());
            return true;
        }
    }

    bool ScriptHotReload_Load(const wchar_t* dllName)
    {
        return LoadInternal(dllName);
    }

    bool ScriptHotReload_Reload(const wchar_t* dllName)
    {
        return LoadInternal(dllName);
    }

    void ScriptHotReload_Unload()
    {
        if (g_ScriptModule)
        {
            ::FreeLibrary(g_ScriptModule);
            g_ScriptModule = nullptr;
        }

        // 동적 스크립트 함수 포인터 해제
        SetDynamicScriptFunctions(nullptr, nullptr, nullptr);

        ALICE_LOG_INFO("ScriptHotReload: unloaded script DLL");
    }
}
