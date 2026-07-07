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

        // DLL 쪽(별도 정적 라이브러리 인스턴스) 스크립트 생존 카운트 조회.
        // Engine은 정적 라이브러리이므로 ScriptInstanceTracker의 익명 네임스페이스
        // 전역 변수가 Launch.exe와 AliceScripts.dll에 각각 따로 존재한다.
        // DLL 언로드 안전성은 반드시 DLL 쪽 카운트를 봐야 한다.
        using AliveCountFunc = int (*)();
        using AliveNameFunc  = bool (*)(int, char*, int);
        AliveCountFunc g_DllAliveCount = nullptr;
        AliveNameFunc  g_DllAliveName  = nullptr;

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
                if (!ScriptHotReload_Unload())
                    return false;
            }

            const auto exeDir = GetExecutableDirectory();
			const auto dllDir = exeDir / "dll";
			const auto dllPathInDir = dllDir / dllName;
			const auto dllPathExe = exeDir / dllName;

			// 원본을 직접 로드하면 파일이 잠겨 다음 빌드/복사가 실패한다.
			// → "_live" 복사본을 만들어 그 파일을 로드한다. (원본은 항상 잠금 없음)
			auto MakeLiveCopy = [](const std::filesystem::path& src) -> std::filesystem::path
			{
				std::error_code ec;
				if (!std::filesystem::exists(src, ec) || ec)
					return {};
				std::filesystem::path live = src;
				live.replace_filename(src.stem().wstring() + L"_live" + src.extension().wstring());
				std::filesystem::copy_file(src, live,
					std::filesystem::copy_options::overwrite_existing, ec);
				if (ec)
				{
					// 복사 실패(이전 live가 아직 로드 중 등) 시 원본 직접 로드로 폴백
					return src;
				}
				return live;
			};

			std::filesystem::path candidate = MakeLiveCopy(dllPathInDir);
			if (candidate.empty())
				candidate = MakeLiveCopy(dllPathExe);

			std::wstring loadedPathStr = candidate.wstring();
			HMODULE mod = loadedPathStr.empty() ? nullptr : ::LoadLibraryW(loadedPathStr.c_str());
			const wchar_t* loadedPath = loadedPathStr.c_str();
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
            // 선택적: DLL 쪽 스크립트 생존 카운트 (구 DLL에는 없을 수 있음 — 없어도 로드는 계속 진행)
            auto aliveCountFn = reinterpret_cast<AliveCountFunc>(
                ::GetProcAddress(mod, "Alice_GetAliveScriptCount"));
            auto aliveNameFn = reinterpret_cast<AliveNameFunc>(
                ::GetProcAddress(mod, "Alice_GetAliveScriptName"));

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
            g_DllAliveCount = aliveCountFn;
            g_DllAliveName = aliveNameFn;
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

    bool ScriptHotReload_Unload()
    {
        if (g_ScriptModule)
        {
            // 불변식: DLL 쪽에 살아있는 스크립트 인스턴스가 있으면 언로드 금지.
            // (vtable이 사라진 뒤 파괴/호출되면 UB — 누수·크래시의 근원)
            // Engine은 정적 라이브러리라 ScriptInstanceTracker의 전역이 exe/DLL에 각각
            // 따로 존재한다 — 위험한 vtable은 DLL 쪽에만 있으므로 반드시 DLL 쪽 카운트를 조회한다.
            if (g_DllAliveCount)
            {
                const int alive = g_DllAliveCount();
                if (alive > 0)
                {
                    ALICE_LOG_ERRORF("ScriptHotReload: unload BLOCKED. %d script instance(s) still alive:", alive);
                    if (g_DllAliveName)
                    {
                        char nameBuf[256] = {};
                        for (int i = 0; i < alive; ++i)
                        {
                            if (g_DllAliveName(i, nameBuf, static_cast<int>(sizeof(nameBuf))))
                                ALICE_LOG_ERRORF("  - %s", nameBuf);
                        }
                    }
                    return false;
                }
            }
            else
            {
                ALICE_LOG_WARN("ScriptHotReload: DLL does not export alive-count; unload safety cannot be verified.");
            }

            if (g_UnbindServices)
                g_UnbindServices();
            ::FreeLibrary(g_ScriptModule);
            g_ScriptModule = nullptr;
        }

        // 동적 스크립트 함수 포인터 해제
        SetDynamicScriptFunctions(nullptr, nullptr, nullptr);
        g_BindServices = nullptr;
        g_UnbindServices = nullptr;
        g_DllAliveCount = nullptr;
        g_DllAliveName = nullptr;

        ALICE_LOG_INFO("ScriptHotReload: unloaded script DLL");
        return true;
    }

    bool ScriptHotReload_IsLoaded()
    {
        return g_ScriptModule != nullptr;
    }
}
