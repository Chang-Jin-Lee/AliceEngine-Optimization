#include "CombatHudScript.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/UI/UIWidgetComponent.h"
#include "Runtime/UI/BindWidget.h"
#include "Runtime/UI/UITransformComponent.h"
#include "Runtime/Gameplay/Combat/HealthComponent.h"
#include "Runtime/Gameplay/Combat/AttackDriverComponent.h"
#include "Runtime/Gameplay/Combat/WeaponTraceComponent.h"
#include "Runtime/Physics/Components/Phy_CCTComponent.h"
#include "Runtime/Rendering/Components/SkinnedAnimationComponent.h"
#include "Runtime/Rendering/Components/SkinnedMeshComponent.h"
#include "C_CombatSessionComponent.h"
#include "C_BossBrainComponent.h"


namespace Alice
{
    REGISTER_SCRIPT(CombatHudScript);

    namespace
    {
        const char* Yn(bool value)
        {
            return value ? "Y" : "N";
        }

        const char* ActionStateLabel(Combat::ActionState state)
        {
            switch (state)
            {
            case Combat::ActionState::Idle: return "Idle";
            case Combat::ActionState::Move: return "Move";
            case Combat::ActionState::Attack: return "Attack";
            case Combat::ActionState::Dodge: return "Dodge";
            case Combat::ActionState::Guard: return "Guard";
            case Combat::ActionState::JustGuardSuccess: return "JustGuard";
            case Combat::ActionState::GuardBreakWeak: return "GuardBreak";
            case Combat::ActionState::Hitstun: return "Hitstun";
            case Combat::ActionState::Groggy: return "Groggy";
            case Combat::ActionState::Dead: return "Dead";
            case Combat::ActionState::Interaction: return "Interact";
            case Combat::ActionState::HealEnter: return "HealEnter";
            case Combat::ActionState::HealLoop: return "HealLoop";
            case Combat::ActionState::HealExit: return "HealExit";
            default: return "Unknown";
            }
        }

        void AppendVec3(std::ostringstream& oss, const DirectX::XMFLOAT3& v)
        {
            oss << "(" << v.x << ", " << v.y << ", " << v.z << ")";
        }

