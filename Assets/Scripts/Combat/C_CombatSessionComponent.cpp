#include "C_CombatSessionComponent.h"

#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <string>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/Components/IDComponent.h"
#include "Runtime/Gameplay/Combat/HealthComponent.h"
#include "Runtime/Gameplay/Combat/AttackDriverComponent.h"
#include "Runtime/Gameplay/Animation/AdvancedAnimationComponent.h"
#include "Runtime/Physics/Components/Phy_CCTComponent.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Rendering/Components/CameraComponent.h"
#include "Runtime/Rendering/Components/CameraFollowComponent.h"
#include "Runtime/Rendering/Components/CameraLookAtComponent.h"
//TODO : Include Ȯ���ؾ���

#include "C_CombatContracts.h"
#include "C_CombatEventBus.h"
#include "C_ActionFsm.h"
#include "C_Fighter.h"
#include "C_CombatResolver.h"
#include "C_CombatApply.h"
#include "C_PlayerInputSourceComponent.h"
#include "C_BossBrainComponent.h"

namespace Alice
{
	struct C_CombatSessionComponent::SessionState
	{
		struct AnimOverrideState
		{
			bool saved = false;
			AdvancedAnimLayer savedBase{};
			bool overrideActive = false;
			bool blending = false;
			bool blendingToOverride = false;
			float blendTimer = 0.0f;
			std::string overrideClip{};
			bool overrideLoop = false;
			std::string attackClip{};
			bool heavyToggle = false;
			bool guardEnterActive = false;
			bool guardExitActive = false;
			float guardEnterTimer = 0.0f;
			float guardExitTimer = 0.0f;
		};

		struct AttackMoveState
		{
			bool configured = false;
			bool active = false;
			bool heavy = false;
			float timerSec = 0.0f;
			float startSec = 0.0f;
			float endSec = 0.0f;
			Combat::Vec2 dir{};
			float speed = 0.0f;
		};

		Combat::Fighter player{};
		Combat::Fighter boss{};
		Combat::FighterSnapshot playerSnapshot{};
		Combat::FighterSnapshot bossSnapshot{};
		Combat::ActionFsm playerFsm{};
		Combat::ActionFsm bossFsm{};
		Combat::CombatEventBus bus{};
		Combat::CombatResolver resolver{};
		Combat::CombatApply apply{};
		std::unordered_map<EntityId, Combat::Fighter*> fighterMap;
		Combat::ActionState prevPlayerState = Combat::ActionState::Idle;
		Combat::ActionState prevBossState = Combat::ActionState::Idle;
		AnimOverrideState playerAnim{};
		AnimOverrideState bossAnim{};
		AttackMoveState playerAttackMove{};
		AttackMoveState bossAttackMove{};
		float playerMoveBlend = 0.0f;
		float bossMoveBlend = 0.0f;
		bool playerLockOnActive = false;
		EntityId playerLockOnTarget = InvalidEntityId;

		void Init()
		{
			fighterMap.clear();
			player = Combat::Fighter{};
			boss = Combat::Fighter{};
			playerSnapshot = Combat::FighterSnapshot{};
			bossSnapshot = Combat::FighterSnapshot{};
			playerFsm.Reset();
			bossFsm.Reset();
			bus.ClearAll();
			playerAnim = {};
			bossAnim = {};
			playerAttackMove = {};
			bossAttackMove = {};
			playerMoveBlend = 0.0f;
			bossMoveBlend = 0.0f;
			playerLockOnActive = false;
			playerLockOnTarget = InvalidEntityId;
		}
	};

	REGISTER_SCRIPT(C_CombatSessionComponent);

	static IScript* FindScriptOnEntity(World& world, EntityId entityId, const char* name)
	{
		auto* scripts = world.GetScripts(entityId);
		if (!scripts)
			return nullptr;
		for (auto& sc : *scripts)
		{
			if (!sc.instance)
				continue;
			if (sc.scriptName == name)
				return sc.instance.get();
		}
		return nullptr;
	}

	static bool HasDeferredEvent(const Combat::ResolveOutput& resolved, Combat::CombatEventType type)
	{
		for (const auto& ev : resolved.deferred)
		{
			if (ev.type == type)
				return true;
		}
		return false;
	}

	static bool HitSortLess(const Combat::HitEvent& a, const Combat::HitEvent& b)
	{
		if (a.attackInstanceId != b.attackInstanceId)
			return a.attackInstanceId < b.attackInstanceId;
		if (a.attackerOwner != b.attackerOwner)
			return a.attackerOwner < b.attackerOwner;
		if (a.victimOwner != b.victimOwner)
			return a.victimOwner < b.victimOwner;

		if (a.hasSweepFraction != b.hasSweepFraction)
			return a.hasSweepFraction;
		if (a.hasSweepFraction && b.hasSweepFraction && a.sweepFraction != b.sweepFraction)
			return a.sweepFraction < b.sweepFraction;
		if (a.subShapeIndex != b.subShapeIndex)
			return a.subShapeIndex < b.subShapeIndex;
		if (a.hurtboxEntity != b.hurtboxEntity)
			return a.hurtboxEntity < b.hurtboxEntity;
		return a.part < b.part;
	}

	struct MoveBasis
	{
		bool valid = false;
		float forwardX = 0.0f;
		float forwardZ = 1.0f;
		float rightX = 1.0f;
		float rightZ = 0.0f;
	};

	static MoveBasis BuildYawBasis(float yawRad)
	{
		MoveBasis basis{};
		basis.forwardX = std::sin(yawRad);
		basis.forwardZ = std::cos(yawRad);
		basis.rightX = std::cos(yawRad);
		basis.rightZ = -std::sin(yawRad);
		basis.valid = true;
		return basis;
	}

	static EntityId ResolvePrimaryCamera(World& world)
	{
		for (auto&& [entityId, cam] : world.GetComponents<CameraComponent>())
		{
			if (cam.primary)
				return entityId;
		}

		auto go = world.FindGameObject("MainCamera");
		return go.IsValid() ? go.id() : InvalidEntityId;
	}

