#include "SceneChangeButtonScript.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "Runtime/UI/UIButtonComponent.h"
#include "Runtime/UI/UITextComponent.h"
#include "Runtime/UI/UIImageComponent.h"
#include "Runtime/UI/BindWidget.h"
#include "../Tempsound/UISoundScript.h"
#include "Runtime/ECS/GameObject.h"

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

        UISoundScript* FindUISound(World& world, const std::string& name)
        {
            if (name.empty())
                return nullptr;
            GameObject go = world.FindGameObject(name);
            if (!go.IsValid())
                return nullptr;
            auto* scripts = world.GetScripts(go.id());
            if (!scripts)
                return nullptr;
            for (auto& sc : *scripts)
            {
                if (sc.scriptName == "UISoundScript" && sc.instance)
                    return static_cast<UISoundScript*>(sc.instance.get());
            }
            return nullptr;
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

        m_buttonEntityId = buttonEntity;

        m_uiSound = FindUISound(*w, Get_uiSoundEntityName());

        const std::string textName = Get_TextWidgetName();
        if (!textName.empty())
            m_textEntityId = AliceUI::FindWidgetByName(*w, buttonEntity, textName);

        const std::string lineName = Get_UnderLineWidgetName();
        if (!lineName.empty())
            m_underLineEntityId = AliceUI::FindWidgetByName(*w, buttonEntity, lineName);

        if (m_textEntityId != InvalidEntityId)
        {
            if (auto* textComp = w->GetComponent<UITextComponent>(m_textEntityId))
                m_textNormalColor = textComp->color;
        }
        if (m_underLineEntityId != InvalidEntityId)
        {
            if (auto* imgComp = w->GetComponent<UIImageComponent>(m_underLineEntityId))
                m_lineNormalColor = imgComp->color;
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

        ApplyChildColors(AliceUI::UIButtonState::Normal);

		//  Scene 변경 요청 플래그 설정
        if (isChangeSceneRequested)
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
        }



        // 호버 시 사운드
        changeSceneButton->AddOnHoveredSafe([this]()
        {
            if (m_uiSound)
                m_uiSound->PlayHover();
        }, isValid);

        // 버튼이 눌렸을 때 클릭 사운드 재생 후, 타이머로 지연 씬 전환
        changeSceneButton->AddOnReleasedSafe([this]()
        {
            if (m_uiSound)
                m_uiSound->PlayClick();

            isChangeSceneRequested = true;
            m_pendingSceneChange = true;
            m_sceneChangeTimer = 2.0f;
        }, isValid);
    }

    void SceneChangeButtonScript::ApplyChildColors(AliceUI::UIButtonState state)
    {
        World* w = GetWorld();
        if (!w)
            return;

        const DirectX::XMFLOAT4* textColor = &m_textNormalColor;
        const DirectX::XMFLOAT4* lineColor = &m_lineNormalColor;
        switch (state)
        {
        case AliceUI::UIButtonState::Hovered:
            textColor = &m_hoverColor;
            lineColor = &m_hoverColor;
            break;
        case AliceUI::UIButtonState::Pressed:
            textColor = &m_pressedColor;
            lineColor = &m_pressedColor;
            break;
        default:
            break;
        }

        if (m_textEntityId != InvalidEntityId)
        {
            if (auto* textComp = w->GetComponent<UITextComponent>(m_textEntityId))
                textComp->color = *textColor;
        }
        if (m_underLineEntityId != InvalidEntityId)
        {
            if (auto* imgComp = w->GetComponent<UIImageComponent>(m_underLineEntityId))
                imgComp->color = *lineColor;
        }
    }

    void SceneChangeButtonScript::Update(float deltaTime)
    {
        // 지연 씬 전환: 클릭 소리 재생 후 타이머가 끝나면 전환
        if (m_pendingSceneChange)
        {
            m_sceneChangeTimer -= deltaTime;
            if (m_sceneChangeTimer <= 0.f)
            {
                m_pendingSceneChange = false;
                auto* scenes = Scenes();
                if (scenes)
                {
                    const std::string scenePath = Get_targetScenePath();
                    if (!scenePath.empty())
                    {
                        ALICE_LOG_INFO("[SceneChangeButtonScript] Changing scene to: %s", scenePath.c_str());
                        scenes->LoadSceneFileRequest(scenePath.c_str());
                    }
                }
            }
        }

        // ConsumeClick: 클릭 시 소리만 재생하고 씬 전환은 타이머로 예약
        if (changeSceneButton && changeSceneButton->ConsumeClick())
        {
            if (m_uiSound)
                m_uiSound->PlayClick();

            m_pendingSceneChange = true;
            m_sceneChangeTimer = 1.0f;
        }

        if (changeSceneButton)
            ApplyChildColors(changeSceneButton->state);
    }

    void SceneChangeButtonScript::OnDestroy()
    {
        if (changeSceneButton)
            changeSceneButton->ClearDelegates();
    }
}
