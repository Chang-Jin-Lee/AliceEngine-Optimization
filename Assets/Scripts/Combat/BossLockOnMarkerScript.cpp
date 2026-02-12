#include "BossLockOnMarkerScript.h"

#include <algorithm>

#include "C_CombatContracts.h"
#include "C_CombatSessionComponent.h"

#include <DirectXMath.h>

#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/World.h"
#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/UI/UIImageComponent.h"
#include "Runtime/UI/UITransformComponent.h"
#include "Runtime/UI/UIWidgetComponent.h"

namespace Alice
{
    namespace
    {
        C_CombatSessionComponent* FindCombatSession(World& world, const std::string& entityName)
        {
            if (entityName.empty())
                return nullptr;

            const GameObject go = world.FindGameObject(entityName);
            if (!go.IsValid())
                return nullptr;

            auto* scripts = world.GetScripts(go.id());
            if (!scripts)
                return nullptr;

            for (auto& script : *scripts)
            {
                if (script.scriptName == "C_CombatSessionComponent" && script.instance)
                    return static_cast<C_CombatSessionComponent*>(script.instance.get());
            }

            return nullptr;
        }
    }

    REGISTER_SCRIPT(BossLockOnMarkerScript);

    void BossLockOnMarkerScript::Start()
    {
        m_session = nullptr;
        m_bossId = InvalidEntityId;
        m_cameraId = InvalidEntityId;
        m_markerId = InvalidEntityId;
        m_markerImage = nullptr;
        m_markerWidget = nullptr;

        ResolveSession();
        ResolveBoss();
        ResolveCamera();
        EnsureMarkerEntity();
        EnsureMarkerAttachment();
        SetMarkerVisible(false);
    }

    void BossLockOnMarkerScript::Update(float /*deltaTime*/)
    {
        ResolveSession();
        ResolveBoss();
        ResolveCamera();
        EnsureMarkerEntity();
        EnsureMarkerAttachment();

        if (!m_session || m_bossId == InvalidEntityId || m_markerId == InvalidEntityId)
        {
            SetMarkerVisible(false);
            return;
        }

        const bool lockOnActive = m_session->IsPlayerLockOnActive();
        const EntityId lockTarget = m_session->GetPlayerLockOnTarget();
        const bool lockTargetValidForBoss = (lockTarget == InvalidEntityId || lockTarget == m_bossId);
        const bool bossDead = (m_session->GetBossState() == Combat::ActionState::Dead);

        const bool showMarker = lockOnActive && lockTargetValidForBoss && !bossDead;
        SetMarkerVisible(showMarker);

        if (!showMarker)
        {
            SetMarkerTexture(Get_lockOnTexturePath());
            return;
        }

        const bool showGroggyPrompt =
            (m_session->GetBossState() == Combat::ActionState::Groggy) &&
            !m_session->IsFatalActive();

        SetMarkerTexture(showGroggyPrompt ? Get_groggyTexturePath() : Get_lockOnTexturePath());
    }

    void BossLockOnMarkerScript::OnDisable()
    {
        SetMarkerVisible(false);
    }

    void BossLockOnMarkerScript::ResolveSession()
    {
        if (m_session)
            return;

        World* world = GetWorld();
        if (!world)
            return;

        m_session = FindCombatSession(*world, Get_sessionEntityName());
    }

    void BossLockOnMarkerScript::ResolveBoss()
    {
        World* world = GetWorld();
        if (!world)
            return;

        const std::string bossName = Get_bossEntityName();
        if (bossName.empty())
            return;

        const GameObject bossGo = world->FindGameObject(bossName);
        m_bossId = bossGo.IsValid() ? bossGo.id() : InvalidEntityId;
    }

    void BossLockOnMarkerScript::ResolveCamera()
    {
        World* world = GetWorld();
        if (!world)
            return;

        const std::string cameraName = Get_cameraEntityName();
        if (cameraName.empty())
            return;

        const GameObject cameraGo = world->FindGameObject(cameraName);
        m_cameraId = cameraGo.IsValid() ? cameraGo.id() : InvalidEntityId;
    }

