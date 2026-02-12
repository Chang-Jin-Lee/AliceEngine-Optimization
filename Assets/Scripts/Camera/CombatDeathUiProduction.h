#pragma once

#include <string>
#include <vector>

#include <DirectXMath.h>

#include "Runtime/ECS/Entity.h"
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    class CombatDeathUiProduction : public IScript
    {
        ALICE_BODY(CombatDeathUiProduction);

    public:
        void Start() override;
        void Update(float deltaTime) override;
        void OnDisable() override;
        void OnDestroy() override;

    private:
        struct MaterialState
        {
            EntityId entity = InvalidEntityId;
            float alpha = 1.0f;
            bool transparent = false;
            bool visible = true;
            DirectX::XMFLOAT3 emissiveColor{ 1.0f, 1.0f, 1.0f };
            float emissiveIntensity = 0.0f;
            float emissiveBloom = 1.0f;
        };

        struct VisibleState
        {
            EntityId entity = InvalidEntityId;
            bool visible = true;
        };

        struct ScriptState
        {
            EntityId entity = InvalidEntityId;
            std::string scriptName{};
            bool enabled = true;
        };

        bool ResolveEntities();
        void ParseEffectNamesIfNeeded();
        void StartSequence();
        void StopSequence(bool restoreState);
        void ApplySequence(float deltaTime);
        void CollectPlayerMaterials();
        void ApplyPlayerAlpha(float alpha);
        void RestorePlayerMaterials();
        void SetEntityVisible(EntityId id, bool visible);
        void SetEntityVisibleRecursive(EntityId rootId, bool visible);
        void CaptureEffectVisibility();
        void RestoreEffectVisibility();
        void DisableBlockingScriptsForSequence();
        void RestoreOverriddenScripts();
        void ForcePlayerIdlePose();
        void ResolveShockWaveEntities();
        void SyncShockWaveAura();
        void ParseAdditionalHideNamesIfNeeded();
        void CaptureAdditionalHideVisibility();
        void RestoreAdditionalHideVisibility();

    private:
        ALICE_PROPERTY(bool, m_enableHotkey, true);
        ALICE_PROPERTY(bool, m_triggerWithAlpha7, true);
        ALICE_PROPERTY(bool, m_restartOnKey, true);
        ALICE_PROPERTY(bool, m_restoreStateOnDisable, true);

        ALICE_PROPERTY(std::string, m_playerEntityName, "Player(Tia)");
        ALICE_PROPERTY(std::string, m_additionalHideEntityNamesCsv, "TiaRibbon,EGO_Blade(combined)");
        ALICE_PROPERTY(std::string, m_uiDeathEntityName, "UI_Death");
        ALICE_PROPERTY(std::string, m_uiFadeEntityName, "UI_Fade");
        ALICE_PROPERTY(std::string, m_effectEntityNamesCsv, "ShockWave,Charging,PlayerHealEffect,PlayerEffectPoint,UI_Vignette_Effect");
        ALICE_PROPERTY(std::string, m_idleClipName, "rig|Tia_IDLE");
        ALICE_PROPERTY(std::string, m_combatSessionScriptName, "C_CombatSessionComponent");
        ALICE_PROPERTY(std::string, m_bossCombatSessionScriptName, "C_BossCombatSessionComponent");
        ALICE_PROPERTY(std::string, m_bossBrainScriptName, "C_BossBrainComponent");
        ALICE_PROPERTY(std::string, m_bossEntityName, "Boss");

        ALICE_PROPERTY(bool, m_forcePlayerIdle, true);
        ALICE_PROPERTY(bool, m_enableEffectsOnStart, true);
        ALICE_PROPERTY(bool, m_hidePlayerAtEnd, true);
        ALICE_PROPERTY(bool, m_disableCombatSessionDuringSequence, true);
        ALICE_PROPERTY(bool, m_disableBossCombatSessionDuringSequence, true);
        ALICE_PROPERTY(bool, m_disableBossBrainDuringSequence, true);
        ALICE_PROPERTY(bool, m_enableShockWaveAura, true);
        ALICE_PROPERTY(std::string, m_shockWaveEntityName, "ShockWave");
        ALICE_PROPERTY(std::string, m_shockWaveAnchorEntityName, "Player(Tia)");
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_shockWaveLocalOffset, DirectX::XMFLOAT3(0.0f, 0.95f, 0.0f));
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_shockWaveScale, DirectX::XMFLOAT3(1.5f, 1.5f, 1.5f));
        ALICE_PROPERTY(float, m_shockWaveRadiusMin, 0.55f);
        ALICE_PROPERTY(float, m_shockWaveSizePxMin, 20.0f);
        ALICE_PROPERTY(float, m_shockWaveIntensityMin, 2.0f);
        ALICE_PROPERTY(float, m_shockWaveSpawnRateMin, 1.0f);

        ALICE_PROPERTY(float, m_fadeDurationSec, 1.1f);
        ALICE_PROPERTY(float, m_uiShowDelaySec, 0.45f);
        ALICE_PROPERTY(float, m_sequenceDurationSec, 2.0f);
        ALICE_PROPERTY(float, m_playerMinAlpha, 0.0f);
        ALICE_PROPERTY(DirectX::XMFLOAT3, m_auraEmissiveColor, DirectX::XMFLOAT3(0.98f, 0.28f, 0.28f));
        ALICE_PROPERTY(float, m_auraEmissiveIntensity, 4.8f);
        ALICE_PROPERTY(float, m_auraEmissiveBloom, 2.2f);
        ALICE_PROPERTY(float, m_auraPulseSpeed, 8.5f);
        ALICE_PROPERTY(float, m_auraPulseAmplitude, 0.55f);

    private:
        EntityId m_playerEntity = InvalidEntityId;
        EntityId m_uiDeathEntity = InvalidEntityId;
        EntityId m_uiFadeEntity = InvalidEntityId;
        EntityId m_bossEntity = InvalidEntityId;
        EntityId m_shockWaveEntity = InvalidEntityId;
        EntityId m_shockWaveAnchorEntity = InvalidEntityId;

        bool m_running = false;
        bool m_uiShown = false;
        float m_elapsedSec = 0.0f;

        bool m_effectNamesParsed = false;
        bool m_additionalHideNamesParsed = false;
        std::vector<std::string> m_effectNames{};
        std::vector<std::string> m_additionalHideNames{};
        std::vector<EntityId> m_effectEntities{};
        std::vector<EntityId> m_additionalHideEntities{};
        std::vector<MaterialState> m_playerMaterials{};
        std::vector<VisibleState> m_effectVisibleStates{};
        std::vector<VisibleState> m_additionalHideVisibleStates{};
        std::vector<ScriptState> m_scriptOverrides{};
    };
}
