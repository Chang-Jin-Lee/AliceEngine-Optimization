#include "Core/EditorComponentRegistry.h"
#include <algorithm>

namespace Alice
{
    void LinkEditorComponentRegistry() {} // 호출만 되면 링크 강제됨

    void EditorComponentRegistry::SortByCategoryThenName()
    {
        std::sort(m_list.begin(), m_list.end(),
            [](const EditorComponentDesc& a, const EditorComponentDesc& b)
            {
                if (a.category != b.category)
                    return a.category < b.category;
                return a.displayName < b.displayName;
            });
    }
}
