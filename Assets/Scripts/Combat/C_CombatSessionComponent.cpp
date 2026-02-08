#include "C_CombatSessionComponent.h"

#include <unordered_map>
#include <algorithm>
#include <cmath>
#include <string>
#include <cctype>
#include <cstdlib>

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/ECS/World.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/ECS/Components/IDComponent.h"
#include "Runtime/Gameplay/Combat/HealthComponent.h"
#include "Runtime/Gameplay/Combat/AttackDriverComponent.h"
#include "Runtime/Gameplay/Combat/WeaponTraceComponent.h"
#include "Runtime/Gameplay/Animation/AdvancedAnimationComponent.h"
#include "Runtime/Physics/Components/Phy_CCTComponent.h"
#include "Runtime/ECS/Components/TransformComponent.h"
#include "Runtime/Rendering/Components/CameraComponent.h"
#include "Runtime/Rendering/Components/CameraFollowComponent.h"
#include "Runtime/Rendering/Components/CameraLookAtComponent.h"
#include "Runtime/Rendering/Components/SkinnedMeshComponent.h"
#include "Runtime/Rendering/SkinnedMeshRegistry.h"
#include "Runtime/Importing/FbxModel.h"
#include <assimp/scene.h>
//TODO : Include Ȯ���ؾ���

