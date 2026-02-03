#include "SceneChangeButtonScript.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "Runtime/UI/BindWidget.h"

namespace Alice
{
    REGISTER_SCRIPT(SceneChangeButtonScript);

    namespace
    {
        EntityId FindRootWidgetByName(World& world, const std::string& name)
        {
            for (auto [id, widget] : world.GetComponents<UIWidgetComponent>())
            {
                const std::string widgetName = widget.widgetName.empty() ? world.GetEntityName(id) : widget.widgetName;
                if (!widgetName.empty() && widgetName == name)
                    return id;
            }
            return InvalidEntityId;
        }
    }

    void SceneChangeButtonScript::Start()
    {
        World* w = GetWorld();
        if (!w)
            return;

        // UI 루트 위젯 찾기
        const EntityId root = FindRootWidgetByName(*w, Get_rootWidgetName());
        if (root == InvalidEntityId)
        {
            ALICE_LOG_WARN("[SceneChangeButtonScript] Root widget not found: %s", Get_rootWidgetName().c_str());
            return;
        }

        // 위젯 바인딩
        const std::string buttonName = Get_buttonWidgetName();
        if (buttonName.empty())
        {
            ALICE_LOG_WARN("[SceneChangeButtonScript] Button widget name is empty");
            return;
        }

        const EntityId buttonEntity = AliceUI::FindWidgetByName(*w, root, buttonName);
        changeSceneButton = (buttonEntity != InvalidEntityId)
            ? w->GetComponent<UIButtonComponent>(buttonEntity)
            : nullptr;

        if (!changeSceneButton)
        {
            ALICE_LOG_WARN("[SceneChangeButtonScript] Button not found: %s", buttonName.c_str());
            return;
        }


        const std::string TextName = Get_TextWidgetName();
        if (TextName.empty())
        {
            ALICE_LOG_WARN("[SceneChangeButtonScript] Text widget name is empty");
            return;
        }

        const EntityId TextEntity = AliceUI::FindWidgetByName(*w, root, TextName);
        changeSceneButton = (buttonEntity != InvalidEntityId)
            ? w->GetComponent<UIButtonComponent>(buttonEntity)
            : nullptr;

        const std::string LineName = Get_UnderLineWidgetName();
        if (LineName.empty())
        {
            ALICE_LOG_WARN("[SceneChangeButtonScript] Line widget name is empty");
            return;
        }


        // 버튼 클릭 이벤트 등록 (델리게이트 방식)
        const EntityId ownerId = GetOwnerId();
        const std::uint32_t ownerGen = w->GetEntityGeneration(ownerId);
        const auto isValid = [w, ownerId, ownerGen, self = this]() -> bool
        {
            if (!w)
                return false;
            if (!w->IsEntityValid(ownerId, ownerGen))
                return false;
            const auto* scripts = w->GetScripts(ownerId);
            if (!scripts)
                return false;
            for (const auto& sc : *scripts)
            {
                if (sc.instance.get() == self)
                    return true;
            }
            return false;
        };

        // 버튼이 눌렸을 때 씬 변경
        changeSceneButton->AddOnReleasedSafe([this]()
        {
            auto* scenes = Scenes();
            if (!scenes)
            {
                ALICE_LOG_WARN("[SceneChangeButtonScript] SceneManager not available");
                return;
            }

            const std::string scenePath = Get_targetScenePath();
            if (scenePath.empty())
            {
                ALICE_LOG_WARN("[SceneChangeButtonScript] Target scene path is empty");
                return;
            }

            ALICE_LOG_INFO("[SceneChangeButtonScript] Changing scene to: %s", scenePath.c_str());
            scenes->LoadSceneFileRequest(scenePath.c_str());
        }, isValid);
    }

    void SceneChangeButtonScript::Update(float deltaTime)
    {
        // Update에서도 클릭을 감지할 수 있습니다 (ConsumeClick 방식)
        if (changeSceneButton && changeSceneButton->ConsumeClick())
        {
            auto* scenes = Scenes();
            if (!scenes)
            {
                ALICE_LOG_WARN("[SceneChangeButtonScript] SceneManager not available");
                return;
            }

            const std::string scenePath = Get_targetScenePath();
            if (scenePath.empty())
            {
                ALICE_LOG_WARN("[SceneChangeButtonScript] Target scene path is empty");
                return;
            }

            ALICE_LOG_INFO("[SceneChangeButtonScript] Button clicked! Changing scene to: %s", scenePath.c_str());
            scenes->LoadSceneFileRequest(scenePath.c_str());
        }

        (void)deltaTime;
    }
}
