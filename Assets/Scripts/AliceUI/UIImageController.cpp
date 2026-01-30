#include "UIImageController.h"

#include "Core/ScriptFactory.h"
#include "Core/Logger.h"
#include "Core/World.h"
#include "Core/InputTypes.h"

namespace Alice
{
    REGISTER_SCRIPT(UIImageController);

    RTTR_REGISTRATION
    {
        rttr::registration::class_<UIImageController>(UIImageController::_Refl_ClassName)
            .property("rootWidgetName", &UIImageController::rootWidgetName)
                (rttr::metadata("SerializeField", true));
    }

    namespace
    {
        // UI 루트 위젯을 이름으로 찾는 헬퍼
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

    void UIImageController::Start()
    {
        ALICE_LOG_INFO("UIImageController::Start()");
        // UI 트리에서 루트 위젯 찾기
        World* w = GetWorld();
        if (!w)
            return;

        const EntityId root = FindRootWidgetByName(*w, rootWidgetName);
        if (root == InvalidEntityId)
        {
            ALICE_LOG_INFO("[UIImageController] Root widget not found: %s", rootWidgetName.c_str());
            return;
        }
		else
		{
			ALICE_LOG_INFO("[UIImageController] Root widget founded!!!!!!: %s", rootWidgetName.c_str());
		}

        // 변수명과 같은 UI 위젯을 바인딩
        const auto result = AliceUI::BindWidgets(this, *w, root);
        if (result.missingRequired > 0)
            ALICE_LOG_INFO("[UIImageController] Missing required widgets: %d", result.missingRequired);
    }

    void UIImageController::Update(float /*deltaTime*/)
    {
        // 숫자 키(1~6)로 이미지 변경
		// 1. TargetImage 바인딩 체크
		if (!TargetImage)
		{
			// 매 프레임 로그가 남으면 콘솔이 마비되므로, 테스트용으로 한 번 확인하고 싶다면 주석을 해제하세요.
			 ALICE_LOG_ERRORF("UIImageController::Update - TargetImage is NULL! (Did BindWidgets fail?)");
			return;
		}

		ALICE_LOG_ERRORF("UIImageController::Update(float /*deltaTime*/)");

		// 2. Input 시스템 체크
		auto* input = Input();
		if (!input)
		{
			ALICE_LOG_ERRORF("UIImageController::Update - Input system is NULL!");
			return;
		}
		else
		{
			ALICE_LOG_ERRORF("UIImageController::Update - Input system is Enable!");
		}

		// 3. 키 입력 디버깅
		if (input->GetKeyDown(KeyCode::Alpha1))
		{
			ALICE_LOG_INFO("[UIImageController] Key 1 Pressed. Changing texture to: %s", m_imagePaths[0].c_str());
			TargetImage->texturePath = m_imagePaths[0];
		}
		if (input->GetKeyDown(KeyCode::Alpha2))
		{
			ALICE_LOG_INFO("[UIImageController] Key 2 Pressed. Changing texture to: %s", m_imagePaths[1].c_str());
			TargetImage->texturePath = m_imagePaths[1];
		}
		if (input->GetKeyDown(KeyCode::Alpha3))
		{
			ALICE_LOG_INFO("[UIImageController] Key 3 Pressed. Changing texture to: %s", m_imagePaths[2].c_str());
			TargetImage->texturePath = m_imagePaths[2];
		}
		if (input->GetKeyDown(KeyCode::Alpha4))
		{
			ALICE_LOG_INFO("[UIImageController] Key 4 Pressed. Changing texture to: %s", m_imagePaths[3].c_str());
			TargetImage->texturePath = m_imagePaths[3];
		}
		if (input->GetKeyDown(KeyCode::Alpha5))
		{
			ALICE_LOG_INFO("[UIImageController] Key 5 Pressed. Changing texture to: %s", m_imagePaths[4].c_str());
			TargetImage->texturePath = m_imagePaths[4];
		}
		if (input->GetKeyDown(KeyCode::Alpha6))
		{
			ALICE_LOG_INFO("[UIImageController] Key 6 Pressed. Changing texture to: %s", m_imagePaths[5].c_str());
			TargetImage->texturePath = m_imagePaths[5];
		}
    }
}
