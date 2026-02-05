#pragma once

#include <string>
#include <array>
#include <cstddef>
#include <DirectXMath.h>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/UI/UIGaugeComponent.h"
#include "Runtime/UI/UITextComponent.h"
#include "C_CombatContracts.h"

//TODO: 간단한 디버그 용도로 제작한 hud임, 나중에 갈아엎어야함

namespace Alice
{
    // Combat HUD binder for ProtoCombat scene.
    // - Shows player/boss HP, weapon durability
    // - Shows player/boss FSM state (5 slots)
    // - Shows active combat windows (attack/guard/parry/invuln)
    class CombatHudScript : public IScript
    {
        ALICE_BODY(CombatHudScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        // Entity names
        ALICE_PROPERTY(std::string, playerEntityName, "Player");
        ALICE_PROPERTY(std::string, bossEntityName, "Enemy");
        ALICE_PROPERTY(std::string, sessionEntityName, "SceneManager");

        // Gauge widgets
        ALICE_PROPERTY(std::string, playerHpGaugeName, "UI_PlayerHP_Gauge");
        ALICE_PROPERTY(std::string, bossHpGaugeName, "UI_BossHP_Gauge");
        ALICE_PROPERTY(std::string, bossGroggyGaugeName, "UI_BossGroggy_Gauge");
        ALICE_PROPERTY(std::string, weaponGaugeName, "UI_WeaponDurability_Gauge");

        // Player FSM text widgets (5 slots)
        ALICE_PROPERTY(std::string, playerStateIdleTextName, "UI_PlayerState_Idle");
        ALICE_PROPERTY(std::string, playerStateMoveTextName, "UI_PlayerState_Move");
        ALICE_PROPERTY(std::string, playerStateAttackTextName, "UI_PlayerState_Attack");
        ALICE_PROPERTY(std::string, playerStateGuardTextName, "UI_PlayerState_Guard");
        ALICE_PROPERTY(std::string, playerStateDodgeTextName, "UI_PlayerState_Dodge");

        // Boss FSM text widgets (5 slots)
        ALICE_PROPERTY(std::string, bossStateIdleTextName, "UI_BossState_Idle");
        ALICE_PROPERTY(std::string, bossStateMoveTextName, "UI_BossState_Move");
        ALICE_PROPERTY(std::string, bossStateAttackTextName, "UI_BossState_Attack");
        ALICE_PROPERTY(std::string, bossStateGuardTextName, "UI_BossState_Guard");
        ALICE_PROPERTY(std::string, bossStateDodgeTextName, "UI_BossState_Dodge");
        ALICE_PROPERTY(std::string, bossStateGroggyTextName, "UI_BossState_Groggy");

        // Active window text widgets
        ALICE_PROPERTY(std::string, playerWindowTextName, "UI_PlayerWindow");
        ALICE_PROPERTY(std::string, bossWindowTextName, "UI_BossWindow");
        ALICE_PROPERTY(std::string, bossBrainTextName, "UI_BossBrain");
        ALICE_PROPERTY(std::string, playerDebugTextName, "UI_PlayerDebug");
        ALICE_PROPERTY(std::string, bossDebugTextName, "UI_BossDebug");

        // Color/alpha for active/inactive state text
        ALICE_PROPERTY(float, activeTextAlpha, 1.0f);
        ALICE_PROPERTY(float, inactiveTextAlpha, 0.35f);
        ALICE_PROPERTY(float, stateTextFontSize, 26.0f);
        ALICE_PROPERTY(float, windowTextFontSize, 26.0f);
        ALICE_PROPERTY(float, activeStateTextFontSize, 32.0f);
        ALICE_PROPERTY(float, activeWindowTextFontSize, 30.0f);
        ALICE_PROPERTY(float, playerStateFontSize, 30.0f);
        ALICE_PROPERTY(float, playerActiveStateFontSize, 38.0f);
        ALICE_PROPERTY(float, bossStateFontSize, 26.0f);
        ALICE_PROPERTY(float, bossActiveStateFontSize, 34.0f);
        ALICE_PROPERTY(float, playerWindowFontSize, 30.0f);
        ALICE_PROPERTY(float, playerActiveWindowFontSize, 36.0f);
        ALICE_PROPERTY(float, bossWindowFontSize, 26.0f);
        ALICE_PROPERTY(float, bossActiveWindowFontSize, 32.0f);
        ALICE_PROPERTY(DirectX::XMFLOAT4, activeTextColor, DirectX::XMFLOAT4(1.0f, 0.9f, 0.2f, 1.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT4, inactiveTextColor, DirectX::XMFLOAT4(0.9f, 0.9f, 0.9f, 1.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT4, activeWindowTextColor, DirectX::XMFLOAT4(0.2f, 1.0f, 0.4f, 1.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT4, inactiveWindowTextColor, DirectX::XMFLOAT4(0.9f, 0.9f, 0.9f, 1.0f));

        // World-space anchoring
        ALICE_PROPERTY(bool, useWorldSpace, true);
        ALICE_PROPERTY(bool, worldBillboard, true);
        ALICE_PROPERTY(bool, worldAttachAsChild, true);
        ALICE_PROPERTY(bool, playerUseWorldSpace, false);
        ALICE_PROPERTY(bool, bossUseWorldSpace, true);
        ALICE_PROPERTY(float, playerScreenPosX, -500.0f);
        ALICE_PROPERTY(float, playerScreenPosY, 300.0f);
        ALICE_PROPERTY(float, playerScreenLineSpacing, 28.0f);
        ALICE_PROPERTY(float, bossScreenPosX, 500.0f);
        ALICE_PROPERTY(float, bossScreenPosY, 300.0f);
        ALICE_PROPERTY(float, bossScreenLineSpacing, 28.0f);
        ALICE_PROPERTY(float, playerDebugScreenPosX, -650.0f);
        ALICE_PROPERTY(float, playerDebugScreenPosY, 220.0f);
        ALICE_PROPERTY(float, bossDebugScreenPosX, 650.0f);
        ALICE_PROPERTY(float, bossDebugScreenPosY, 220.0f);
        ALICE_PROPERTY(float, playerDebugFontSize, 28.0f);
        ALICE_PROPERTY(float, bossDebugFontSize, 28.0f);
        ALICE_PROPERTY(float, debugLineSpacing, 2.0f);
        ALICE_PROPERTY(bool, worldAutoScaleBySize, true);
        ALICE_PROPERTY(float, worldTargetSize, 0.6f);
        ALICE_PROPERTY(float, worldScale, 0.01f);
        ALICE_PROPERTY(float, playerRightOffset, 0.8f);
        ALICE_PROPERTY(float, bossRightOffset, 0.8f);
        ALICE_PROPERTY(float, playerHeightOffset, 1.6f);
        ALICE_PROPERTY(float, bossHeightOffset, 1.6f);
        ALICE_PROPERTY(float, worldLineSpacing, 0.15f);

    private:
        UIGaugeComponent* m_playerHpGauge = nullptr;
        UIGaugeComponent* m_bossHpGauge = nullptr;
        UIGaugeComponent* m_bossGroggyGauge = nullptr;
        UIGaugeComponent* m_weaponGauge = nullptr;
        std::array<UITextComponent*, 5> m_playerStateTexts{};
        std::array<UITextComponent*, 6> m_bossStateTexts{};
        UITextComponent* m_playerWindowText = nullptr;
        UITextComponent* m_bossWindowText = nullptr;
        UITextComponent* m_bossBrainText = nullptr;
        UITextComponent* m_playerDebugText = nullptr;
        UITextComponent* m_bossDebugText = nullptr;

        EntityId m_playerHpGaugeId = InvalidEntityId;
        EntityId m_bossHpGaugeId = InvalidEntityId;
        EntityId m_bossGroggyGaugeId = InvalidEntityId;
        EntityId m_weaponGaugeId = InvalidEntityId;
        std::array<EntityId, 5> m_playerStateTextIds{};
        std::array<EntityId, 6> m_bossStateTextIds{};
        EntityId m_playerWindowTextId = InvalidEntityId;
        EntityId m_bossWindowTextId = InvalidEntityId;
        EntityId m_bossBrainTextId = InvalidEntityId;
        EntityId m_playerDebugTextId = InvalidEntityId;
        EntityId m_bossDebugTextId = InvalidEntityId;

        EntityId m_playerId = InvalidEntityId;
        EntityId m_bossId = InvalidEntityId;

        void RefreshEntityIds();
        void BindWidgets();
        void UpdateWorldAnchors();
        void UpdateGauge(UIGaugeComponent* gauge, float value, float maxValue);
        void UpdateStateTexts(UITextComponent* const* texts,
                              const Combat::ActionState* states,
                              size_t count,
                              Combat::ActionState state,
                              float inactiveSize,
                              float activeSize);
        void UpdateWindowText(UITextComponent* text, const Combat::ActionFlags& flags, float inactiveSize, float activeSize);
        std::string BuildWindowLabel(const Combat::ActionFlags& flags);
    };
}
