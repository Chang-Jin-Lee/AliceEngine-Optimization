#include "ChangeScreeenScript.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "Runtime/UI/UIButtonComponent.h"
#include "Runtime/UI/BindWidget.h"

namespace Alice
{
    // 이 스크립트를 리플렉션/팩토리 시스템에 등록합니다.
    REGISTER_SCRIPT(ChangeScreeenScript);

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

    void ChangeScreeenScript::Start()
    {
        World* w = GetWorld();
        if (!w)
            return;

        // 루트 위젯 찾기
        const EntityId root = FindRootWidgetByName(*w, Get_m_rootWidgetName());
        if (root == InvalidEntityId)
        {
            ALICE_LOG_WARN("[NewScript] Root widget not found: %s", Get_m_rootWidgetName().c_str());
            return;
        }

        // 버튼1 찾기
        const std::string button1Name = Get_m_button1Name();
        if (!button1Name.empty())
        {
            const EntityId button1Entity = AliceUI::FindWidgetByName(*w, root, button1Name);
            if (button1Entity != InvalidEntityId)
            {
                m_button1 = w->GetComponent<UIButtonComponent>(button1Entity);
                if (!m_button1)
                {
                    ALICE_LOG_WARN("[NewScript] Button1 component not found: %s", button1Name.c_str());
                }
            }
            else
            {
                ALICE_LOG_WARN("[NewScript] Button1 widget not found: %s", button1Name.c_str());
            }
        }

        // 버튼2 찾기
        const std::string button2Name = Get_m_button2Name();
        if (!button2Name.empty())
        {
            const EntityId button2Entity = AliceUI::FindWidgetByName(*w, root, button2Name);
            if (button2Entity != InvalidEntityId)
            {
                m_button2 = w->GetComponent<UIButtonComponent>(button2Entity);
                if (!m_button2)
                {
                    ALICE_LOG_WARN("[NewScript] Button2 component not found: %s", button2Name.c_str());
                }
            }
            else
            {
                ALICE_LOG_WARN("[NewScript] Button2 widget not found: %s", button2Name.c_str());
            }
        }

        // UI1 찾기
        const std::string ui1Name = Get_m_ui1Name();
        if (!ui1Name.empty())
        {
            m_ui1EntityId = AliceUI::FindWidgetByName(*w, root, ui1Name);
            if (m_ui1EntityId == InvalidEntityId)
            {
                ALICE_LOG_WARN("[NewScript] UI1 widget not found: %s", ui1Name.c_str());
            }
        }

        // UI2 찾기
        const std::string ui2Name = Get_m_ui2Name();
        if (!ui2Name.empty())
        {
            m_ui2EntityId = AliceUI::FindWidgetByName(*w, root, ui2Name);
            if (m_ui2EntityId == InvalidEntityId)
            {
                ALICE_LOG_WARN("[ChangeScreeenScript] UI2 widget not found: %s", ui2Name.c_str());
            }
        }

        // UI_Setting 버튼 찾기
        const std::string settingButtonName = Get_m_settingButtonName();
        if (!settingButtonName.empty())
        {
            const EntityId settingButtonEntity = AliceUI::FindWidgetByName(*w, root, settingButtonName);
            if (settingButtonEntity != InvalidEntityId)
            {
                m_settingButton = w->GetComponent<UIButtonComponent>(settingButtonEntity);
                if (!m_settingButton)
                {
                    ALICE_LOG_WARN("[ChangeScreeenScript] Setting button component not found: %s", settingButtonName.c_str());
                }
            }
            else
            {
                ALICE_LOG_WARN("[ChangeScreeenScript] Setting button widget not found: %s", settingButtonName.c_str());
            }
        }
        const std::string settingBoardName = Get_m_settingBoardName();
        if (!settingBoardName.empty())
        {
            m_settingBoardEntityId = FindRootWidgetByName(*w, settingBoardName);
            if (m_settingBoardEntityId == InvalidEntityId)
            {
                ALICE_LOG_WARN("[ChangeScreeenScript] Setting board widget not found: %s", settingBoardName.c_str());
            }
        }

        // 초기 상태 설정: 두 UI 모두 숨김
        SetUIVisibility(m_ui1EntityId, AliceUI::UIVisibility::Collapsed);
        SetUIVisibility(m_ui2EntityId, AliceUI::UIVisibility::Collapsed);

        m_settingButtonClickedPrev = false;
        m_button1ClickedPrev = false;
        m_button2ClickedPrev = false;
        m_isUIOpened = false;
    }

