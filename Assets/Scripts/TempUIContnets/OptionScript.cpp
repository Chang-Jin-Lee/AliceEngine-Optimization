#include "OptionScript.h"

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Input/Input.h"
#include "Runtime/UI/UIWidgetComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(OptionScript);

    namespace
    {
        EntityId SearchRootWidgetByName(World& world, const std::string& name)
        {
            if (name.empty())
                return InvalidEntityId;

            for (auto [id, widget] : world.GetComponents<UIWidgetComponent>())
            {
                const std::string widgetName = widget.widgetName.empty() ? world.GetEntityName(id) : widget.widgetName;
                if (!widgetName.empty() && widgetName == name)
                    return id;
            }
            return InvalidEntityId;
        }

        void SetChildrenVisibility(World& world, EntityId root, AliceUI::UIVisibility visibility)
        {
            auto children = world.GetChildren(root);
            ALICE_LOG_INFO("[OptionScript] Setting visibility for %zu children of entity %llu", 
                children.size(), static_cast<unsigned long long>(root));
            
            for (EntityId child : children)
            {
                if (auto* widget = world.GetComponent<UIWidgetComponent>(child))
                {
                    widget->visibility = visibility;
                    ALICE_LOG_INFO("[OptionScript] Set child %llu visibility to %d", 
                        static_cast<unsigned long long>(child), static_cast<int>(visibility));
                }
                SetChildrenVisibility(world, child, visibility);  // 재귀 호출
            }
        }
    }

    void OptionScript::Start()
    {
        World* w = GetWorld();
        if (!w)
            return;

        if (!Get_rootWidgetName().empty())
        {
            rootEntity = SearchRootWidgetByName(*w, Get_rootWidgetName());
        }

        if (rootEntity == InvalidEntityId)
        {
            rootEntity = GetOwnerId();
        }

        if (rootEntity == InvalidEntityId)
        {
            ALICE_LOG_WARN("[OptionScript] Root entity not found (rootWidgetName=%s)", Get_rootWidgetName().c_str());
        }
    }

    void OptionScript::Update(float /*deltaTime*/)
    {
        if (rootEntity == InvalidEntityId)
            return;

        auto* input = Input();
        if (!input)
            return;

        World* w = GetWorld();  // null 체크 추가
        if (!w)
            return;

        if (input->GetKeyDown(KeyCode::Escape))
        {
            childrenVisible = !childrenVisible;
            const auto vis = childrenVisible ? AliceUI::UIVisibility::Visible : AliceUI::UIVisibility::Collapsed;
            
            // 디버깅 로그 추가
            ALICE_LOG_INFO("[OptionScript] ESC pressed. Setting visibility to %s", 
                childrenVisible ? "Visible" : "Collapsed");
            
            SetChildrenVisibility(*w, rootEntity, vis);
            
            // 자식 개수 확인
            auto children = w->GetChildren(rootEntity);
            ALICE_LOG_INFO("[OptionScript] Found %zu children", children.size());
        }
    }

    void OptionScript::ExampleFunction()
    {
        if (auto* transform = GetComponent<TransformComponent>())
        {
            transform->position = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
        }
    }
}
