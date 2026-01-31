#include "AdvancedUIController.h"

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include <cmath>
#include "Runtime/ECS/World.h"

namespace Alice
{
    REGISTER_SCRIPT(AdvancedUIController);

    RTTR_REGISTRATION
    {
        rttr::registration::class_<AdvancedUIController>(AdvancedUIController::_Refl_ClassName)
            .property("rootWidgetName", &AdvancedUIController::rootWidgetName)
                (rttr::metadata("SerializeField", true));
    }

    namespace
    {
        EntityId FindRootWidgetByName(World& world, const std::string& name)
        {
            for (auto&& [id, widget] : world.GetComponents<UIWidgetComponent>())
            {
                const std::string widgetName = widget.widgetName.empty() ? world.GetEntityName(id) : widget.widgetName;
                if (!widgetName.empty() && widgetName == name)
                    return id;
            }
            return InvalidEntityId;
        }
    }

    void AdvancedUIController::Start()
    {
        m_rng.seed(std::random_device{}());

        World* w = GetWorld();
        if (!w)
            return;

        const EntityId root = FindRootWidgetByName(*w, rootWidgetName);
        const auto result = AliceUI::BindWidgets(this, *w, root);
        if (result.missingRequired > 0)
        {
            ALICE_LOG_WARN("[AdvancedUIController] Missing required widgets: %d", result.missingRequired);
        }

        if (CooldownEffect)
        {
            CooldownEffect->radialEnabled = true;
            CooldownEffect->radialInner = 0.0f;
            CooldownEffect->radialOuter = 0.5f;
            CooldownEffect->radialSoftness = 0.01f;
            CooldownEffect->radialDim = 0.35f;
        }
        if (OutlineEffect)
        {
            OutlineEffect->outlineEnabled = false;
            OutlineEffect->outlineThickness = 2.0f;
        }
        if (GlowEffect)
        {
            GlowEffect->glowEnabled = true;
            GlowEffect->glowStrength = 0.8f;
            GlowEffect->glowWidth = 0.25f;
            GlowEffect->glowSpeed = 1.2f;
        }
        if (GlowGauge)
        {
            GlowGauge->normalized = true;
            GlowGauge->smoothing = 0.1f;
        }
        if (FadeText && FadeText->text.empty())
        {
            FadeText->text = "Fade In / Out";
        }
    }

    void AdvancedUIController::Update(float deltaTime)
    {
        auto* input = Input();
        if (!input)
            return;

        if (input->GetKeyDown(KeyCode::Alpha1))
        {
            if (DropAnim)
                DropAnim->PlayAll(true);
        }
        if (input->GetKeyDown(KeyCode::Alpha2))
        {
            if (CooldownAnim)
                CooldownAnim->PlayAll(true);
        }
        if (input->GetKeyDown(KeyCode::Alpha3))
        {
            m_outlineLocked = !m_outlineLocked;
        }
        if (input->GetKeyDown(KeyCode::Alpha4))
        {
            m_glowEnabled = !m_glowEnabled;
            if (GlowEffect)
                GlowEffect->glowEnabled = m_glowEnabled;
        }
        if (input->GetKeyDown(KeyCode::Alpha5))
        {
            if (ShakeComp)
                ShakeComp->Start(12.0f, 0.4f, 18.0f);
        }
        if (input->GetKeyDown(KeyCode::Alpha6))
        {
            if (FadeAnim)
                FadeAnim->PlayAll(true);
        }
        if (input->GetKeyDown(KeyCode::Alpha7))
        {
            SpawnDamageText();
        }

        if (OutlineEffect)
        {
            bool hover = false;
            if (OutlineButton)
                hover = (OutlineButton->state == AliceUI::UIButtonState::Hovered || OutlineButton->state == AliceUI::UIButtonState::Pressed);
            OutlineEffect->outlineEnabled = m_outlineLocked || hover;
        }

        if (GlowGauge)
        {
            m_gaugeTime += deltaTime;
            GlowGauge->value = 0.5f + 0.5f * std::sin(m_gaugeTime);
        }

        World* w = GetWorld();
        if (!w)
            return;

        for (auto it = m_damageTexts.begin(); it != m_damageTexts.end();)
        {
            it->age += deltaTime;
            if (it->age >= it->lifetime)
            {
                w->DestroyEntity(it->id);
                it = m_damageTexts.erase(it);
            }
            else
            {
                ++it;
            }
        }
    }