        std::string BuildActorDebug(World& world,
                                    EntityId actorId,
                                    const char* header,
                                    Combat::ActionState state,
                                    const Combat::ActionFlags& flags,
                                    const std::string& extraLabel)
        {
            std::ostringstream oss;
            oss.setf(std::ios::fixed);
            oss << std::setprecision(2);

            if (header && header[0] != '\0')
                oss << header << " ";

            if (actorId == InvalidEntityId)
            {
                oss << "None";
                return oss.str();
            }

            std::string name = world.GetEntityName(actorId);
            if (name.empty())
                name = "Entity";
            oss << name << " (#" << actorId << ")\n";
            oss << "State: " << ActionStateLabel(state)
                << " | Flags: atk=" << Yn(flags.hitActive)
                << " guard=" << Yn(flags.guardActive)
                << " parry=" << Yn(flags.parryWindowActive)
                << " inv=" << Yn(flags.invulnActive)
                << " charge=" << Yn(flags.chargeActive)
                << "(" << flags.chargeLevel << ")"
                << " combo=" << flags.attackComboIndex
                << " interrupt=" << Yn(flags.canBeInterrupted)
                << "\n";

            if (auto* health = world.GetComponent<HealthComponent>(actorId))
            {
                oss << "HP: " << health->currentHealth << "/" << health->maxHealth
                    << " Groggy: " << health->groggy << "/" << health->groggyMax
                    << " Weapon: " << health->weaponDurability << "/" << health->weaponDurabilityMax
                    << " Alive: " << Yn(health->alive)
                    << " Team: " << health->teamId
                    << "\n";
                oss << "Invuln: " << health->invulnRemaining << "/" << health->invulnDuration
                    << " Weak: " << health->weakRemainingSec
                    << " Pushback: " << health->pushbackRemainingSec << "@"
                    << health->pushbackSpeed << " ";
                AppendVec3(oss, health->pushbackDir);
                oss << "\n";
                oss << "Hit: hit=" << Yn(health->hitThisFrame)
                    << " guardHit=" << Yn(health->guardHitThisFrame)
                    << " dodgeAvoid=" << Yn(health->dodgeAvoidedThisFrame)
                    << " lastDmg=" << health->lastHitDamage
                    << " part=" << health->lastHitPart;
                if (health->lastHitAttacker != InvalidEntityId)
                {
                    std::string attackerName = world.GetEntityName(health->lastHitAttacker);
                    if (attackerName.empty())
                        attackerName = "Entity";
                    oss << " attacker=" << attackerName;
                }
                oss << "\n";
            }
            else
            {
                oss << "Health: None\n";
            }

            if (auto* transform = world.GetComponent<TransformComponent>(actorId))
            {
                const float yawDeg = transform->rotation.y * 57.2957795f;
                oss << "Pos ";
                AppendVec3(oss, transform->position);
                oss << " RotY " << yawDeg << "deg"
                    << " Scale ";
                AppendVec3(oss, transform->scale);
                oss << " Visible=" << Yn(transform->visible) << "\n";
            }
            else
            {
                oss << "Transform: None\n";
            }

            if (auto* cct = world.GetComponent<Phy_CCTComponent>(actorId))
            {
                oss << "CCT: ground=" << Yn(cct->onGround)
                    << " dist=" << cct->groundDistance
                    << " vVel=" << cct->verticalVelocity
                    << " desired=";
                AppendVec3(oss, cct->desiredVelocity);
                oss << " jump=" << Yn(cct->jumpRequested)
                    << " teleport=" << Yn(cct->teleport)
                    << " flags=" << static_cast<int>(cct->collisionFlags)
                    << "\n";
            }

            if (auto* driver = world.GetComponent<AttackDriverComponent>(actorId))
            {
                oss << "Driver: atk=" << Yn(driver->attackActive)
                    << " pause=" << Yn(driver->attackPaused)
                    << " cancelable=" << Yn(driver->attackCancelable)
                    << " cancelReq=" << Yn(driver->cancelAttackRequested)
                    << " suppressed=" << Yn(driver->attackSuppressed)
                    << " guard=" << Yn(driver->guardActive)
                    << " dodge=" << Yn(driver->dodgeActive)
                    << " parry=" << Yn(driver->parryActive)
                    << " guardLock=" << Yn(driver->guardLockActive)
                    << "(" << driver->guardLockRemainingSec << ")"
                    << " parryOverride=" << driver->parryOverrideRemainingSec
                    << " dur=" << driver->attackStateDurationSec
                    << " auto=" << driver->attackStateDurationAutoSec
                    << "\n";
                oss << "DriverInput: guardHeld=" << Yn(driver->guardInputHeld)
                    << " pressed=" << Yn(driver->guardInputPressed)
                    << " released=" << Yn(driver->guardInputReleased)
                    << " parryTap=" << static_cast<int>(driver->parryTapCredit)
                    << " guardSession=" << Yn(driver->guardSessionActive)
                    << "\n";
                if (driver->prevSkinned.valid)
                {
                    oss << "LastAnim: " << driver->prevSkinned.clipName
                        << " t=" << driver->prevSkinned.prevTimeSec
                        << "\n";
                }

                EntityId traceId = driver->traceCached;
                if (traceId == InvalidEntityId && driver->traceGuid != 0)
                    traceId = world.FindEntityByGuid(driver->traceGuid);
                if (traceId == InvalidEntityId)
                    traceId = actorId;
                if (auto* trace = world.GetComponent<WeaponTraceComponent>(traceId))
                {
                    std::string traceName = world.GetEntityName(traceId);
                    if (traceName.empty())
                        traceName = "Entity";
                    oss << "Trace(" << traceName << "): active=" << Yn(trace->active)
                        << " dmg=" << trace->baseDamage
                        << " atkId=" << trace->attackInstanceId
                        << " shapes=" << trace->shapes.size()
                        << "\n";
                }
            }

            if (auto* anim = world.GetComponent<SkinnedAnimationComponent>(actorId))
            {
                oss << "Anim: clip=" << anim->clipIndex
                    << " t=" << anim->timeSec
                    << " speed=" << anim->speed
                    << " playing=" << Yn(anim->playing)
                    << "\n";
            }
            if (auto* mesh = world.GetComponent<SkinnedMeshComponent>(actorId))
            {
                oss << "Mesh: " << mesh->meshAssetPath
                    << " bones=" << mesh->boneCount
                    << "\n";
            }

            if (!extraLabel.empty())
                oss << "Brain: " << extraLabel;

            return oss.str();
        }

