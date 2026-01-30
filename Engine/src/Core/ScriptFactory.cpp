#include "Core/ScriptFactory.h"
#include "Core/IScript.h"

#include <unordered_map>

namespace Alice
{
    namespace
    {
        // 전역 스크립트 레지스트리 (간단한 이름 → 생성 함수 매핑)
        std::unordered_map<std::string, ScriptCreateFunc>& GetScriptRegistry()
        {
            static std::unordered_map<std::string, ScriptCreateFunc> s_registry;
            return s_registry;
        }

        // 동적 스크립트 DLL (라이브 코딩)에서 제공하는 함수 포인터들
        DynamicScriptCreateFunc   g_DynCreate  = nullptr;
        DynamicScriptCountFunc    g_DynCount   = nullptr;
        DynamicScriptGetNameFunc  g_DynGetName = nullptr;
    }

    // === ScriptFactory 구현 및 동적 스크립트 함수 ===

    void SetDynamicScriptFunctions(DynamicScriptCreateFunc   createFn,
                                   DynamicScriptCountFunc    countFn,
                                   DynamicScriptGetNameFunc  getNameFn)
    {
        g_DynCreate  = createFn;
        g_DynCount   = countFn;
        g_DynGetName = getNameFn;
    }

    void ScriptFactory::Register(const char* name, ScriptCreateFunc func)
    {
        if (!name || !func)
            return;

        auto& registry = GetScriptRegistry();
        registry[name] = func;
    }

    std::unique_ptr<IScript> ScriptFactory::Create(const char* name)
    {
        if (!name) return nullptr;

        // 1) 정적(내장) 스크립트 레지스트리에서 먼저 찾습니다.
        auto& registry = GetScriptRegistry();
        auto  it       = registry.find(name);
        if (it != registry.end())
        {
            IScript* raw = it->second();
            return std::unique_ptr<IScript>(raw);
        }

        // 2) 동적 스크립트 DLL 이 있다면, 그쪽에서 생성 시도
        if (g_DynCreate)
        {
            IScript* raw = g_DynCreate(name);
            if (raw)
            {
                return std::unique_ptr<IScript>(raw);
            }
        }

        return nullptr;
    }

    std::vector<std::string> ScriptFactory::GetRegisteredScriptNames()
    {
        std::vector<std::string> result;

        // 1) 정적(내장) 스크립트들
        auto& registry = GetScriptRegistry();
        result.reserve(registry.size());

        for (const auto& [name, _] : registry)
        {
            result.push_back(name);
        }

        // 2) 동적 스크립트 DLL 이 제공하는 스크립트들
        if (g_DynCount && g_DynGetName)
        {
            const int count = g_DynCount();
            for (int i = 0; i < count; ++i)
            {
                char buffer[128] = {};
                if (g_DynGetName(i, buffer, static_cast<int>(sizeof(buffer))))
                {
                    result.emplace_back(buffer);
                }
            }
        }

        return result;
    }
}
