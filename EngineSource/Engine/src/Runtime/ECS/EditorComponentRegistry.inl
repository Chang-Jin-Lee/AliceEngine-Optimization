#pragma once

#include "Runtime/ECS/EditorComponentRegistry.h"
#include "Runtime/ECS/World.h"
#include <algorithm>

namespace Alice
{
    template<typename T>
    EditorComponentDesc MakeDefaultDesc(std::string_view displayName,
                                       std::string_view category,
                                       std::function<void(World&, EntityId)> addFn,
                                       bool addable,
                                       bool removable)
    {
        const rttr::type typeObj = rttr::type::get<T>();
        EditorComponentDesc d(typeObj);
        d.displayName = std::string(displayName.empty() ? typeObj.get_name().to_string() : std::string(displayName));
        d.category = std::string(category.empty() ? "Misc" : std::string(category));

        d.addable = addable;
        d.removable = removable;

        d.has = [](World& w, EntityId e) { return w.GetComponent<T>(e) != nullptr; };

        if (addFn)
            d.add = std::move(addFn);
        else
            d.add = [](World& w, EntityId e) { w.AddComponent<T>(e); };

        d.remove = [removable](World& w, EntityId e)
        {
            if (!removable) return;
            w.RemoveComponent<T>(e);
        };

        d.getInstance = [](World& w, EntityId e) -> rttr::instance
        {
            if (auto* p = w.GetComponent<T>(e))
                return *p; // lvalue ref instance
            return rttr::instance{};
        };

        return d;
    }

    template<typename T>
    void EditorComponentRegistry::Register(std::string_view displayName,
                                          std::string_view category,
                                          std::function<void(World&, EntityId)> addFn,
                                          bool addable,
                                          bool removable)
    {
        // 중복 등록 방지
        const rttr::type t = rttr::type::get<T>();
        if (Find(t)) return;

        m_list.push_back(MakeDefaultDesc<T>(displayName, category, std::move(addFn), addable, removable));
    }
}