#include "C_CombatContracts.h"
#include "BossCombatTypes.h"
#include "C_CombatEventBus.h"
#include "C_ActionFsm.h"
#include "C_Fighter.h"
#include "C_CombatResolver.h"
#include "C_CombatApply.h"
#include "C_PlayerInputSourceComponent.h"
#include "C_BossBrainComponent.h"
#include "C_BossCombatSessionComponent.h"
#include "../Physics/Gimmick.h"
#include "../Physics/HealEyeGimmick.h"

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
			bool chargeEnterActive = false;
			float chargeEnterTimer = 0.0f;
			float chargeEnterDurationSec = 0.0f;
			bool chargeActivePrev = false;
			bool guardEnterActive = false;
			bool guardExitActive = false;
			float guardEnterTimer = 0.0f;
			float guardExitTimer = 0.0f;
			float guardEnterAnimDurationSec = 0.0f;
			float guardExitAnimDurationSec = 0.0f;
			bool groggyRecoverActive = false;
			float groggyRecoverTimer = 0.0f;
			float groggyRecoverDurationSec = 0.0f;
			std::string groggyRecoverClip{};
			bool rootMotionUnlockSaved = false;
			bool rootMotionUnlockDefault = false;
			bool rootMotionDriveCctDefault = false;
			bool dashActive = false;
			bool dashReverse = false;
			float dashTimer = 0.0f;
			float dashForwardSec = 0.0f;
			float dashReverseSec = 0.0f;
			std::string dashClipName{};
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
			std::string clipName{};
		};
		struct PendingDeferredEvent
		{
			Combat::CombatEvent ev{};
			float timerSec = 0.0f;
		};
		struct PendingImmediateCommand
		{
			Combat::Command cmd{};
			float timerSec = 0.0f;
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
		float bossGroggyEnterBlendBlockSec = 0.0f;
		Combat::Vec2 playerMoveSmoothedDir{};
		bool playerMoveSmoothedValid = false;
		bool playerLockOnActive = false;
		EntityId playerLockOnTarget = InvalidEntityId;
		bool playerAttackFacingLocked = false;
		float playerAttackFacingYawRad = 0.0f;
		bool playerLastAttackHeavy = false;
		bool bossLastAttackHeavy = false;
		int playerLastAttackChargeLevel = 0;
		int bossLastAttackChargeLevel = 0;
		bool playerChargeActive = false;
		bool bossChargeActive = false;
		int playerLightComboIndex = 0;
		int playerLightComboPendingIndex = 0;
		bool playerLightComboPending = false;
		bool playerLightComboQueued = false;
		float playerLightComboWindowSec = 0.0f;
		bool playerRageActive = false;
		float playerRageRemainingSec = 0.0f;
		bool playerAttackWindowSeen = false;
		float playerParryNoDurabilitySec = 0.0f;
		float bossParryNoDurabilitySec = 0.0f;
		float playerGuardExitLockSec = 0.0f;
		float bossGuardExitLockSec = 0.0f;
		float playerHowlingGuardLockSec = 0.0f;
		bool playerHowlingGuardActivePrev = false;
		float playerHitstunDurationSec = 0.0f;
		float bossHitstunDurationSec = 0.0f;
		float playerHitstopTimer = 0.0f;
		float bossHitstopTimer = 0.0f;
		float playerPushbackFreezeMax = 0.0f;
		float bossPushbackFreezeMax = 0.0f;
		float playerAttackSpeedScale = 1.0f;
		float bossAttackSpeedScale = 1.0f;
		bool playerGuardHeldAtHitstop = false;
		bool bossGuardHeldAtHitstop = false;
		float playerHealLoopSec = 0.0f;
		float playerHealNextTickSec = 0.0f;
		std::vector<PendingDeferredEvent> pendingDeferred;
		std::vector<PendingImmediateCommand> pendingImmediate;
		Combat::BossSignals bossSignals{};

		struct ParryLockKey
		{
			EntityId attacker = InvalidEntityId;
			uint32_t attackInstanceId = 0;
		};
		std::unordered_map<EntityId, ParryLockKey> parryResolvedByVictim;

		struct FatalState
		{
			bool active = false;
			float timerSec = 0.0f;
			float approachSec = 0.0f;
			float holdSec = 0.0f;
			float totalSec = 0.0f;
			DirectX::XMFLOAT3 bossStartPos{ 0.0f, 0.0f, 0.0f };
			DirectX::XMFLOAT3 bossTargetPos{ 0.0f, 0.0f, 0.0f };
			bool hasTarget = false;
			bool damageApplied = false;
			float damageAmount = 0.0f;
		};
		FatalState fatal{};

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
			bossGroggyEnterBlendBlockSec = 0.0f;
			playerMoveSmoothedDir = {};
			playerMoveSmoothedValid = false;
			playerLockOnActive = false;
			playerLockOnTarget = InvalidEntityId;
			playerAttackFacingLocked = false;
			playerAttackFacingYawRad = 0.0f;
			playerLastAttackHeavy = false;
			bossLastAttackHeavy = false;
			playerLastAttackChargeLevel = 0;
			bossLastAttackChargeLevel = 0;
			playerChargeActive = false;
			bossChargeActive = false;
			playerLightComboIndex = 0;
			playerLightComboPendingIndex = 0;
			playerLightComboPending = false;
			playerLightComboQueued = false;
			playerLightComboWindowSec = 0.0f;
			playerRageActive = false;
			playerRageRemainingSec = 0.0f;
			playerAttackWindowSeen = false;
			playerParryNoDurabilitySec = 0.0f;
			bossParryNoDurabilitySec = 0.0f;
			playerGuardExitLockSec = 0.0f;
			bossGuardExitLockSec = 0.0f;
			playerHowlingGuardLockSec = 0.0f;
			playerHowlingGuardActivePrev = false;
			playerHitstunDurationSec = 0.0f;
			bossHitstunDurationSec = 0.0f;
			playerHitstopTimer = 0.0f;
			bossHitstopTimer = 0.0f;
			playerPushbackFreezeMax = 0.0f;
			bossPushbackFreezeMax = 0.0f;
			playerAttackSpeedScale = 1.0f;
			bossAttackSpeedScale = 1.0f;
			playerHealLoopSec = 0.0f;
			playerHealNextTickSec = 0.0f;
			pendingDeferred.clear();
			pendingImmediate.clear();
			parryResolvedByVictim.clear();
			bossSignals = {};
			fatal = {};
		}
	};

    void C_CombatSessionComponent::SessionStateDeleter::operator()(SessionState* ptr) const
    {
        delete ptr;
    }

    C_CombatSessionComponent::~C_CombatSessionComponent() = default;

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

	static EntityId ResolveTraceEntity(World& world, EntityId ownerOrWeapon)
	{
		if (world.GetComponent<WeaponTraceComponent>(ownerOrWeapon))
			return ownerOrWeapon;

		auto* driver = world.GetComponent<AttackDriverComponent>(ownerOrWeapon);
		if (!driver || driver->traceGuid == 0)
			return ownerOrWeapon;

		EntityId resolved = world.FindEntityByGuid(driver->traceGuid);
		return (resolved != InvalidEntityId) ? resolved : ownerOrWeapon;
	}

	static float GetWeaponTraceBaseDamage(World& world, EntityId ownerOrWeapon)
	{
		const EntityId traceId = ResolveTraceEntity(world, ownerOrWeapon);
		if (auto* trace = world.GetComponent<WeaponTraceComponent>(traceId))
			return trace->baseDamage;
		return 0.0f;
	}

	namespace
	{
		static bool TryParseIndex(const std::string& key, int& outIdx)
		{
			if (key.empty())
				return false;
			for (char c : key)
			{
				if (!std::isdigit(static_cast<unsigned char>(c)))
					return false;
			}
			outIdx = std::atoi(key.c_str());
			return true;
		}

		static float GetClipDurationSecByName(const SkinnedMeshRegistry* registry,
			World& world,
			EntityId entityId,
			const std::string& clipName)
		{
			if (clipName.empty())
				return 0.0f;
			if (!registry)
				return 0.0f;
			auto* skinned = world.GetComponent<SkinnedMeshComponent>(entityId);
			if (!skinned || skinned->meshAssetPath.empty())
				return 0.0f;
			auto mesh = registry->Find(skinned->meshAssetPath);
			if (!mesh || !mesh->sourceModel)
				return 0.0f;
			const auto& names = mesh->sourceModel->GetAnimationNames();
			const auto* scene = mesh->sourceModel->GetScenePtr();
			const size_t clipCount = scene ? scene->mNumAnimations : names.size();
			for (size_t i = 0; i < names.size() && i < clipCount; ++i)
			{
				if (names[i] == clipName)
					return static_cast<float>(mesh->sourceModel->GetClipDurationSec(static_cast<int>(i)));
			}
			if (scene)
			{
				for (size_t i = 0; i < scene->mNumAnimations; ++i)
				{
					const auto* anim = scene->mAnimations[i];
					if (anim && anim->mName.length > 0 && clipName == anim->mName.C_Str())
						return static_cast<float>(mesh->sourceModel->GetClipDurationSec(static_cast<int>(i)));
				}
			}
			int idx = -1;
			if (TryParseIndex(clipName, idx))
			{
				if (idx >= 0 && static_cast<size_t>(idx) < clipCount)
					return static_cast<float>(mesh->sourceModel->GetClipDurationSec(idx));
			}
			return 0.0f;
		}
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

	static bool HasEvent(const std::vector<Combat::CombatEvent>& events, Combat::CombatEventType type)
	{
		for (const auto& ev : events)
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

C_CombatSessionComponent::AnimConfig C_CombatSessionComponent::BuildAnimConfig(EntityId entityId,
	EntityId playerId,
	EntityId bossId) const
{
	const bool isPlayer = (entityId == playerId);
	const bool isBoss = (entityId == bossId);
	AnimConfig cfg{};
	cfg.idleClip = m_idleClip;
	cfg.moveClip = m_moveClip;
	cfg.lightAttackClip = m_lightAttackClip;
	cfg.lightAttackClip1 = m_lightAttackClip1.empty() ? cfg.lightAttackClip : m_lightAttackClip1;
	cfg.lightAttackClip2 = m_lightAttackClip2.empty() ? cfg.lightAttackClip : m_lightAttackClip2;
	cfg.lightAttackClip3 = m_lightAttackClip3.empty() ? cfg.lightAttackClip : m_lightAttackClip3;
	cfg.heavyAttackClipA = m_heavyAttackClipA;
	cfg.heavyAttackClipB = m_heavyAttackClipB;
	cfg.dodgeClip = m_dodgeClip;
	cfg.chargeEnterClip = m_chargeEnterClip;
	cfg.chargeLoopClip = m_chargeLoopClip;
	cfg.hitClip = m_hitClip;
	cfg.guardBreakClip = m_guardBreakClip;
	cfg.fatalAttackClip = m_fatalAttackClip;
	cfg.interactionClip = m_interactionClip;
	cfg.healLoopClip = m_healLoopClip;
	cfg.groggyLoopClip = "";
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
		if (!m_playerLightAttackClip1.empty()) cfg.lightAttackClip1 = m_playerLightAttackClip1;
		else if (!m_playerLightAttackClip.empty()) cfg.lightAttackClip1 = cfg.lightAttackClip;
		if (!m_playerLightAttackClip2.empty()) cfg.lightAttackClip2 = m_playerLightAttackClip2;
		else if (!m_playerLightAttackClip.empty()) cfg.lightAttackClip2 = cfg.lightAttackClip;
		if (!m_playerLightAttackClip3.empty()) cfg.lightAttackClip3 = m_playerLightAttackClip3;
		else if (!m_playerLightAttackClip.empty()) cfg.lightAttackClip3 = cfg.lightAttackClip;
		if (!m_playerRageAttackClip.empty()) cfg.rageAttackClip = m_playerRageAttackClip;
		if (!m_playerHeavyAttackClipA.empty()) cfg.heavyAttackClipA = m_playerHeavyAttackClipA;
		if (!m_playerHeavyAttackClipB.empty()) cfg.heavyAttackClipB = m_playerHeavyAttackClipB;
		if (!m_playerDodgeClip.empty()) cfg.dodgeClip = m_playerDodgeClip;
		if (!m_playerChargeEnterClip.empty()) cfg.chargeEnterClip = m_playerChargeEnterClip;
		if (!m_playerChargeLoopClip.empty()) cfg.chargeLoopClip = m_playerChargeLoopClip;
		if (!m_playerHitClip.empty()) cfg.hitClip = m_playerHitClip;
		if (!m_playerGuardBreakClip.empty()) cfg.guardBreakClip = m_playerGuardBreakClip;
		if (!m_playerFatalAttackClip.empty()) cfg.fatalAttackClip = m_playerFatalAttackClip;
		if (!m_playerInteractionClip.empty()) cfg.interactionClip = m_playerInteractionClip;
		if (!m_playerHealLoopClip.empty()) cfg.healLoopClip = m_playerHealLoopClip;
		if (!m_playerGuardEnterClip.empty()) cfg.guardEnterClip = m_playerGuardEnterClip;
		if (!m_playerGuardLoopClip.empty()) cfg.guardLoopClip = m_playerGuardLoopClip;
		if (!m_playerGuardExitClip.empty()) cfg.guardExitClip = m_playerGuardExitClip;
		if (m_playerGuardEnterDurationSec > 0.0f) cfg.guardEnterDurationSec = m_playerGuardEnterDurationSec;
		if (m_playerGuardExitDurationSec > 0.0f) cfg.guardExitDurationSec = m_playerGuardExitDurationSec;

		// Fix mis-assigned charge clips for the player.
		const std::string kChargeEnter = "rig|Tia_Charging";
		const std::string kChargeLoop = "rig|Tia_Charged";
		if (cfg.chargeEnterClip == kChargeLoop || cfg.chargeEnterClip.empty())
			cfg.chargeEnterClip = kChargeEnter;
		if (cfg.chargeLoopClip == kChargeEnter || cfg.chargeLoopClip.empty())
			cfg.chargeLoopClip = kChargeLoop;
	}
	else if (isBoss)
	{
		if (!m_bossIdleClip.empty()) cfg.idleClip = m_bossIdleClip;
		if (!m_bossMoveClip.empty()) cfg.moveClip = m_bossMoveClip;
		if (!m_bossLightAttackClip.empty()) cfg.lightAttackClip = m_bossLightAttackClip;
		if (!m_bossLightAttackClip1.empty()) cfg.lightAttackClip1 = m_bossLightAttackClip1;
		else if (!m_bossLightAttackClip.empty()) cfg.lightAttackClip1 = cfg.lightAttackClip;
		if (!m_bossLightAttackClip2.empty()) cfg.lightAttackClip2 = m_bossLightAttackClip2;
		else if (!m_bossLightAttackClip.empty()) cfg.lightAttackClip2 = cfg.lightAttackClip;
		if (!m_bossLightAttackClip3.empty()) cfg.lightAttackClip3 = m_bossLightAttackClip3;
		else if (!m_bossLightAttackClip.empty()) cfg.lightAttackClip3 = cfg.lightAttackClip;
		if (!m_bossHeavyAttackClipA.empty()) cfg.heavyAttackClipA = m_bossHeavyAttackClipA;
		if (!m_bossHeavyAttackClipB.empty()) cfg.heavyAttackClipB = m_bossHeavyAttackClipB;
		if (!m_bossDodgeClip.empty()) cfg.dodgeClip = m_bossDodgeClip;
		if (!m_bossChargeEnterClip.empty()) cfg.chargeEnterClip = m_bossChargeEnterClip;
		if (!m_bossChargeLoopClip.empty()) cfg.chargeLoopClip = m_bossChargeLoopClip;
		if (!m_bossHitClip.empty()) cfg.hitClip = m_bossHitClip;
		if (!m_bossGuardBreakClip.empty()) cfg.guardBreakClip = m_bossGuardBreakClip;
		if (!m_bossGroggyLoopClip.empty()) cfg.groggyLoopClip = m_bossGroggyLoopClip;
		if (!m_bossGuardEnterClip.empty()) cfg.guardEnterClip = m_bossGuardEnterClip;
		if (!m_bossGuardLoopClip.empty()) cfg.guardLoopClip = m_bossGuardLoopClip;
		if (!m_bossGuardExitClip.empty()) cfg.guardExitClip = m_bossGuardExitClip;
		if (!m_bossInteractionClip.empty()) cfg.interactionClip = m_bossInteractionClip;
		if (!m_bossHealLoopClip.empty()) cfg.healLoopClip = m_bossHealLoopClip;
		if (m_bossGuardEnterDurationSec > 0.0f) cfg.guardEnterDurationSec = m_bossGuardEnterDurationSec;
		if (m_bossGuardExitDurationSec > 0.0f) cfg.guardExitDurationSec = m_bossGuardExitDurationSec;
	}
	return cfg;
}

	void C_CombatSessionComponent::Start()
	{
        if (!m_state)
            m_state.reset(new SessionState());
		m_state->Init();

		if (auto* world = GetWorld())
			world->SetScriptCombatEnabled(true);
	}

	void C_CombatSessionComponent::OnEnable()
	{
        if (!m_state)
            m_state.reset(new SessionState());

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

    Combat::ActionState C_CombatSessionComponent::GetPlayerState() const
    {
        return m_state ? m_state->player.state : Combat::ActionState::Idle;
    }

    Combat::ActionState C_CombatSessionComponent::GetBossState() const
    {
        return m_state ? m_state->boss.state : Combat::ActionState::Idle;
    }

    Combat::ActionFlags C_CombatSessionComponent::GetPlayerFlags() const
    {
        return m_state ? m_state->player.flags : Combat::ActionFlags{};
    }

    Combat::ActionFlags C_CombatSessionComponent::GetBossFlags() const
    {
        return m_state ? m_state->boss.flags : Combat::ActionFlags{};
    }

    bool C_CombatSessionComponent::IsPlayerRageActive() const
    {
        return m_state ? m_state->playerRageActive : false;
    }

    float C_CombatSessionComponent::GetPlayerRageRemainingSec() const
    {
        return m_state ? std::max(0.0f, m_state->playerRageRemainingSec) : 0.0f;
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

		m_state->prevPlayerState = m_state->player.state;
		m_state->prevBossState = m_state->boss.state;

		m_state->player.id = playerId;
		m_state->player.team = Combat::Team::Player;
		m_state->player.canBeHitstunned = m_playerCanBeHitstunned;
		m_state->boss.id = bossId;
		m_state->boss.team = Combat::Team::Enemy;
		m_state->boss.canBeHitstunned = false;
		Combat::BossSignals bossSignals = m_state->bossSignals;
		m_state->bossSignals = {};
		auto* playerHealth = world.GetComponent<HealthComponent>(playerId);
		auto* bossHealth = world.GetComponent<HealthComponent>(bossId);
		if (bossHealth && bossHealth->currentHealth <= 0.0f)
			bossSignals.dead = true;
		m_state->playerHitstopTimer = std::max(0.0f, m_state->playerHitstopTimer - deltaTime);
		m_state->bossHitstopTimer = std::max(0.0f, m_state->bossHitstopTimer - deltaTime);
		const bool playerHitstopActive = (m_state->playerHitstopTimer > 0.0f);
		const bool bossHitstopActive = (m_state->bossHitstopTimer > 0.0f);
		if (!playerHitstopActive)
			m_state->playerGuardHeldAtHitstop = false;
		if (!bossHitstopActive)
			m_state->bossGuardHeldAtHitstop = false;
		const float playerLogicDt = playerHitstopActive ? 0.0f : deltaTime;
		const float bossLogicDt = bossHitstopActive ? 0.0f : deltaTime;
		m_state->bossGroggyEnterBlendBlockSec =
			std::max(0.0f, m_state->bossGroggyEnterBlendBlockSec - bossLogicDt);
		m_state->boss.moveSpeed = std::max(0.0f, m_state->player.moveSpeed * 0.5f);

		auto FreezePushbackDuringHitstop = [&](EntityId entityId, bool hitstopActive, float& freezeMax)
			{
				if (auto* hc = world.GetComponent<HealthComponent>(entityId))
				{
					if (hitstopActive && hc->pushbackRemainingSec > 0.0f)
					{
						freezeMax = std::max(freezeMax, hc->pushbackRemainingSec);
						hc->pushbackRemainingSec = freezeMax;
					}
					else
					{
						freezeMax = hc->pushbackRemainingSec;
					}
				}
			};

		FreezePushbackDuringHitstop(playerId, playerHitstopActive, m_state->playerPushbackFreezeMax);
		FreezePushbackDuringHitstop(bossId, bossHitstopActive, m_state->bossPushbackFreezeMax);

		m_state->fighterMap.clear();
		m_state->fighterMap[playerId] = &m_state->player;
		m_state->fighterMap[bossId] = &m_state->boss;

		if (!m_state->pendingImmediate.empty())
		{
			std::vector<Combat::Command> due;
			for (size_t i = 0; i < m_state->pendingImmediate.size();)
			{
				auto& pending = m_state->pendingImmediate[i];
				pending.timerSec -= deltaTime;
				if (pending.timerSec <= 0.0f)
				{
					due.push_back(pending.cmd);
					m_state->pendingImmediate[i] = m_state->pendingImmediate.back();
					m_state->pendingImmediate.pop_back();
					continue;
				}
				++i;
			}
			if (!due.empty())
				m_state->apply.ApplyImmediate(world, m_state->fighterMap, m_state->bus, due, false);
		}

		Combat::Intent playerIntent{};
		if (auto* script = FindScriptOnEntity(world, playerId, "C_PlayerInputSourceComponent"))
		{
			if (auto* input = dynamic_cast<C_PlayerInputSourceComponent*>(script))
				playerIntent = input->GetIntent(playerHitstopActive ? 0.0f : deltaTime);
		}
		if (playerHitstopActive)
		{
			Combat::Intent filtered{};
			filtered.guardHeld = playerIntent.guardHeld;
			filtered.guardPressed = playerIntent.guardPressed;
			filtered.guardReleased = playerIntent.guardReleased;
			filtered.guardHeldSec = playerIntent.guardHeldSec;
			playerIntent = filtered;
			if (m_state->playerGuardHeldAtHitstop)
			{
				// During hitstop, treat guard as "still held" if it was active on entry.
				// This avoids interpreting a release edge mid-hitstop as a guard cancel.
				playerIntent.guardHeld = true;
				playerIntent.guardPressed = false;
				playerIntent.guardReleased = false;
			}
		}

		Combat::BossIntent bossIntent{};
		Combat::Intent bossIntentCompat{};
		C_BossBrainComponent* bossBrain = nullptr;
		C_BossCombatSessionComponent* bossSession = nullptr;
		HealEyeGimmick* healGimmick = nullptr;
		if (auto* script = FindScriptOnEntity(world, bossId, "C_BossBrainComponent"))
		{
			if (auto* brain = dynamic_cast<C_BossBrainComponent*>(script))
				bossBrain = brain;
		}
		if (auto* script = FindScriptOnEntity(world, GetOwnerId(), "C_BossCombatSessionComponent"))
		{
			if (auto* session = dynamic_cast<C_BossCombatSessionComponent*>(script))
				bossSession = session;
		}
		if (!m_healGimmickEntityName.empty())
		{
			const EntityId healGimmickId = ResolveEntityByName(m_healGimmickEntityName);
			if (healGimmickId != InvalidEntityId)
			{
				if (auto* script = FindScriptOnEntity(world, healGimmickId, "HealEyeGimmick"))
				{
					if (auto* gimmick = dynamic_cast<HealEyeGimmick*>(script))
						healGimmick = gimmick;
				}
			}
		}

		const bool playerGuardReleased = playerIntent.guardReleased;

		bool blockPlayerActions = false;
		if (m_blockPlayerActionsDuringGimmick && !m_gimmickEntityName.empty())
		{
			const EntityId gimmickId = ResolveEntityByName(m_gimmickEntityName);
			if (gimmickId != InvalidEntityId)
			{
				if (auto* script = FindScriptOnEntity(world, gimmickId, "Gimmick"))
				{
					if (auto* gimmick = dynamic_cast<Gimmick*>(script))
						blockPlayerActions = gimmick->IsLoopActive();
				}
			}
		}

		const float minWeaponRatio = std::max(0.0f, m_healWeaponMinRatio);
		const float maxHpRatio = std::max(0.0f, m_healPlayerMaxRatio);
		float playerHpRatio = 1.0f;
		float playerWeaponRatio = 0.0f;
		if (playerHealth)
		{
			const float hpMax = std::max(0.0001f, playerHealth->maxHealth);
			playerHpRatio = playerHealth->currentHealth / hpMax;
			const float weaponMax = playerHealth->weaponDurabilityMax;
			if (weaponMax > 0.0001f)
				playerWeaponRatio = playerHealth->weaponDurability / weaponMax;
			else
				playerWeaponRatio = 0.0f;
		}
		const bool playerCanHeal = playerHealth
			&& (playerWeaponRatio > minWeaponRatio)
			&& (playerHpRatio <= maxHpRatio)
			&& !blockPlayerActions;
		const bool playerCanInteract = m_playerInteractionEnabled && !blockPlayerActions;

		auto ResolveGuardExitDuration = [&](EntityId entityId) -> float
			{
				float duration = m_guardExitDurationSec;
				if (entityId == playerId && m_playerGuardExitDurationSec > 0.0f)
					duration = m_playerGuardExitDurationSec;
				else if (entityId == bossId && m_bossGuardExitDurationSec > 0.0f)
					duration = m_bossGuardExitDurationSec;
				return std::max(0.0f, duration);
			};
		auto ResolveGuardEnterDuration = [&](EntityId entityId) -> float
			{
				float duration = m_guardEnterDurationSec;
				if (entityId == playerId && m_playerGuardEnterDurationSec > 0.0f)
					duration = m_playerGuardEnterDurationSec;
				else if (entityId == bossId && m_bossGuardEnterDurationSec > 0.0f)
					duration = m_bossGuardEnterDurationSec;
				return std::max(0.0f, duration);
			};

		auto BeginGuardExitLock = [&](EntityId entityId, float& lockSec)
			{
				const float duration = ResolveGuardExitDuration(entityId);
				if (duration <= 0.0f)
					return;
				lockSec = std::max(lockSec, duration);
				if (auto* driver = world.GetComponent<AttackDriverComponent>(entityId))
				{
					driver->guardLockRemainingSec = 0.0f;
					driver->parryUsedThisPress = false;
				}
			};

		if (playerGuardReleased
			&& (m_state->player.state == Combat::ActionState::Guard
				))
		{
			BeginGuardExitLock(playerId, m_state->playerGuardExitLockSec);
		}

		if (blockPlayerActions)
		{
			Combat::Intent filtered{};
			filtered.move = playerIntent.move;
			filtered.dodgePressed = playerIntent.dodgePressed;
			filtered.runHeld = playerIntent.runHeld;
			playerIntent = filtered;

			if (auto* driver = world.GetComponent<AttackDriverComponent>(playerId))
			{
				if (driver->attackCancelable)
					driver->cancelAttackRequested = true;
				driver->guardLockRemainingSec = 0.0f;
				driver->parryOverrideRemainingSec = 0.0f;
				driver->parryUsedThisPress = false;
			}
		}

		auto ApplyGuardExitLockIntent = [&](Combat::Intent& intent, float& lockSec, EntityId entityId)
			{
				if (lockSec <= 0.0f)
					return;
				intent = {};
				if (auto* driver = world.GetComponent<AttackDriverComponent>(entityId))
				{
					if (driver->attackCancelable)
						driver->cancelAttackRequested = true;
					driver->guardLockRemainingSec = 0.0f;
					driver->parryUsedThisPress = false;
				}
			};

		// Guard exit recovery can always be canceled by dodge.
		if (playerIntent.dodgePressed
			&& (m_state->playerGuardExitLockSec > 0.0f || m_state->playerAnim.guardExitActive))
		{
			m_state->playerGuardExitLockSec = 0.0f;
			m_state->playerAnim.guardExitActive = false;
			m_state->playerAnim.guardExitTimer = 0.0f;
			m_state->playerAnim.guardExitAnimDurationSec = 0.0f;
			if (auto* driver = world.GetComponent<AttackDriverComponent>(playerId))
			{
				driver->guardLockRemainingSec = 0.0f;
				driver->parryOverrideRemainingSec = 0.0f;
				driver->parryUsedThisPress = false;
			}
		}

		ApplyGuardExitLockIntent(playerIntent, m_state->playerGuardExitLockSec, playerId);

		auto ApplyForcedInputLock = [&](Combat::Intent& intent,
			EntityId entityId,
			bool forceGuard,
			bool lockInput,
			float guardLockSec)
			{
				if (!forceGuard && !lockInput)
					return;

				if (lockInput)
					intent = {};

				if (forceGuard)
				{
					intent.guardHeld = true;
					intent.guardPressed = false;
					intent.guardReleased = false;
					intent.guardHeldSec = std::max(0.0f, intent.guardHeldSec);
				}

				if (auto* driver = world.GetComponent<AttackDriverComponent>(entityId))
				{
					if (driver->attackCancelable)
						driver->cancelAttackRequested = true;
					if (forceGuard && guardLockSec > 0.0f)
						driver->guardLockRemainingSec = std::max(driver->guardLockRemainingSec, guardLockSec);
					driver->parryOverrideRemainingSec = 0.0f;
					driver->parryUsedThisPress = false;
				}
			};

		if (m_state->playerHowlingGuardLockSec > 0.0f)
		{
			ApplyForcedInputLock(playerIntent,
				playerId,
				m_phaseHowlingForceGuard,
				m_phaseHowlingLockInput,
				m_state->playerHowlingGuardLockSec);
		}

		// Even after guard-exit lock timer ends, keep motion/actions blocked while exit animation is active.
		if (m_state->playerAnim.guardExitActive)
		{
			ApplyForcedInputLock(playerIntent, playerId, false, true, 0.0f);
		}

		if (!playerCanInteract)
			playerIntent.interactPressed = false;
		if (!playerCanHeal)
		{
			playerIntent.itemPressed = false;
			playerIntent.itemHeld = false;
			playerIntent.itemReleased = false;
			playerIntent.itemHeldSec = 0.0f;
		}

		const bool playerInInteraction = (m_state->player.state == Combat::ActionState::Interaction);
		const bool playerInHealEnter = (m_state->player.state == Combat::ActionState::HealEnter);
		const bool playerInHealLoop = (m_state->player.state == Combat::ActionState::HealLoop);
		const bool playerInHealExit = (m_state->player.state == Combat::ActionState::HealExit);
		if (playerInInteraction)
		{
			playerIntent = {};
		}
		else if (playerInHealEnter || playerInHealExit)
		{
			Combat::Intent filtered{};
			filtered.dodgePressed = playerIntent.dodgePressed;
			filtered.itemPressed = playerIntent.itemPressed;
			filtered.itemHeld = playerIntent.itemHeld;
			filtered.itemReleased = playerIntent.itemReleased;
			filtered.itemHeldSec = playerIntent.itemHeldSec;
			playerIntent = filtered;
		}
		else if (playerInHealLoop)
		{
			Combat::Intent filtered{};
			filtered.itemPressed = playerIntent.itemPressed;
			filtered.itemHeld = playerIntent.itemHeld;
			filtered.itemReleased = playerIntent.itemReleased;
			filtered.itemHeldSec = playerIntent.itemHeldSec;
			playerIntent = filtered;
		}

		if (playerIntent.itemPressed || playerIntent.interactPressed)
		{
			playerIntent.guardHeld = false;
			playerIntent.guardPressed = false;
			playerIntent.guardReleased = false;
			playerIntent.lightAttackPressed = false;
			playerIntent.heavyAttackPressed = false;
			playerIntent.attackPressed = false;
			playerIntent.attackHeld = false;
			playerIntent.attackHeldSec = 0.0f;
		}

		auto CanChargeInState = [](Combat::ActionState state)
			{
				return state == Combat::ActionState::Idle
					|| state == Combat::ActionState::Move
					|| state == Combat::ActionState::Guard;
			};

		auto CancelPlayerCharge = [&]()
			{
				if (auto* script = FindScriptOnEntity(world, playerId, "C_PlayerInputSourceComponent"))
				{
					if (auto* input = dynamic_cast<C_PlayerInputSourceComponent*>(script))
						input->CancelCharge();
				}
				playerIntent.chargeActive = false;
				playerIntent.chargeHeldSec = 0.0f;
				playerIntent.chargeLevel = 0;
				playerIntent.heavyAttackPressed = false;
				playerIntent.attackPressed = playerIntent.lightAttackPressed;
			};

		if (playerIntent.itemPressed || playerIntent.interactPressed)
		{
			CancelPlayerCharge();
			playerIntent.chargeActive = false;
			playerIntent.chargeHeldSec = 0.0f;
			playerIntent.chargeLevel = 0;
		}

		const bool guardPriority = playerIntent.guardHeld || playerIntent.guardPressed;
		if (guardPriority && (playerIntent.chargeActive || playerIntent.heavyAttackPressed))
		{
			CancelPlayerCharge();
		}

		if (playerIntent.chargeActive || playerIntent.heavyAttackPressed)
		{
			if (!CanChargeInState(m_state->player.state))
			{
				CancelPlayerCharge();
			}
		}

		if (playerIntent.chargeActive)
		{
			playerIntent.move = { 0.0f, 0.0f };
			playerIntent.runHeld = false;
		}

		// Re-apply post-filters so heal/interaction gating cannot override forced howl guard lock.
		if (m_state->playerHowlingGuardLockSec > 0.0f)
		{
			ApplyForcedInputLock(playerIntent,
				playerId,
				m_phaseHowlingForceGuard,
				m_phaseHowlingLockInput,
				m_state->playerHowlingGuardLockSec);
		}
		if (m_state->playerAnim.guardExitActive)
		{
			ApplyForcedInputLock(playerIntent, playerId, false, true, 0.0f);
		}

		const float rageDurationSec = std::max(0.0f, m_rageDurationSec);
		if (playerIntent.ragePressed && rageDurationSec > 0.0f)
		{
			m_state->playerRageActive = true;
			m_state->playerRageRemainingSec = rageDurationSec;
		}
		if (m_state->playerRageActive)
		{
			if (rageDurationSec <= 0.0f)
			{
				m_state->playerRageActive = false;
				m_state->playerRageRemainingSec = 0.0f;
			}
			else
			{
				m_state->playerRageRemainingSec = std::max(0.0f, m_state->playerRageRemainingSec - playerLogicDt);
				if (m_state->playerRageRemainingSec <= 0.0f)
					m_state->playerRageActive = false;
			}
		}

		// Combo input is handled after sensors are available.

		constexpr float kDegToRad = 0.01745329252f;
		constexpr float kRadToDeg = 57.2957795f;

		bool fatalTriggered = false;
		auto* registry = SkinnedRegistry();
		bool forceFatalAttack = false;
		if (bossBrain && registry)
		{
			const std::string& patternClip = bossBrain->GetPatternClip(bossBrain->GetActivePattern());
			const bool brainAttacking = (bossBrain->GetBrainState() == C_BossBrainComponent::BrainState::Attack);
			if (auto* driver = world.GetComponent<AttackDriverComponent>(bossId))
			{
				if (!patternClip.empty())
				{
					float duration = GetClipDurationSecByName(registry, world, bossId, patternClip);
					if (duration > 0.0f)
					{
						if (patternClip.find("Dash_Attack") != std::string::npos)
							duration *= 2.0f;
						driver->attackStateDurationSec = duration;
					}
				}
				else if (!brainAttacking)
				{
					driver->attackStateDurationSec = 0.0f;
				}
			}
		}
		if (!m_state->fatal.active
			&& playerIntent.lightAttackPressed
			&& m_state->boss.state == Combat::ActionState::Groggy
			&& m_state->bossGroggyEnterBlendBlockSec <= 0.0f)
		{
			auto* playerTr = world.GetComponent<TransformComponent>(playerId);
			auto* bossTr = world.GetComponent<TransformComponent>(bossId);
			if (playerTr && bossTr)
			{
				auto InFrontCone = [&](const TransformComponent& self, const DirectX::XMFLOAT3& targetPos) -> bool
					{
						const float dx = targetPos.x - self.position.x;
						const float dz = targetPos.z - self.position.z;
						const float dist = std::sqrt(dx * dx + dz * dz);
						if (dist <= 0.0001f)
							return false;

						const float offsetRad = m_rotationOffsetDeg * kDegToRad;
						const float yawRad = self.rotation.y - offsetRad;
						const float fx = std::sin(yawRad);
						const float fz = std::cos(yawRad);
						const float tx = dx / dist;
						const float tz = dz / dist;
						const float dot = fx * tx + fz * tz;
						const float halfAngleRad = std::clamp(m_fatalFrontAngleDeg * 0.5f, 0.0f, 180.0f) * kDegToRad;
						const float threshold = std::cos(halfAngleRad);
						return dot >= threshold;
					};

				if (InFrontCone(*bossTr, playerTr->position) && InFrontCone(*playerTr, bossTr->position))
				{
					fatalTriggered = true;
					forceFatalAttack = true;
					m_state->fatal.active = true;
					m_state->fatal.timerSec = 0.0f;
					m_state->fatal.hasTarget = true;
					m_state->fatal.damageApplied = false;
					m_state->fatal.damageAmount = 0.0f;
					m_state->fatal.bossStartPos = bossTr->position;

					DirectX::XMFLOAT3 dir{ bossTr->position.x - playerTr->position.x, 0.0f, bossTr->position.z - playerTr->position.z };
					float len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
					if (len <= 0.0001f)
					{
						const float offsetRad = m_rotationOffsetDeg * kDegToRad;
						const float yawRad = playerTr->rotation.y - offsetRad;
						dir.x = std::sin(yawRad);
						dir.z = std::cos(yawRad);
						len = std::sqrt(dir.x * dir.x + dir.z * dir.z);
					}
					if (len > 0.0001f)
					{
						dir.x /= len;
						dir.z /= len;
					}
					else
					{
						dir = { 0.0f, 0.0f, 1.0f };
					}

					const float dist = std::max(0.0f, m_fatalDistance);
					m_state->fatal.bossTargetPos = {
						playerTr->position.x + dir.x * dist,
						m_state->fatal.bossStartPos.y,
						playerTr->position.z + dir.z * dist
					};

					const float heavyDamage = GetWeaponTraceBaseDamage(world, playerId);
					const float scale = std::max(0.0f, m_fatalDamageScale);
					if (heavyDamage > 0.0f && scale > 0.0f)
						m_state->fatal.damageAmount = heavyDamage * scale;

					const float approachSec = std::max(0.0f, m_fatalApproachSec);
					float holdSec = std::max(0.0f, m_fatalHoldSec);
					if (holdSec <= 0.0f)
					{
						const std::string& fatalClip = !m_playerFatalAttackClip.empty()
							? m_playerFatalAttackClip
							: m_fatalAttackClip;
						if (!fatalClip.empty())
						{
							const float clipDur = GetClipDurationSecByName(registry, world, playerId, fatalClip);
							if (clipDur > 0.0f)
								holdSec = std::max(0.0f, clipDur - approachSec);
						}
					}
					m_state->fatal.approachSec = approachSec;
					m_state->fatal.holdSec = holdSec;
					m_state->fatal.totalSec = approachSec + holdSec;
					const float groggyAttackDelaySec = std::max(0.0f, m_groggyAttackStartDelaySec);
					const float groggyExtendSec = std::max(0.0f, m_state->fatal.totalSec + groggyAttackDelaySec);
					if (groggyExtendSec > 0.0f)
						bossSignals.groggyExtendSec = std::max(bossSignals.groggyExtendSec, groggyExtendSec);

					// TODO: replace with proper fatal attack animation pairing.
				}
			}
		}

		if (m_state->fatal.active || fatalTriggered)
		{
			playerIntent = {};
			bossIntent = {};
		}
		bossSignals.groggyHold = (m_state->fatal.active || fatalTriggered);

		if (!playerHitstopActive)
			m_state->playerChargeActive = playerIntent.chargeActive;

		const bool playerGuardPressed = playerIntent.guardPressed;

		auto UpdateDriverInput = [&](EntityId entityId, const Combat::Intent& intent, float inputDt)
			{
				if (auto* driver = world.GetComponent<AttackDriverComponent>(entityId))
				{
					driver->guardInputHeld = intent.guardHeld;
					driver->guardInputPressed = intent.guardPressed;
					driver->guardInputReleased = intent.guardReleased;
					driver->guardLockRemainingSec = std::max(0.0f, driver->guardLockRemainingSec - inputDt);
					driver->parryOverrideRemainingSec = std::max(0.0f, driver->parryOverrideRemainingSec - inputDt);
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
						const float parryWindowSec = ResolveGuardEnterDuration(entityId);
						if (parryWindowSec > 0.0f)
							driver->parryOverrideRemainingSec = std::max(driver->parryOverrideRemainingSec, parryWindowSec);
					}
				}
			};

		UpdateDriverInput(playerId, playerIntent, playerHitstopActive ? 0.0f : deltaTime);
		UpdateDriverInput(bossId, bossIntentCompat, bossHitstopActive ? 0.0f : deltaTime);

		auto ApplyHitstopGuardHold = [&](EntityId entityId, bool hitstopActive, bool holdGuard)
			{
				if (!hitstopActive || !holdGuard)
					return;
				if (auto* driver = world.GetComponent<AttackDriverComponent>(entityId))
				{
					driver->guardInputHeld = true;
					driver->guardInputPressed = false;
					driver->guardInputReleased = false;
				}
			};
		ApplyHitstopGuardHold(playerId, playerHitstopActive, m_state->playerGuardHeldAtHitstop);
		ApplyHitstopGuardHold(bossId, bossHitstopActive, m_state->bossGuardHeldAtHitstop);

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
			{
				camLookAt->targetName = m_bossName.empty() ? std::string("Enemy") : m_bossName;
				camLookAt->targetYOffset = m_lockOnTargetYOffset;
			}
		}

		const bool playerSuperArmorEarly = (m_state->player.state == Combat::ActionState::Attack
			&& m_state->playerLastAttackHeavy)
			|| m_state->playerChargeActive;
		m_state->player.canBeHitstunned = m_playerCanBeHitstunned && !playerSuperArmorEarly;

		float healEnterDurationSec = 0.0f;
		float healExitDurationSec = 0.0f;
		Combat::Sensors sPlayer = m_state->player.BuildSensors(world, bossId, deltaTime);
		Combat::Sensors sBoss = m_state->boss.BuildSensors(world, playerId, deltaTime);
		sPlayer.hitstunDurationSec = m_state->playerHitstunDurationSec;
		sBoss.hitstunDurationSec = m_state->bossHitstunDurationSec;
		sPlayer.interactAvailable = playerCanInteract;
		sPlayer.healAllowed = playerCanHeal;
		sBoss.interactAvailable = false;
		sBoss.healAllowed = false;
		sPlayer.guardEnterDurationSec = ResolveGuardEnterDuration(playerId);
		sBoss.guardEnterDurationSec = ResolveGuardEnterDuration(bossId);

		{
			const AnimConfig playerCfg = BuildAnimConfig(playerId, playerId, bossId);
			float interactionDuration = GetClipDurationSecByName(registry, world, playerId, playerCfg.interactionClip);
			if (interactionDuration <= 0.0f)
				interactionDuration = 0.5f;
			sPlayer.interactionDurationSec = interactionDuration;
			sPlayer.healEnterDurationSec = interactionDuration;
			sPlayer.healExitDurationSec = interactionDuration;
			healEnterDurationSec = sPlayer.healEnterDurationSec;
			healExitDurationSec = sPlayer.healExitDurationSec;
		}
		if (m_state->player.state != Combat::ActionState::Attack)
			sPlayer.attackWindowActive = false;
		if (m_state->boss.state != Combat::ActionState::Attack)
			sBoss.attackWindowActive = false;
		bool fatalActive = (m_state->fatal.active || fatalTriggered);
		if (fatalActive && m_state->fatal.totalSec > 0.0f)
		{
			sPlayer.attackStateDurationSec = m_state->fatal.totalSec;
			sPlayer.attackCancelable = false;
		}
		if (fatalActive)
		{
			sPlayer.stamina = std::max(sPlayer.stamina, 15.0f);
		}
		if (!fatalActive)
		{
			const float playerAttackScale = std::clamp(m_state->playerAttackSpeedScale, 0.0f, 1.0f);
			if (m_state->player.state == Combat::ActionState::Attack
				&& playerAttackScale > 0.0f
				&& playerAttackScale < 1.0f
				&& sPlayer.attackStateDurationSec > 0.0f)
			{
				sPlayer.attackStateDurationSec /= playerAttackScale;
			}
			const float bossAttackScale = std::clamp(m_state->bossAttackSpeedScale, 0.0f, 1.0f);
			if (m_state->boss.state == Combat::ActionState::Attack
				&& bossAttackScale > 0.0f
				&& bossAttackScale < 1.0f
				&& sBoss.attackStateDurationSec > 0.0f)
			{
				sBoss.attackStateDurationSec /= bossAttackScale;
			}
		}

		auto ResolveClipSpeed = [&](const AdvancedAnimationComponent& anim, const std::string& clip) -> float
			{
				if (clip.empty())
					return 1.0f;
				if (anim.base.clipA == clip)
					return anim.base.speedA;
				if (anim.base.clipB == clip)
					return anim.base.speedB;
				if (anim.upper.clipA == clip)
					return anim.upper.speedA;
				if (anim.upper.clipB == clip)
					return anim.upper.speedB;
				if (anim.additive.clip == clip)
					return anim.additive.speed;
				return 1.0f;
			};

		auto ApplyAttackDurationOverride = [&](EntityId entityId,
			Combat::ActionState state,
			const SessionState::AnimOverrideState& animState,
			Combat::Sensors& sensors,
			bool forceOverride)
			{
				if (state != Combat::ActionState::Attack)
					return;
				if (animState.attackClip.empty())
					return;

				float duration = GetClipDurationSecByName(registry, world, entityId, animState.attackClip);
				if (duration <= 0.0f)
					return;

				if (entityId == bossId && animState.attackClip.find("Dash_Attack") != std::string::npos)
					duration *= 2.0f;

				float speed = 1.0f;
				const auto* anim = world.GetComponent<AdvancedAnimationComponent>(entityId);
				if (anim)
					speed = ResolveClipSpeed(*anim, animState.attackClip);

				const float speedAbs = std::abs(speed);
				const bool speedAdjusted = (speedAbs > 0.0001f && std::abs(speedAbs - 1.0f) > 0.0001f);
				if (speedAdjusted)
					duration /= speedAbs;

				if (forceOverride || sensors.attackStateDurationSec <= 0.0f || speedAdjusted)
					sensors.attackStateDurationSec = duration;
			};

		const bool playerForceAttackDuration = !fatalActive;
		ApplyAttackDurationOverride(playerId, m_state->player.state, m_state->playerAnim, sPlayer, playerForceAttackDuration);
		ApplyAttackDurationOverride(bossId, m_state->boss.state, m_state->bossAnim, sBoss, false);

		auto RecomputeTargetInFront = [&](EntityId selfId,
			EntityId targetId,
			Combat::Sensors& s,
			Combat::Fighter& fighter)
		{
			auto* selfTr = world.GetComponent<TransformComponent>(selfId);
			auto* targetTr = world.GetComponent<TransformComponent>(targetId);
			if (!selfTr || !targetTr)
				return;

			const float dx = targetTr->position.x - selfTr->position.x;
			const float dz = targetTr->position.z - selfTr->position.z;
			const float dist = std::sqrt(dx * dx + dz * dz);
			if (dist <= 0.0001f)
				return;

			const float offsetRad = m_rotationOffsetDeg * kDegToRad;
			const float yawRad = selfTr->rotation.y - offsetRad;
			const float fx = std::sin(yawRad);
			const float fz = std::cos(yawRad);
			const float tx = dx / dist;
			const float tz = dz / dist;
			const float dot = fx * tx + fz * tz;
			s.targetInFront = (dot >= 0.0f);
			fighter.lastTargetInFront = s.targetInFront;
		};
		RecomputeTargetInFront(playerId, bossId, sPlayer, m_state->player);
		RecomputeTargetInFront(bossId, playerId, sBoss, m_state->boss);

		auto SetDodgeFallback = [&](Combat::Sensors& s,
			const Combat::Intent& intent,
			EntityId entityId,
			bool lockOnActive)
			{
				s.dodgeFallbackValid = false;
				const float inputMag = std::abs(intent.move.x) + std::abs(intent.move.y);
				if (inputMag > 0.001f)
					return;

				if (!lockOnActive)
				{
					s.dodgeFallbackDir = { 0.0f, 1.0f };
					s.dodgeFallbackValid = true;
					return;
				}

				auto* tr = world.GetComponent<TransformComponent>(entityId);
				if (!tr)
					return;

				const float offsetRad = m_rotationOffsetDeg * kDegToRad;
				const float yawRad = tr->rotation.y - offsetRad;
				const float fx = std::sin(yawRad);
				const float fz = std::cos(yawRad);

				float inputX = fx;
				float inputZ = fz;
				if (camBasis.valid)
				{
					inputX = fx * camBasis.rightX + fz * camBasis.rightZ;
					inputZ = fx * camBasis.forwardX + fz * camBasis.forwardZ;
				}

				const float len = std::sqrt(inputX * inputX + inputZ * inputZ);
				if (len <= 0.0001f)
					return;

				s.dodgeFallbackDir = { inputX / len, inputZ / len };
				s.dodgeFallbackValid = true;
			};
		SetDodgeFallback(sPlayer, playerIntent, playerId, m_state->playerLockOnActive);

		if (fatalTriggered)
			sBoss.groggyDuration = 0.0f;

		m_state->player.hp = sPlayer.hp;
		m_state->boss.hp = sBoss.hp;
		m_state->player.weaponDurability = sPlayer.weaponDurability;
		m_state->player.weaponDurabilityMax = sPlayer.weaponDurabilityMax;
		m_state->player.weakRemainingSec = sPlayer.weakRemainingSec;
		m_state->boss.weaponDurability = sBoss.weaponDurability;
		m_state->boss.weaponDurabilityMax = sBoss.weaponDurabilityMax;
		m_state->boss.weakRemainingSec = sBoss.weakRemainingSec;

		m_state->playerLightComboWindowSec = std::max(0.0f, m_state->playerLightComboWindowSec - playerLogicDt);
		const bool playerWasInAttack = (m_state->player.state == Combat::ActionState::Attack);
		constexpr int kMaxLightCombo = 3;

		if (playerIntent.heavyAttackPressed)
		{
			m_state->playerLightComboPending = false;
			m_state->playerLightComboPendingIndex = 0;
			m_state->playerLightComboQueued = false;
			m_state->playerLightComboWindowSec = 0.0f;
			m_state->playerLightComboIndex = 0;
		}

		if (playerIntent.lightAttackPressed)
		{
			if (playerWasInAttack)
			{
				// No attack-cancel during current attack.
				playerIntent.lightAttackPressed = false;
			}
			else
			{
				const int nextIndex = (m_state->playerLightComboWindowSec > 0.0f)
					? std::min(kMaxLightCombo, std::max(1, m_state->playerLightComboIndex + 1))
					: 1;
				m_state->playerLightComboPending = true;
				m_state->playerLightComboPendingIndex = nextIndex;
			}
		}

		if (!playerWasInAttack && m_state->playerLightComboPending)
			playerIntent.lightAttackPressed = true;

		if (forceFatalAttack)
		{
			playerIntent.lightAttackPressed = true;
			playerIntent.heavyAttackPressed = false;
			playerIntent.attackHeld = false;
		}
		playerIntent.attackPressed = playerIntent.lightAttackPressed || playerIntent.heavyAttackPressed;

		if (!m_state->pendingDeferred.empty())
		{
			for (size_t i = 0; i < m_state->pendingDeferred.size();)
			{
				auto& pending = m_state->pendingDeferred[i];
				pending.timerSec -= deltaTime;
				if (pending.timerSec <= 0.0f)
				{
					m_state->bus.PushDeferred(pending.ev);
					m_state->pendingDeferred[i] = m_state->pendingDeferred.back();
					m_state->pendingDeferred.pop_back();
					continue;
				}
				++i;
			}
		}

		auto TryForceBossGroggy = [&]() -> bool
			{
				auto* bossHealth = world.GetComponent<HealthComponent>(bossId);
				if (!bossHealth)
					return false;
				if (bossHealth->groggyMax <= 0.0f)
					return false;
				if (m_state->boss.state == Combat::ActionState::Groggy)
					return false;
				if (bossHealth->groggy < bossHealth->groggyMax)
					return false;

				bossHealth->groggy = bossHealth->groggyMax;

				std::vector<Combat::Command> groggyImmediate;
				groggyImmediate.push_back({ Combat::CommandType::ForceCancelAttack, Combat::CmdForceCancelAttack{ bossId } });
				groggyImmediate.push_back({ Combat::CommandType::DisableTrace, Combat::CmdDisableTrace{ bossId } });
				if (auto* driver = world.GetComponent<AttackDriverComponent>(bossId))
				{
					driver->forceCancelRequested = true;
					driver->cancelAttackRequested = true;
				}
				m_state->apply.ApplyImmediate(world, m_state->fighterMap, m_state->bus, groggyImmediate, true);

				m_state->bus.PushDeferred({ Combat::CombatEventType::OnGroggy, bossId, playerId, 0, 0.0f });
				return true;
			};
		if (TryForceBossGroggy())
			bossSignals.groggyTriggered = true;

		const auto& ePlayer = m_state->bus.PeekDeferred(playerId);
		const bool freezePlayerFsm = playerHitstopActive;
		const bool freezeBossFsm = bossHitstopActive;

		Combat::FsmOutput outPlayer{};
		Combat::FsmOutput outBoss{};
		if (freezePlayerFsm)
		{
			outPlayer.state = m_state->player.state;
			outPlayer.flags = m_state->player.flags;
		}
		else
		{
			outPlayer = m_state->playerFsm.Update(playerId, playerIntent, sPlayer, ePlayer, playerLogicDt);
		}

		Combat::BossOutput bossOut{};
		if (bossSession)
		{
			bossOut = bossSession->Tick(world, bossLogicDt, bossId, playerId, bossBrain, sBoss, bossHitstopActive, bossSignals);
		}
		else
		{
			if (bossBrain)
				bossIntent = bossBrain->Think(bossLogicDt, playerId);

			Combat::ActionState nextState = Combat::ActionState::Idle;
			if (bossSignals.dead)
			{
				nextState = Combat::ActionState::Dead;
			}
			else if (bossSignals.groggyTriggered)
			{
				nextState = Combat::ActionState::Groggy;
			}
			else if (bossBrain)
			{
				switch (bossBrain->GetBrainState())
				{
				case C_BossBrainComponent::BrainState::Attack:
					nextState = Combat::ActionState::Attack;
					break;
				case C_BossBrainComponent::BrainState::Gimmick:
					nextState = (bossBrain->GetActivePattern() == C_BossBrainComponent::PatternType::Special)
						? Combat::ActionState::Attack
						: Combat::ActionState::Idle;
					break;
				case C_BossBrainComponent::BrainState::Idle:
					nextState = Combat::ActionState::Idle;
					break;
				case C_BossBrainComponent::BrainState::Orbit:
				case C_BossBrainComponent::BrainState::Approach:
				case C_BossBrainComponent::BrainState::Retreat:
				case C_BossBrainComponent::BrainState::Chase:
					nextState = Combat::ActionState::Move;
					break;
				default:
					nextState = Combat::ActionState::Idle;
					break;
				}
			}
			else
			{
				const float moveMag = std::abs(bossIntent.move.x) + std::abs(bossIntent.move.y);
				if (bossIntent.attackRequested)
					nextState = Combat::ActionState::Attack;
				else if (moveMag > 0.001f)
					nextState = Combat::ActionState::Move;
				else
					nextState = Combat::ActionState::Idle;
			}

			bossOut.state = nextState;
			bossOut.intent = bossIntent;
			bossOut.wantsFaceTarget = bossBrain ? bossBrain->WantsFaceTarget() : bossIntent.wantsFaceTarget;
			bossOut.hitstopActive = bossHitstopActive;
			if (bossBrain && bossOut.state == Combat::ActionState::Attack)
				bossOut.attackClip = bossBrain->GetPatternClip(bossBrain->GetActivePattern());

			bossOut.flags.hitActive = (bossOut.state == Combat::ActionState::Attack) && sBoss.attackWindowActive;
			bossOut.flags.invulnActive = sBoss.invulnActive;
			bossOut.flags.canBeInterrupted = false;
			bossOut.flags.chargeActive = bossIntent.chargeActive;
			bossOut.flags.chargeLevel = bossIntent.chargeLevel;
		}

		const bool bossPhaseHowlingActive = bossBrain
			&& (bossBrain->GetActivePattern() == C_BossBrainComponent::PatternType::Special);
		if (bossPhaseHowlingActive)
			bossOut.flags.hitActive = false;
		if (bossBrain && bossBrain->ConsumePhase2HowlingStarted())
		{
			float basePushSpeed = 3.0f;
			if (auto* trace = world.GetComponent<WeaponTraceComponent>(bossId))
				basePushSpeed = std::max(0.0f, trace->guardBreakPushbackSpeed);

			const float pushSpeed = std::max(0.0f, basePushSpeed * std::max(0.0f, m_phaseHowlingPushbackScale));
			const float pushDuration = std::max(0.0f, m_phaseHowlingPushbackDurationSec);
			if (pushSpeed > 0.0f && pushDuration > 0.0f)
			{
				std::vector<Combat::Command> phase2Commands;
				phase2Commands.push_back({ Combat::CommandType::ApplyPushback,
					Combat::CmdApplyPushback{ bossId, playerId, pushSpeed, pushDuration } });
				m_state->apply.ApplyImmediate(world, m_state->fighterMap, m_state->bus, phase2Commands, true);
			}
			if (pushDuration > 0.0f && (m_phaseHowlingForceGuard || m_phaseHowlingLockInput))
			{
				m_state->playerHowlingGuardLockSec = std::max(m_state->playerHowlingGuardLockSec, pushDuration);
				if (auto* driver = world.GetComponent<AttackDriverComponent>(playerId))
				{
					if (driver->attackCancelable)
						driver->cancelAttackRequested = true;
					if (m_phaseHowlingForceGuard)
						driver->guardLockRemainingSec = std::max(driver->guardLockRemainingSec, pushDuration);
					driver->parryOverrideRemainingSec = 0.0f;
					driver->parryUsedThisPress = false;
				}
			}
			// TODO: add dedicated camera shake cue for phase-2 howling.
		}

		bossIntent = bossOut.intent;
		bossIntentCompat = {};
		bossIntentCompat.move = bossIntent.move;
		bossIntentCompat.attackPressed = bossIntent.attackRequested;
		bossIntentCompat.lightAttackPressed = bossIntent.attackRequested;
		bossIntentCompat.chargeActive = bossIntent.chargeActive;
		bossIntentCompat.chargeLevel = bossIntent.chargeLevel;

		outBoss.state = bossOut.state;
		outBoss.flags = bossOut.flags;
		outBoss.attackRestarted = (outBoss.state == Combat::ActionState::Attack)
			&& (m_state->prevBossState != Combat::ActionState::Attack
				|| (!bossOut.attackClip.empty() && bossOut.attackClip != m_state->bossAnim.attackClip));
		m_state->bossChargeActive = outBoss.flags.chargeActive;

		if (!playerHitstopActive)
		{
			outPlayer.flags.chargeActive = playerIntent.chargeActive;
			outPlayer.flags.chargeLevel = playerIntent.chargeLevel;
		}

		auto UpdateAttackKind = [&](bool& lastHeavy,
			int& lastChargeLevel,
			const Combat::Intent& intent,
			Combat::ActionState curr,
			Combat::ActionState prev,
			bool attackRestarted)
			{
				if (curr == Combat::ActionState::Attack
					&& (prev != Combat::ActionState::Attack || attackRestarted))
				{
					lastHeavy = intent.heavyAttackPressed;
					lastChargeLevel = lastHeavy ? std::clamp(intent.chargeLevel, 0, 3) : 0;
				}
			};
		UpdateAttackKind(m_state->playerLastAttackHeavy, m_state->playerLastAttackChargeLevel, playerIntent,
			outPlayer.state, m_state->prevPlayerState, outPlayer.attackRestarted);
		UpdateAttackKind(m_state->bossLastAttackHeavy, m_state->bossLastAttackChargeLevel, bossIntentCompat,
			outBoss.state, m_state->prevBossState, outBoss.attackRestarted);

		const bool playerAttackStarted = (outPlayer.state == Combat::ActionState::Attack
			&& (m_state->prevPlayerState != Combat::ActionState::Attack || outPlayer.attackRestarted));
		const bool playerAttackEnded = (m_state->prevPlayerState == Combat::ActionState::Attack
			&& outPlayer.state != Combat::ActionState::Attack);
		const bool playerAttackEndedOnFinal = playerAttackEnded
			&& !m_state->playerLastAttackHeavy
			&& (m_state->playerLightComboIndex >= kMaxLightCombo);

		if (playerAttackStarted)
		{
			if (!m_state->playerLastAttackHeavy)
			{
				int comboIndex = m_state->playerLightComboPending ? m_state->playerLightComboPendingIndex : 1;
				m_state->playerLightComboIndex = std::clamp(comboIndex, 1, kMaxLightCombo);
			}
			else
			{
				m_state->playerLightComboIndex = 0;
			}
			m_state->playerLightComboPending = false;
			m_state->playerLightComboQueued = false;
			m_state->playerLightComboWindowSec = 0.0f;
		}

		if (playerAttackEnded)
		{
			if (!m_state->playerLastAttackHeavy)
			{
				if (m_state->playerLightComboIndex >= kMaxLightCombo)
				{
					m_state->playerLightComboIndex = 0;
					m_state->playerLightComboPending = false;
					m_state->playerLightComboPendingIndex = 0;
					m_state->playerLightComboQueued = false;
					m_state->playerLightComboWindowSec = 0.0f;
				}
				else
				{
					m_state->playerLightComboWindowSec = std::max(0.0f, m_lightComboWindowSec);
					m_state->playerLightComboPending = false;
					m_state->playerLightComboPendingIndex = 0;
					m_state->playerLightComboQueued = false;
				}
			}
			else
			{
				m_state->playerLightComboIndex = 0;
				m_state->playerLightComboPending = false;
				m_state->playerLightComboPendingIndex = 0;
				m_state->playerLightComboQueued = false;
				m_state->playerLightComboWindowSec = 0.0f;
			}
		}

		if (outPlayer.state != Combat::ActionState::Attack
			&& m_state->playerLightComboWindowSec <= 0.0f
			&& !playerAttackStarted)
		{
			m_state->playerLightComboIndex = 0;
			m_state->playerLightComboPending = false;
			m_state->playerLightComboPendingIndex = 0;
			m_state->playerLightComboQueued = false;
		}

		m_state->playerAttackWindowSeen = false;

		outPlayer.flags.attackComboIndex = (!m_state->playerLastAttackHeavy)
			? m_state->playerLightComboIndex
			: 0;
		outBoss.flags.attackComboIndex = 0;

		auto FacePlayerForAttackGuard = [&](Combat::ActionState curr,
			Combat::ActionState prev,
			bool chargeActive,
			bool attackRestarted,
			bool suppressFacing) {
			if (suppressFacing || curr == Combat::ActionState::Dodge)
				return;
			const bool inAttack = (curr == Combat::ActionState::Attack);
			const bool attackStarted = (inAttack
				&& (prev != Combat::ActionState::Attack || attackRestarted));
			const bool attackEnded = (!inAttack && prev == Combat::ActionState::Attack);
			const bool rageLightAttackActive = inAttack
				&& m_state->playerRageActive
				&& !m_state->playerLastAttackHeavy;
			const auto* playerAnim = world.GetComponent<AdvancedAnimationComponent>(playerId);
			const bool rootMotionAttackYawDriven = inAttack
				&& playerAnim
				&& playerAnim->rootMotionUnlock
				&& playerAnim->rootMotionDriveCct;
			if (attackEnded)
				m_state->playerAttackFacingLocked = false;
			if (rootMotionAttackYawDriven && !rageLightAttackActive)
			{
				// While root motion drives transform via CCT, forcing yaw here
				// can fight root-yaw extraction and produce backward snaps.
				m_state->playerAttackFacingLocked = false;
				return;
			}
			if (rageLightAttackActive)
				m_state->playerAttackFacingLocked = false;
			if (chargeActive && !attackStarted && !inAttack)
				return;

			const bool wantsAttack = playerIntent.lightAttackPressed
				|| playerIntent.heavyAttackPressed
				|| (playerIntent.attackPressed && !playerIntent.attackHeld);
			const bool wantsGuard = playerIntent.guardHeld || playerIntent.guardPressed;
			const bool inGuard = (curr == Combat::ActionState::Guard);
			if (!(wantsAttack || wantsGuard || inAttack || inGuard))
				return;

			auto* playerTr = world.GetComponent<TransformComponent>(playerId);
			if (!playerTr)
				return;

			const float offsetRad = m_rotationOffsetDeg * kDegToRad;
			if (attackStarted)
			{
				float dx = 0.0f;
				float dz = 0.0f;
				bool hasDir = false;

				if (m_state->playerLockOnActive && m_state->playerLockOnTarget != InvalidEntityId)
				{
					if (auto* targetTr = world.GetComponent<TransformComponent>(m_state->playerLockOnTarget))
					{
						dx = targetTr->position.x - playerTr->position.x;
						dz = targetTr->position.z - playerTr->position.z;
						const float len = std::sqrt(dx * dx + dz * dz);
						if (len > 0.0001f)
						{
							dx /= len;
							dz /= len;
							hasDir = true;
						}
					}
				}

				if (!hasDir && camBasis.valid)
				{
					dx = camBasis.forwardX;
					dz = camBasis.forwardZ;
					const float len = std::sqrt(dx * dx + dz * dz);
					if (len > 0.0001f)
					{
						dx /= len;
						dz /= len;
						hasDir = true;
					}
				}

				if (hasDir)
				{
					const float yawRad = std::atan2(dx, dz) + offsetRad;
					if (rageLightAttackActive)
					{
						playerTr->SetRotation(0.0f, yawRad * kRadToDeg, 0.0f);
					}
					else
					{
						m_state->playerAttackFacingLocked = true;
						m_state->playerAttackFacingYawRad = yawRad;
					}
				}
			}

			if (inAttack && m_state->playerAttackFacingLocked)
			{
				playerTr->SetRotation(0.0f, m_state->playerAttackFacingYawRad * kRadToDeg, 0.0f);
				return;
			}

			float dx = 0.0f;
			float dz = 0.0f;
			bool hasDir = false;

			if (m_state->playerLockOnActive && m_state->playerLockOnTarget != InvalidEntityId)
			{
				if (auto* targetTr = world.GetComponent<TransformComponent>(m_state->playerLockOnTarget))
				{
					dx = targetTr->position.x - playerTr->position.x;
					dz = targetTr->position.z - playerTr->position.z;
					const float len = std::sqrt(dx * dx + dz * dz);
					if (len > 0.0001f)
					{
						dx /= len;
						dz /= len;
						hasDir = true;
					}
				}
			}

			if (!hasDir && camBasis.valid)
			{
				dx = camBasis.forwardX;
				dz = camBasis.forwardZ;
				const float len = std::sqrt(dx * dx + dz * dz);
				if (len > 0.0001f)
				{
					dx /= len;
					dz /= len;
					hasDir = true;
				}
			}

			if (hasDir)
			{
				const float yawRad = std::atan2(dx, dz) + offsetRad;
				playerTr->SetRotation(0.0f, yawRad * kRadToDeg, 0.0f);
			}
		};
		const bool suppressGuardFacing = (outPlayer.state == Combat::ActionState::Dodge)
			|| playerIntent.dodgePressed;
		FacePlayerForAttackGuard(outPlayer.state, m_state->prevPlayerState,
			m_state->playerChargeActive, outPlayer.attackRestarted, suppressGuardFacing);

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

		auto GetBossAttackMoveDistance = [&](const std::string& clipName) -> float
			{
				if (clipName.empty())
					return 0.0f;
				if (clipName.find("Dash_Attack") != std::string::npos)
					return 4.5f;
				if (clipName.find("Attack_ABC") != std::string::npos)
					return 1.5f;
				if (clipName.find("Attack_BC") != std::string::npos)
					return 1.0f;
				if (clipName.find("Attack_A") != std::string::npos)
					return 0.5f;
				if (clipName.find("Attack_B") != std::string::npos)
					return 0.5f;
				if (clipName.find("Attack_C") != std::string::npos)
					return 0.5f;
				return 0.0f;
			};

		auto UpdateBossAttackMove = [&](SessionState::AttackMoveState& moveState,
			EntityId entityId,
			const Combat::Intent& intent,
			Combat::ActionState curr,
			Combat::ActionState prev,
			const std::string& clipName,
			std::vector<Combat::Command>& cmds,
			float deltaTime)
			{
				if (curr != Combat::ActionState::Attack)
				{
					moveState = {};
					return;
				}

				const float dist = GetBossAttackMoveDistance(clipName);
				if (dist <= 0.0f)
				{
					moveState = {};
					return;
				}

				const bool isDash = (clipName.find("Dash_Attack") != std::string::npos);
				float dashClipDuration = 0.0f;
				if (isDash)
					dashClipDuration = GetClipDurationSecByName(registry, world, entityId, clipName);
				if (dashClipDuration <= 0.0f)
					dashClipDuration = 0.5f;
				float startSec = 0.1f;
				if (isDash)
				{
					const float configuredStartSec = m_bossDashMoveStartSec;
					startSec = (configuredStartSec >= 0.0f) ? configuredStartSec : dashClipDuration;
				}
				const float duration = isDash ? 0.25f : 0.2f;

				if (prev != Combat::ActionState::Attack || moveState.clipName != clipName)
				{
					moveState.configured = true;
					moveState.heavy = true;
					moveState.timerSec = 0.0f;
					moveState.startSec = std::max(0.0f, startSec);
					moveState.endSec = moveState.startSec + std::max(0.0f, duration);
					moveState.dir = ResolveAttackMoveDir(entityId, intent, false);
					moveState.clipName = clipName;

					const float dirLen = std::sqrt(moveState.dir.x * moveState.dir.x + moveState.dir.y * moveState.dir.y);
					if (dirLen <= 0.0001f || moveState.endSec <= moveState.startSec)
					{
						moveState = {};
						return;
					}
					moveState.speed = dist / (moveState.endSec - moveState.startSec);
					moveState.active = false;
					return;
				}

				if (!moveState.configured)
				{
					moveState.active = false;
					return;
				}

				moveState.timerSec += deltaTime;
				const bool withinWindow = (moveState.timerSec >= moveState.startSec && moveState.timerSec <= moveState.endSec);
				moveState.active = withinWindow;

				if (withinWindow)
				{
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
		std::vector<Combat::Command> bossCommands;
		const std::string& bossAttackClipForMove = !bossOut.attackClip.empty()
			? bossOut.attackClip
			: m_state->bossAnim.attackClip;
		UpdateBossAttackMove(m_state->bossAttackMove,
			bossId,
			bossIntentCompat,
			outBoss.state,
			m_state->prevBossState,
			bossAttackClipForMove,
			bossCommands,
			bossLogicDt);
		if (outBoss.state == Combat::ActionState::Move)
		{
			bossCommands.push_back({ Combat::CommandType::RequestMove,
				Combat::CmdRequestMove{ bossId, bossIntentCompat.move, m_state->boss.moveSpeed, false, true } });
		}
		std::vector<Combat::Command> bossTraceCommands;
		if (m_state->boss.flags.hitActive != outBoss.flags.hitActive)
		{
			if (outBoss.flags.hitActive)
				bossTraceCommands.push_back({ Combat::CommandType::EnableTrace, Combat::CmdEnableTrace{ bossId } });
			else
				bossTraceCommands.push_back({ Combat::CommandType::DisableTrace, Combat::CmdDisableTrace{ bossId } });
		}

		m_state->player.state = outPlayer.state;
		m_state->player.flags = outPlayer.flags;
		m_state->boss.state = outBoss.state;
		m_state->boss.flags = outBoss.flags;
		if (auto* driver = world.GetComponent<AttackDriverComponent>(playerId))
		{
			if (outPlayer.state != Combat::ActionState::Attack)
				driver->attackSuppressed = false;
		}
		if (auto* driver = world.GetComponent<AttackDriverComponent>(bossId))
		{
			if (outBoss.state != Combat::ActionState::Attack)
				driver->attackSuppressed = false;
		}

		const bool playerHealLoop = (outPlayer.state == Combat::ActionState::HealLoop);
		const bool enteredHealLoop = playerHealLoop
			&& (m_state->prevPlayerState != Combat::ActionState::HealLoop);
		if (healGimmick)
		{
			const bool playerHealEnter = (outPlayer.state == Combat::ActionState::HealEnter);
			const bool playerHealExit = (outPlayer.state == Combat::ActionState::HealExit);
			const bool prevHeal = (m_state->prevPlayerState == Combat::ActionState::HealEnter)
				|| (m_state->prevPlayerState == Combat::ActionState::HealLoop)
				|| (m_state->prevPlayerState == Combat::ActionState::HealExit);
			const bool currHeal = playerHealEnter || playerHealLoop || playerHealExit;
			const bool enteredHealEnter = playerHealEnter
				&& (m_state->prevPlayerState != Combat::ActionState::HealEnter);
			const bool enteredHealExit = playerHealExit
				&& (m_state->prevPlayerState != Combat::ActionState::HealExit);
			const bool cancelledHeal = prevHeal && !currHeal
				&& (m_state->prevPlayerState != Combat::ActionState::HealExit);

			if (enteredHealEnter)
				healGimmick->BeginHeal(healEnterDurationSec);
			if (enteredHealLoop)
				healGimmick->BeginHealLoop();
			if (enteredHealExit || cancelledHeal)
				healGimmick->EndHeal(healExitDurationSec);
		}
		if (enteredHealLoop)
		{
			m_state->playerHealLoopSec = 0.0f;
			m_state->playerHealNextTickSec = std::max(0.0f, m_healStartDelaySec);
		}
		if (!playerHealLoop)
		{
			m_state->playerHealLoopSec = 0.0f;
			m_state->playerHealNextTickSec = 0.0f;
		}
		else if (playerLogicDt > 0.0f && playerHealth)
		{
			m_state->playerHealLoopSec += playerLogicDt;
			const float interval = std::max(0.01f, m_healTickIntervalSec);
			const float transferRatio = std::max(0.0f, m_healTransferRatio);
			const float weaponMax = std::max(0.0f, playerHealth->weaponDurabilityMax);
			const float healthMax = std::max(0.0f, playerHealth->maxHealth);
			while (interval > 0.0f && transferRatio > 0.0f
				&& m_state->playerHealLoopSec >= m_state->playerHealNextTickSec)
			{
				if (healthMax > 0.0f && (playerHealth->currentHealth / healthMax) > maxHpRatio)
					break;
				const float minWeapon = weaponMax * minWeaponRatio;
				const float available = std::max(0.0f, playerHealth->weaponDurability - minWeapon);
				if (available <= 0.0f)
					break;
				float amount = weaponMax * transferRatio;
				if (amount > available)
					amount = available;
				if (amount <= 0.0f)
					break;
				playerHealth->weaponDurability = std::max(0.0f, playerHealth->weaponDurability - amount);
				playerHealth->currentHealth = std::min(healthMax, playerHealth->currentHealth + amount);
				m_state->playerHealNextTickSec += interval;
			}
			m_state->player.hp = playerHealth->currentHealth;
			m_state->player.weaponDurability = playerHealth->weaponDurability;
			m_state->player.weaponDurabilityMax = playerHealth->weaponDurabilityMax;
		}
		if (m_state->prevPlayerState == Combat::ActionState::Hitstun
			&& outPlayer.state != Combat::ActionState::Hitstun)
		{
			m_state->playerHitstunDurationSec = 0.0f;
		}
		if (m_state->prevBossState == Combat::ActionState::Hitstun
			&& outBoss.state != Combat::ActionState::Hitstun)
		{
			m_state->bossHitstunDurationSec = 0.0f;
		}
		const bool playerSuperArmor = (outPlayer.state == Combat::ActionState::Attack
			&& m_state->playerLastAttackHeavy)
			|| m_state->playerChargeActive;
		m_state->player.canBeHitstunned = m_playerCanBeHitstunned && !playerSuperArmor;
		m_state->playerSnapshot = m_state->player.Snapshot();
		m_state->bossSnapshot = m_state->boss.Snapshot();

		auto ResetGroggyIfEnded = [&](Combat::ActionState prev, Combat::ActionState curr, EntityId id)
			{
				if (prev == Combat::ActionState::Groggy && curr != Combat::ActionState::Groggy)
				{
					if (auto* hc = world.GetComponent<HealthComponent>(id))
						hc->groggy = 0.0f;
				}
			};
		ResetGroggyIfEnded(m_state->prevBossState, outBoss.state, bossId);
		const bool bossEnteredGroggy = (m_state->prevBossState != Combat::ActionState::Groggy
			&& outBoss.state == Combat::ActionState::Groggy);
		if (bossEnteredGroggy)
		{
			if (bossBrain)
				bossBrain->ForceCompleteIntent();
			m_state->bossGroggyEnterBlendBlockSec =
				std::max(0.0f, m_bossGroggyEnterBlendSec);
		}
		else if (outBoss.state != Combat::ActionState::Groggy)
		{
			m_state->bossGroggyEnterBlendBlockSec = 0.0f;
		}

		if (!freezePlayerFsm)
			m_state->bus.ClearDeferred(playerId);
		if (!freezeBossFsm)
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
					const bool isPlayerDodge = isPlayer
						&& (outPlayer.state == Combat::ActionState::Dodge);
					const auto* moveHealth = world.GetComponent<HealthComponent>(entityId);
					const bool pushbackOverrideActive = moveHealth
						&& moveHealth->pushbackRemainingSec > 0.0f
						&& moveHealth->pushbackSpeed > 0.0f;
					if (pushbackOverrideActive)
					{
						// While pushback is active, ignore normal move requests.
						// Pushback velocity is authored in ApplyPushback().
						cct->desiredVelocity.x = 0.0f;
						cct->desiredVelocity.z = 0.0f;
						cct->desiredVelocity.y = 0.0f;
						if (isPlayer)
						{
							m_state->playerMoveSmoothedDir = {};
							m_state->playerMoveSmoothedValid = false;
						}
						continue;
					}

					const bool playerMovementLocked = isPlayer
						&& ((m_state->playerGuardExitLockSec > 0.0f)
							|| m_state->playerAnim.guardExitActive
							|| (m_phaseHowlingLockInput && m_state->playerHowlingGuardLockSec > 0.0f));
					if (playerMovementLocked)
					{
						cct->desiredVelocity.x = 0.0f;
						cct->desiredVelocity.z = 0.0f;
						cct->desiredVelocity.y = 0.0f;
						m_state->playerMoveSmoothedDir = {};
						m_state->playerMoveSmoothedValid = false;
						continue;
					}

					if (isPlayer && payload.useCameraRelative && camBasis.valid)
					{
						dx = camBasis.rightX * inputX + camBasis.forwardX * inputZ;
						dz = camBasis.rightZ * inputX + camBasis.forwardZ * inputZ;
					}

					const float len = std::sqrt(dx * dx + dz * dz);
					const bool hasMoveDir = (len > 0.0001f);
					if (hasMoveDir)
					{
						dx /= len;
						dz /= len;
					}
					else
					{
						dx = 0.0f;
						dz = 0.0f;
					}

					if (isPlayer && payload.useCameraRelative && !isPlayerDodge)
					{
						if (hasMoveDir)
						{
							if (!m_state->playerMoveSmoothedValid)
							{
								m_state->playerMoveSmoothedDir = { dx, dz };
								m_state->playerMoveSmoothedValid = true;
							}
							else
							{
								const float damp = std::max(0.0f, m_playerMoveInputDamping);
								if (damp > 0.0f)
								{
									const float alpha = 1.0f - std::exp(-damp * std::max(0.0f, deltaTime));
									m_state->playerMoveSmoothedDir.x += (dx - m_state->playerMoveSmoothedDir.x) * alpha;
									m_state->playerMoveSmoothedDir.y += (dz - m_state->playerMoveSmoothedDir.y) * alpha;
								}
								else
								{
									m_state->playerMoveSmoothedDir = { dx, dz };
								}
							}

							const float smoothLen = std::sqrt(
								m_state->playerMoveSmoothedDir.x * m_state->playerMoveSmoothedDir.x
								+ m_state->playerMoveSmoothedDir.y * m_state->playerMoveSmoothedDir.y);
							if (smoothLen > 0.0001f)
							{
								dx = m_state->playerMoveSmoothedDir.x / smoothLen;
								dz = m_state->playerMoveSmoothedDir.y / smoothLen;
							}
							else
							{
								dx = 0.0f;
								dz = 0.0f;
							}
						}
						else
						{
							m_state->playerMoveSmoothedDir = {};
							m_state->playerMoveSmoothedValid = false;
						}
					}
					else if (isPlayerDodge)
					{
						// Dodge direction must react immediately to fresh input.
						// Keep no carry-over smoothing to avoid direction/yaw mismatch.
						m_state->playerMoveSmoothedDir = {};
						m_state->playerMoveSmoothedValid = false;
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

					const bool lockFacing = (entityId == playerId
						&& outPlayer.state == Combat::ActionState::Attack
						&& m_state->playerAttackFacingLocked);
					if (!lockFacing && payload.faceMove && (dx != 0.0f || dz != 0.0f))
					{
						if (auto* tr = world.GetComponent<TransformComponent>(entityId))
						{
							const float targetYawRad = std::atan2(dx, dz) + offsetRad;
							const bool useMoveYawSmoothing = isPlayer
								&& payload.useCameraRelative
								&& !isPlayerDodge
								&& (m_playerMoveYawDamping > 0.0f);
							if (!useMoveYawSmoothing)
							{
								tr->SetRotation(0.0f, targetYawRad * kRadToDeg, 0.0f);
							}
							else
							{
								constexpr float kPi = 3.14159265359f;
								constexpr float kTwoPi = 6.28318530718f;
								float currentYawRad = tr->rotation.y;
								float deltaYawRad = targetYawRad - currentYawRad;
								while (deltaYawRad > kPi) deltaYawRad -= kTwoPi;
								while (deltaYawRad < -kPi) deltaYawRad += kTwoPi;
								const float alpha = 1.0f - std::exp(
									-std::max(0.0f, m_playerMoveYawDamping) * std::max(0.0f, deltaTime));
								const float smoothedYawRad = currentYawRad + deltaYawRad * alpha;
								tr->SetRotation(0.0f, smoothedYawRad * kRadToDeg, 0.0f);
							}
						}
					}
				}
			};
		ApplyMove(playerId, playerIntent, outPlayer.commands);
		ApplyMove(bossId, bossIntentCompat, bossCommands);

		auto StopIfNotMoving = [&](EntityId entityId, Combat::ActionState state, const SessionState::AttackMoveState& attackMove)
			{
				const bool forcePlayerMotionLock = (entityId == playerId)
					&& ((m_state->playerGuardExitLockSec > 0.0f)
						|| m_state->playerAnim.guardExitActive
						|| (m_phaseHowlingLockInput && m_state->playerHowlingGuardLockSec > 0.0f));
				if (!forcePlayerMotionLock
					&& (state == Combat::ActionState::Move
						|| state == Combat::ActionState::Dodge
						|| (state == Combat::ActionState::Attack && attackMove.active)))
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
				if (entityId == playerId)
				{
					m_state->playerMoveSmoothedDir = {};
					m_state->playerMoveSmoothedValid = false;
				}
			};
		StopIfNotMoving(playerId, outPlayer.state, m_state->playerAttackMove);
		StopIfNotMoving(bossId, outBoss.state, m_state->bossAttackMove);

		auto FaceTarget = [&](EntityId selfId, EntityId targetId, float yawDamping)
			{
				auto* selfTr = world.GetComponent<TransformComponent>(selfId);
				auto* targetTr = world.GetComponent<TransformComponent>(targetId);
				if (!selfTr || !targetTr)
					return;
				const float dx = targetTr->position.x - selfTr->position.x;
				const float dz = targetTr->position.z - selfTr->position.z;
				if (std::abs(dx) + std::abs(dz) <= 0.0001f)
					return;
				const float offsetRad = m_rotationOffsetDeg * kDegToRad;
				const float targetYawRad = std::atan2(dx, dz) + offsetRad;
				if (yawDamping <= 0.0f)
				{
					selfTr->SetRotation(0.0f, targetYawRad * kRadToDeg, 0.0f);
					return;
				}

				constexpr float kPi = 3.14159265359f;
				constexpr float kTwoPi = 6.28318530718f;
				float currentYawRad = selfTr->rotation.y;
				float deltaYawRad = targetYawRad - currentYawRad;
				while (deltaYawRad > kPi) deltaYawRad -= kTwoPi;
				while (deltaYawRad < -kPi) deltaYawRad += kTwoPi;
				const float alpha = 1.0f - std::exp(-yawDamping * std::max(0.0f, deltaTime));
				const float smoothedYawRad = currentYawRad + deltaYawRad * alpha;
				selfTr->SetRotation(0.0f, smoothedYawRad * kRadToDeg, 0.0f);
			};

		auto ShouldTrackBossFacingThisFrame = [&]() -> bool
			{
				if (bossHitstopActive || !bossOut.wantsFaceTarget)
					return false;
				if (outBoss.state == Combat::ActionState::Groggy)
					return false;

				// Boss attacks should keep their initial direction.
				// Exception: charge attack can track only for the configured early-time window.
				if (outBoss.state != Combat::ActionState::Attack)
					return true;

				const std::string attackClip = !bossOut.attackClip.empty()
					? bossOut.attackClip
					: m_state->bossAnim.attackClip;

				const bool chargePatternActive = bossBrain
					&& (bossBrain->GetActivePattern() == C_BossBrainComponent::PatternType::Charge);
				const bool chargeClip = (attackClip.find("Charge") != std::string::npos)
					|| (attackClip.find("charge") != std::string::npos);
				const bool isChargeAttack = chargePatternActive || chargeClip;
				if (!isChargeAttack)
					return false;

				if (attackClip.empty())
					return false;

				float clipTimeSec = 0.0f;
				if (!TryGetClipTime(bossId, attackClip, clipTimeSec))
					return false;

				const float trackSec = std::max(0.0f, m_bossChargeFacingTrackSec);
				if (trackSec <= 0.0f)
					return false;

				return clipTimeSec < trackSec;
			};

		if (ShouldTrackBossFacingThisFrame())
		{
			const float idleFacingDamping = (outBoss.state == Combat::ActionState::Idle)
				? std::max(0.0f, m_bossIdleFacingDamping)
				: 0.0f;
			FaceTarget(bossId, playerId, idleFacingDamping);
		}

		// Root motion unlock/drive is now left to component settings (disable attack-only override).
		// auto UpdateRootMotionDrive = [&](EntityId entityId, Combat::ActionState state)
		// 	{
		// 		if (auto* anim = world.GetComponent<AdvancedAnimationComponent>(entityId))
		// 		{
		// 			anim->rootMotionDriveCct = anim->rootMotionUnlock
		// 				&& (state == Combat::ActionState::Attack);
		// 		}
		// 	};

		// UpdateRootMotionDrive(playerId, outPlayer.state);
		// UpdateRootMotionDrive(bossId, outBoss.state);

		auto ApplyPushback = [&](EntityId entityId, bool hitstopActive)
			{
				auto* hc = world.GetComponent<HealthComponent>(entityId);
				if (!hc || hc->pushbackRemainingSec <= 0.0f || hc->pushbackSpeed <= 0.0f)
					return;
				if (hitstopActive)
					return;

				const float dx = hc->pushbackDir.x;
				const float dz = hc->pushbackDir.z;
				const float len = std::sqrt(dx * dx + dz * dz);
				if (len <= 0.0001f)
					return;

				if (auto* cct = world.GetComponent<Phy_CCTComponent>(entityId))
				{
					const float pushX = (dx / len) * hc->pushbackSpeed;
					const float pushZ = (dz / len) * hc->pushbackSpeed;
					cct->desiredVelocity.x += pushX;
					cct->desiredVelocity.z += pushZ;
					const float velLen = std::sqrt(
						cct->desiredVelocity.x * cct->desiredVelocity.x
						+ cct->desiredVelocity.z * cct->desiredVelocity.z);
					const float maxPushVel = std::max(0.0f, hc->pushbackSpeed * 4.0f);
					if (maxPushVel > 0.0f && velLen > maxPushVel)
					{
						const float s = maxPushVel / velLen;
						cct->desiredVelocity.x *= s;
						cct->desiredVelocity.z *= s;
					}
					cct->desiredVelocity.y = 0.0f;
					if (entityId == playerId)
					{
						m_state->playerMoveSmoothedDir = {};
						m_state->playerMoveSmoothedValid = false;
					}
				}
				if (auto* tr = world.GetComponent<TransformComponent>(entityId))
				{
					const float faceX = -dx / len;
					const float faceZ = -dz / len;
					const float offsetRad = m_rotationOffsetDeg * kDegToRad;
					const float yawRad = std::atan2(faceX, faceZ) + offsetRad;
					tr->SetRotation(0.0f, yawRad * kRadToDeg, 0.0f);
				}
			};

		ApplyPushback(playerId, playerHitstopActive);
		ApplyPushback(bossId, bossHitstopActive);

		auto UpdateFatalSequence = [&](float dt)
			{
				if (!m_state->fatal.active)
					return;

				auto* playerTr = world.GetComponent<TransformComponent>(playerId);
				auto* bossTr = world.GetComponent<TransformComponent>(bossId);
				if (!playerTr || !bossTr)
				{
					m_state->fatal = {};
					return;
				}

				float approachSec = (m_state->fatal.totalSec > 0.0f)
					? m_state->fatal.approachSec
					: std::max(0.0f, m_fatalApproachSec);
				float holdSec = (m_state->fatal.totalSec > 0.0f)
					? m_state->fatal.holdSec
					: std::max(0.0f, m_fatalHoldSec);
				if (m_state->fatal.totalSec <= 0.0f && holdSec <= 0.0f)
				{
					const std::string& fatalClip = !m_playerFatalAttackClip.empty()
						? m_playerFatalAttackClip
						: m_fatalAttackClip;
					if (!fatalClip.empty())
					{
					const float clipDur = GetClipDurationSecByName(registry, world, playerId, fatalClip);
						if (clipDur > 0.0f)
							holdSec = std::max(0.0f, clipDur - approachSec);
					}
				}
				const float totalSec = (m_state->fatal.totalSec > 0.0f)
					? m_state->fatal.totalSec
					: (approachSec + holdSec);
				m_state->fatal.timerSec += dt;

				if (m_state->fatal.hasTarget)
				{
					const float t = (approachSec > 0.0f)
						? std::clamp(m_state->fatal.timerSec / approachSec, 0.0f, 1.0f)
						: 1.0f;
					bossTr->position = {
						m_state->fatal.bossStartPos.x + (m_state->fatal.bossTargetPos.x - m_state->fatal.bossStartPos.x) * t,
						m_state->fatal.bossStartPos.y + (m_state->fatal.bossTargetPos.y - m_state->fatal.bossStartPos.y) * t,
						m_state->fatal.bossStartPos.z + (m_state->fatal.bossTargetPos.z - m_state->fatal.bossStartPos.z) * t
					};
				}

				auto FaceTarget = [&](TransformComponent& self, const DirectX::XMFLOAT3& target)
					{
						const float dx = target.x - self.position.x;
						const float dz = target.z - self.position.z;
						const float len = std::sqrt(dx * dx + dz * dz);
						if (len <= 0.0001f)
							return;
						const float fx = dx / len;
						const float fz = dz / len;
						const float offsetRad = m_rotationOffsetDeg * kDegToRad;
						const float yawRad = std::atan2(fx, fz) + offsetRad;
						self.SetRotation(0.0f, yawRad * kRadToDeg, 0.0f);
					};

				FaceTarget(*bossTr, playerTr->position);
				FaceTarget(*playerTr, bossTr->position);

				if (!m_state->fatal.damageApplied
					&& m_state->fatal.damageAmount > 0.0f
					&& m_state->fatal.timerSec >= approachSec)
				{
					std::vector<Combat::Command> fatalCmds;
					fatalCmds.push_back({ Combat::CommandType::ApplyDamage,
						Combat::CmdApplyDamage{ bossId, m_state->fatal.damageAmount } });
					m_state->apply.ApplyImmediate(world, m_state->fighterMap, m_state->bus, fatalCmds, false);
					m_state->fatal.damageApplied = true;
				}

				if (auto* cct = world.GetComponent<Phy_CCTComponent>(playerId))
				{
					cct->desiredVelocity = { 0.0f, 0.0f, 0.0f };
				}
				if (auto* cct = world.GetComponent<Phy_CCTComponent>(bossId))
				{
					cct->desiredVelocity = { 0.0f, 0.0f, 0.0f };
				}

				if (auto* playerHealth = world.GetComponent<HealthComponent>(playerId))
				{
					if (totalSec > 0.0f)
					{
						const float remain = std::max(0.0f, totalSec - m_state->fatal.timerSec);
						playerHealth->invulnRemaining = std::max(playerHealth->invulnRemaining, remain);
					}
				}

				if (totalSec <= 0.0f || m_state->fatal.timerSec >= totalSec)
				{
					m_state->fatal = {};
				}
			};
		UpdateFatalSequence(deltaTime);

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
                        if (cmd.type == Combat::CommandType::EnableTrace)
                        {
                            const auto payload = std::get<Combat::CmdEnableTrace>(cmd.payload);
                            if (auto* driver = world.GetComponent<AttackDriverComponent>(payload.weaponOrOwner))
                            {
                                if (driver->attackSuppressed)
                                    continue;
                            }
                        }
                        traceCmds.push_back(cmd);
                    }
                }
				if (!traceCmds.empty())
					m_state->apply.ApplyImmediate(world, m_state->fighterMap, m_state->bus, traceCmds, true);
			};
		ApplyTraceCommands(outPlayer.commands);
		ApplyTraceCommands(bossTraceCommands);

		auto SmoothApproach = [&](float current, float target, float speed, float dt) {
			const float t = std::clamp(speed * dt, 0.0f, 1.0f);
			return current + (target - current) * t;
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

		auto ResolveHeavyFallbackClip = [&](const AnimConfig& cfg) -> std::string {
				if (!cfg.heavyAttackClipA.empty())
					return cfg.heavyAttackClipA;
				if (!cfg.heavyAttackClipB.empty())
					return cfg.heavyAttackClipB;
				return {};
			};

		auto SelectLightComboClip = [&](int comboIndex, const AnimConfig& cfg) -> std::string {
				const int idx = std::clamp(comboIndex, 1, 3);
				if (idx == 3)
				{
					if (!cfg.lightAttackClip3.empty())
						return cfg.lightAttackClip3;
					if (!cfg.lightAttackClip2.empty())
						return cfg.lightAttackClip2;
				}
				if (idx == 2)
				{
					if (!cfg.lightAttackClip2.empty())
						return cfg.lightAttackClip2;
					if (!cfg.guardEnterClip.empty())
						return cfg.guardEnterClip;
				}
				if (idx == 1 && !cfg.lightAttackClip1.empty())
					return cfg.lightAttackClip1;
				if (!cfg.lightAttackClip.empty())
					return cfg.lightAttackClip;
				return {};
			};

		auto SelectAttackClip = [&](const Combat::Intent& intent,
			SessionState::AnimOverrideState& animState,
			const AnimConfig& cfg,
			int comboIndex) -> std::string {
				if (intent.heavyAttackPressed)
				{
					std::string heavy = ResolveHeavyAttackClip(animState, cfg);
					if (!heavy.empty())
						return heavy;
				}
				if (intent.lightAttackPressed)
				{
					std::string light = SelectLightComboClip(comboIndex, cfg);
					if (!light.empty())
						return light;
				}
				if (!cfg.lightAttackClip.empty())
					return cfg.lightAttackClip;
				return {};
			};

		auto UpdateAttackClip = [&](EntityId entityId,
			const Combat::Intent& intent,
			Combat::ActionState curr,
			Combat::ActionState prev,
			SessionState::AnimOverrideState& animState,
			const AnimConfig& cfg,
			bool fatalActive,
			int comboIndex,
			bool attackRestarted) {
				if (curr == Combat::ActionState::Attack)
				{
					if (fatalActive && !cfg.fatalAttackClip.empty())
					{
						animState.attackClip = cfg.fatalAttackClip;
						return;
					}
					if (entityId == bossId && bossBrain)
					{
						const auto pattern = bossBrain->GetActivePattern();
						const std::string& patternClip = bossBrain->GetPatternClip(pattern);
						if (!patternClip.empty())
						{
							animState.attackClip = patternClip;
							return;
						}
					}
					if (attackRestarted || prev != Combat::ActionState::Attack || animState.attackClip.empty())
					{
						if (entityId == playerId
							&& intent.lightAttackPressed
							&& m_state->playerRageActive
							&& !cfg.rageAttackClip.empty())
						{
							animState.attackClip = cfg.rageAttackClip;
						}
						else
						{
							animState.attackClip = SelectAttackClip(intent, animState, cfg, comboIndex);
						}
					}
				}
				else
				{
					animState.attackClip.clear();
				}
			};

		fatalActive = (m_state->fatal.active || fatalTriggered);
		const bool playerFatalActive = fatalActive;
		UpdateAttackClip(playerId, playerIntent, outPlayer.state, m_state->prevPlayerState, m_state->playerAnim,
			BuildAnimConfig(playerId, playerId, bossId), playerFatalActive, m_state->playerLightComboIndex, outPlayer.attackRestarted);
		UpdateAttackClip(bossId, bossIntentCompat, outBoss.state, m_state->prevBossState, m_state->bossAnim,
			BuildAnimConfig(bossId, playerId, bossId), false, 1, outBoss.attackRestarted);
		if (outBoss.state == Combat::ActionState::Attack && !bossOut.attackClip.empty())
			m_state->bossAnim.attackClip = bossOut.attackClip;

		auto ApplyAnimByState = [&](EntityId entityId,
			const Combat::Intent& intent,
			Combat::ActionState curr,
			Combat::ActionState& prev,
			SessionState::AnimOverrideState& animState,
			float& moveBlend,
			bool guardEnterPulse,
			bool guardExitPulse,
			bool chargeActive,
			bool attackRestartPulse,
			bool blendIdleOnAttackEnd,
			bool hitReactActive,
			bool suppressGuardExit,
			bool forceGuardLoopOnly,
			bool forceGroggyRecoverClip) {
				auto* anim = world.GetComponent<AdvancedAnimationComponent>(entityId);
				if (!anim)
					anim = &world.AddComponent<AdvancedAnimationComponent>(entityId);
				auto* driver = world.GetComponent<AttackDriverComponent>(entityId);
				if (!anim || !driver)
				{
					if (entityId == playerId)
						m_state->playerAttackSpeedScale = 1.0f;
					else if (entityId == bossId)
						m_state->bossAttackSpeedScale = 1.0f;
					prev = curr;
					return;
				}
				const bool animHitstopActive = (entityId == playerId)
					? playerHitstopActive
					: (entityId == bossId)
						? bossHitstopActive
						: false;
				const float animDt = animHitstopActive ? 0.0f : deltaTime;
				const float fatalGroggyStartDelaySec = std::max(0.0f, m_groggyAttackStartDelaySec);
				const bool fatalGroggyClipReady = fatalActive
					&& (!m_state->fatal.active || m_state->fatal.timerSec >= fatalGroggyStartDelaySec);
				if (!animState.rootMotionUnlockSaved)
				{
					animState.rootMotionUnlockSaved = true;
					animState.rootMotionUnlockDefault = anim->rootMotionUnlock;
					animState.rootMotionDriveCctDefault = anim->rootMotionDriveCct;
				}
				const bool rootMotionCctConfigured = animState.rootMotionUnlockDefault
					&& animState.rootMotionDriveCctDefault;
				if (rootMotionCctConfigured)
				{
					const bool allowRootMotion = (curr == Combat::ActionState::Attack);
					anim->rootMotionUnlock = allowRootMotion;
					anim->rootMotionDriveCct = allowRootMotion;
				}
				else
				{
					anim->rootMotionUnlock = animState.rootMotionUnlockDefault;
					anim->rootMotionDriveCct = animState.rootMotionDriveCctDefault;
				}
				const AnimConfig cfg = BuildAnimConfig(entityId, playerId, bossId);
				std::string idleClip = cfg.idleClip;
				std::string moveClip = cfg.moveClip;
				std::string moveSideClip;
				std::string hitClip = cfg.hitClip;
				std::string groggyClip = cfg.groggyLoopClip;
				std::string groggyRecoverClip;
				std::string deadClip;
				if (entityId == bossId && bossBrain)
				{
					const std::string groggyAttackedClip = bossBrain->GetGroggyClip();
					if (!bossBrain->GetIdleClip().empty()) idleClip = bossBrain->GetIdleClip();
					if (!bossBrain->GetWalkForwardClip().empty()) moveClip = bossBrain->GetWalkForwardClip();
					if (!bossBrain->GetWalkSideClip().empty()) moveSideClip = bossBrain->GetWalkSideClip();
					if (!bossBrain->GetHitClip().empty()) hitClip = bossBrain->GetHitClip();
					if (fatalGroggyClipReady && !groggyAttackedClip.empty())
						groggyClip = groggyAttackedClip;
					else if (groggyClip.empty() && !groggyAttackedClip.empty())
						groggyClip = groggyAttackedClip;
					if (!bossBrain->GetGroggyRecoverClip().empty()) groggyRecoverClip = bossBrain->GetGroggyRecoverClip();
					if (!bossBrain->GetDieClip().empty()) deadClip = bossBrain->GetDieClip();
				}

				const bool chargeActiveNow = chargeActive;
				if (!chargeActiveNow)
				{
					animState.chargeEnterActive = false;
					animState.chargeEnterTimer = 0.0f;
					animState.chargeEnterDurationSec = 0.0f;
				}
				else if (!animState.chargeActivePrev)
				{
					animState.chargeEnterActive = !cfg.chargeEnterClip.empty();
					animState.chargeEnterTimer = 0.0f;
					if (animState.chargeEnterActive)
					{
						const float duration = GetClipDurationSecByName(registry, world, entityId, cfg.chargeEnterClip);
						animState.chargeEnterDurationSec = (duration > 0.0f) ? duration : 0.2f;
					}
				}
				animState.chargeActivePrev = chargeActiveNow;

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
				const bool exitingGuard = !suppressGuardExit && (prev == Combat::ActionState::Guard
					&& curr != Combat::ActionState::Guard
					&& (curr == Combat::ActionState::Idle || curr == Combat::ActionState::Move));
				float guardEnterAnimDurationSec = cfg.guardEnterDurationSec;
				if (!cfg.guardEnterClip.empty())
				{
					const float clipDuration = GetClipDurationSecByName(registry, world, entityId, cfg.guardEnterClip);
					if (clipDuration > 0.0f)
						guardEnterAnimDurationSec = clipDuration;
				}
				if (!forceGuardLoopOnly
					&& (enteringGuard || guardEnterPulse)
					&& !cfg.guardEnterClip.empty()
					&& guardEnterAnimDurationSec > 0.0f)
				{
					animState.guardEnterActive = true;
					animState.guardEnterTimer = 0.0f;
					animState.guardEnterAnimDurationSec = guardEnterAnimDurationSec;
				}
				float guardExitAnimDurationSec = cfg.guardExitDurationSec;
				if (!cfg.guardExitClip.empty())
				{
					const float clipDuration = GetClipDurationSecByName(registry, world, entityId, cfg.guardExitClip);
					if (clipDuration > 0.0f)
						guardExitAnimDurationSec = clipDuration;
				}
				if ((exitingGuard || guardExitPulse)
					&& !cfg.guardExitClip.empty()
					&& guardExitAnimDurationSec > 0.0f)
				{
					animState.guardExitActive = true;
					animState.guardExitTimer = 0.0f;
					animState.guardExitAnimDurationSec = guardExitAnimDurationSec;
					if (guardExitPulse)
					{
						animState.guardEnterActive = false;
						animState.guardEnterAnimDurationSec = 0.0f;
					}
				}
				const bool exitingGroggy = (prev == Combat::ActionState::Groggy && curr != Combat::ActionState::Groggy);
				const bool useExitGroggyRecover = (entityId != bossId);
				if (useExitGroggyRecover && exitingGroggy && !fatalActive && !groggyRecoverClip.empty())
				{
					animState.groggyRecoverActive = true;
					animState.groggyRecoverTimer = 0.0f;
					animState.groggyRecoverClip = groggyRecoverClip;
					float duration = GetClipDurationSecByName(registry, world, entityId, groggyRecoverClip);
					if (duration <= 0.0f)
						duration = 0.4f;
					animState.groggyRecoverDurationSec = duration;
				}
				if (animState.groggyRecoverActive)
				{
					animState.groggyRecoverTimer += animDt;
					if (animState.groggyRecoverDurationSec > 0.0f
						&& animState.groggyRecoverTimer >= animState.groggyRecoverDurationSec)
					{
						animState.groggyRecoverActive = false;
					}
				}
				if (curr == Combat::ActionState::Attack || curr == Combat::ActionState::Dodge
					|| curr == Combat::ActionState::Hitstun || curr == Combat::ActionState::Groggy
					|| curr == Combat::ActionState::GuardBreakWeak || curr == Combat::ActionState::Dead
					|| curr == Combat::ActionState::Interaction || curr == Combat::ActionState::HealEnter
					|| curr == Combat::ActionState::HealLoop || curr == Combat::ActionState::HealExit)
				{
					animState.guardEnterActive = false;
					animState.guardExitActive = false;
					animState.guardEnterAnimDurationSec = 0.0f;
					animState.guardExitAnimDurationSec = 0.0f;
					animState.groggyRecoverActive = false;
				}

				std::string clipName;
				bool loop = false;
				if (entityId == bossId && fatalGroggyClipReady && bossBrain)
				{
					const std::string& groggyAttacked = bossBrain->GetGroggyClip();
					if (!groggyAttacked.empty())
					{
						clipName = groggyAttacked;
						loop = false;
					}
				}
				if (clipName.empty() && entityId == bossId && hitReactActive
					&& curr != Combat::ActionState::Attack
					&& curr != Combat::ActionState::Groggy
					&& curr != Combat::ActionState::Dead)
				{
					clipName = hitClip;
					loop = false;
				}
				if (clipName.empty() && forceGroggyRecoverClip && !groggyRecoverClip.empty())
				{
					clipName = groggyRecoverClip;
					loop = false;
				}
				if (chargeActive)
				{
					if (animState.chargeEnterActive && !cfg.chargeEnterClip.empty())
					{
						clipName = cfg.chargeEnterClip;
						loop = false;
					}
					else
					{
						const std::string chargeClip = !cfg.chargeLoopClip.empty()
							? cfg.chargeLoopClip
							: (!cfg.guardEnterClip.empty() ? cfg.guardEnterClip : cfg.guardLoopClip);
						if (!chargeClip.empty())
						{
							clipName = chargeClip;
							loop = true;
						}
					}
				}
				if (clipName.empty() && curr == Combat::ActionState::Attack)
				{
					clipName = animState.attackClip.empty()
						? resolveClipByType(AttackDriverNotifyType::Attack)
						: animState.attackClip;
				}
				else if (clipName.empty() && curr == Combat::ActionState::Dodge)
				{
					clipName = cfg.dodgeClip.empty()
						? resolveClipByType(AttackDriverNotifyType::Dodge)
						: cfg.dodgeClip;
				}
				else if (clipName.empty() && curr == Combat::ActionState::Hitstun)
				{
					clipName = hitClip;
					loop = false;
				}
				else if (clipName.empty() && curr == Combat::ActionState::GuardBreakWeak)
				{
					clipName = cfg.guardBreakClip;
					loop = false;
				}
				else if (clipName.empty() && curr == Combat::ActionState::Groggy)
				{
					clipName = !groggyClip.empty() ? groggyClip : idleClip;
					loop = true;
				}
				else if (clipName.empty() && curr == Combat::ActionState::Dead)
				{
					clipName = deadClip;
					loop = false;
				}
				else if (clipName.empty() && curr == Combat::ActionState::Interaction)
				{
					clipName = cfg.interactionClip;
					loop = false;
				}
				else if (clipName.empty() && curr == Combat::ActionState::HealEnter)
				{
					clipName = cfg.interactionClip;
					loop = false;
				}
				else if (clipName.empty() && curr == Combat::ActionState::HealLoop)
				{
					clipName = !cfg.healLoopClip.empty() ? cfg.healLoopClip : cfg.interactionClip;
					loop = true;
				}
				else if (clipName.empty() && curr == Combat::ActionState::HealExit)
				{
					clipName = cfg.interactionClip;
					loop = false;
				}
				else if (clipName.empty() && animState.groggyRecoverActive)
				{
					clipName = animState.groggyRecoverClip;
					loop = false;
				}
				else if (clipName.empty() && animState.guardExitActive)
				{
					clipName = cfg.guardExitClip;
					loop = false;
				}
				else if (clipName.empty() && (curr == Combat::ActionState::Guard))
				{
					const std::string guardLoop = !cfg.guardLoopClip.empty()
						? cfg.guardLoopClip
						: resolveClipByType(AttackDriverNotifyType::Guard);
					if (forceGuardLoopOnly)
					{
						animState.guardEnterActive = false;
						animState.guardEnterTimer = 0.0f;
						animState.guardEnterAnimDurationSec = 0.0f;
						clipName = guardLoop;
						loop = true;
					}
					else
					{
						const bool parryWindowActive = driver && driver->parryActive;
						if (parryWindowActive && !cfg.guardEnterClip.empty())
						{
							clipName = cfg.guardEnterClip;
							loop = false;
						}
						else if (animState.guardEnterActive)
						{
							clipName = cfg.guardEnterClip;
							loop = false;
						}
						else
						{
							clipName = guardLoop;
							const bool guardHeld = driver ? driver->guardInputHeld : false;
							loop = (curr == Combat::ActionState::Guard) && guardHeld;
						}
					}
				}

				const bool wantsOverride = !clipName.empty()
					&& (curr == Combat::ActionState::Attack
						|| curr == Combat::ActionState::Dodge
						|| curr == Combat::ActionState::Hitstun
						|| curr == Combat::ActionState::Groggy
						|| curr == Combat::ActionState::GuardBreakWeak
						|| curr == Combat::ActionState::Dead
						|| curr == Combat::ActionState::Interaction
						|| curr == Combat::ActionState::HealEnter
						|| curr == Combat::ActionState::HealLoop
						|| curr == Combat::ActionState::HealExit
						|| curr == Combat::ActionState::Guard
						|| animState.guardExitActive
						|| animState.groggyRecoverActive
						|| chargeActive
						|| hitReactActive);
				const bool isDashClip = (entityId == bossId)
					&& (clipName.find("Dash_Attack") != std::string::npos)
					&& (curr == Combat::ActionState::Attack || curr == Combat::ActionState::Groggy);
				bool dashPhaseChanged = false;
				if (!isDashClip)
				{
					animState.dashActive = false;
					animState.dashReverse = false;
					animState.dashTimer = 0.0f;
					animState.dashForwardSec = 0.0f;
					animState.dashReverseSec = 0.0f;
					animState.dashClipName.clear();
				}
				else
				{
					float dashDuration = GetClipDurationSecByName(registry, world, entityId, clipName);
					if (dashDuration <= 0.0f)
						dashDuration = 0.5f;
					if (!animState.dashActive || animState.dashClipName != clipName || prev != curr)
					{
						animState.dashActive = true;
						animState.dashReverse = false;
						animState.dashTimer = 0.0f;
						animState.dashForwardSec = dashDuration;
						animState.dashReverseSec = dashDuration;
						animState.dashClipName = clipName;
						dashPhaseChanged = true;
					}
					else
					{
						animState.dashTimer += animDt;
						if (!animState.dashReverse && animState.dashTimer >= animState.dashForwardSec)
						{
							animState.dashReverse = true;
							animState.dashTimer = 0.0f;
							dashPhaseChanged = true;
						}
						else if (animState.dashReverse && animState.dashTimer >= animState.dashReverseSec)
						{
							if (curr == Combat::ActionState::Groggy)
							{
								animState.dashReverse = false;
								animState.dashTimer = 0.0f;
								dashPhaseChanged = true;
							}
							else
							{
								animState.dashActive = false;
							}
						}
					}
				}

				std::string moveClipResolved = moveClip;
				if (entityId == bossId && bossBrain)
				{
					if (bossBrain->GetBrainState() == C_BossBrainComponent::BrainState::Orbit && !moveSideClip.empty())
						moveClipResolved = moveSideClip;
				}

				const bool isLocomotion = (curr == Combat::ActionState::Idle || curr == Combat::ActionState::Move);
				AdvancedAnimLayer locomotionBase{};
				if (isLocomotion && !idleClip.empty())
				{
					const float targetBlend = (curr == Combat::ActionState::Move && !moveClipResolved.empty()) ? 1.0f : 0.0f;
					moveBlend = SmoothApproach(moveBlend, targetBlend, m_moveBlendSpeed, animDt);

					locomotionBase.autoAdvance = true;
					locomotionBase.clipA = idleClip;
					locomotionBase.clipB = moveClipResolved.empty() ? idleClip : moveClipResolved;
					locomotionBase.loopA = true;
					locomotionBase.loopB = true;
					locomotionBase.speedA = 1.0f;
					locomotionBase.speedB = 1.0f;
					locomotionBase.blend01 = moveBlend;
					locomotionBase.timeA = 0.0f;
					locomotionBase.timeB = 0.0f;

					if (animState.overrideActive && !wantsOverride)
					{
						animState.savedBase = locomotionBase;
						animState.saved = true;
					}
				}

				if (isLocomotion && !animState.overrideActive && !idleClip.empty())
				{
					anim->base.autoAdvance = true;
					anim->base.clipA = idleClip;
					anim->base.clipB = moveClipResolved.empty() ? idleClip : moveClipResolved;
					anim->base.loopA = true;
					anim->base.loopB = true;
					anim->base.speedA = 1.0f;
					anim->base.speedB = 1.0f;
					anim->base.blend01 = moveBlend;
				}

				float attackSpeedScale = 1.0f;
				if (entityId == playerId && curr == Combat::ActionState::Attack
					&& m_state->playerLastAttackHeavy && m_state->playerLastAttackChargeLevel > 0)
				{
					const bool heavyClipMatch =
						(!cfg.heavyAttackClipA.empty() && clipName == cfg.heavyAttackClipA)
						|| (!cfg.heavyAttackClipB.empty() && clipName == cfg.heavyAttackClipB);
					if (heavyClipMatch)
						attackSpeedScale = std::max(0.0f, m_chargeCombo2Speed);
				}
				if (entityId == playerId)
					m_state->playerAttackSpeedScale = attackSpeedScale;
				else if (entityId == bossId)
					m_state->bossAttackSpeedScale = attackSpeedScale;

				float overrideSpeed = 1.0f;
				if (attackSpeedScale != 1.0f)
					overrideSpeed = attackSpeedScale;
				float reverseStartTime = 0.0f;
				bool wantsReverse = false;
				if (animState.guardExitActive && !cfg.guardExitClip.empty())
				{
					const float guardExitDuration = (animState.guardExitAnimDurationSec > 0.0f)
						? animState.guardExitAnimDurationSec
						: cfg.guardExitDurationSec;
					if (guardExitDuration > 0.0f)
					{
					wantsReverse = true;
						reverseStartTime = guardExitDuration;
					}
				}
				else if (curr == Combat::ActionState::HealExit && !cfg.interactionClip.empty())
				{
					wantsReverse = true;
					reverseStartTime = GetClipDurationSecByName(registry, world, entityId, cfg.interactionClip);
					if (reverseStartTime <= 0.0f)
						reverseStartTime = 0.5f;
				}
				else if (isDashClip && animState.dashActive && animState.dashReverse)
				{
					wantsReverse = true;
					reverseStartTime = std::max(0.0f, animState.dashForwardSec);
				}
				if (wantsReverse && overrideSpeed > 0.0f)
					overrideSpeed = -overrideSpeed;
				float blendSec = (entityId == bossId)
					? std::max(0.0f, m_bossAnimBlendSec)
					: std::max(0.0f, m_animBlendSec);
				const bool bossGroggyEnterBlendLock = (entityId == bossId)
					&& (curr == Combat::ActionState::Groggy)
					&& (m_state->bossGroggyEnterBlendBlockSec > 0.0f);
				if (bossGroggyEnterBlendLock)
				{
					blendSec = std::max(blendSec, std::max(0.0f, m_bossGroggyEnterBlendSec));
				}
				const bool bossGroggyRecoverBlendLock = (entityId == bossId)
					&& forceGroggyRecoverClip;
				if (bossGroggyRecoverBlendLock)
				{
					blendSec = std::max(blendSec, std::max(0.0f, m_bossGroggyRecoverBlendSec));
				}

				auto BeginBlendToOverride = [&](const std::string& nextClip, bool nextLoop, float startTime) {
					if (!animState.overrideActive)
					{
						animState.savedBase = anim->base;
						animState.saved = true;
					}

					animState.overrideActive = true;
					animState.overrideClip = nextClip;
					animState.overrideLoop = nextLoop;

					if (blendSec <= 0.0f)
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

					if (blendSec <= 0.0f)
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
					if (!animState.blending || blendSec <= 0.0f)
						return;

					animState.blendTimer += animDt;
					float alpha = animState.blendTimer / blendSec;
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

				const bool attackEnded = (prev == Combat::ActionState::Attack
					&& curr != Combat::ActionState::Attack);
				const bool avoidRootMotionBlendBack = animState.rootMotionUnlockDefault
					&& animState.rootMotionDriveCctDefault;
				if (wantsOverride)
				{
					const bool clipChanged = !animState.overrideActive
						|| animState.overrideClip != clipName
						|| animState.overrideLoop != loop
						|| (attackRestartPulse && curr == Combat::ActionState::Attack)
						|| dashPhaseChanged;
					if (clipChanged)
					{
						const float startTime = wantsReverse ? reverseStartTime : 0.0f;
						BeginBlendToOverride(clipName, loop, startTime);
					}
				}
				else if (animState.overrideActive)
				{
					const bool blendOnAttackEnd = (entityId == bossId);
					if (attackEnded && !blendOnAttackEnd)
					{
						if (blendIdleOnAttackEnd
							&& !avoidRootMotionBlendBack)
						{
							if (!animState.blending || animState.blendingToOverride)
								BeginBlendToSaved();
						}
						else
						{
							if (animState.saved)
							{
								anim->base = animState.savedBase;
								anim->base.timeA = 0.0f;
								anim->base.timeB = 0.0f;
							}
							animState.overrideActive = false;
							animState.overrideClip.clear();
							animState.saved = false;
							animState.blending = false;
							animState.blendingToOverride = false;
							animState.blendTimer = 0.0f;
						}
					}
					else
					{
						if (!animState.blending || animState.blendingToOverride)
							BeginBlendToSaved();
					}
				}

				StepBlend();
				if (animState.overrideActive && !animState.blending)
				{
					anim->base.speedA = overrideSpeed;
					anim->base.speedB = overrideSpeed;
				}
				const float timerStep = animDt * std::abs(overrideSpeed);
				if (animState.guardEnterActive)
				{
					animState.guardEnterTimer += timerStep;
					const float guardEnterDuration = (animState.guardEnterAnimDurationSec > 0.0f)
						? animState.guardEnterAnimDurationSec
						: cfg.guardEnterDurationSec;
					if (guardEnterDuration > 0.0f && animState.guardEnterTimer >= guardEnterDuration)
					{
						animState.guardEnterActive = false;
						animState.guardEnterAnimDurationSec = 0.0f;
					}
				}
				if (animState.guardExitActive)
				{
					animState.guardExitTimer += timerStep;
					const float guardExitDuration = (animState.guardExitAnimDurationSec > 0.0f)
						? animState.guardExitAnimDurationSec
						: cfg.guardExitDurationSec;
					if (guardExitDuration > 0.0f && animState.guardExitTimer >= guardExitDuration)
					{
						animState.guardExitActive = false;
						animState.guardExitAnimDurationSec = 0.0f;
					}
				}
				if (animState.chargeEnterActive)
				{
					animState.chargeEnterTimer += timerStep;
					if (animState.chargeEnterDurationSec > 0.0f
						&& animState.chargeEnterTimer >= animState.chargeEnterDurationSec)
					{
						animState.chargeEnterActive = false;
					}
				}
				prev = curr;
			};

		const bool playerHowlingGuardActive = m_phaseHowlingForceGuard
			&& (m_state->playerHowlingGuardLockSec > 0.0f);
		const bool playerHowlingGuardJustEnded = m_state->playerHowlingGuardActivePrev
			&& !playerHowlingGuardActive;
		const bool suppressPlayerGuardExitAnim = playerHowlingGuardActive || playerHowlingGuardJustEnded;
		const bool forcePlayerGuardLoopOnly = playerHowlingGuardActive;

		const bool playerGuardEnterPulse = playerGuardPressed;
		ApplyAnimByState(playerId, playerIntent, outPlayer.state, m_state->prevPlayerState, m_state->playerAnim, m_state->playerMoveBlend,
			playerGuardEnterPulse, false, m_state->playerChargeActive, outPlayer.attackRestarted, playerAttackEndedOnFinal, false, suppressPlayerGuardExitAnim, forcePlayerGuardLoopOnly, false);
		ApplyAnimByState(bossId, bossIntentCompat, outBoss.state, m_state->prevBossState, m_state->bossAnim, m_state->bossMoveBlend,
			false, false, m_state->bossChargeActive, outBoss.attackRestarted, false, bossOut.hitReactActive, false, false, bossOut.groggyRecoverActive);
		m_state->playerHowlingGuardActivePrev = playerHowlingGuardActive;

		auto ApplyHitstopVelocityStop = [&](EntityId entityId, float timerSec)
			{
				if (timerSec <= 0.0f)
					return;
				if (auto* cct = world.GetComponent<Phy_CCTComponent>(entityId))
				{
					cct->desiredVelocity.x = 0.0f;
					cct->desiredVelocity.y = 0.0f;
					cct->desiredVelocity.z = 0.0f;
				}
			};
		ApplyHitstopVelocityStop(playerId, m_state->playerHitstopTimer);
		ApplyHitstopVelocityStop(bossId, m_state->bossHitstopTimer);

		auto ApplyHitstopToAnim = [&](EntityId entityId, float timerSec)
			{
				if (timerSec <= 0.0f)
					return;
				if (auto* anim = world.GetComponent<AdvancedAnimationComponent>(entityId))
				{
					anim->base.speedA = 0.0f;
					anim->base.speedB = 0.0f;
					anim->upper.speedA = 0.0f;
					anim->upper.speedB = 0.0f;
					anim->additive.speed = 0.0f;
				}
			};

		ApplyHitstopToAnim(playerId, m_state->playerHitstopTimer);
		ApplyHitstopToAnim(bossId, m_state->bossHitstopTimer);
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

		C_BossBrainComponent* bossBrain = nullptr;
		if (auto* script = FindScriptOnEntity(world, bossId, "C_BossBrainComponent"))
		{
			if (auto* brain = dynamic_cast<C_BossBrainComponent*>(script))
				bossBrain = brain;
		}

		Combat::BossSignals nextBossSignals{};
		if (auto* hc = world.GetComponent<HealthComponent>(bossId))
			nextBossSignals.dead = (hc->currentHealth <= 0.0f);
		const bool bossWasAttacking = (m_state->boss.state == Combat::ActionState::Attack);

		m_state->player.id = playerId;
		m_state->player.team = Combat::Team::Player;
		const bool playerSuperArmorResolve = (m_state->player.state == Combat::ActionState::Attack
			&& m_state->playerLastAttackHeavy)
			|| m_state->playerChargeActive;
		m_state->player.canBeHitstunned = m_playerCanBeHitstunned && !playerSuperArmorResolve;
		m_state->boss.id = bossId;
		m_state->boss.team = Combat::Team::Enemy;
		m_state->boss.canBeHitstunned = false;

		auto* registry = SkinnedRegistry();

		bool playerHitstopActive = (m_state->playerHitstopTimer > 0.0f);
		bool bossHitstopActive = (m_state->bossHitstopTimer > 0.0f);
		bool playerHitstopTriggeredThisFrame = false;
		bool bossHitstopTriggeredThisFrame = false;
		const float playerLogicDt = playerHitstopActive ? 0.0f : deltaTime;
		const float bossLogicDt = bossHitstopActive ? 0.0f : deltaTime;

		m_state->playerParryNoDurabilitySec = std::max(0.0f, m_state->playerParryNoDurabilitySec - playerLogicDt);
		m_state->bossParryNoDurabilitySec = std::max(0.0f, m_state->bossParryNoDurabilitySec - bossLogicDt);
		m_state->playerGuardExitLockSec = std::max(0.0f, m_state->playerGuardExitLockSec - playerLogicDt);
		m_state->bossGuardExitLockSec = std::max(0.0f, m_state->bossGuardExitLockSec - bossLogicDt);
		m_state->playerHowlingGuardLockSec = std::max(0.0f, m_state->playerHowlingGuardLockSec - playerLogicDt);

		auto BuildResolveSnapshot = [&](Combat::Fighter& fighter,
			Combat::ActionState state,
			const Combat::Sensors& sensors,
			bool hitstopActive,
			bool guardEnterPhaseActive,
			bool forceNoInterrupt) -> Combat::FighterSnapshot
			{
				Combat::FighterSnapshot snap = fighter.Snapshot();
				snap.hp = sensors.hp;
				snap.weaponDurability = sensors.weaponDurability;
				snap.weaponDurabilityMax = sensors.weaponDurabilityMax;
				snap.weakActive = sensors.weakActive;
				snap.targetInFront = sensors.targetInFront;
				snap.canBeHitstunned = fighter.canBeHitstunned;

				Combat::ActionFlags flags = snap.flags;
				flags.hitActive = (state == Combat::ActionState::Attack) && sensors.attackWindowActive;
				const bool weakActive = sensors.weakActive;
				const bool inGuard = (state == Combat::ActionState::Guard) && !weakActive;
				const bool parryPhase = inGuard && guardEnterPhaseActive;
				flags.guardActive = inGuard && !parryPhase && sensors.guardWindowActive;
				flags.invulnActive = sensors.dodgeWindowActive || sensors.invulnActive;
				// GuardEnter animation phase is treated as full parry window.
				flags.parryWindowActive = parryPhase;
				flags.canBeInterrupted = (state != Combat::ActionState::Dodge)
					&& (state != Combat::ActionState::Dead)
					&& (state != Combat::ActionState::Groggy)
					&& (state != Combat::ActionState::GuardBreakWeak);
				if (forceNoInterrupt)
					flags.canBeInterrupted = false;
				if (!sensors.attackCancelable)
					flags.canBeInterrupted = false;

				if (state == Combat::ActionState::Interaction)
				{
					flags.hitActive = false;
					flags.guardActive = false;
					flags.parryWindowActive = false;
					flags.invulnActive = true;
					flags.canBeInterrupted = false;
				}
				else if (state == Combat::ActionState::HealEnter
					|| state == Combat::ActionState::HealLoop
					|| state == Combat::ActionState::HealExit)
				{
					flags.hitActive = false;
					flags.guardActive = false;
					flags.parryWindowActive = false;
				}

				if (state == Combat::ActionState::Hitstun)
					flags.canBeInterrupted = false;
				if (state == Combat::ActionState::Groggy)
					flags.canBeInterrupted = false;

				if (hitstopActive)
				{
					// Preserve guard/parry flags while time is frozen.
					flags.guardActive = flags.guardActive || snap.flags.guardActive;
					flags.parryWindowActive = flags.parryWindowActive || snap.flags.parryWindowActive;
				}

				// Enforce sequential phase separation even when preserving frozen flags.
				if (!inGuard)
				{
					flags.guardActive = false;
					flags.parryWindowActive = false;
				}
				else if (parryPhase)
				{
					flags.guardActive = false;
				}
				else
				{
					flags.parryWindowActive = false;
				}

				snap.flags = flags;
				return snap;
			};

		Combat::Sensors resolvePlayerSensors = m_state->player.BuildSensors(world, bossId, deltaTime);
		Combat::Sensors resolveBossSensors = m_state->boss.BuildSensors(world, playerId, deltaTime);

		{
			auto RecomputeTargetInFront = [&](EntityId selfId,
				EntityId targetId,
				Combat::Sensors& s,
				Combat::Fighter& fighter)
				{
					auto* selfTr = world.GetComponent<TransformComponent>(selfId);
					auto* targetTr = world.GetComponent<TransformComponent>(targetId);
					if (!selfTr || !targetTr)
						return;

					const float dx = targetTr->position.x - selfTr->position.x;
					const float dz = targetTr->position.z - selfTr->position.z;
					const float dist = std::sqrt(dx * dx + dz * dz);
					if (dist <= 0.0001f)
						return;

					const float offsetRad = m_rotationOffsetDeg * 0.01745329252f;
					const float yawRad = selfTr->rotation.y - offsetRad;
					const float fx = std::sin(yawRad);
					const float fz = std::cos(yawRad);
					const float tx = dx / dist;
					const float tz = dz / dist;
					const float dot = fx * tx + fz * tz;
					s.targetInFront = (dot >= 0.0f);
					fighter.lastTargetInFront = s.targetInFront;
				};

			RecomputeTargetInFront(playerId, bossId, resolvePlayerSensors, m_state->player);
			RecomputeTargetInFront(bossId, playerId, resolveBossSensors, m_state->boss);
		}
		m_state->playerSnapshot = BuildResolveSnapshot(
			m_state->player,
			m_state->player.state,
			resolvePlayerSensors,
			playerHitstopActive,
			m_state->playerAnim.guardEnterActive,
			false);
		m_state->bossSnapshot = BuildResolveSnapshot(
			m_state->boss,
			m_state->boss.state,
			resolveBossSensors,
			bossHitstopActive,
			m_state->bossAnim.guardEnterActive,
			true);

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
		auto ChargeScale = [&](int level) -> float
			{
				switch (level)
				{
				case 1: return std::max(0.0f, m_chargeScale1);
				case 2: return std::max(0.0f, m_chargeScale2);
				case 3: return std::max(0.0f, m_chargeScale3);
				default: return std::max(0.0f, m_chargeScale0);
				}
			};

		const float hitstopSec = std::max(0.0f, m_hitstopSec);
		auto ApplyHitstopTimer = [&](EntityId entityId)
			{
				if (hitstopSec <= 0.0f)
					return;
				if (entityId == playerId)
				{
					if (m_state->playerHitstopTimer <= 0.0f)
					{
						m_state->playerHitstopTimer = hitstopSec;
						playerHitstopActive = true;
						playerHitstopTriggeredThisFrame = true;
						m_state->playerGuardHeldAtHitstop = (m_state->player.state == Combat::ActionState::Guard)
							|| m_state->playerSnapshot.flags.guardActive
							|| m_state->playerSnapshot.flags.parryWindowActive;
					}
				}
				else if (entityId == bossId)
				{
					if (m_state->bossHitstopTimer <= 0.0f)
					{
						m_state->bossHitstopTimer = hitstopSec;
						bossHitstopActive = true;
						bossHitstopTriggeredThisFrame = true;
						m_state->bossGuardHeldAtHitstop = (m_state->boss.state == Combat::ActionState::Guard)
							|| m_state->bossSnapshot.flags.guardActive
							|| m_state->bossSnapshot.flags.parryWindowActive;
					}
				}
			};

		for (auto hit : m_state->bus.Hits())
		{
			const bool attackerHitstop = (hit.attackerOwner == playerId)
				? playerHitstopActive
				: (hit.attackerOwner == bossId) ? bossHitstopActive : false;
			if (attackerHitstop)
				continue;

			auto itParry = m_state->parryResolvedByVictim.find(hit.victimOwner);
			if (itParry != m_state->parryResolvedByVictim.end())
			{
				const auto& key = itParry->second;
				if (key.attacker == hit.attackerOwner && key.attackInstanceId == hit.attackInstanceId)
					continue;
			}

			const Combat::FighterSnapshot& playerSnap = m_state->playerSnapshot;
			const Combat::FighterSnapshot& bossSnap = m_state->bossSnapshot;
			Combat::FighterSnapshot attacker = (hit.attackerOwner == playerId) ? playerSnap : bossSnap;
			Combat::FighterSnapshot victim = (hit.victimOwner == playerId) ? playerSnap : bossSnap;

			float chargeScale = 1.0f;
			if (hit.attackerOwner == playerId && m_state->playerLastAttackHeavy)
			{
				chargeScale = ChargeScale(m_state->playerLastAttackChargeLevel);
				if (chargeScale != 1.0f && chargeScale > 0.0f)
					hit.damage *= chargeScale;
			}

			auto resolvedDetail = m_state->resolver.ResolveOneDetailed(hit, attacker, victim);
			auto resolved = resolvedDetail.output;
			const Combat::ResolveResult resolveResult = resolvedDetail.result;

			// SFX/VFX hook (resolved hit result):
			// - Use hit.hitPosWS / hit.hitNormalWS for impact location and orientation.
			// - Use resolveResult to branch: Parry, Guard, GuardBreak, Hit.
			// - Good place to fire: parry spark + clang, guard block spark, hit blood, guard-break burst.
			// - If you need attacker/weapon position, fetch sockets from AdvancedAnimationComponent here.
			// - For footsteps/attack whoosh, use anim notifies (AdvancedAnimationComponent::AddNotify).

			// Notify external scripts about resolve result
			if (OnCombatResolved.IsBound() && resolveResult != Combat::ResolveResult::None)
			{
				OnCombatResolved.Execute(hit.victimOwner, hit.attackerOwner, 
					static_cast<std::uint8_t>(resolveResult), hit.damage, hit.hitPosWS);
			}

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
			std::vector<Combat::Command> immediate = resolved.immediate;
			const bool parrySuccess = (resolveResult == Combat::ResolveResult::Parry);
			const bool wasGuarded = (resolveResult == Combat::ResolveResult::Guard);
			const bool wasGuardBreak = (resolveResult == Combat::ResolveResult::GuardBreak);
			const bool wasHit = (resolveResult == Combat::ResolveResult::Hit);
			if (wasHit && hit.victimOwner == bossId)
			{
				nextBossSignals.hitThisFrame = true;
				nextBossSignals.wasAttacking = nextBossSignals.wasAttacking || bossWasAttacking;
				nextBossSignals.hitstopSec = hitstopSec;
			}

			if (bossBrain && hit.attackerOwner == bossId && hit.victimOwner == playerId)
			{
				if (parrySuccess || wasHit)
					bossBrain->NotifyAttackOutcome(false);
				else if (wasGuarded || wasGuardBreak)
					bossBrain->NotifyAttackOutcome(true);
			}

			if (parrySuccess || wasGuarded || wasGuardBreak || wasHit)
				ApplyHitstopTimer(hit.attackerOwner);

			// Victim hitstop disabled: only the attacker receives hitstop (counter-stiff).

			const bool shouldStopTrace = parrySuccess || wasGuarded || wasGuardBreak || wasHit;
			if (shouldStopTrace)
			{
				immediate.push_back({ Combat::CommandType::DisableTrace,
					Combat::CmdDisableTrace{ hit.attackerOwner } });
			}

			float* parryNoDurability = nullptr;
			if (hit.victimOwner == playerId)
				parryNoDurability = &m_state->playerParryNoDurabilitySec;
			else if (hit.victimOwner == bossId)
				parryNoDurability = &m_state->bossParryNoDurabilitySec;

			if (parrySuccess)
			{
				m_state->parryResolvedByVictim[hit.victimOwner] = { hit.attackerOwner, hit.attackInstanceId };
				const float lockSec = (hit.parryLockSec > 0.0f) ? hit.parryLockSec : m_parryNoDurabilitySec;
				if (parryNoDurability && lockSec > 0.0f)
					*parryNoDurability = std::max(*parryNoDurability, lockSec);
			}

			const bool blockDurability = parrySuccess || (parryNoDurability && *parryNoDurability > 0.0f);
			if (blockDurability)
			{
				immediate.erase(std::remove_if(immediate.begin(), immediate.end(),
					[](const Combat::Command& cmd)
					{
						return cmd.type == Combat::CommandType::ConsumeWeaponDurability;
					}),
					immediate.end());
			}
			const bool suppressHitstun = (hit.victimOwner == playerId && !victim.canBeHitstunned);
			float hitAnimDuration = 0.0f;
			if (wasHit && !suppressHitstun)
			{
				const AnimConfig hitCfg = BuildAnimConfig(hit.victimOwner, playerId, bossId);
				if (!hitCfg.hitClip.empty())
					hitAnimDuration = GetClipDurationSecByName(registry, world, hit.victimOwner, hitCfg.hitClip);
			}
			if (wasHit && !suppressHitstun)
			{
				const float hitDuration = (hitAnimDuration > 0.0f) ? hitAnimDuration : 0.4f;
				if (hit.victimOwner == playerId)
					m_state->playerHitstunDurationSec = std::max(m_state->playerHitstunDurationSec, hitDuration);
				else if (hit.victimOwner == bossId)
					m_state->bossHitstunDurationSec = std::max(m_state->bossHitstunDurationSec, hitDuration);
			}
			const float hitstopDelaySec = std::max(0.0f, m_hitstopSec);
			if (hitstopDelaySec > 0.0f)
			{
				for (size_t i = 0; i < immediate.size();)
				{
					if (immediate[i].type == Combat::CommandType::ApplyDamage)
					{
						const auto payload = std::get<Combat::CmdApplyDamage>(immediate[i].payload);
						if (payload.target == playerId || payload.target == bossId)
						{
							m_state->pendingImmediate.push_back({ immediate[i], hitstopDelaySec });
							immediate[i] = immediate.back();
							immediate.pop_back();
							continue;
						}
					}
					++i;
				}
			}
			const float basePushSpeed = hit.guardBreakPushbackSpeed;
			const float basePushDuration = hit.guardBreakPushbackDuration;
			if (wasGuarded && basePushSpeed > 0.0f && basePushDuration > 0.0f)
			{
				const float scale = std::max(0.0f, m_guardSuccessPushbackScale);
				const float speed = basePushSpeed * scale;
				if (speed > 0.0f)
				{
					immediate.push_back({ Combat::CommandType::ApplyPushback,
						Combat::CmdApplyPushback{ hit.attackerOwner, hit.victimOwner, speed, basePushDuration } });
				}
			}
			if (wasHit && !suppressHitstun && basePushSpeed > 0.0f)
			{
				const float scale = std::max(0.0f, m_hitPushbackScale);
				const float speed = basePushSpeed * scale;
				const float pushDuration = std::max(0.0f, m_hitPushbackDurationSec);
				if (speed > 0.0f && pushDuration > 0.0f)
				{
					immediate.push_back({ Combat::CommandType::ApplyPushback,
						Combat::CmdApplyPushback{ hit.attackerOwner, hit.victimOwner, speed, pushDuration } });
				}
			}
			if (wasHit)
			{
				const float invulnSec = std::max(0.0f, m_hitPushbackDurationSec);
				if (invulnSec > 0.0f)
				{
					if (auto* hc = world.GetComponent<HealthComponent>(hit.victimOwner))
						hc->invulnRemaining = std::max(hc->invulnRemaining, invulnSec);
				}
			}
			if (parrySuccess)
			{
				if (auto* hc = world.GetComponent<HealthComponent>(hit.victimOwner))
				{
					hc->pushbackRemainingSec = 0.0f;
					hc->pushbackSpeed = 0.0f;
					hc->pushbackDir = { 0.0f, 0.0f, 0.0f };
				}
				immediate.erase(std::remove_if(immediate.begin(), immediate.end(),
					[](const Combat::Command& cmd)
					{
						return cmd.type == Combat::CommandType::ApplyPushback
							|| cmd.type == Combat::CommandType::ApplyPushbackToBoth;
					}),
					immediate.end());
			}
			for (auto& cmd : immediate)
			{
				if (cmd.type != Combat::CommandType::ApplyPushbackToBoth)
					continue;
				auto& payload = std::get<Combat::CmdApplyPushbackToBoth>(cmd.payload);
				const float scale = std::max(0.0f, m_guardBreakPushbackScale);
				payload.speed *= scale;
				payload.durationSec = std::max(0.0f, m_guardBreakPushbackDurationSec);
			}
			if (wasGuardBreak)
			{
				const float invulnSec = std::max(0.0f, m_guardBreakPushbackDurationSec);
				if (invulnSec > 0.0f)
				{
					if (auto* hc = world.GetComponent<HealthComponent>(hit.victimOwner))
						hc->invulnRemaining = std::max(hc->invulnRemaining, invulnSec);
				}
			}
			if (bossId != InvalidEntityId)
			{
				immediate.erase(std::remove_if(immediate.begin(), immediate.end(),
					[&](const Combat::Command& cmd)
					{
						if (cmd.type != Combat::CommandType::ForceCancelAttack)
							return false;
						const auto& payload = std::get<Combat::CmdForceCancelAttack>(cmd.payload);
						return payload.target == bossId;
					}),
					immediate.end());
			}
			m_state->apply.ApplyImmediate(world, m_state->fighterMap, m_state->bus, immediate, false);

			if (parrySuccess || wasGuarded || wasGuardBreak)
			{
				if (auto* driver = world.GetComponent<AttackDriverComponent>(hit.victimOwner))
					driver->parryTapCredit = 1;
			}

			if (hit.victimOwner == playerId && m_state->playerChargeActive
				&& HasDeferredEvent(resolved, Combat::CombatEventType::OnHit))
			{
				if (auto* script = FindScriptOnEntity(world, playerId, "C_PlayerInputSourceComponent"))
				{
					if (auto* input = dynamic_cast<C_PlayerInputSourceComponent*>(script))
						input->CancelCharge();
				}
				m_state->playerChargeActive = false;
			}

		for (const auto& ev : resolved.deferred)
		{
			if (suppressHitstun && ev.type == Combat::CombatEventType::OnHit)
				continue;
			if (ev.type == Combat::CombatEventType::OnHit)
			{
				const float invulnSec = std::max(0.0f, m_hitInvulnSec);
				if (invulnSec > 0.0f)
				{
					if (auto* hc = world.GetComponent<HealthComponent>(hit.victimOwner))
						hc->invulnRemaining = std::max(hc->invulnRemaining, invulnSec);
				}
			}
			const bool delayFsm = (ev.type == Combat::CombatEventType::OnHit
				|| ev.type == Combat::CombatEventType::OnGuardBreak);
			if (delayFsm && (ev.subject == playerId || ev.subject == bossId))
			{
				const float delaySec = std::max(0.0f, m_hitstopSec);
				if (delaySec > 0.0f)
				{
					m_state->pendingDeferred.push_back({ ev, delaySec });
					continue;
				}
			}
			m_state->bus.PushDeferred(ev);
		}

			if (!bossGroggyTriggered && hit.victimOwner == bossId && hit.attackerOwner == playerId)
			{
				if (HasDeferredEvent(resolved, Combat::CombatEventType::OnHit))
				{
					if (auto* hc = world.GetComponent<HealthComponent>(bossId))
					{
						if (hc->groggyMax > 0.0f && m_state->boss.state != Combat::ActionState::Groggy)
						{
							if (hc->groggy < hc->groggyMax)
							{
								const bool heavy = m_state->playerLastAttackHeavy;
								float gain = heavy ? m_bossGroggyGainHeavy : m_bossGroggyGainLight;
								bool gainFromDamage = false;
								if (gain <= 0.0f)
								{
									const float gainScale = (hc->groggyGainScale > 0.0f) ? hc->groggyGainScale : 0.0f;
									gain = hit.damage * gainScale;
									gainFromDamage = true;
								}
								if (heavy && !gainFromDamage && chargeScale != 1.0f && chargeScale > 0.0f)
									gain *= chargeScale;
								if (gain > 0.0f)
									hc->groggy = std::min(hc->groggy + gain, hc->groggyMax);
							}

							if (hc->groggy >= hc->groggyMax)
							{
								hc->groggy = hc->groggyMax;
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

		nextBossSignals.groggyTriggered = bossGroggyTriggered;

		if (playerHitstopTriggeredThisFrame || bossHitstopTriggeredThisFrame)
		{
			auto ApplyHitstopVelocityStop = [&](EntityId entityId, float timerSec)
				{
					if (timerSec <= 0.0f)
						return;
					if (auto* cct = world.GetComponent<Phy_CCTComponent>(entityId))
						cct->desiredVelocity = { 0.0f, 0.0f, 0.0f };
				};
			auto ApplyHitstopToAnim = [&](EntityId entityId, float timerSec)
				{
					if (timerSec <= 0.0f)
						return;
					if (auto* anim = world.GetComponent<AdvancedAnimationComponent>(entityId))
					{
						anim->base.speedA = 0.0f;
						anim->base.speedB = 0.0f;
						anim->upper.speedA = 0.0f;
						anim->upper.speedB = 0.0f;
						anim->additive.speed = 0.0f;
					}
				};

			if (playerHitstopTriggeredThisFrame)
			{
				ApplyHitstopVelocityStop(playerId, m_state->playerHitstopTimer);
				ApplyHitstopToAnim(playerId, m_state->playerHitstopTimer);
			}
			if (bossHitstopTriggeredThisFrame)
			{
				ApplyHitstopVelocityStop(bossId, m_state->bossHitstopTimer);
				ApplyHitstopToAnim(bossId, m_state->bossHitstopTimer);
			}
		}

		m_state->bossSignals = nextBossSignals;
	}
}






