    void ChangeScreeenScript::Update(float deltaTime)
    {
        World* w = GetWorld();
        if (!w)
            return;

        // UI_Setting 버튼 클릭 감지
        if (m_settingButton)
        {
            if (m_settingBoardEntityId != InvalidEntityId)
            {
                if (auto* boardWidget = w->GetComponent<UIWidgetComponent>(m_settingBoardEntityId))
                {
                    const bool boardVisible = (boardWidget->visibility == AliceUI::UIVisibility::Visible);
                    if (!boardVisible && m_isUIOpened)
                    {
                        m_isUIOpened = false;
                        SetUIVisibility(m_ui1EntityId, AliceUI::UIVisibility::Collapsed);
                        SetUIVisibility(m_ui2EntityId, AliceUI::UIVisibility::Collapsed);
                    }
                }
            }

            const bool settingButtonClicked = m_settingButton->ConsumeClick();
            if (settingButtonClicked && !m_settingButtonClickedPrev)
            {
                if (!m_isUIOpened)
                {
                    // 처음 열릴 때: UI1 visible, UI2 collapse
                    SetUIVisibility(m_ui1EntityId, AliceUI::UIVisibility::Visible);
                    SetUIVisibility(m_ui2EntityId, AliceUI::UIVisibility::Collapsed);
                    m_isUIOpened = true;
                    ALICE_LOG_INFO("[ChangeScreeenScript] Setting button clicked: UI opened (UI1 visible, UI2 collapsed)");
                }
                else
                {
                    // 이미 열려있으면 닫기 (두 UI 모두 숨김)
                    SetUIVisibility(m_ui1EntityId, AliceUI::UIVisibility::Collapsed);
                    SetUIVisibility(m_ui2EntityId, AliceUI::UIVisibility::Collapsed);
                    m_isUIOpened = false;
                    ALICE_LOG_INFO("[ChangeScreeenScript] Setting button clicked: UI closed");
                }
            }
            m_settingButtonClickedPrev = settingButtonClicked;
        }

        // UI가 열려있을 때만 버튼1/버튼2로 전환 가능
        if (m_isUIOpened)
        {
            // 버튼1 클릭 감지
            if (m_button1)
            {
                const bool button1Clicked = m_button1->ConsumeClick();
                if (button1Clicked && !m_button1ClickedPrev)
                {
                    // UI1 visible, UI2 collapse
                    SetUIVisibility(m_ui1EntityId, AliceUI::UIVisibility::Visible);
                    SetUIVisibility(m_ui2EntityId, AliceUI::UIVisibility::Collapsed);
                    ALICE_LOG_INFO("[ChangeScreeenScript] Button1 clicked: UI1 visible, UI2 collapsed");
                }
                m_button1ClickedPrev = button1Clicked;
            }

            // 버튼2 클릭 감지
            if (m_button2)
            {
                const bool button2Clicked = m_button2->ConsumeClick();
                if (button2Clicked && !m_button2ClickedPrev)
                {
                    // UI1 collapse, UI2 visible
                    SetUIVisibility(m_ui1EntityId, AliceUI::UIVisibility::Collapsed);
                    SetUIVisibility(m_ui2EntityId, AliceUI::UIVisibility::Visible);
                    ALICE_LOG_INFO("[ChangeScreeenScript] Button2 clicked: UI1 collapsed, UI2 visible");
                }
                m_button2ClickedPrev = button2Clicked;
            }
        }
    }

    void ChangeScreeenScript::SetUIVisibility(EntityId entityId, AliceUI::UIVisibility visibility)
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

    void ChangeScreeenScript::ExampleFunction()
    {
        // 리플렉션으로 등록된 함수 예시입니다.
        // 이 함수는 에디터에서 호출할 수 있습니다.
        
        // 예시: Transform 컴포넌트 가져오기
        if (auto* transform = GetComponent<TransformComponent>())
        {
            // 위치를 (0, 0, 0)으로 리셋하는 예시
            transform->position = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
        }
    }
}
