#include "TimeOptionScript1.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "Runtime/UI/BindWidget.h"
#include "Runtime/Input/InputTypes.h"

namespace Alice
{
    // 이 스크립트를 리플렉션/팩토리 시스템에 등록합니다.
    REGISTER_SCRIPT(TimeOptionScript1);

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

    void TimeOptionScript1::Start()
    {
        World* w = GetWorld();
        if (!w)
            return;

        // 루트 위젯 찾기
        const EntityId root = FindRootWidgetByName(*w, Get_m_rootWidgetName());
        if (root == InvalidEntityId)
        {
            ALICE_LOG_WARN("[TimeOptionScript1] Root widget not found: %s", Get_m_rootWidgetName().c_str());
            return;
        }

        // 설정 창 UI 찾기
        const std::string settingBoardName = Get_m_settingBoardName();
        if (!settingBoardName.empty())
        {
            m_settingBoardEntityId = AliceUI::FindWidgetByName(*w, root, settingBoardName);
            if (m_settingBoardEntityId == InvalidEntityId)
            {
                ALICE_LOG_WARN("[TimeOptionScript1] Setting board widget not found: %s", settingBoardName.c_str());
            }
        }

        // 초기 상태: 설정 창 숨김
        SetUIVisibility(m_settingBoardEntityId, AliceUI::UIVisibility::Collapsed);
        m_isPaused = false;
    }

    void TimeOptionScript1::Update(float deltaTime)
    {
        World* w = GetWorld();
        if (!w)
            return;

        // ESC 키로 설정 창 열기/닫기 및 시간 정지
        if (Get_m_enableEscToggle())
        {
            auto* input = Input();
            if (input && input->GetKeyDown(KeyCode::Escape))
            {
                m_isPaused = !m_isPaused;
                
                if (m_isPaused)
                {
                    // 일시정지: 설정 창 표시, 게임 시간 멈춤, 커서 표시
                    SetUIVisibility(m_settingBoardEntityId, AliceUI::UIVisibility::Visible);
                    input->StopDeltaTime(true);
                    input->SetCursorVisible(true);
                    input->SetCursorLocked(false);
                    ALICE_LOG_INFO("[TimeOptionScript1] ESC pressed: Game paused, setting board opened");
                }
                else
                {
                    // 재개: 설정 창 숨김, 게임 시간 재개, 커서 숨김
                    SetUIVisibility(m_settingBoardEntityId, AliceUI::UIVisibility::Collapsed);
                    input->StopDeltaTime(false);
                    input->SetCursorVisible(false);
                    input->SetCursorLocked(true);
                    ALICE_LOG_INFO("[TimeOptionScript1] ESC pressed: Game resumed, setting board closed");
                }
            }
        }
    }

    void TimeOptionScript1::SetUIVisibility(EntityId entityId, AliceUI::UIVisibility visibility)
    {
        if (entityId == InvalidEntityId)
            return;

        World* w = GetWorld();
        if (!w)
            return;

        auto* widget = w->GetComponent<UIWidgetComponent>(entityId);
        if (widget)
        {
            widget->visibility = visibility;
        }
    }
}