        EntityId FindWidgetByName(World& world, const std::string& name)
        {
            if (name.empty())
                return InvalidEntityId;
            for (auto [id, widget] : world.GetComponents<UIWidgetComponent>())
            {
                const std::string widgetName = widget.widgetName.empty() ? world.GetEntityName(id) : widget.widgetName;
                if (!widgetName.empty() && widgetName == name)
                    return id;
            }
            return InvalidEntityId;
        }

        UITextComponent* FindText(World& world, const std::string& name)
        {
            const EntityId id = FindWidgetByName(world, name);
            return (id != InvalidEntityId) ? world.GetComponent<UITextComponent>(id) : nullptr;
        }

        UIGaugeComponent* FindGauge(World& world, const std::string& name)
        {
            const EntityId id = FindWidgetByName(world, name);
            return (id != InvalidEntityId) ? world.GetComponent<UIGaugeComponent>(id) : nullptr;
        }
    }

    void CombatHudScript::Start()
    {
        BindWidgets();
        RefreshEntityIds();
    }

    void CombatHudScript::Update(float /*deltaTime*/)
    {
        World* world = GetWorld();
        if (!world)
            return;

        if (m_playerId == InvalidEntityId || m_bossId == InvalidEntityId)
            RefreshEntityIds();

        auto* playerHealth = (m_playerId != InvalidEntityId) ? world->GetComponent<HealthComponent>(m_playerId) : nullptr;
        auto* bossHealth = (m_bossId != InvalidEntityId) ? world->GetComponent<HealthComponent>(m_bossId) : nullptr;
        if (playerHealth)
        {
            UpdateGauge(m_playerHpGauge, playerHealth->currentHealth, playerHealth->maxHealth);
            UpdateGauge(m_weaponGauge, playerHealth->weaponDurability, playerHealth->weaponDurabilityMax);
        }
        if (bossHealth)
        {
            UpdateGauge(m_bossHpGauge, bossHealth->currentHealth, bossHealth->maxHealth);
            UpdateGauge(m_bossGroggyGauge, bossHealth->groggy, bossHealth->groggyMax);
        }

        Combat::ActionState playerState = Combat::ActionState::Idle;
        Combat::ActionState bossState = Combat::ActionState::Idle;
        Combat::ActionFlags playerFlags{};
        Combat::ActionFlags bossFlags{};
        bool playerRageActive = false;
        float playerRageRemainingSec = 0.0f;
        float playerRageCooldownRemainingSec = 0.0f;
        float playerRageCooldownNormalized = 0.0f;

        GameObject sessionGo = world->FindGameObject(Get_sessionEntityName());
        if (sessionGo.IsValid())
        {
            if (auto* scripts = world->GetScripts(sessionGo.id()))
            {
                for (auto& sc : *scripts)
                {
                    if (sc.scriptName == "C_CombatSessionComponent" && sc.instance)
                    {
                        auto* session = static_cast<C_CombatSessionComponent*>(sc.instance.get());
                        playerState = session->GetPlayerState();
                        bossState = session->GetBossState();
                        playerFlags = session->GetPlayerFlags();
                        bossFlags = session->GetBossFlags();
                        playerRageActive = session->IsPlayerRageActive();
                        playerRageRemainingSec = session->GetPlayerRageRemainingSec();
                        playerRageCooldownRemainingSec = session->GetPlayerRageCooldownRemainingSec();
                        playerRageCooldownNormalized = session->GetPlayerRageCooldownNormalized();
                        break;
                    }
                }
            }
        }

        const float playerStateInactive = (Get_playerStateFontSize() > 0.0f) ? Get_playerStateFontSize() : Get_stateTextFontSize();
        const float playerStateActive = (Get_playerActiveStateFontSize() > 0.0f) ? Get_playerActiveStateFontSize() : Get_activeStateTextFontSize();
        const float bossStateInactive = (Get_bossStateFontSize() > 0.0f) ? Get_bossStateFontSize() : Get_stateTextFontSize();
        const float bossStateActive = (Get_bossActiveStateFontSize() > 0.0f) ? Get_bossActiveStateFontSize() : Get_activeStateTextFontSize();

        const float playerWindowInactive = (Get_playerWindowFontSize() > 0.0f) ? Get_playerWindowFontSize() : Get_windowTextFontSize();
        const float playerWindowActive = (Get_playerActiveWindowFontSize() > 0.0f) ? Get_playerActiveWindowFontSize() : Get_activeWindowTextFontSize();
        const float bossWindowInactive = (Get_bossWindowFontSize() > 0.0f) ? Get_bossWindowFontSize() : Get_windowTextFontSize();
        const float bossWindowActive = (Get_bossActiveWindowFontSize() > 0.0f) ? Get_bossActiveWindowFontSize() : Get_activeWindowTextFontSize();

        const Combat::ActionState playerStates[] = {
            Combat::ActionState::Idle,
            Combat::ActionState::Move,
            Combat::ActionState::Attack,
            Combat::ActionState::Guard,
            Combat::ActionState::Dodge
        };
        const Combat::ActionState bossStates[] = {
            Combat::ActionState::Idle,
            Combat::ActionState::Move,
            Combat::ActionState::Attack,
            Combat::ActionState::Guard,
            Combat::ActionState::Dodge,
            Combat::ActionState::Groggy
        };

        const size_t playerStateCount = sizeof(playerStates) / sizeof(playerStates[0]);
        const size_t bossStateCount = sizeof(bossStates) / sizeof(bossStates[0]);
        UpdateStateTexts(m_playerStateTexts.data(), playerStates, playerStateCount, playerState, playerStateInactive, playerStateActive);
        UpdateStateTexts(m_bossStateTexts.data(), bossStates, bossStateCount, bossState, bossStateInactive, bossStateActive);
        UpdateWindowText(m_playerWindowText, playerFlags, playerWindowInactive, playerWindowActive);
        UpdateWindowText(m_bossWindowText, bossFlags, bossWindowInactive, bossWindowActive);
        const bool showRageInfo = playerRageActive || playerRageCooldownRemainingSec > 0.0f;
        if (m_playerWindowText && showRageInfo)
        {
            std::ostringstream oss;
            oss.setf(std::ios::fixed);
            oss << std::setprecision(1);
            if (m_playerWindowText->text != "None" && !m_playerWindowText->text.empty())
                oss << m_playerWindowText->text << " | ";
            if (playerRageActive)
            {
                oss << "Rage " << std::max(0.0f, playerRageRemainingSec) << "s";
            }
            else
            {
                const float cooldownPercent = std::clamp(playerRageCooldownNormalized * 100.0f, 0.0f, 100.0f);
                oss << "Rage CD " << std::max(0.0f, playerRageCooldownRemainingSec) << "s (" << cooldownPercent << "%)";
            }
            m_playerWindowText->text = oss.str();

            const bool baseWindowActive = playerFlags.hitActive || playerFlags.guardActive
                || playerFlags.parryWindowActive || playerFlags.invulnActive || playerFlags.chargeActive;
            if (!baseWindowActive)
            {
                const DirectX::XMFLOAT4 activeColor = Get_activeWindowTextColor();
                m_playerWindowText->color = activeColor;
                m_playerWindowText->color.w = Get_activeTextAlpha();
                if (playerWindowActive > 0.0f)
                    m_playerWindowText->fontSize = playerWindowActive;
            }
        }

        std::string bossBrainLabel;
        if (m_bossId != InvalidEntityId)
        {
            if (auto* scripts = world->GetScripts(m_bossId))
            {
                for (auto& sc : *scripts)
                {
                    if (sc.scriptName == "C_BossBrainComponent" && sc.instance)
                    {
                        auto* brain = static_cast<C_BossBrainComponent*>(sc.instance.get());
                        bossBrainLabel = brain->GetDebugLabel();
                        break;
                    }
                }
            }
        }
        if (bossBrainLabel.empty())
            bossBrainLabel = "None";
        if (m_bossBrainText)
            m_bossBrainText->text = bossBrainLabel;
        if (m_playerDebugText)
        {
            m_playerDebugText->text = BuildActorDebug(*world, m_playerId, "PLAYER", playerState, playerFlags, "");
            const float size = Get_playerDebugFontSize();
            if (size > 0.0f)
                m_playerDebugText->fontSize = size;
            if (Get_debugLineSpacing() != 0.0f)
                m_playerDebugText->lineSpacing = Get_debugLineSpacing();
        }
        if (m_bossDebugText)
        {
            m_bossDebugText->text = BuildActorDebug(*world, m_bossId, "BOSS", bossState, bossFlags, bossBrainLabel);
            const float size = Get_bossDebugFontSize();
            if (size > 0.0f)
                m_bossDebugText->fontSize = size;
            if (Get_debugLineSpacing() != 0.0f)
                m_bossDebugText->lineSpacing = Get_debugLineSpacing();
        }

        if (Get_useWorldSpace())
            UpdateWorldAnchors();
    }

