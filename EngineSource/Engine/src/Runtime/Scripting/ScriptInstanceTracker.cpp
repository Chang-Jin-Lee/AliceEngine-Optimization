#include "Runtime/Scripting/ScriptInstanceTracker.h"
#include "Runtime/Scripting/IScript.h"

#include <mutex>
#include <unordered_set>

namespace Alice::ScriptInstanceTracker
{
    namespace
    {
        std::mutex g_mutex;
        std::unordered_set<IScript*> g_alive;
    }

    void OnCreated(IScript* instance)
    {
        if (!instance) return;
        std::lock_guard<std::mutex> lock(g_mutex);
        g_alive.insert(instance);
    }

    void OnDestroyed(IScript* instance)
    {
        if (!instance) return;
        std::lock_guard<std::mutex> lock(g_mutex);
        g_alive.erase(instance);
    }

    std::size_t AliveCount()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        return g_alive.size();
    }

    std::vector<std::string> AliveNames()
    {
        std::lock_guard<std::mutex> lock(g_mutex);
        std::vector<std::string> names;
        names.reserve(g_alive.size());
        for (IScript* s : g_alive)
            names.emplace_back(s->GetName());
        return names;
    }
}
