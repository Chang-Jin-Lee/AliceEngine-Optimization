#include "CombatDeathUiProduction.h"

#include <algorithm>
#include <cctype>
#include <cmath>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Input/InputTypes.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Gameplay/Animation/AdvancedAnimationComponent.h"
#include "Runtime/Rendering/Components/MaterialComponent.h"
#include "Runtime/Rendering/Components/ComputeEffectComponent.h"
#include "Runtime/Rendering/Components/UnityVfxComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(CombatDeathUiProduction);

    namespace
    {
        float Saturate(float v)
        {
            return std::clamp(v, 0.0f, 1.0f);
        }

        float Lerp(float a, float b, float t)
        {
            return a + (b - a) * t;
        }

        std::string Trim(const std::string& s)
        {
            std::size_t start = 0;
            while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start])))
                ++start;

            std::size_t end = s.size();
            while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1])))
                --end;

            return s.substr(start, end - start);
        }

        bool FetchWorldTransform(World* world, EntityId id,
                                 DirectX::XMFLOAT3& outPos,
                                 DirectX::XMFLOAT3& outRot,
                                 DirectX::XMFLOAT3& outScale)
        {
            if (!world || id == InvalidEntityId)
                return false;

            auto* tr = world->GetComponent<TransformComponent>(id);
            if (!tr || !tr->enabled)
                return false;

            if (tr->parent == InvalidEntityId)
            {
                outPos = tr->position;
                outRot = tr->rotation;
                outScale = tr->scale;
                return true;
            }

            DirectX::XMMATRIX worldM = world->ComputeWorldMatrix(id);
            DirectX::XMVECTOR s, q, t;
            if (DirectX::XMMatrixDecompose(&s, &q, &t, worldM))
            {
                DirectX::XMStoreFloat3(&outPos, t);
                DirectX::XMStoreFloat3(&outScale, s);
                DirectX::XMFLOAT4 quat{};
                DirectX::XMStoreFloat4(&quat, q);
                const DirectX::XMVECTOR qv = DirectX::XMLoadFloat4(&quat);
                const DirectX::XMVECTOR f = DirectX::XMVector3Rotate(DirectX::XMVectorSet(0, 0, 1, 0), qv);
                DirectX::XMFLOAT3 f3{};
                DirectX::XMStoreFloat3(&f3, f);
                outRot = {
                    -std::atan2(f3.y, std::sqrt(f3.x * f3.x + f3.z * f3.z)),
                    std::atan2(f3.x, f3.z),
                    0.0f
                };
                return true;
            }

            outPos = tr->position;
            outRot = tr->rotation;
            outScale = tr->scale;
            return true;
        }
    }

    void CombatDeathUiProduction::Start()
    {
        ParseEffectNamesIfNeeded();
        ParseAdditionalHideNamesIfNeeded();
        ResolveEntities();
        ResolveShockWaveEntities();
    }

    void CombatDeathUiProduction::Update(float deltaTime)
    {
        ParseEffectNamesIfNeeded();
        ParseAdditionalHideNamesIfNeeded();
        ResolveEntities();
        ResolveShockWaveEntities();

        if (Get_m_enableHotkey() && Get_m_triggerWithAlpha7())
        {
            if (auto* input = Input())
            {
                if (input->GetKeyDown(KeyCode::Alpha7))
                {
                    if (!m_running || Get_m_restartOnKey())
                        StartSequence();
                }
            }
        }

        if (m_running)
            ApplySequence(std::max(0.0f, deltaTime));
    }

    void CombatDeathUiProduction::OnDisable()
    {
        if (Get_m_restoreStateOnDisable())
            StopSequence(true);
    }

    void CombatDeathUiProduction::OnDestroy()
    {
        if (Get_m_restoreStateOnDisable())
            StopSequence(true);
    }

    bool CombatDeathUiProduction::ResolveEntities()
    {
        World* world = GetWorld();
        if (!world)
            return false;

        m_playerEntity = InvalidEntityId;
        m_uiDeathEntity = InvalidEntityId;
        m_uiFadeEntity = InvalidEntityId;
        m_bossEntity = InvalidEntityId;

        if (!Get_m_playerEntityName().empty())
        {
            const GameObject go = world->FindGameObject(Get_m_playerEntityName());
            if (go.IsValid())
                m_playerEntity = go.id();
        }

        if (!Get_m_uiDeathEntityName().empty())
        {
            const GameObject go = world->FindGameObject(Get_m_uiDeathEntityName());
            if (go.IsValid())
                m_uiDeathEntity = go.id();
        }

        if (!Get_m_uiFadeEntityName().empty())
        {
            const GameObject go = world->FindGameObject(Get_m_uiFadeEntityName());
            if (go.IsValid())
                m_uiFadeEntity = go.id();
        }

        if (!Get_m_bossEntityName().empty())
        {
            const GameObject go = world->FindGameObject(Get_m_bossEntityName());
            if (go.IsValid())
                m_bossEntity = go.id();
        }

        m_effectEntities.clear();
        for (const std::string& name : m_effectNames)
        {
            if (name.empty())
                continue;

            const GameObject go = world->FindGameObject(name);
            if (go.IsValid())
                m_effectEntities.push_back(go.id());
        }

        m_additionalHideEntities.clear();
        for (const std::string& name : m_additionalHideNames)
        {
            if (name.empty())
                continue;

            const GameObject go = world->FindGameObject(name);
            if (go.IsValid())
                m_additionalHideEntities.push_back(go.id());
        }

        return true;
    }

    void CombatDeathUiProduction::ParseEffectNamesIfNeeded()
    {
        if (m_effectNamesParsed)
            return;

        m_effectNames.clear();
        std::string token;
        const std::string csv = Get_m_effectEntityNamesCsv();
        for (char c : csv)
        {
            if (c == ',')
            {
                const std::string trimmed = Trim(token);
                if (!trimmed.empty())
                    m_effectNames.push_back(trimmed);
                token.clear();
            }
            else
            {
                token.push_back(c);
            }
        }

        const std::string tail = Trim(token);
        if (!tail.empty())
            m_effectNames.push_back(tail);

        m_effectNamesParsed = true;
    }

    void CombatDeathUiProduction::ParseAdditionalHideNamesIfNeeded()
    {
        if (m_additionalHideNamesParsed)
            return;

        m_additionalHideNames.clear();
        std::string token;
        const std::string csv = Get_m_additionalHideEntityNamesCsv();
        for (char c : csv)
        {
            if (c == ',')
            {
                const std::string trimmed = Trim(token);
                if (!trimmed.empty())
                    m_additionalHideNames.push_back(trimmed);
                token.clear();
            }
            else
            {
                token.push_back(c);
            }
        }

        const std::string tail = Trim(token);
        if (!tail.empty())
            m_additionalHideNames.push_back(tail);

        m_additionalHideNamesParsed = true;
    }

    void CombatDeathUiProduction::StartSequence()
    {
        if (Get_m_restoreStateOnDisable())
            StopSequence(true);

        ParseEffectNamesIfNeeded();
        ParseAdditionalHideNamesIfNeeded();
        ResolveEntities();
        ResolveShockWaveEntities();
        DisableBlockingScriptsForSequence();

        m_running = true;
        m_uiShown = false;
        m_elapsedSec = 0.0f;

        CollectPlayerMaterials();

        if (Get_m_forcePlayerIdle())
            ForcePlayerIdlePose();

        if (Get_m_enableShockWaveAura())
            SyncShockWaveAura();

        CaptureEffectVisibility();
        CaptureAdditionalHideVisibility();

        if (Get_m_enableEffectsOnStart())
        {
            for (EntityId id : m_effectEntities)
                SetEntityVisible(id, true);
        }

        SetEntityVisible(m_uiDeathEntity, false);
        SetEntityVisible(m_uiFadeEntity, false);
    }

    void CombatDeathUiProduction::StopSequence(bool restoreState)
    {
        m_running = false;
        m_uiShown = false;
        m_elapsedSec = 0.0f;

        if (!restoreState)
            return;

        RestorePlayerMaterials();
        RestoreEffectVisibility();
        RestoreAdditionalHideVisibility();
        RestoreOverriddenScripts();
        SetEntityVisible(m_uiDeathEntity, false);
        SetEntityVisible(m_uiFadeEntity, false);
    }

    void CombatDeathUiProduction::ApplySequence(float deltaTime)
    {
        m_elapsedSec += std::max(0.0f, deltaTime);

        if (Get_m_forcePlayerIdle())
            ForcePlayerIdlePose();

        const float fadeDuration = std::max(0.001f, Get_m_fadeDurationSec());
        const float t = Saturate(m_elapsedSec / fadeDuration);
        const float alpha = Lerp(1.0f, std::clamp(Get_m_playerMinAlpha(), 0.0f, 1.0f), t);
        ApplyPlayerAlpha(alpha);

        const float pulse = 1.0f + std::sin(m_elapsedSec * std::max(0.0f, Get_m_auraPulseSpeed())) * std::max(0.0f, Get_m_auraPulseAmplitude());
        const float emissiveIntensity = std::max(0.0f, Get_m_auraEmissiveIntensity()) * pulse;
        const float emissiveBloom = std::max(0.0f, Get_m_auraEmissiveBloom());
        const DirectX::XMFLOAT3 emissiveColor = Get_m_auraEmissiveColor();
        for (const MaterialState& state : m_playerMaterials)
        {
            if (auto* mat = GetWorld()->GetComponent<MaterialComponent>(state.entity))
            {
                mat->Set_emissiveColor(emissiveColor);
                mat->Set_emissiveIntensity(emissiveIntensity);
                mat->Set_emissiveBloom(emissiveBloom);
            }
        }

        if (Get_m_hidePlayerAtEnd() && t >= 0.999f && m_playerEntity != InvalidEntityId)
        {
            SetEntityVisibleRecursive(m_playerEntity, false);
            for (EntityId id : m_additionalHideEntities)
                SetEntityVisibleRecursive(id, false);
        }

        if (!m_uiShown && m_elapsedSec >= std::max(0.0f, Get_m_uiShowDelaySec()))
        {
            SetEntityVisible(m_uiDeathEntity, true);
            SetEntityVisible(m_uiFadeEntity, true);
            m_uiShown = true;
        }

        const float seqDuration = std::max(0.0f, Get_m_sequenceDurationSec());
        if (seqDuration > 0.0f && m_elapsedSec >= seqDuration)
        {
            for (EntityId id : m_effectEntities)
                SetEntityVisible(id, false);

            if (m_shockWaveEntity != InvalidEntityId)
                SetEntityVisible(m_shockWaveEntity, false);

            m_running = false;
            RestoreOverriddenScripts();
        }
    }

    void CombatDeathUiProduction::ResolveShockWaveEntities()
    {
        World* world = GetWorld();
        if (!world)
            return;

        m_shockWaveEntity = InvalidEntityId;
        m_shockWaveAnchorEntity = InvalidEntityId;

        if (!Get_m_shockWaveEntityName().empty())
        {
            const GameObject go = world->FindGameObject(Get_m_shockWaveEntityName());
            if (go.IsValid())
                m_shockWaveEntity = go.id();
        }

        if (!Get_m_shockWaveAnchorEntityName().empty())
        {
            const GameObject go = world->FindGameObject(Get_m_shockWaveAnchorEntityName());
            if (go.IsValid())
                m_shockWaveAnchorEntity = go.id();
        }

        if (m_shockWaveAnchorEntity == InvalidEntityId)
            m_shockWaveAnchorEntity = m_playerEntity;
    }

    void CombatDeathUiProduction::SyncShockWaveAura()
    {
        World* world = GetWorld();
        if (!world || m_shockWaveEntity == InvalidEntityId)
            return;

        auto* shockTr = world->GetComponent<TransformComponent>(m_shockWaveEntity);
        if (!shockTr)
            return;

        shockTr->enabled = true;
        shockTr->visible = true;

        DirectX::XMFLOAT3 anchorPos{};
        DirectX::XMFLOAT3 anchorRot{};
        DirectX::XMFLOAT3 anchorScale{};
        if (FetchWorldTransform(world, m_shockWaveAnchorEntity, anchorPos, anchorRot, anchorScale))
        {
            const DirectX::XMFLOAT3 localOffset = Get_m_shockWaveLocalOffset();
            shockTr->position = {
                anchorPos.x + localOffset.x,
                anchorPos.y + localOffset.y,
                anchorPos.z + localOffset.z
            };
        }

        const DirectX::XMFLOAT3 targetScale = Get_m_shockWaveScale();
        shockTr->scale = {
            std::max(0.01f, targetScale.x),
            std::max(0.01f, targetScale.y),
            std::max(0.01f, targetScale.z)
        };
        world->MarkTransformDirty(m_shockWaveEntity);

        if (auto* ce = world->GetComponent<ComputeEffectComponent>(m_shockWaveEntity))
        {
            ce->enabled = true;
            ce->emitNewParticles = true;
            ce->loop = true;
            ce->simulationSpace = ParticleSimulationSpace::World;
            ce->spawnRate = std::max(ce->spawnRate, std::max(0.01f, Get_m_shockWaveSpawnRateMin()));
            ce->radius = std::max(ce->radius, std::max(0.01f, Get_m_shockWaveRadiusMin()));
            ce->sizePx = std::max(ce->sizePx, std::max(1.0f, Get_m_shockWaveSizePxMin()));
            ce->intensity = std::max(ce->intensity, std::max(0.1f, Get_m_shockWaveIntensityMin()));
        }

        if (auto* vfx = world->GetComponent<UnityVfxComponent>(m_shockWaveEntity))
        {
            vfx->enabled = true;
            vfx->emitNewParticles = true;
            vfx->overrideLoop = true;
            vfx->loop = true;
            vfx->spawnRateScale = std::max(vfx->spawnRateScale, std::max(0.01f, Get_m_shockWaveSpawnRateMin()));
            vfx->intensityScale = std::max(vfx->intensityScale, std::max(0.1f, Get_m_shockWaveIntensityMin()));
            vfx->sizeScale = std::max(vfx->sizeScale, 1.25f);
        }
    }

    void CombatDeathUiProduction::CollectPlayerMaterials()
    {
        m_playerMaterials.clear();

        World* world = GetWorld();
        if (!world || m_playerEntity == InvalidEntityId)
            return;

        auto collectFromRoot = [&](EntityId rootId)
        {
            if (rootId == InvalidEntityId)
                return;

            std::vector<EntityId> stack;
            stack.push_back(rootId);

            while (!stack.empty())
            {
                const EntityId current = stack.back();
                stack.pop_back();

                const auto alreadySaved = std::find_if(
                    m_playerMaterials.begin(),
                    m_playerMaterials.end(),
                    [&](const MaterialState& s)
                    {
                        return s.entity == current;
                    });

                auto* mat = world->GetComponent<MaterialComponent>(current);
                auto* tr = world->GetComponent<TransformComponent>(current);
                if (mat && alreadySaved == m_playerMaterials.end())
                {
                    MaterialState state{};
                    state.entity = current;
                    state.alpha = mat->Get_alpha();
                    state.transparent = mat->Get_transparent();
                    state.emissiveColor = mat->Get_emissiveColor();
                    state.emissiveIntensity = mat->Get_emissiveIntensity();
                    state.emissiveBloom = mat->Get_emissiveBloom();
                    state.visible = tr ? tr->visible : true;
                    m_playerMaterials.push_back(state);
                }

                const auto children = world->GetChildren(current);
                for (const EntityId child : children)
                    stack.push_back(child);
            }
        };

        collectFromRoot(m_playerEntity);
        for (EntityId id : m_additionalHideEntities)
            collectFromRoot(id);
    }

    void CombatDeathUiProduction::ApplyPlayerAlpha(float alpha)
    {
        World* world = GetWorld();
        if (!world)
            return;

        const float clamped = std::clamp(alpha, 0.0f, 1.0f);
        for (const MaterialState& state : m_playerMaterials)
        {
            if (auto* mat = world->GetComponent<MaterialComponent>(state.entity))
            {
                mat->Set_alpha(clamped);
                mat->Set_transparent(clamped < 0.999f ? true : state.transparent);
            }

            if (auto* tr = world->GetComponent<TransformComponent>(state.entity))
            {
                tr->visible = (clamped > 1e-3f);
                world->MarkTransformDirty(state.entity);
            }
        }
    }

    void CombatDeathUiProduction::RestorePlayerMaterials()
    {
        World* world = GetWorld();
        if (!world)
        {
            m_playerMaterials.clear();
            return;
        }

        for (const MaterialState& state : m_playerMaterials)
        {
            if (auto* mat = world->GetComponent<MaterialComponent>(state.entity))
            {
                mat->Set_alpha(state.alpha);
                mat->Set_transparent(state.transparent);
                mat->Set_emissiveColor(state.emissiveColor);
                mat->Set_emissiveIntensity(state.emissiveIntensity);
                mat->Set_emissiveBloom(state.emissiveBloom);
            }

            if (auto* tr = world->GetComponent<TransformComponent>(state.entity))
            {
                tr->visible = state.visible;
                world->MarkTransformDirty(state.entity);
            }
        }

        m_playerMaterials.clear();
    }

    void CombatDeathUiProduction::SetEntityVisible(EntityId id, bool visible)
    {
        World* world = GetWorld();
        if (!world || id == InvalidEntityId)
            return;

        if (auto* tr = world->GetComponent<TransformComponent>(id))
        {
            tr->visible = visible;
            world->MarkTransformDirty(id);
        }
    }

    void CombatDeathUiProduction::SetEntityVisibleRecursive(EntityId rootId, bool visible)
    {
        World* world = GetWorld();
        if (!world || rootId == InvalidEntityId)
            return;

        std::vector<EntityId> stack;
        stack.push_back(rootId);

        while (!stack.empty())
        {
            const EntityId current = stack.back();
            stack.pop_back();

            if (auto* tr = world->GetComponent<TransformComponent>(current))
            {
                tr->visible = visible;
                world->MarkTransformDirty(current);
            }

            const auto children = world->GetChildren(current);
            for (const EntityId child : children)
                stack.push_back(child);
        }
    }

    void CombatDeathUiProduction::DisableBlockingScriptsForSequence()
    {
        World* world = GetWorld();
        if (!world)
            return;

        auto disableScript = [&](EntityId entity, const std::string& scriptName)
        {
            if (entity == InvalidEntityId || scriptName.empty())
                return;

            auto* scripts = world->GetScripts(entity);
            if (!scripts)
                return;

            for (auto& scriptComp : *scripts)
            {
                if (scriptComp.scriptName != scriptName)
                    continue;

                const auto alreadySaved = std::find_if(
                    m_scriptOverrides.begin(),
                    m_scriptOverrides.end(),
                    [&](const ScriptState& s)
                    {
                        return s.entity == entity && s.scriptName == scriptComp.scriptName;
                    });

                if (alreadySaved == m_scriptOverrides.end())
                {
                    ScriptState state{};
                    state.entity = entity;
                    state.scriptName = scriptComp.scriptName;
                    state.enabled = scriptComp.enabled;
                    m_scriptOverrides.push_back(state);
                }

                scriptComp.enabled = false;
            }
        };

        if (Get_m_disableCombatSessionDuringSequence())
            disableScript(GetOwnerId(), Get_m_combatSessionScriptName());

        if (Get_m_disableBossCombatSessionDuringSequence())
            disableScript(GetOwnerId(), Get_m_bossCombatSessionScriptName());

        if (Get_m_disableBossBrainDuringSequence())
            disableScript(m_bossEntity, Get_m_bossBrainScriptName());
    }

    void CombatDeathUiProduction::RestoreOverriddenScripts()
    {
        World* world = GetWorld();
        if (!world)
        {
            m_scriptOverrides.clear();
            return;
        }

        for (const ScriptState& state : m_scriptOverrides)
        {
            auto* scripts = world->GetScripts(state.entity);
            if (!scripts)
                continue;

            for (auto& scriptComp : *scripts)
            {
                if (scriptComp.scriptName == state.scriptName)
                {
                    scriptComp.enabled = state.enabled;
                    break;
                }
            }
        }

        m_scriptOverrides.clear();
    }

    void CombatDeathUiProduction::ForcePlayerIdlePose()
    {
        World* world = GetWorld();
        if (!world || m_playerEntity == InvalidEntityId)
            return;

        const std::string clip = Get_m_idleClipName();
        if (clip.empty())
            return;

        std::vector<EntityId> stack;
        stack.push_back(m_playerEntity);

        while (!stack.empty())
        {
            const EntityId current = stack.back();
            stack.pop_back();

            if (auto* anim = world->GetComponent<AdvancedAnimationComponent>(current))
            {
                anim->enabled = true;
                anim->playing = true;
                anim->base.enabled = true;
                anim->base.autoAdvance = true;

                std::string localClip = clip;
                if (anim->base.clipA.find("Armature|") != std::string::npos || anim->base.clipB.find("Armature|") != std::string::npos)
                {
                    if (localClip.find("Armature|") == std::string::npos)
                        localClip = "Armature|Tia_IDLE";
                }
                else if (anim->base.clipA.find("rig|") != std::string::npos || anim->base.clipB.find("rig|") != std::string::npos)
                {
                    if (localClip.find("rig|") == std::string::npos)
                        localClip = "rig|Tia_IDLE";
                }

                anim->base.clipA = localClip;
                anim->base.clipB = localClip;
                anim->base.speedA = 1.0f;
                anim->base.speedB = 1.0f;
                anim->base.loopA = true;
                anim->base.loopB = true;
                anim->base.blend01 = 0.0f;
                anim->upper.enabled = false;
                anim->additive.enabled = false;
            }

            const auto children = world->GetChildren(current);
            for (const EntityId child : children)
                stack.push_back(child);
        }
    }

    void CombatDeathUiProduction::CaptureEffectVisibility()
    {
        m_effectVisibleStates.clear();

        World* world = GetWorld();
        if (!world)
            return;

        for (EntityId id : m_effectEntities)
        {
            if (auto* tr = world->GetComponent<TransformComponent>(id))
            {
                VisibleState state{};
                state.entity = id;
                state.visible = tr->visible;
                m_effectVisibleStates.push_back(state);
            }
        }
    }

    void CombatDeathUiProduction::CaptureAdditionalHideVisibility()
    {
        m_additionalHideVisibleStates.clear();

        World* world = GetWorld();
        if (!world)
            return;

        for (EntityId rootId : m_additionalHideEntities)
        {
            if (rootId == InvalidEntityId)
                continue;

            std::vector<EntityId> stack;
            stack.push_back(rootId);
            while (!stack.empty())
            {
                const EntityId current = stack.back();
                stack.pop_back();

                if (auto* tr = world->GetComponent<TransformComponent>(current))
                {
                    const auto alreadySaved = std::find_if(
                        m_additionalHideVisibleStates.begin(),
                        m_additionalHideVisibleStates.end(),
                        [&](const VisibleState& s)
                        {
                            return s.entity == current;
                        });

                    if (alreadySaved == m_additionalHideVisibleStates.end())
                    {
                        VisibleState state{};
                        state.entity = current;
                        state.visible = tr->visible;
                        m_additionalHideVisibleStates.push_back(state);
                    }
                }

                const auto children = world->GetChildren(current);
                for (const EntityId child : children)
                    stack.push_back(child);
            }
        }
    }

    void CombatDeathUiProduction::RestoreEffectVisibility()
    {
        World* world = GetWorld();
        if (!world)
        {
            m_effectVisibleStates.clear();
            return;
        }

        for (const VisibleState& state : m_effectVisibleStates)
        {
            if (auto* tr = world->GetComponent<TransformComponent>(state.entity))
            {
                tr->visible = state.visible;
                world->MarkTransformDirty(state.entity);
            }
        }

        m_effectVisibleStates.clear();
    }

    void CombatDeathUiProduction::RestoreAdditionalHideVisibility()
    {
        World* world = GetWorld();
        if (!world)
        {
            m_additionalHideVisibleStates.clear();
            return;
        }

        for (const VisibleState& state : m_additionalHideVisibleStates)
        {
            if (auto* tr = world->GetComponent<TransformComponent>(state.entity))
            {
                tr->visible = state.visible;
                world->MarkTransformDirty(state.entity);
            }
        }

        m_additionalHideVisibleStates.clear();
    }
}