    void CombatHudScript::RefreshEntityIds()
    {
        World* world = GetWorld();
        if (!world)
            return;

        GameObject playerGo = world->FindGameObject(Get_playerEntityName());
        GameObject bossGo = world->FindGameObject(Get_bossEntityName());
        m_playerId = playerGo.IsValid() ? playerGo.id() : InvalidEntityId;
        m_bossId = bossGo.IsValid() ? bossGo.id() : InvalidEntityId;
    }

    void CombatHudScript::BindWidgets()
    {
        World* world = GetWorld();
        if (!world)
            return;

        m_playerHpGaugeId = FindWidgetByName(*world, Get_playerHpGaugeName());
        m_bossHpGaugeId = FindWidgetByName(*world, Get_bossHpGaugeName());
        m_bossGroggyGaugeId = FindWidgetByName(*world, Get_bossGroggyGaugeName());
        m_weaponGaugeId = FindWidgetByName(*world, Get_weaponGaugeName());
        m_playerHpGauge = (m_playerHpGaugeId != InvalidEntityId) ? world->GetComponent<UIGaugeComponent>(m_playerHpGaugeId) : nullptr;
        m_bossHpGauge = (m_bossHpGaugeId != InvalidEntityId) ? world->GetComponent<UIGaugeComponent>(m_bossHpGaugeId) : nullptr;
        m_bossGroggyGauge = (m_bossGroggyGaugeId != InvalidEntityId) ? world->GetComponent<UIGaugeComponent>(m_bossGroggyGaugeId) : nullptr;
        m_weaponGauge = (m_weaponGaugeId != InvalidEntityId) ? world->GetComponent<UIGaugeComponent>(m_weaponGaugeId) : nullptr;

        m_playerStateTexts = {
            FindText(*world, Get_playerStateIdleTextName()),
            FindText(*world, Get_playerStateMoveTextName()),
            FindText(*world, Get_playerStateAttackTextName()),
            FindText(*world, Get_playerStateGuardTextName()),
            FindText(*world, Get_playerStateDodgeTextName())
        };

        m_bossStateTexts = {
            FindText(*world, Get_bossStateIdleTextName()),
            FindText(*world, Get_bossStateMoveTextName()),
            FindText(*world, Get_bossStateAttackTextName()),
            FindText(*world, Get_bossStateGuardTextName()),
            FindText(*world, Get_bossStateDodgeTextName()),
            FindText(*world, Get_bossStateGroggyTextName())
        };

        m_playerWindowTextId = FindWidgetByName(*world, Get_playerWindowTextName());
        m_bossWindowTextId = FindWidgetByName(*world, Get_bossWindowTextName());
        m_bossBrainTextId = FindWidgetByName(*world, Get_bossBrainTextName());
        m_playerDebugTextId = FindWidgetByName(*world, Get_playerDebugTextName());
        m_bossDebugTextId = FindWidgetByName(*world, Get_bossDebugTextName());
        m_playerWindowText = (m_playerWindowTextId != InvalidEntityId) ? world->GetComponent<UITextComponent>(m_playerWindowTextId) : nullptr;
        m_bossWindowText = (m_bossWindowTextId != InvalidEntityId) ? world->GetComponent<UITextComponent>(m_bossWindowTextId) : nullptr;
        m_bossBrainText = (m_bossBrainTextId != InvalidEntityId) ? world->GetComponent<UITextComponent>(m_bossBrainTextId) : nullptr;
        m_playerDebugText = (m_playerDebugTextId != InvalidEntityId) ? world->GetComponent<UITextComponent>(m_playerDebugTextId) : nullptr;
        m_bossDebugText = (m_bossDebugTextId != InvalidEntityId) ? world->GetComponent<UITextComponent>(m_bossDebugTextId) : nullptr;

        m_playerStateTextIds = {
            FindWidgetByName(*world, Get_playerStateIdleTextName()),
            FindWidgetByName(*world, Get_playerStateMoveTextName()),
            FindWidgetByName(*world, Get_playerStateAttackTextName()),
            FindWidgetByName(*world, Get_playerStateGuardTextName()),
            FindWidgetByName(*world, Get_playerStateDodgeTextName())
        };

        m_bossStateTextIds = {
            FindWidgetByName(*world, Get_bossStateIdleTextName()),
            FindWidgetByName(*world, Get_bossStateMoveTextName()),
            FindWidgetByName(*world, Get_bossStateAttackTextName()),
            FindWidgetByName(*world, Get_bossStateGuardTextName()),
            FindWidgetByName(*world, Get_bossStateDodgeTextName()),
            FindWidgetByName(*world, Get_bossStateGroggyTextName())
        };
    }

