#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <functional>
#include <rttr/type.h>
#include <rttr/instance.h>

#include "Core/Entity.h"

namespace Alice
{
    class World;

    struct EditorComponentDesc
    {
        rttr::type type;
        std::string displayName;
        std::string category;

        bool addable = true;
        bool removable = true;

        std::function<bool(World&, EntityId)> has;
        std::function<void(World&, EntityId)> add;
        std::function<void(World&, EntityId)> remove;
        std::function<rttr::instance(World&, EntityId)> getInstance; // Inspector/Undo용

        // rttr::type은 기본 생성자가 없으므로 type을 받는 생성자만 제공
        EditorComponentDesc(rttr::type t) : type(t) {}
    };

    class EditorComponentRegistry
    {
    public:
        static EditorComponentRegistry& Get()
        {
            static EditorComponentRegistry inst;
            return inst;
        }

        const std::vector<EditorComponentDesc>& All() const { return m_list; }

        const EditorComponentDesc* Find(rttr::type t) const
        {
            for (auto& d : m_list)
                if (d.type == t)
                    return &d;
            return nullptr;
        }

        template<typename T>
        void Register(std::string_view displayName,
                     std::string_view category,
                     std::function<void(World&, EntityId)> addFn = {},
                     bool addable = true,
                     bool removable = true);

        void SortByCategoryThenName();

    private:
        std::vector<EditorComponentDesc> m_list;
    };

    // 링크 강제용 (static lib로 분리될 때 대비)
    void LinkEditorComponentRegistry();
}

// 템플릿 구현은 헤더에 포함 (링크 이슈 방지)
#include "Core/EditorComponentRegistry.inl"
