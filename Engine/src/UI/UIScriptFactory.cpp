#include "UIScriptFactory.h"
#include "IUIScript.h"
#include "Core/Logger.h"

#include <unordered_map>

std::unordered_map<std::string, UIScriptCreateFunc>& UIScriptFactory::GetRegistry()
{
    static std::unordered_map<std::string, UIScriptCreateFunc> s_registry;
    return s_registry;
}

void UIScriptFactory::Register(const char* name, UIScriptCreateFunc func)
{
    if (!name || !func)
    {
        //ALICE_LOG_WARN("[UIScriptFactory::Register] Invalid name or func!");
        return;
    }

    auto& registry = GetRegistry();
    registry[name] = func;
    //ALICE_LOG_INFO("[UIScriptFactory::Register] Registered UI script: %s", name);
}

std::unique_ptr<IUIScript> UIScriptFactory::Create(const char* name)
{
    if (!name)
    {
        //ALICE_LOG_WARN("[UIScriptFactory::Create] name is null!");
        return nullptr;
    }

    auto& registry = GetRegistry();
    //ALICE_LOG_INFO("[UIScriptFactory::Create] Looking for: %s (registry size: %zu)", name, registry.size());
    
    auto it = registry.find(name);
    if (it != registry.end())
    {
        //ALICE_LOG_INFO("[UIScriptFactory::Create] Found in registry, creating instance...");
        IUIScript* raw = it->second();
        /*if (raw)
        {
            ALICE_LOG_INFO("[UIScriptFactory::Create] Instance created successfully: %p", raw);
        }
        else
        {
            ALICE_LOG_WARN("[UIScriptFactory::Create] Factory function returned nullptr!");
        }*/
        return std::unique_ptr<IUIScript>(raw);
    }

    // ALICE_LOG_WARN("[UIScriptFactory::Create] Script not found in registry: %s", name);
    // ALICE_LOG_INFO("[UIScriptFactory::Create] Available scripts:");
    for (const auto& [regName, _] : registry)
    {
        ALICE_LOG_INFO("[UIScriptFactory::Create]   - %s", regName.c_str());
    }

    return nullptr;
}

std::vector<std::string> UIScriptFactory::GetRegisteredUIScriptNames()
{
    std::vector<std::string> result;

    auto& registry = GetRegistry();
    result.reserve(registry.size());

    for (const auto& [name, _] : registry)
    {
        result.push_back(name);
    }

    return result;
}