    void CombatHudScript::UpdateWorldAnchors()
    {
        World* world = GetWorld();
        if (!world)
            return;

        auto ApplyWidgetCommon = [&](EntityId id)
        {
            if (id == InvalidEntityId)
                return;
            if (auto* widget = world->GetComponent<UIWidgetComponent>(id))
            {
                widget->space = AliceUI::UISpace::World;
                widget->billboard = Get_worldBillboard();
            }
            const UITransformComponent* sizeSource = nullptr;
            if (auto* uiTransform = world->GetComponent<UITransformComponent>(id))
            {
                uiTransform->position = { 0.0f, 0.0f };
                uiTransform->useAlignment = false;
                sizeSource = uiTransform;
            }
            if (auto* transform = world->GetComponent<TransformComponent>(id))
            {
                const float current = transform->scale.x;
                const bool hasSceneScale =
                    std::abs(transform->scale.x - 1.0f) > 1e-4f ||
                    std::abs(transform->scale.y - 1.0f) > 1e-4f ||
                    std::abs(transform->scale.z - 1.0f) > 1e-4f;
                float scale = std::max(0.0001f, Get_worldScale());
                if (!hasSceneScale && Get_worldAutoScaleBySize() && sizeSource)
                {
                    const float maxSize = std::max(sizeSource->size.x, sizeSource->size.y);
                    if (maxSize > 0.0001f)
                        scale = std::max(0.0001f, Get_worldTargetSize() / maxSize);
                }
                if (!hasSceneScale)
                    transform->scale = { scale, scale, scale };
            }
        };

        auto PlaceWidgetWorld = [&](EntityId id, const DirectX::XMFLOAT3& pos)
        {
            if (id == InvalidEntityId)
                return;
            if (auto* transform = world->GetComponent<TransformComponent>(id))
                transform->position = pos;
        };

        auto ComputeRight = [&](const TransformComponent& actor) -> DirectX::XMFLOAT3
        {
            const float yaw = actor.rotation.y;
            const float cx = std::cos(yaw);
            const float sx = std::sin(yaw);
            return DirectX::XMFLOAT3(cx, 0.0f, -sx);
        };

        const float line = std::max(0.001f, Get_worldLineSpacing());

        auto EnsureParent = [&](EntityId id, EntityId parent)
        {
            if (id == InvalidEntityId || parent == InvalidEntityId)
                return;
            if (world->GetParent(id) != parent)
                world->SetParent(id, parent, false);
        };

        auto PlaceWidgetLocal = [&](EntityId id, const DirectX::XMFLOAT3& localPos)
        {
            if (id == InvalidEntityId)
                return;
            world->SetLocalPosition(id, localPos);
        };

        auto ApplyScreenCommon = [&](EntityId id)
        {
            if (id == InvalidEntityId)
                return;
            if (auto* widget = world->GetComponent<UIWidgetComponent>(id))
            {
                widget->space = AliceUI::UISpace::Screen;
                widget->billboard = false;
            }
            if (auto* uiTransform = world->GetComponent<UITransformComponent>(id))
            {
                uiTransform->anchorMin = { 0.5f, 0.5f };
                uiTransform->anchorMax = { 0.5f, 0.5f };
                uiTransform->useAlignment = false;
            }
            if (world->GetParent(id) != InvalidEntityId)
                world->SetParent(id, InvalidEntityId, false);
        };

        auto ApplyScreenLayout = [&](EntityId id, const DirectX::XMFLOAT2& pos)
        {
            if (id == InvalidEntityId)
                return;
            ApplyScreenCommon(id);
            if (auto* uiTransform = world->GetComponent<UITransformComponent>(id))
                uiTransform->position = pos;
        };

        if (m_playerId != InvalidEntityId)
        {
            if (auto* actor = world->GetComponent<TransformComponent>(m_playerId))
            {
                if (!Get_playerUseWorldSpace())
                {
                    const float baseX = Get_playerScreenPosX();
                    const float baseY = Get_playerScreenPosY();
                    const float line = std::max(1.0f, Get_playerScreenLineSpacing());
                    const EntityId playerWidgets[] = {
                        m_playerHpGaugeId,
                        m_weaponGaugeId,
                        m_playerWindowTextId,
                        m_playerStateTextIds[0],
                        m_playerStateTextIds[1],
                        m_playerStateTextIds[2],
                        m_playerStateTextIds[3],
                        m_playerStateTextIds[4]
                    };
                    for (size_t i = 0; i < std::size(playerWidgets); ++i)
                    {
                        ApplyScreenLayout(
                            playerWidgets[i],
                            { baseX, baseY + static_cast<float>(i) * line }
                        );
                    }
                }
                else
                {
                    const bool attachAsChild = Get_worldAttachAsChild();
                    DirectX::XMFLOAT3 base{};
                    if (attachAsChild)
                    {
                        base = { Get_playerRightOffset(), Get_playerHeightOffset(), 0.0f };
                    }
                    else
                    {
                        const DirectX::XMFLOAT3 right = ComputeRight(*actor);
                        base = {
                            actor->position.x + right.x * Get_playerRightOffset(),
                            actor->position.y + Get_playerHeightOffset(),
                            actor->position.z + right.z * Get_playerRightOffset()
                        };
                    }

                    float y = 0.0f;
                    const EntityId playerWidgets[] = {
                        m_playerHpGaugeId,
                        m_weaponGaugeId,
                        m_playerWindowTextId,
                        m_playerStateTextIds[0],
                        m_playerStateTextIds[1],
                        m_playerStateTextIds[2],
                        m_playerStateTextIds[3],
                        m_playerStateTextIds[4]
                    };
                    for (EntityId id : playerWidgets)
                    {
                        ApplyWidgetCommon(id);
                        if (attachAsChild)
                        {
                            EnsureParent(id, m_playerId);
                            PlaceWidgetLocal(id, { base.x, base.y + y, base.z });
                        }
                        else
                        {
                            PlaceWidgetWorld(id, { base.x, base.y + y, base.z });
                        }
                        y -= line;
                    }
                }
            }
        }

        if (m_bossId != InvalidEntityId)
        {
            if (auto* actor = world->GetComponent<TransformComponent>(m_bossId))
            {
                if (!Get_bossUseWorldSpace())
                {
                    const float baseX = Get_bossScreenPosX();
                    const float baseY = Get_bossScreenPosY();
                    const float line = std::max(1.0f, Get_bossScreenLineSpacing());
                    const EntityId bossWidgets[] = {
                        m_bossHpGaugeId,
                        m_bossGroggyGaugeId,
                        m_bossWindowTextId,
                        m_bossBrainTextId,
                        m_bossStateTextIds[0],
                        m_bossStateTextIds[1],
                        m_bossStateTextIds[2],
                        m_bossStateTextIds[3],
                        m_bossStateTextIds[4],
                        m_bossStateTextIds[5]
                    };
                    for (size_t i = 0; i < std::size(bossWidgets); ++i)
                    {
                        ApplyScreenLayout(
                            bossWidgets[i],
                            { baseX, baseY + static_cast<float>(i) * line }
                        );
                    }
                }
                else
                {
                    const bool attachAsChild = Get_worldAttachAsChild();
                    DirectX::XMFLOAT3 base{};
                    if (attachAsChild)
                    {
                        base = { Get_bossRightOffset(), Get_bossHeightOffset(), 0.0f };
                    }
                    else
                    {
                        const DirectX::XMFLOAT3 right = ComputeRight(*actor);
                        base = {
                            actor->position.x + right.x * Get_bossRightOffset(),
                            actor->position.y + Get_bossHeightOffset(),
                            actor->position.z + right.z * Get_bossRightOffset()
                        };
                    }

                    float y = 0.0f;
                    const EntityId bossWidgets[] = {
                        m_bossHpGaugeId,
                        m_bossGroggyGaugeId,
                        m_bossWindowTextId,
                        m_bossBrainTextId,
                        m_bossStateTextIds[0],
                        m_bossStateTextIds[1],
                        m_bossStateTextIds[2],
                        m_bossStateTextIds[3],
                        m_bossStateTextIds[4],
                        m_bossStateTextIds[5]
                    };
                    for (EntityId id : bossWidgets)
                    {
                        ApplyWidgetCommon(id);
                        if (attachAsChild)
                        {
                            EnsureParent(id, m_bossId);
                            PlaceWidgetLocal(id, { base.x, base.y + y, base.z });
                        }
                        else
                        {
                            PlaceWidgetWorld(id, { base.x, base.y + y, base.z });
                        }
                        y -= line;
                    }
                }
            }
        }

        ApplyScreenLayout(m_playerDebugTextId, { Get_playerDebugScreenPosX(), Get_playerDebugScreenPosY() });
        ApplyScreenLayout(m_bossDebugTextId, { Get_bossDebugScreenPosX(), Get_bossDebugScreenPosY() });
    }