    void BossLockOnMarkerScript::EnsureMarkerEntity()
    {
        World* world = GetWorld();
        if (!world)
            return;

        if (m_markerId == InvalidEntityId)
        {
            const std::string markerName = Get_markerEntityName();
            if (!markerName.empty())
            {
                const GameObject markerGo = world->FindGameObject(markerName);
                if (markerGo.IsValid())
                    m_markerId = markerGo.id();
            }
        }

        if (m_markerId == InvalidEntityId)
        {
            m_markerId = world->CreateEntity();
            if (!Get_markerEntityName().empty())
                world->SetEntityName(m_markerId, Get_markerEntityName());
        }

        if (!world->GetComponent<TransformComponent>(m_markerId))
            world->AddComponent<TransformComponent>(m_markerId);
        if (!world->GetComponent<UIWidgetComponent>(m_markerId))
            world->AddComponent<UIWidgetComponent>(m_markerId);
        if (!world->GetComponent<UITransformComponent>(m_markerId))
            world->AddComponent<UITransformComponent>(m_markerId);
        if (!world->GetComponent<UIImageComponent>(m_markerId))
            world->AddComponent<UIImageComponent>(m_markerId);

        m_markerWidget = world->GetComponent<UIWidgetComponent>(m_markerId);
        m_markerImage = world->GetComponent<UIImageComponent>(m_markerId);
        auto* markerUiTransform = world->GetComponent<UITransformComponent>(m_markerId);

        if (m_markerWidget)
        {
            m_markerWidget->widgetName = Get_markerEntityName();
            m_markerWidget->space = AliceUI::UISpace::World;
            m_markerWidget->billboard = true;
            m_markerWidget->interactable = false;
            m_markerWidget->raycastTarget = false;
            m_markerWidget->shaderName = "Default";
        }

        if (markerUiTransform)
        {
            markerUiTransform->anchorMin = { 0.5f, 0.5f };
            markerUiTransform->anchorMax = { 0.5f, 0.5f };
            markerUiTransform->pivot = { 0.5f, 0.5f };
            markerUiTransform->position = { 0.0f, 0.0f };
            markerUiTransform->size = {
                std::max(1.0f, Get_markerSizeX()),
                std::max(1.0f, Get_markerSizeY())
            };
            markerUiTransform->useAlignment = false;
            markerUiTransform->sortOrder = 0;
        }

        if (auto* markerTransform = world->GetComponent<TransformComponent>(m_markerId))
        {
            const float s = std::max(0.0001f, Get_markerWorldScale());
            markerTransform->scale = { s, s, s };
        }

        if (m_markerImage)
        {
            m_markerImage->preserveAspect = true;
            m_markerImage->color = { 1.0f, 1.0f, 1.0f, 1.0f };
            if (m_markerImage->texturePath.empty())
                m_markerImage->texturePath = Get_lockOnTexturePath();
        }
    }

    void BossLockOnMarkerScript::EnsureMarkerAttachment()
    {
        World* world = GetWorld();
        if (!world || m_markerId == InvalidEntityId || m_bossId == InvalidEntityId)
            return;

        if (world->GetParent(m_markerId) != InvalidEntityId)
            world->SetParent(m_markerId, InvalidEntityId, true);

        auto* bossTransform = world->GetComponent<TransformComponent>(m_bossId);
        auto* markerTransform = world->GetComponent<TransformComponent>(m_markerId);
        if (!bossTransform || !markerTransform)
            return;

        DirectX::XMFLOAT3 worldPos = bossTransform->position;
        worldPos.y += Get_markerYOffset();

        if (const auto* cameraTransform = world->GetComponent<TransformComponent>(m_cameraId))
        {
            DirectX::XMVECTOR markerV = DirectX::XMLoadFloat3(&worldPos);
            DirectX::XMVECTOR cameraV = DirectX::XMLoadFloat3(&cameraTransform->position);
            DirectX::XMVECTOR toCamera = DirectX::XMVectorSubtract(cameraV, markerV);
            const float lenSq = DirectX::XMVectorGetX(DirectX::XMVector3LengthSq(toCamera));
            if (lenSq > 1e-6f)
            {
                const float towardCameraOffset = std::max(0.0f, Get_markerTowardCameraOffset());
                toCamera = DirectX::XMVector3Normalize(toCamera);
                markerV = DirectX::XMVectorAdd(markerV, DirectX::XMVectorScale(toCamera, towardCameraOffset));
                DirectX::XMStoreFloat3(&worldPos, markerV);
            }
        }

        world->SetLocalPosition(m_markerId, worldPos);
    }

    void BossLockOnMarkerScript::SetMarkerVisible(bool visible)
    {
        if (!m_markerWidget)
            return;

        m_markerWidget->visibility = visible ? AliceUI::UIVisibility::Visible : AliceUI::UIVisibility::Collapsed;
    }

    void BossLockOnMarkerScript::SetMarkerTexture(const std::string& path)
    {
        if (!m_markerImage)
            return;
        if (path.empty())
            return;

        if (m_markerImage->texturePath != path)
            m_markerImage->texturePath = path;
    }
}
