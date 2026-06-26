#include "Runtime/Scripting/ScriptHotReload.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include <filesystem>
#include <string>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"

namespace Alice
{
    class World;
    class ResourceManager;

    namespace
    {
        HMODULE g_ScriptModule = nullptr;
        World* g_BindWorld = nullptr;
        ResourceManager* g_BindResources = nullptr;

        using BindServicesFunc = void (*)(World*, ResourceManager*);
        using UnbindServicesFunc = void (*)();
        BindServicesFunc g_BindServices = nullptr;
        UnbindServicesFunc g_UnbindServices = nullptr;

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
			const auto dllDir = exeDir / "dll";
			const auto dllPathInDir = dllDir / dllName;
			const auto dllPathExe = exeDir / dllName;

			const wchar_t* loadedPath = dllPathInDir.c_str();
			HMODULE mod = ::LoadLibraryW(loadedPath);
			if (!mod)
			{
				loadedPath = dllPathExe.c_str();
				mod = ::LoadLibraryW(loadedPath);
			}
            if (!mod)
            {
				ALICE_LOG_WARN("ScriptHotReload: failed to load DLL \"%ls\" (also tried \"%ls\")",
					dllPathInDir.c_str(),
					dllPathExe.c_str());
                SetDynamicScriptFunctions(nullptr, nullptr, nullptr);
                return false;
            }

            auto getCount = reinterpret_cast<DynamicScriptCountFunc>(
                ::GetProcAddress(mod, "Alice_GetDynamicScriptCount"));
            auto getName  = reinterpret_cast<DynamicScriptGetNameFunc>(
                ::GetProcAddress(mod, "Alice_GetDynamicScriptName"));
            auto createFn = reinterpret_cast<DynamicScriptCreateFunc>(
                ::GetProcAddress(mod, "Alice_CreateDynamicScript"));
            auto bindFn = reinterpret_cast<BindServicesFunc>(
                ::GetProcAddress(mod, "Alice_BindEngineServices"));
            auto unbindFn = reinterpret_cast<UnbindServicesFunc>(
                ::GetProcAddress(mod, "Alice_UnbindEngineServices"));

            if (!getCount || !getName || !createFn)
            {
                ALICE_LOG_WARN("ScriptHotReload: DLL \"%ls\" is missing required world script exports", dllPathInDir.c_str());
                ::FreeLibrary(mod);
                SetDynamicScriptFunctions(nullptr, nullptr, nullptr);
                return false;
            }

            g_ScriptModule = mod;
            SetDynamicScriptFunctions(createFn, getCount, getName);
            g_BindServices = bindFn;
            g_UnbindServices = unbindFn;
            if (g_BindServices)
                g_BindServices(g_BindWorld, g_BindResources);

            ALICE_LOG_INFO("ScriptHotReload: loaded \"%ls\"", loadedPath);
            return true;
        }
    }

    void ScriptHotReload_SetServices(World* world, ResourceManager* resources)
    {
        g_BindWorld = world;
        g_BindResources = resources;
        if (g_ScriptModule && g_BindServices)
            g_BindServices(g_BindWorld, g_BindResources);
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
            if (g_UnbindServices)
                g_UnbindServices();
            ::FreeLibrary(g_ScriptModule);
            g_ScriptModule = nullptr;
        }

        // 동적 스크립트 함수 포인터 해제
        SetDynamicScriptFunctions(nullptr, nullptr, nullptr);
        g_BindServices = nullptr;
        g_UnbindServices = nullptr;

        ALICE_LOG_INFO("ScriptHotReload: unloaded script DLL");
    }
}
