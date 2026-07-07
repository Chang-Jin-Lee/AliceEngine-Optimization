#include "Runtime/Scripting/ScriptDomain.h"

#include "Runtime/ECS/World.h"
#include "Runtime/Resources/Serialization/JsonRttr.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Scripting/ScriptHotReload.h"
#include "Runtime/Scripting/ScriptInstanceTracker.h"
#include "Runtime/Foundation/Logger.h"
#include "ThirdParty/json/json.hpp"

#include <string>
#include <vector>

namespace Alice::ScriptDomain
{
    namespace
    {
        struct ScriptReloadSnap
        {
            std::string name;
            bool enabled{};
            nlohmann::json props;
        };

        struct EntityReloadSnap
        {
            EntityId id{};
            std::vector<ScriptReloadSnap> scripts;
        };

        void SnapshotAndDestroyScripts(World& world, std::vector<EntityReloadSnap>& out)
        {
            out.clear();
            auto& map = world.GetAllScriptsInWorld();
            out.reserve(map.size());
            for (auto& [id, list] : map)
            {
                EntityReloadSnap e{};
                e.id = id;
                e.scripts.reserve(list.size());
                for (auto& sc : list)
                {
                    ScriptReloadSnap s{};
                    s.name = sc.scriptName;
                    s.enabled = sc.enabled;
                    if (sc.instance && !sc.scriptName.empty())
                    {
                        rttr::type t = rttr::type::get_by_name(sc.scriptName);
                        s.props = JsonRttr::ToJsonObject(*sc.instance, t);
                        sc.instance->OnDisable();
                        sc.instance->OnDestroy();
                        sc.instance.reset();
                    }
                    sc.awoken = false;
                    sc.started = false;
                    sc.wasEnabled = sc.enabled;
                    sc.defaultsApplied = false;
                    e.scripts.push_back(std::move(s));
                }
                out.push_back(std::move(e));
            }
        }

        void RestoreScripts(World& world, const std::vector<EntityReloadSnap>& snaps)
        {
            auto& map = world.GetAllScriptsInWorld();
            for (const auto& e : snaps)
            {
                auto it = map.find(e.id);
                if (it == map.end())
                    continue;
                std::vector<ScriptComponent> rebuilt;
                rebuilt.reserve(e.scripts.size());
                for (const auto& s : e.scripts)
                {
                    if (s.name.empty())
                        continue;
                    ScriptComponent sc{};
                    sc.scriptName = s.name;
                    sc.enabled = s.enabled;
                    sc.instance = ScriptFactory::Create(s.name.c_str());
                    if (!sc.instance)
                    {
                        ALICE_LOG_WARN("ScriptDomain: script \"%s\" not found in new DLL. Component kept without instance.", s.name.c_str());
                        continue;
                    }
                    sc.instance->SetContext(&world, e.id);
                    rttr::instance inst = *sc.instance;
                    rttr::type t = rttr::type::get_by_name(sc.scriptName);
                    JsonRttr::FromJsonObject(inst, s.props, t);
                    sc.defaultsApplied = true;
                    rebuilt.push_back(std::move(sc));
                }
                it->second = std::move(rebuilt);
                if (it->second.empty())
                    map.erase(it);
            }
        }

        // Task 4에서 구현이 채워진다. 지금은 no-op.
        void ClearDllOriginatedCallbacks(World& world)
        {
            (void)world;
        }

        bool TeardownForUnload(World& world, std::vector<EntityReloadSnap>* outSnaps)
        {
            std::vector<EntityReloadSnap> localSnaps;
            SnapshotAndDestroyScripts(world, outSnaps ? *outSnaps : localSnaps);
            ClearDllOriginatedCallbacks(world);

            if (!ScriptHotReload_Unload())
            {
                ALICE_LOG_ERRORF("ScriptDomain: unload blocked by alive instances. See log above.");
                return false;
            }
            return true;
        }
    }

    bool LoadInitial()
    {
        return ScriptHotReload_Load();
    }

    bool Reload(World& world)
    {
        std::vector<EntityReloadSnap> snaps;
        if (!TeardownForUnload(world, &snaps))
            return false;

        if (!ScriptHotReload_Reload())
        {
            ALICE_LOG_ERRORF("ScriptDomain: reload failed after unload.");
            return false;
        }

        RestoreScripts(world, snaps);
        return true;
    }

    bool Unload(World& world)
    {
        return TeardownForUnload(world, nullptr);
    }

    bool IsLoaded()
    {
        return ScriptHotReload_IsLoaded();
    }
}