    void AdvancedUIController::SpawnDamageText()
    {
        World* w = GetWorld();
        if (!w)
            return;

        std::uniform_real_distribution<float> offsetDist(-0.4f, 0.4f);
        std::uniform_int_distribution<int> damageDist(12, 120);

        const float baseX = offsetDist(m_rng);
        const float baseY = 1.6f;
        const float baseZ = offsetDist(m_rng);
        const int damage = damageDist(m_rng);

        EntityId e = w->CreateEntity();
        w->SetEntityName(e, "DamageText");

        auto& worldTransform = w->AddComponent<TransformComponent>(e);
        worldTransform.enabled = true;
        worldTransform.position = DirectX::XMFLOAT3(baseX, baseY, baseZ);
        worldTransform.rotation = DirectX::XMFLOAT3(0.0f, 0.0f, 0.0f);
        worldTransform.scale = DirectX::XMFLOAT3(0.01f, 0.01f, 0.01f);

        auto& widget = w->AddComponent<UIWidgetComponent>(e);
        widget.space = AliceUI::UISpace::World;
        widget.visibility = AliceUI::UIVisibility::Visible;
        widget.billboard = true;
        widget.raycastTarget = false;
        widget.interactable = false;

        auto& uiTransform = w->AddComponent<UITransformComponent>(e);
        uiTransform.size = DirectX::XMFLOAT2(200.0f, 60.0f);
        uiTransform.pivot = DirectX::XMFLOAT2(0.0f, 0.0f);
        uiTransform.position = DirectX::XMFLOAT2(0.0f, 0.0f);
        uiTransform.scale = DirectX::XMFLOAT2(1.0f, 1.0f);
        uiTransform.rotationRad = 0.0f;
        uiTransform.useAlignment = false;

        auto& text = w->AddComponent<UITextComponent>(e);
        text.fontPath = "Resource/Fonts/NotoSansKR-Regular.ttf";
        text.fontSize = 28.0f;
        text.text = std::to_string(damage);
        text.alignH = AliceUI::UIAlignH::Center;
        text.alignV = AliceUI::UIAlignV::Center;
        text.wrap = false;
        text.maxWidth = 0.0f;
        text.lineSpacing = 0.0f;

        if (damage >= 80)
            text.color = DirectX::XMFLOAT4(1.0f, 0.2f, 0.2f, 1.0f);
        else if (damage >= 40)
            text.color = DirectX::XMFLOAT4(1.0f, 0.75f, 0.2f, 1.0f);
        else
            text.color = DirectX::XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);

        auto& anim = w->AddComponent<UIAnimationComponent>(e);
        anim.playOnStart = true;

        UIAnimTrack trackX{};
        trackX.name = "DamageX";
        trackX.property = UIAnimProperty::PositionX;
        trackX.curvePath = "Assets/Curves/UI/DamageX.uicurve";
        trackX.duration = 1.0f;
        trackX.from = 0.0f;
        trackX.to = 0.6f;
        trackX.useNormalizedTime = true;

        UIAnimTrack trackY{};
        trackY.name = "DamageY";
        trackY.property = UIAnimProperty::PositionY;
        trackY.curvePath = "Assets/Curves/UI/DamageY.uicurve";
        trackY.duration = 1.0f;
        trackY.from = 0.0f;
        trackY.to = 0.8f;
        trackY.useNormalizedTime = true;

        UIAnimTrack trackA{};
        trackA.name = "DamageAlpha";
        trackA.property = UIAnimProperty::TextAlpha;
        trackA.curvePath = "Assets/Curves/UI/DamageAlpha.uicurve";
        trackA.duration = 1.0f;
        trackA.from = 0.0f;
        trackA.to = 1.0f;
        trackA.useNormalizedTime = true;

        anim.tracks.push_back(trackX);
        anim.tracks.push_back(trackY);
        anim.tracks.push_back(trackA);

        DamageText entry{};
        entry.id = e;
        entry.age = 0.0f;
        entry.lifetime = 1.2f;
        m_damageTexts.push_back(entry);
    }
}