	static void UpdateHealthHitInfo(World& world,
		const Combat::HitEvent& hit,
		const Combat::ResolveOutput& resolved,
		const Combat::FighterSnapshot& victim)
	{
		auto* hc = world.GetComponent<HealthComponent>(hit.victimOwner);
		if (!hc)
			return;

		hc->lastHitAttacker = hit.attackerOwner;
		hc->lastHitPart = hit.part;
		hc->lastHitPosWS = hit.hitPosWS;
		hc->lastHitNormalWS = hit.hitNormalWS;

		const bool wasHit = HasDeferredEvent(resolved, Combat::CombatEventType::OnHit);
		const bool wasGuard = HasDeferredEvent(resolved, Combat::CombatEventType::OnGuarded);
		const bool wasGuardBreak = HasDeferredEvent(resolved, Combat::CombatEventType::OnGuardBreak);
		const bool wasParry = HasDeferredEvent(resolved, Combat::CombatEventType::OnParrySuccess);

		if (wasHit || wasGuard || wasGuardBreak || wasParry)
			hc->hitThisFrame = true;

		if (wasGuard || wasGuardBreak || wasParry)
			hc->guardHitThisFrame = true;

		if (wasHit)
			hc->lastHitDamage = hit.damage;
		else
			hc->lastHitDamage = 0.0f;

		if (!wasHit && !wasGuard && !wasGuardBreak && !wasParry && victim.flags.invulnActive)
			hc->dodgeAvoidedThisFrame = true;
	}

	static std::string GetEntityLabel(World& world, EntityId id)
	{
		std::string name = world.GetEntityName(id);
		if (!name.empty())
			return name;
		return "Unknown";
	}

	EntityId C_CombatSessionComponent::ResolveEntity(uint64_t guid) const
	{
		if (guid == 0)
			return InvalidEntityId;
		if (!GetWorld())
			return InvalidEntityId;
		return GetWorld()->FindEntityByGuid(guid);
	}

	EntityId C_CombatSessionComponent::ResolveEntityByName(const std::string& name) const
	{
		if (name.empty() || !GetWorld())
			return InvalidEntityId;
		auto go = GetWorld()->FindGameObject(name);
		return go.IsValid() ? go.id() : InvalidEntityId;
	}

	void C_CombatSessionComponent::Start()
	{
		if (!m_state)
			m_state = std::make_unique<SessionState>();
		m_state->Init();

		if (auto* world = GetWorld())
			world->SetScriptCombatEnabled(true);
	}

	void C_CombatSessionComponent::OnEnable()
	{
		if (!m_state)
			m_state = std::make_unique<SessionState>();

		if (auto* world = GetWorld())
			world->SetScriptCombatEnabled(true);
	}

	void C_CombatSessionComponent::OnDisable()
	{
		if (m_state)
			m_state->Init();

		if (auto* world = GetWorld())
			world->SetScriptCombatEnabled(false);
	}

