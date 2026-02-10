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

        // UI 猷⑦듃 ?꾩젽 李얘린
        const EntityId root = FindRootWidgetByName(*w, Get_rootWidgetName());
        if (root == InvalidEntityId)
        {
            ALICE_LOG_WARN("[SceneChangeButtonScript] Root widget not found: %s", Get_rootWidgetName().c_str());
            return;
        }

        // ?꾩젽 諛붿씤??
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
        if (m_buttonEntityId != InvalidEntityId)
        {
            if (auto* imgComp = w->GetComponent<UIImageComponent>(m_buttonEntityId))
            {
                m_buttonNormalColor = imgComp->color;
                m_buttonNormalTexturePath = imgComp->texturePath;
                if (Get_showButtonImageOnHoverOnly())
                    imgComp->texturePath.clear();
            }
        }


        // 踰꾪듉 ?대┃ ?대깽???깅줉 (?몃━寃뚯씠??諛⑹떇)
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

		//  Scene 蹂寃??붿껌 ?뚮옒洹??ㅼ젙
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



        // ?몃쾭 ???ъ슫??
        changeSceneButton->AddOnHoveredSafe([this]()
        {
            if (m_uiSound)
                m_uiSound->PlayHover();
        }, isValid);

        // 
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

        const bool isHovered = (state == AliceUI::UIButtonState::Hovered || state == AliceUI::UIButtonState::Pressed);
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
            {
                m_textNormalColor = textComp->color;
                // Alpha가 0이라면 강제로 1로 설정 (보험)
                if (m_textNormalColor.w <= 0.0f) m_textNormalColor.w = 1.0f;
            }
        }
        if (m_underLineEntityId != InvalidEntityId)
        {
            if (auto* imgComp = w->GetComponent<UIImageComponent>(m_underLineEntityId))
            {
                const std::string& normalPath = Get_underImageNormalPath();
                const std::string& hoverPath = Get_underImageHoverPath();
                const bool hasPaths = !normalPath.empty() || !hoverPath.empty();
                if (hasPaths || Get_showUnderImageOnHoverOnly())
                {
                    std::string desired;
                    if (Get_showUnderImageOnHoverOnly())
                    {
                        if (isHovered)
                            desired = hoverPath.empty() ? normalPath : hoverPath;
                        else
                            desired.clear();
                    }
                    else
                    {
                        desired = isHovered
                            ? (hoverPath.empty() ? normalPath : hoverPath)
                            : (normalPath.empty() ? hoverPath : normalPath);
                    }

                    if (imgComp->texturePath != desired)
                        imgComp->texturePath = desired;
                }

                imgComp->color = *lineColor;
            }

        }

        if (m_buttonEntityId != InvalidEntityId)
        {
            if (auto* imgComp = w->GetComponent<UIImageComponent>(m_buttonEntityId))
            {
                if (Get_showButtonImageOnHoverOnly())
                {
                    const std::string desired = isHovered ? m_buttonNormalTexturePath : std::string();
                    if (imgComp->texturePath != desired)
                        imgComp->texturePath = desired;
                }
                imgComp->color = m_buttonNormalColor;
            }
        }
    }

    void SceneChangeButtonScript::Update(float deltaTime)
    {
        // 吏?????꾪솚: ?대┃ ?뚮━ ?ъ깮 ????대㉧媛 ?앸굹硫??꾪솚
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

        // ConsumeClick: ?대┃ ???뚮━留??ъ깮?섍퀬 ???꾪솚? ??대㉧濡??덉빟
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