    void CombatHudScript::UpdateGauge(UIGaugeComponent* gauge, float value, float maxValue)
    {
        if (!gauge)
            return;
        const float maxV = std::max(1e-6f, maxValue);
        gauge->normalized = true;
        gauge->minValue = 0.0f;
        gauge->maxValue = 1.0f;
        gauge->value = std::clamp(value / maxV, 0.0f, 1.0f);
    }

    void CombatHudScript::UpdateStateTexts(UITextComponent* const* texts,
                                           const Combat::ActionState* states,
                                           size_t count,
                                           Combat::ActionState state,
                                           float inactiveSize,
                                           float activeSize)
    {
        if (!texts || !states || count == 0)
            return;

        for (size_t i = 0; i < count; ++i)
        {
            auto* text = texts[i];
            if (!text)
                continue;
            const bool active = (state == states[i]);
            const float alpha = active ? Get_activeTextAlpha() : Get_inactiveTextAlpha();
            const DirectX::XMFLOAT4 baseColor = active ? Get_activeTextColor() : Get_inactiveTextColor();
            text->color = baseColor;
            text->color.w = alpha;
            const float size = active ? activeSize : inactiveSize;
            if (size > 0.0f)
                text->fontSize = size;
        }
    }

    void CombatHudScript::UpdateWindowText(UITextComponent* text, const Combat::ActionFlags& flags, float inactiveSize, float activeSize)
    {
        if (!text)
            return;
        text->text = BuildWindowLabel(flags);
        const bool active = flags.hitActive || flags.guardActive || flags.parryWindowActive || flags.invulnActive || flags.chargeActive;
        const float alpha = active ? Get_activeTextAlpha() : Get_inactiveTextAlpha();
        const DirectX::XMFLOAT4 baseColor = active ? Get_activeWindowTextColor() : Get_inactiveWindowTextColor();
        text->color = baseColor;
        text->color.w = alpha;
        const float size = active ? activeSize : inactiveSize;
        if (size > 0.0f)
            text->fontSize = size;
    }

    std::string CombatHudScript::BuildWindowLabel(const Combat::ActionFlags& flags)
    {
        std::ostringstream oss;
        bool any = false;
        if (flags.hitActive)
        {
            const int combo = std::clamp(flags.attackComboIndex, 0, 3);
            if (combo > 0)
                oss << "Attack" << combo;
            else
                oss << "Attack";
            any = true;
        }
        if (flags.guardActive)
        {
            if (any) oss << " | ";
            oss << "Guard";
            any = true;
        }
        if (flags.parryWindowActive)
        {
            if (any) oss << " | ";
            oss << "Parry";
            any = true;
        }
        if (flags.invulnActive)
        {
            if (any) oss << " | ";
            oss << "Invuln";
            any = true;
        }
        if (flags.chargeActive)
        {
            if (any) oss << " | ";
            const int level = std::clamp(flags.chargeLevel, 0, 3);
            if (level > 0)
                oss << "Charge" << level;
            else
                oss << "Charge";
            any = true;
        }
        if (!any)
            oss << "None";
        return oss.str();
    }
}