	void C_CombatSessionComponent::ForceReset()
	{
		if (m_state)
			m_state->Init();
	}
	void C_CombatSessionComponent::Update(float deltaTime)
	{
		if (!m_state || !GetWorld())
			return;

		World& world = *GetWorld();
		EntityId playerId = ResolveEntity(m_playerGuid);
		EntityId bossId = ResolveEntity(m_bossGuid);
		if (playerId == InvalidEntityId && m_autoResolveByName)
			playerId = ResolveEntityByName(m_playerName);
		if (bossId == InvalidEntityId && m_autoResolveByName)
			bossId = ResolveEntityByName(m_bossName);
		if (playerId == InvalidEntityId || bossId == InvalidEntityId)
		{
			if (m_enableLogs)
			{
				ALICE_LOG_WARN("[CombatSession] Update skipped: player=%llu boss=%llu (guid=%llu/%llu name=%s/%s)",
					static_cast<unsigned long long>(playerId),
					static_cast<unsigned long long>(bossId),
					static_cast<unsigned long long>(m_playerGuid),
					static_cast<unsigned long long>(m_bossGuid),
					m_playerName.c_str(),
					m_bossName.c_str());
			}
			return;
		}

		m_state->player.id = playerId;
		m_state->player.team = Combat::Team::Player;
		m_state->player.canBeHitstunned = m_playerCanBeHitstunned;
		m_state->boss.id = bossId;
		m_state->boss.team = Combat::Team::Enemy;
		m_state->boss.canBeHitstunned = m_bossCanBeHitstunned;

		m_state->fighterMap.clear();
		m_state->fighterMap[playerId] = &m_state->player;
		m_state->fighterMap[bossId] = &m_state->boss;

		Combat::Intent playerIntent{};
		if (auto* script = FindScriptOnEntity(world, playerId, "C_PlayerInputSourceComponent"))
		{
			if (auto* input = dynamic_cast<C_PlayerInputSourceComponent*>(script))
				playerIntent = input->GetIntent(deltaTime);
		}

		Combat::Intent bossIntent{};
		if (auto* script = FindScriptOnEntity(world, bossId, "C_BossBrainComponent"))
		{
			if (auto* brain = dynamic_cast<C_BossBrainComponent*>(script))
				bossIntent = brain->Think(deltaTime, playerId);
		}

		auto UpdateDriverInput = [&](EntityId entityId, const Combat::Intent& intent)
			{
				if (auto* driver = world.GetComponent<AttackDriverComponent>(entityId))
				{
					driver->guardInputHeld = intent.guardHeld;
					driver->guardInputPressed = intent.guardPressed;
					driver->guardInputReleased = intent.guardReleased;
					driver->guardLockRemainingSec = std::max(0.0f, driver->guardLockRemainingSec - deltaTime);
					driver->parryOverrideRemainingSec = std::max(0.0f, driver->parryOverrideRemainingSec - deltaTime);
					const bool sessionActive = intent.guardHeld || intent.guardPressed || driver->guardLockRemainingSec > 0.0f;
					if (!sessionActive)
					{
						driver->guardSessionActive = false;
						driver->parryTapCredit = 0;
					}
					else if (!driver->guardSessionActive)
					{
						driver->guardSessionActive = true;
						driver->parryTapCredit = 1;
					}

					if (intent.guardPressed && driver->parryTapCredit > 0)
					{
						driver->parryTapCredit = 0;
						driver->parryUsedThisPress = false;
						if (intent.parryTapWindowSec > 0.0f)
							driver->parryOverrideRemainingSec = std::max(driver->parryOverrideRemainingSec, intent.parryTapWindowSec);
					}
				}
			};

		UpdateDriverInput(playerId, playerIntent);
		UpdateDriverInput(bossId, bossIntent);

		constexpr float kDegToRad = 0.01745329252f;

		const EntityId cameraId = ResolvePrimaryCamera(world);
		auto* camFollow = (cameraId != InvalidEntityId) ? world.GetComponent<CameraFollowComponent>(cameraId) : nullptr;
		auto* camLookAt = (cameraId != InvalidEntityId) ? world.GetComponent<CameraLookAtComponent>(cameraId) : nullptr;
		auto* camTr = (cameraId != InvalidEntityId) ? world.GetComponent<TransformComponent>(cameraId) : nullptr;

		MoveBasis camBasis{};
		if (cameraId != InvalidEntityId)
		{
			float yawRad = 0.0f;
			bool hasYaw = false;
			if (camFollow && camFollow->enabled)
			{
				yawRad = camFollow->yawDeg * kDegToRad;
				hasYaw = true;
			}
			else if (camTr)
			{
				yawRad = camTr->rotation.y;
				hasYaw = true;
			}
			if (hasYaw)
				camBasis = BuildYawBasis(yawRad);
		}

		const bool canLockOn = (camFollow && camFollow->enableLockOn);
		if (playerIntent.lockOnToggle && canLockOn)
		{
			if (m_state->playerLockOnActive)
			{
				m_state->playerLockOnActive = false;
				m_state->playerLockOnTarget = InvalidEntityId;
			}
			else
			{
				m_state->playerLockOnActive = true;
				m_state->playerLockOnTarget = bossId;
			}
		}

		if (m_state->playerLockOnActive)
			m_state->playerLockOnTarget = bossId;

		if (camFollow)
		{
			camFollow->lockOnActive = m_state->playerLockOnActive;
			if (m_state->playerLockOnActive)
			{
				camFollow->lockOnTargetId = m_state->playerLockOnTarget;
				camFollow->mode = 2;
			}
			else
			{
				camFollow->lockOnTargetId = InvalidEntityId;
				camFollow->mode = 0;
			}
		}

		if (camLookAt)
		{
			camLookAt->enabled = m_state->playerLockOnActive;
			if (m_state->playerLockOnActive)
				camLookAt->targetName = m_bossName.empty() ? std::string("Enemy") : m_bossName;
		}

		Combat::Sensors sPlayer = m_state->player.BuildSensors(world, bossId, deltaTime);
		Combat::Sensors sBoss = m_state->boss.BuildSensors(world, playerId, deltaTime);

		m_state->player.hp = sPlayer.hp;
		m_state->boss.hp = sBoss.hp;
		m_state->player.weaponDurability = sPlayer.weaponDurability;
		m_state->player.weaponDurabilityMax = sPlayer.weaponDurabilityMax;
		m_state->player.weakRemainingSec = sPlayer.weakRemainingSec;
		m_state->boss.weaponDurability = sBoss.weaponDurability;
		m_state->boss.weaponDurabilityMax = sBoss.weaponDurabilityMax;
		m_state->boss.weakRemainingSec = sBoss.weakRemainingSec;

		const auto& ePlayer = m_state->bus.PeekDeferred(playerId);
		const auto& eBoss = m_state->bus.PeekDeferred(bossId);

		auto outPlayer = m_state->playerFsm.Update(playerId, playerIntent, sPlayer, ePlayer, deltaTime);
		auto outBoss = m_state->bossFsm.Update(bossId, bossIntent, sBoss, eBoss, deltaTime);

		const float attackForwardOffsetRad = m_rotationOffsetDeg * kDegToRad;

		auto ResolveAttackMoveDir = [&](EntityId entityId,
			const Combat::Intent& intent,
			bool useCameraBasis) -> Combat::Vec2
			{
				float dx = 0.0f;
				float dz = 0.0f;
				const float inputMag = std::abs(intent.move.x) + std::abs(intent.move.y);
				if (inputMag > 0.001f)
				{
					dx = intent.move.x;
					dz = intent.move.y;
					if (useCameraBasis && camBasis.valid)
					{
						const float inputX = dx;
						const float inputZ = dz;
						dx = camBasis.rightX * inputX + camBasis.forwardX * inputZ;
						dz = camBasis.rightZ * inputX + camBasis.forwardZ * inputZ;
					}
				}
				else if (auto* tr = world.GetComponent<TransformComponent>(entityId))
				{
					const float yawRad = tr->rotation.y - attackForwardOffsetRad;
					dx = std::sin(yawRad);
					dz = std::cos(yawRad);
				}

				const float len = std::sqrt(dx * dx + dz * dz);
				if (len > 0.0001f)
				{
					dx /= len;
					dz /= len;
				}
				else
				{
					dx = 0.0f;
					dz = 0.0f;
				}

				return { dx, dz };
			};

		auto TryGetClipTime = [&](EntityId entityId, const std::string& clip, float& outTime) -> bool
			{
				if (clip.empty())
					return false;

				auto* anim = world.GetComponent<AdvancedAnimationComponent>(entityId);
				if (!anim)
					return false;

				if (anim->base.clipA == clip)
				{
					outTime = anim->base.timeA;
					return true;
				}
				if (anim->base.clipB == clip)
				{
					outTime = anim->base.timeB;
					return true;
				}
				if (anim->upper.clipA == clip)
				{
					outTime = anim->upper.timeA;
					return true;
				}
				if (anim->upper.clipB == clip)
				{
					outTime = anim->upper.timeB;
					return true;
				}
				if (anim->additive.clip == clip)
				{
					outTime = anim->additive.time;
					return true;
				}

				return false;
			};

		auto TryGetAttackClipTime = [&](EntityId entityId,
			const SessionState::AttackMoveState& moveState,
			float& outTime) -> bool
			{
				if (moveState.heavy)
				{
					if (TryGetClipTime(entityId, m_heavyAttackClipA, outTime))
						return true;
					if (TryGetClipTime(entityId, m_heavyAttackClipB, outTime))
						return true;
				}
				else
				{
					if (TryGetClipTime(entityId, m_lightAttackClip, outTime))
						return true;
				}

				if (TryGetClipTime(entityId, m_lightAttackClip, outTime))
					return true;
				if (TryGetClipTime(entityId, m_heavyAttackClipA, outTime))
					return true;
				if (TryGetClipTime(entityId, m_heavyAttackClipB, outTime))
					return true;

				return false;
			};

		auto UpdateAttackMove = [&](SessionState::AttackMoveState& moveState,
			EntityId entityId,
			const Combat::Intent& intent,
			Combat::ActionState curr,
			Combat::ActionState prev,
			bool useCameraBasis,
			std::vector<Combat::Command>& cmds,
			float deltaTime,
			float lightDist,
			float heavyDist,
			float lightStart,
			float heavyStart,
			float lightDuration,
			float heavyDuration)
			{
				if (curr != Combat::ActionState::Attack)
				{
					moveState = {};
					return;
				}

				if (prev != Combat::ActionState::Attack)
				{
					const bool heavy = intent.heavyAttackPressed;
					const float dist = heavy ? heavyDist : lightDist;
					const float startSec = heavy ? heavyStart : lightStart;
					const float duration = heavy ? heavyDuration : lightDuration;

					if (dist > 0.0f && duration > 0.0f)
					{
						moveState.configured = true;
						moveState.heavy = heavy;
						moveState.timerSec = 0.0f;
						moveState.startSec = std::max(0.0f, startSec);
						moveState.endSec = moveState.startSec + duration;
						moveState.dir = ResolveAttackMoveDir(entityId, intent, useCameraBasis);

						const float dirLen = std::sqrt(moveState.dir.x * moveState.dir.x + moveState.dir.y * moveState.dir.y);
						if (dirLen <= 0.0001f)
						{
							moveState = {};
							return;
						}

						moveState.speed = dist / duration;
					}
					else
					{
						moveState = {};
					}

					moveState.active = false;
					return;
				}

				if (!moveState.configured)
				{
					moveState.active = false;
					return;
				}

				// TODO: temp feel-tuning. Use real animation timing / root-motion later.
				moveState.timerSec += deltaTime;
				const bool withinWindow = (moveState.timerSec >= moveState.startSec && moveState.timerSec <= moveState.endSec);
				moveState.active = withinWindow;

				if (withinWindow)
				{
					if (m_debugAttackMoveTime)
					{
						ALICE_LOG_INFO("[AttackMove] entity=%llu heavy=%d t=%.3f window=[%.3f, %.3f]",
							static_cast<unsigned long long>(entityId),
							moveState.heavy ? 1 : 0,
							moveState.timerSec,
							moveState.startSec,
							moveState.endSec);
					}
					cmds.push_back({ Combat::CommandType::RequestMove,
						Combat::CmdRequestMove{ entityId, moveState.dir, moveState.speed, false, false } });
				}
			};

		// TEMP: disable attack-driven forward move while tuning.
		// UpdateAttackMove(m_state->playerAttackMove,
		//                  playerId,
		//                  playerIntent,
		//                  outPlayer.state,
		//                  m_state->prevPlayerState,
		//                  true,
		//                  outPlayer.commands,
		//                  deltaTime,
		//                  m_lightAttackMoveDistance,
		//                  m_heavyAttackMoveDistance,
		//                  m_lightAttackMoveStartSec,
		//                  m_heavyAttackMoveStartSec,
		//                  m_lightAttackMoveDurationSec,
		//                  m_heavyAttackMoveDurationSec);
		m_state->player.state = outPlayer.state;
		m_state->player.flags = outPlayer.flags;
		m_state->boss.state = outBoss.state;
		m_state->boss.flags = outBoss.flags;
		m_state->playerSnapshot = m_state->player.Snapshot();
		m_state->bossSnapshot = m_state->boss.Snapshot();

		m_state->bus.ClearDeferred(playerId);
		m_state->bus.ClearDeferred(bossId);

		auto ApplyMove = [&](EntityId entityId,
			const Combat::Intent& intent,
			const std::vector<Combat::Command>& cmds)
			{
				constexpr float kRadToDeg = 57.2957795f;
				const float offsetRad = m_rotationOffsetDeg * kDegToRad;

				for (const auto& cmd : cmds)
				{
					if (cmd.type != Combat::CommandType::RequestMove)
						continue;
					const auto payload = std::get<Combat::CmdRequestMove>(cmd.payload);
					auto* cct = world.GetComponent<Phy_CCTComponent>(payload.target);
					if (!cct)
					{
						if (m_enableLogs)
						{
							ALICE_LOG_WARN("[CombatSession] Missing CCT on entity=%llu",
								static_cast<unsigned long long>(payload.target));
						}
						continue;
					}
					float inputX = payload.move.x;
					float inputZ = payload.move.y;
					float dx = inputX;
					float dz = inputZ;
					const bool isPlayer = (entityId == playerId);

					if (isPlayer && payload.useCameraRelative && camBasis.valid)
					{
						dx = camBasis.rightX * inputX + camBasis.forwardX * inputZ;
						dz = camBasis.rightZ * inputX + camBasis.forwardZ * inputZ;
					}

					const float len = std::sqrt(dx * dx + dz * dz);
					if (len > 0.0001f)
					{
						dx /= len;
						dz /= len;
					}
					cct->desiredVelocity.x = dx * payload.speed;
					cct->desiredVelocity.z = dz * payload.speed;
					cct->desiredVelocity.y = 0.0f;
					if (m_enableLogs && (dx != 0.0f || dz != 0.0f))
					{
						ALICE_LOG_INFO("[CombatSession] Move entity=%llu dir(%.2f,%.2f) speed=%.2f",
							static_cast<unsigned long long>(entityId),
							dx, dz, payload.speed);
					}

					if (payload.faceMove && (dx != 0.0f || dz != 0.0f))
					{
						if (auto* tr = world.GetComponent<TransformComponent>(entityId))
						{
							const float yawRad = std::atan2(dx, dz) + offsetRad;
							tr->SetRotation(0.0f, yawRad * kRadToDeg, 0.0f);
						}
					}
				}
			};
		ApplyMove(playerId, playerIntent, outPlayer.commands);
		ApplyMove(bossId, bossIntent, outBoss.commands);

		auto StopIfNotMoving = [&](EntityId entityId, Combat::ActionState state, const SessionState::AttackMoveState& attackMove)
			{
				if (state == Combat::ActionState::Move || state == Combat::ActionState::Dodge || (state == Combat::ActionState::Attack && attackMove.active))
					return;
				if (auto* hc = world.GetComponent<HealthComponent>(entityId))
				{
					if (hc->pushbackRemainingSec > 0.0f && hc->pushbackSpeed > 0.0f)
						return;
				}
				auto* cct = world.GetComponent<Phy_CCTComponent>(entityId);
				if (!cct)
					return;
				cct->desiredVelocity.x = 0.0f;
				cct->desiredVelocity.z = 0.0f;
				cct->desiredVelocity.y = 0.0f;
			};
		StopIfNotMoving(playerId, outPlayer.state, m_state->playerAttackMove);
		StopIfNotMoving(bossId, outBoss.state, m_state->bossAttackMove);

		auto ApplyPushback = [&](EntityId entityId)
			{
				auto* hc = world.GetComponent<HealthComponent>(entityId);
				if (!hc || hc->pushbackRemainingSec <= 0.0f || hc->pushbackSpeed <= 0.0f)
					return;

				const float dx = hc->pushbackDir.x;
				const float dz = hc->pushbackDir.z;
				const float len = std::sqrt(dx * dx + dz * dz);
				if (len <= 0.0001f)
					return;

				if (auto* cct = world.GetComponent<Phy_CCTComponent>(entityId))
				{
					cct->desiredVelocity.x += (dx / len) * hc->pushbackSpeed;
					cct->desiredVelocity.z += (dz / len) * hc->pushbackSpeed;
				}
			};

		ApplyPushback(playerId);
		ApplyPushback(bossId);

		if (m_enableLogs)
		{
			ALICE_LOG_INFO("[CombatSession] Player state=%u cmds=%zu",
				static_cast<unsigned>(outPlayer.state),
				outPlayer.commands.size());
		}

		auto ApplyTraceCommands = [&](const std::vector<Combat::Command>& cmds)
			{
				std::vector<Combat::Command> traceCmds;
				for (const auto& cmd : cmds)
				{
					if (cmd.type == Combat::CommandType::EnableTrace ||
						cmd.type == Combat::CommandType::DisableTrace)
					{
						traceCmds.push_back(cmd);
					}
				}
				if (!traceCmds.empty())
					m_state->apply.ApplyImmediate(world, m_state->fighterMap, m_state->bus, traceCmds, true);
			};
		ApplyTraceCommands(outPlayer.commands);
		ApplyTraceCommands(outBoss.commands);

		auto SmoothApproach = [&](float current, float target, float speed, float dt) {
			const float t = std::clamp(speed * dt, 0.0f, 1.0f);
			return current + (target - current) * t;
			};
		struct AnimConfig
		{
			std::string idleClip;
			std::string moveClip;
			std::string lightAttackClip;
			std::string heavyAttackClipA;
			std::string heavyAttackClipB;
			std::string dodgeClip;
			std::string guardEnterClip;
			std::string guardLoopClip;
			std::string guardExitClip;
			float guardEnterDurationSec = 0.0f;
			float guardExitDurationSec = 0.0f;
		};
		auto GetAnimConfig = [&](EntityId entityId) -> AnimConfig
			{
				const bool isPlayer = (entityId == playerId);
				const bool isBoss = (entityId == bossId);
				AnimConfig cfg{};
				cfg.idleClip = m_idleClip;
				cfg.moveClip = m_moveClip;
				cfg.lightAttackClip = m_lightAttackClip;
				cfg.heavyAttackClipA = m_heavyAttackClipA;
				cfg.heavyAttackClipB = m_heavyAttackClipB;
				cfg.dodgeClip = m_dodgeClip;
				cfg.guardEnterClip = m_guardEnterClip;
				cfg.guardLoopClip = m_guardLoopClip;
				cfg.guardExitClip = m_guardExitClip;
				cfg.guardEnterDurationSec = m_guardEnterDurationSec;
				cfg.guardExitDurationSec = m_guardExitDurationSec;

				if (isPlayer)
				{
					if (!m_playerIdleClip.empty()) cfg.idleClip = m_playerIdleClip;
					if (!m_playerMoveClip.empty()) cfg.moveClip = m_playerMoveClip;
					if (!m_playerLightAttackClip.empty()) cfg.lightAttackClip = m_playerLightAttackClip;
					if (!m_playerHeavyAttackClipA.empty()) cfg.heavyAttackClipA = m_playerHeavyAttackClipA;
					if (!m_playerHeavyAttackClipB.empty()) cfg.heavyAttackClipB = m_playerHeavyAttackClipB;
					if (!m_playerDodgeClip.empty()) cfg.dodgeClip = m_playerDodgeClip;
					if (!m_playerGuardEnterClip.empty()) cfg.guardEnterClip = m_playerGuardEnterClip;
					if (!m_playerGuardLoopClip.empty()) cfg.guardLoopClip = m_playerGuardLoopClip;
					if (!m_playerGuardExitClip.empty()) cfg.guardExitClip = m_playerGuardExitClip;
					if (m_playerGuardEnterDurationSec > 0.0f) cfg.guardEnterDurationSec = m_playerGuardEnterDurationSec;
					if (m_playerGuardExitDurationSec > 0.0f) cfg.guardExitDurationSec = m_playerGuardExitDurationSec;
				}
				else if (isBoss)
				{
					if (!m_bossIdleClip.empty()) cfg.idleClip = m_bossIdleClip;
					if (!m_bossMoveClip.empty()) cfg.moveClip = m_bossMoveClip;
					if (!m_bossLightAttackClip.empty()) cfg.lightAttackClip = m_bossLightAttackClip;
					if (!m_bossHeavyAttackClipA.empty()) cfg.heavyAttackClipA = m_bossHeavyAttackClipA;
					if (!m_bossHeavyAttackClipB.empty()) cfg.heavyAttackClipB = m_bossHeavyAttackClipB;
					if (!m_bossDodgeClip.empty()) cfg.dodgeClip = m_bossDodgeClip;
					if (!m_bossGuardEnterClip.empty()) cfg.guardEnterClip = m_bossGuardEnterClip;
					if (!m_bossGuardLoopClip.empty()) cfg.guardLoopClip = m_bossGuardLoopClip;
					if (!m_bossGuardExitClip.empty()) cfg.guardExitClip = m_bossGuardExitClip;
					if (m_bossGuardEnterDurationSec > 0.0f) cfg.guardEnterDurationSec = m_bossGuardEnterDurationSec;
					if (m_bossGuardExitDurationSec > 0.0f) cfg.guardExitDurationSec = m_bossGuardExitDurationSec;
				}
				return cfg;
			};
		auto ResolveHeavyAttackClip = [&](SessionState::AnimOverrideState& animState,
			const AnimConfig& cfg) -> std::string {
				if (!cfg.heavyAttackClipA.empty() && !cfg.heavyAttackClipB.empty())
				{
					animState.heavyToggle = !animState.heavyToggle;
					return animState.heavyToggle ? cfg.heavyAttackClipA : cfg.heavyAttackClipB;
				}
				if (!cfg.heavyAttackClipA.empty())
					return cfg.heavyAttackClipA;
				if (!cfg.heavyAttackClipB.empty())
					return cfg.heavyAttackClipB;
				return {};
			};

		auto SelectAttackClip = [&](const Combat::Intent& intent,
			SessionState::AnimOverrideState& animState,
			const AnimConfig& cfg) -> std::string {
				if (intent.heavyAttackPressed)
				{
					std::string heavy = ResolveHeavyAttackClip(animState, cfg);
					if (!heavy.empty())
						return heavy;
				}
				if (intent.lightAttackPressed && !cfg.lightAttackClip.empty())
					return cfg.lightAttackClip;
				if (!cfg.lightAttackClip.empty())
					return cfg.lightAttackClip;
				return {};
			};

		auto UpdateAttackClip = [&](const Combat::Intent& intent,
			Combat::ActionState curr,
			Combat::ActionState prev,
			SessionState::AnimOverrideState& animState,
			const AnimConfig& cfg) {
				if (curr == Combat::ActionState::Attack)
				{
					if (intent.heavyAttackPressed || intent.lightAttackPressed || prev != Combat::ActionState::Attack || animState.attackClip.empty())
						animState.attackClip = SelectAttackClip(intent, animState, cfg);
				}
				else
				{
					animState.attackClip.clear();
				}
			};

		UpdateAttackClip(playerIntent, outPlayer.state, m_state->prevPlayerState, m_state->playerAnim, GetAnimConfig(playerId));
		UpdateAttackClip(bossIntent, outBoss.state, m_state->prevBossState, m_state->bossAnim, GetAnimConfig(bossId));

		auto ApplyAnimByState = [&](EntityId entityId,
			Combat::ActionState curr,
			Combat::ActionState& prev,
			SessionState::AnimOverrideState& animState,
			float& moveBlend) {
				auto* anim = world.GetComponent<AdvancedAnimationComponent>(entityId);
				if (!anim)
					anim = &world.AddComponent<AdvancedAnimationComponent>(entityId);
				auto* driver = world.GetComponent<AttackDriverComponent>(entityId);
				if (!anim || !driver)
				{
					prev = curr;
					return;
				}
				const AnimConfig cfg = GetAnimConfig(entityId);

				anim->enabled = true;
				anim->playing = true;
				anim->upper.enabled = false;
				anim->additive.enabled = false;
				anim->ik.enabled = false;
				for (auto& ik : anim->ikChains)
					ik.enabled = false;

				auto resolveClipByType = [&](AttackDriverNotifyType type) -> std::string {
					for (const auto& clip : driver->clips)
					{
						if (!clip.enabled || clip.type != type)
							continue;

						switch (clip.source)
						{
						case AttackDriverClipSource::BaseA: return anim->base.clipA;
						case AttackDriverClipSource::BaseB: return anim->base.clipB;
						case AttackDriverClipSource::UpperA: return anim->upper.clipA;
						case AttackDriverClipSource::UpperB: return anim->upper.clipB;
						case AttackDriverClipSource::Additive: return anim->additive.clip;
						case AttackDriverClipSource::Explicit:
						default: return clip.clipName;
						}
					}
					return {};
					};

				// Attack clip slow-motion was removed.
				// auto ResolveOverrideSpeed = [&](EntityId targetId,
				//                                 Combat::ActionState state,
				//                                 const std::string& name) -> float
				// {
				//     if (state != Combat::ActionState::Attack)
				//         return 1.0f;
				//     if (targetId != playerId)
				//         return 1.0f;
				//     if (m_attackSlowClipName.empty() || name != m_attackSlowClipName)
				//         return 1.0f;
				//     return std::max(0.0f, m_attackSlowSpeed);
				// };

				const bool enteringGuard = (curr == Combat::ActionState::Guard && prev != Combat::ActionState::Guard);
				const bool exitingGuard = (prev == Combat::ActionState::Guard
					&& curr != Combat::ActionState::Guard
					&& (curr == Combat::ActionState::Idle || curr == Combat::ActionState::Move));
				if (enteringGuard && !cfg.guardEnterClip.empty() && cfg.guardEnterDurationSec > 0.0f)
				{
					animState.guardEnterActive = true;
					animState.guardEnterTimer = 0.0f;
				}
				if (exitingGuard && !cfg.guardExitClip.empty() && cfg.guardExitDurationSec > 0.0f)
				{
					animState.guardExitActive = true;
					animState.guardExitTimer = 0.0f;
				}
				if (curr == Combat::ActionState::Attack || curr == Combat::ActionState::Dodge
					|| curr == Combat::ActionState::Hitstun || curr == Combat::ActionState::Groggy
					|| curr == Combat::ActionState::Dead)
				{
					animState.guardEnterActive = false;
					animState.guardExitActive = false;
				}

				std::string clipName;
				bool loop = false;
				if (curr == Combat::ActionState::Attack)
				{
					clipName = animState.attackClip.empty()
						? resolveClipByType(AttackDriverNotifyType::Attack)
						: animState.attackClip;
				}
				else if (curr == Combat::ActionState::Dodge)
				{
					clipName = cfg.dodgeClip.empty()
						? resolveClipByType(AttackDriverNotifyType::Dodge)
						: cfg.dodgeClip;
				}
				else if (curr == Combat::ActionState::Guard || curr == Combat::ActionState::JustGuardSuccess)
				{
					if (animState.guardEnterActive)
					{
						clipName = cfg.guardEnterClip;
						loop = false;
					}
					else
					{
						const std::string guardLoop = !cfg.guardLoopClip.empty()
							? cfg.guardLoopClip
							: resolveClipByType(AttackDriverNotifyType::Guard);
						clipName = guardLoop;
						loop = (curr == Combat::ActionState::Guard);
					}
				}
				else if (animState.guardExitActive)
				{
					clipName = cfg.guardExitClip;
					loop = false;
				}

				const bool wantsOverride = !clipName.empty()
					&& (curr == Combat::ActionState::Attack
						|| curr == Combat::ActionState::Dodge
						|| curr == Combat::ActionState::Guard
						|| curr == Combat::ActionState::JustGuardSuccess
						|| animState.guardExitActive);

				const bool isLocomotion = (curr == Combat::ActionState::Idle || curr == Combat::ActionState::Move);
				if (isLocomotion && !animState.overrideActive && !cfg.idleClip.empty())
				{
					const float targetBlend = (curr == Combat::ActionState::Move && !cfg.moveClip.empty()) ? 1.0f : 0.0f;
					moveBlend = SmoothApproach(moveBlend, targetBlend, m_moveBlendSpeed, deltaTime);

					anim->base.autoAdvance = true;
					anim->base.clipA = cfg.idleClip;
					anim->base.clipB = cfg.moveClip.empty() ? cfg.idleClip : cfg.moveClip;
					anim->base.loopA = true;
					anim->base.loopB = true;
					anim->base.speedA = 1.0f;
					anim->base.speedB = 1.0f;
					anim->base.blend01 = moveBlend;
				}

				float overrideSpeed = 1.0f;
				const bool wantsReverse = animState.guardExitActive && !cfg.guardExitClip.empty() && cfg.guardExitDurationSec > 0.0f;
				if (wantsReverse && overrideSpeed > 0.0f)
					overrideSpeed = -overrideSpeed;

				auto BeginBlendToOverride = [&](const std::string& nextClip, bool nextLoop, float startTime) {
					if (!animState.overrideActive)
					{
						animState.savedBase = anim->base;
						animState.saved = true;
					}

					animState.overrideActive = true;
					animState.overrideClip = nextClip;
					animState.overrideLoop = nextLoop;

					if (m_animBlendSec <= 0.0f)
					{
						anim->base.autoAdvance = true;
						anim->base.clipA = nextClip;
						anim->base.clipB = nextClip;
						anim->base.timeA = startTime;
						anim->base.timeB = startTime;
						anim->base.speedA = overrideSpeed;
						anim->base.speedB = overrideSpeed;
						anim->base.loopA = nextLoop;
						anim->base.loopB = nextLoop;
						anim->base.blend01 = 0.0f;
						animState.blending = false;
						animState.blendingToOverride = true;
						return;
					}

					animState.blending = true;
					animState.blendingToOverride = true;
					animState.blendTimer = 0.0f;

					anim->base.autoAdvance = true;
					anim->base.clipB = nextClip;
					anim->base.timeB = startTime;
					anim->base.speedB = overrideSpeed;
					anim->base.loopB = nextLoop;
					anim->base.blend01 = 0.0f;
					};

				auto BeginBlendToSaved = [&]() {
					if (!animState.saved)
					{
						animState.overrideActive = false;
						animState.overrideClip.clear();
						animState.blending = false;
						return;
					}

					if (m_animBlendSec <= 0.0f)
					{
						anim->base = animState.savedBase;
						anim->base.timeA = 0.0f;
						anim->base.timeB = 0.0f;
						animState.overrideActive = false;
						animState.overrideClip.clear();
						animState.saved = false;
						animState.blending = false;
						return;
					}

					animState.blending = true;
					animState.blendingToOverride = false;
					animState.blendTimer = 0.0f;

					anim->base.autoAdvance = true;
					anim->base.clipB = animState.savedBase.clipA;
					anim->base.timeB = 0.0f;
					anim->base.speedB = animState.savedBase.speedA;
					anim->base.loopB = animState.savedBase.loopA;
					anim->base.blend01 = 0.0f;
					};

				auto StepBlend = [&]() {
					if (!animState.blending || m_animBlendSec <= 0.0f)
						return;

					animState.blendTimer += deltaTime;
					float alpha = animState.blendTimer / m_animBlendSec;
					if (alpha > 1.0f)
						alpha = 1.0f;

					anim->base.blend01 = alpha;

					if (alpha >= 1.0f)
					{
						if (animState.blendingToOverride)
						{
							anim->base.clipA = animState.overrideClip;
							anim->base.timeA = anim->base.timeB;
							anim->base.speedA = anim->base.speedB;
							anim->base.loopA = animState.overrideLoop;
							anim->base.clipB = animState.overrideClip;
							anim->base.timeB = anim->base.timeA;
							anim->base.blend01 = 0.0f;
							animState.blending = false;
						}
						else
						{
							anim->base = animState.savedBase;
							anim->base.timeA = 0.0f;
							anim->base.timeB = 0.0f;
							animState.overrideActive = false;
							animState.overrideClip.clear();
							animState.saved = false;
							animState.blending = false;
						}
					}
					};

				if (wantsOverride)
				{
					const bool clipChanged = !animState.overrideActive
						|| animState.overrideClip != clipName
						|| animState.overrideLoop != loop;
					if (clipChanged)
					{
						const float startTime = wantsReverse ? cfg.guardExitDurationSec : 0.0f;
						BeginBlendToOverride(clipName, loop, startTime);
					}
				}
				else if (animState.overrideActive)
				{
					if (!animState.blending || animState.blendingToOverride)
						BeginBlendToSaved();
				}

				StepBlend();
				if (animState.overrideActive && !animState.blending)
				{
					anim->base.speedA = overrideSpeed;
					anim->base.speedB = overrideSpeed;
				}
				if (animState.guardEnterActive)
				{
					animState.guardEnterTimer += deltaTime;
					if (animState.guardEnterTimer >= cfg.guardEnterDurationSec)
						animState.guardEnterActive = false;
				}
				if (animState.guardExitActive)
				{
					animState.guardExitTimer += deltaTime;
					if (animState.guardExitTimer >= cfg.guardExitDurationSec)
						animState.guardExitActive = false;
				}
				prev = curr;
			};

		ApplyAnimByState(playerId, outPlayer.state, m_state->prevPlayerState, m_state->playerAnim, m_state->playerMoveBlend);
		ApplyAnimByState(bossId, outBoss.state, m_state->prevBossState, m_state->bossAnim, m_state->bossMoveBlend);
	}

	void C_CombatSessionComponent::PostCombatUpdate(float deltaTime)
	{
		if (!m_state || !GetWorld())
			return;

		World& world = *GetWorld();
		EntityId playerId = ResolveEntity(m_playerGuid);
		EntityId bossId = ResolveEntity(m_bossGuid);
		if (playerId == InvalidEntityId && m_autoResolveByName)
			playerId = ResolveEntityByName(m_playerName);
		if (bossId == InvalidEntityId && m_autoResolveByName)
			bossId = ResolveEntityByName(m_bossName);
		if (playerId == InvalidEntityId || bossId == InvalidEntityId)
			return;

		m_state->player.id = playerId;
		m_state->player.team = Combat::Team::Player;
		m_state->player.canBeHitstunned = m_playerCanBeHitstunned;
		m_state->boss.id = bossId;
		m_state->boss.team = Combat::Team::Enemy;
		m_state->boss.canBeHitstunned = m_bossCanBeHitstunned;

		m_state->bus.ClearFrame();
		if (world.HasFrameCombatHits())
		{
			std::vector<Combat::HitEvent> sortedHits = world.GetFrameCombatHits();
			std::sort(sortedHits.begin(), sortedHits.end(), HitSortLess);

			uint32_t lastAttackInstanceId = 0;
			EntityId lastAttacker = InvalidEntityId;
			EntityId lastVictim = InvalidEntityId;
			bool hasLast = false;

			for (const auto& hit : sortedHits)
			{
				const bool sameGroup = hasLast
					&& hit.attackInstanceId == lastAttackInstanceId
					&& hit.attackerOwner == lastAttacker
					&& hit.victimOwner == lastVictim;
				if (sameGroup)
					continue;

				m_state->bus.PushHit(hit);
				hasLast = true;
				lastAttackInstanceId = hit.attackInstanceId;
				lastAttacker = hit.attackerOwner;
				lastVictim = hit.victimOwner;
			}
		}

		bool bossGroggyTriggered = false;
		for (const auto& hit : m_state->bus.Hits())
		{
			const Combat::FighterSnapshot& playerSnap = m_state->playerSnapshot;
			const Combat::FighterSnapshot& bossSnap = m_state->bossSnapshot;
			Combat::FighterSnapshot attacker = (hit.attackerOwner == playerId) ? playerSnap : bossSnap;
			Combat::FighterSnapshot victim = (hit.victimOwner == playerId) ? playerSnap : bossSnap;

			auto resolved = m_state->resolver.ResolveOne(hit, attacker, victim);

			if (m_enableCombatLogs)
			{
				const std::string attackerName = GetEntityLabel(world, attacker.id);
				const std::string victimName = GetEntityLabel(world, victim.id);
				const bool wasParrySuccess = HasDeferredEvent(resolved, Combat::CombatEventType::OnParrySuccess);
				const bool wasGotParried = HasDeferredEvent(resolved, Combat::CombatEventType::OnGotParried);
				const bool wasGuard = HasDeferredEvent(resolved, Combat::CombatEventType::OnGuarded);
				const bool wasGuardBreak = HasDeferredEvent(resolved, Combat::CombatEventType::OnGuardBreak);
				const bool wasHit = HasDeferredEvent(resolved, Combat::CombatEventType::OnHit);
				if (wasParrySuccess)
				{
					ALICE_LOG_INFO("[Combat] ParrySuccess victim=%s attacker=%s attackId=%u",
						victimName.c_str(), attackerName.c_str(), hit.attackInstanceId);
				}
				if (wasGotParried)
				{
					ALICE_LOG_INFO("[Combat] GotParried attacker=%s victim=%s attackId=%u",
						attackerName.c_str(), victimName.c_str(), hit.attackInstanceId);
				}
				if (wasGuard)
				{
					ALICE_LOG_INFO("[Combat] Guarded victim=%s attacker=%s cost=%.2f attackId=%u",
						victimName.c_str(), attackerName.c_str(), hit.guardDurabilityCost, hit.attackInstanceId);
				}
				if (wasGuardBreak)
				{
					ALICE_LOG_INFO("[Combat] GuardBreak victim=%s attacker=%s attackId=%u",
						victimName.c_str(), attackerName.c_str(), hit.attackInstanceId);
				}
				if (wasHit)
				{
					ALICE_LOG_INFO("[Combat] Hit victim=%s attacker=%s dmg=%.2f attackId=%u",
						victimName.c_str(), attackerName.c_str(), hit.damage, hit.attackInstanceId);
				}
			}

			UpdateHealthHitInfo(world, hit, resolved, victim);
			m_state->apply.ApplyImmediate(world, m_state->fighterMap, m_state->bus, resolved.immediate, false);

			if (HasDeferredEvent(resolved, Combat::CombatEventType::OnParrySuccess)
				|| HasDeferredEvent(resolved, Combat::CombatEventType::OnGuarded))
			{
				if (auto* driver = world.GetComponent<AttackDriverComponent>(hit.victimOwner))
					driver->parryTapCredit = 1;
			}

			for (const auto& ev : resolved.deferred)
				m_state->bus.PushDeferred(ev);

			if (!bossGroggyTriggered && hit.victimOwner == bossId && hit.attackerOwner == playerId)
			{
				if (HasDeferredEvent(resolved, Combat::CombatEventType::OnHit))
				{
					if (auto* hc = world.GetComponent<HealthComponent>(bossId))
					{
						if (hc->groggyMax > 0.0f && m_state->boss.state != Combat::ActionState::Groggy)
						{
							const float gainScale = (hc->groggyGainScale > 0.0f) ? hc->groggyGainScale : 0.0f;
							const float gain = hit.damage * gainScale;
							if (gain > 0.0f)
								hc->groggy = std::min(hc->groggy + gain, hc->groggyMax);

							if (hc->groggy >= hc->groggyMax)
							{
								hc->groggy = 0.0f;
								bossGroggyTriggered = true;

								std::vector<Combat::Command> groggyImmediate;
								groggyImmediate.push_back({ Combat::CommandType::ForceCancelAttack, Combat::CmdForceCancelAttack{ bossId } });
								groggyImmediate.push_back({ Combat::CommandType::DisableTrace, Combat::CmdDisableTrace{ bossId } });
								m_state->apply.ApplyImmediate(world, m_state->fighterMap, m_state->bus, groggyImmediate, true);

								m_state->bus.PushDeferred({ Combat::CombatEventType::OnGroggy, bossId, hit.attackerOwner, hit.attackInstanceId, 0.0f });
							}
						}
					}
				}
			}
		}
	}
}





















