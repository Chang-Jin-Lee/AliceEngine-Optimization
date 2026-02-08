#pragma once
/*
* 전투 시스템 전체에서 쓰는 타입·상수 정의. EntityId, Team, ActionState, Intent, Sensors, CombatEvent, Command, FsmOutput, ResolveOutput 등. 스크립트가 아니라 헤더만 있음.
*/
#include <cstdint>
#include <string>
#include <vector>
#include <variant>

#include <DirectXMath.h>

#include "Runtime/ECS/Entity.h"
#include "Runtime/Gameplay/Combat/CombatHitEvent.h"

namespace Alice::Combat
{
    using EntityId = Alice::EntityId;
    static constexpr EntityId InvalidEntityId = Alice::InvalidEntityId;

    struct Vec2
    {
        float x = 0.0f;
        float y = 0.0f;
    };

    using Vec3 = DirectX::XMFLOAT3;

    enum class Team : uint8_t
    {
        Player = 0,
        Enemy = 1,
        Neutral = 2,
    };

    enum class ActionState : uint8_t
    {
        Idle,
        Move,
        Attack,
        Dodge,
        Guard,
        JustGuardSuccess,
        GuardBreakWeak,
        Hitstun,
        Groggy,
        Dead,
        Interaction,
        HealEnter,
        HealLoop,
        HealExit,
    };

    struct ActionFlags
    {
        bool hitActive = false;
        bool guardActive = false;
        bool parryWindowActive = false;
        bool invulnActive = false;
        bool canBeInterrupted = true;
        bool chargeActive = false;
        int chargeLevel = 0;
        int attackComboIndex = 0;
    };

    struct Intent
    {
        Vec2 move{};
        bool attackPressed = false;
        bool guardHeld = false;
        bool dodgePressed = false;
        bool lockOnToggle = false;
        bool lightAttackPressed = false;
        bool heavyAttackPressed = false;
        bool attackHeld = false;
        float attackHeldSec = 0.0f;
        bool guardPressed = false;
        bool guardReleased = false;
        float guardHeldSec = 0.0f;
        float parryTapWindowSec = 0.0f;
        bool itemPressed = false;
        bool itemHeld = false;
        bool itemReleased = false;
        float itemHeldSec = 0.0f;
        bool interactPressed = false;
        bool ragePressed = false;
        bool runHeld = false;
        bool chargeActive = false;
        float chargeHeldSec = 0.0f;
        int chargeLevel = 0;
    };

    struct Sensors
    {
        float dt = 0.0f;
        float hp = 100.0f;
        float stamina = 100.0f;

        bool grounded = true;
        bool blocked = false;

        EntityId targetId = InvalidEntityId;
        float distToTarget = 9999.0f;
        float angleToTargetDeg = 0.0f;
        bool targetInFront = true;

        // Anim/driver windows (source of truth; resolver uses flags derived from these).
        bool attackWindowActive = false;
        bool guardWindowActive = false;
        bool parryWindowActive = false;
        bool dodgeWindowActive = false;
        bool invulnActive = false;
        float attackStateDurationSec = 0.0f;
        bool attackCancelable = true;
        bool canBeHitstunned = true;
        bool guardLockActive = false;
        bool weakActive = false;
        float weakRemainingSec = 0.0f;
        float weaponDurability = 100.0f;
        float weaponDurabilityMax = 100.0f;
        float hitstunDurationSec = 0.0f;
        bool interactAvailable = false;
        bool healAllowed = false;
        float interactionDurationSec = 0.0f;
        float healEnterDurationSec = 0.0f;
        float healExitDurationSec = 0.0f;
        float guardEnterDurationSec = 0.0f;

        float groggyDuration = 1.5f;
        float moveSpeed = 5.0f;

        Vec2 dodgeFallbackDir{};
        bool dodgeFallbackValid = false;
    };

    using HitEvent = Alice::CombatHitEvent;

    enum class CombatEventType : uint8_t
    {
        OnHit,
        OnGuarded,
        OnParrySuccess,
        OnGotParried,
        OnGuardBreak,
        OnGroggy,
        OnDeath
    };

    struct CombatEvent
    {
        CombatEventType type{};
        EntityId subject = InvalidEntityId;
        EntityId other = InvalidEntityId;
        uint32_t attackInstanceId = 0;
        float value = 0.0f;
    };

    enum class CommandType : uint8_t
    {
        ApplyDamage,
        ConsumeStamina,
        ConsumeWeaponDurability,
        EnterHitstun,
        ForceCancelAttack,
        DisableTrace,
        EnableTrace,
        PlayAnim,
        RequestMove,
        StartGuardLock,
        ConsumeParry,
        AddGroggy,
        EnterWeakState,
        ApplyPushback,
        ApplyPushbackToBoth
    };

    struct CmdApplyDamage { EntityId target = InvalidEntityId; float amount = 0.0f; };
    struct CmdConsumeStamina { EntityId target = InvalidEntityId; float amount = 0.0f; };
    struct CmdConsumeWeaponDurability { EntityId target = InvalidEntityId; float amount = 0.0f; };
    struct CmdEnterHitstun { EntityId target = InvalidEntityId; float durationSec = 0.0f; };
    struct CmdForceCancelAttack { EntityId target = InvalidEntityId; };
    struct CmdDisableTrace { EntityId weaponOrOwner = InvalidEntityId; };
    struct CmdEnableTrace
    {
        EntityId weaponOrOwner = InvalidEntityId;
        std::uint32_t traceSlotMask = 0u; // 0 = all slots
        float activeWindowDurationSec = 0.0f;
    };
    struct CmdPlayAnim
    {
        EntityId target = InvalidEntityId;
        std::string clip;
        bool immediate = true;
        bool loop = false;
    };
    struct CmdRequestMove
    {
        EntityId target = InvalidEntityId;
        Vec2 move{};
        float speed = 5.0f;
        bool useCameraRelative = true;
        bool faceMove = true;
    };
    struct CmdStartGuardLock { EntityId target = InvalidEntityId; float durationSec = 0.0f; };
    struct CmdConsumeParry { EntityId target = InvalidEntityId; };
    struct CmdAddGroggy { EntityId target = InvalidEntityId; float amount = 0.0f; };
    struct CmdEnterWeakState { EntityId target = InvalidEntityId; float durationSec = 0.0f; };
    struct CmdApplyPushback
    {
        EntityId attacker = InvalidEntityId;
        EntityId victim = InvalidEntityId;
        float speed = 0.0f;
        float durationSec = 0.0f;
    };
    struct CmdApplyPushbackToBoth
    {
        EntityId attacker = InvalidEntityId;
        EntityId victim = InvalidEntityId;
        float speed = 0.0f;
        float durationSec = 0.0f;
    };

    using CommandPayload = std::variant<
        CmdApplyDamage,
        CmdConsumeStamina,
        CmdConsumeWeaponDurability,
        CmdEnterHitstun,
        CmdForceCancelAttack,
        CmdDisableTrace,
        CmdEnableTrace,
        CmdPlayAnim,
        CmdRequestMove,
        CmdStartGuardLock,
        CmdConsumeParry,
        CmdAddGroggy,
        CmdEnterWeakState,
        CmdApplyPushback,
        CmdApplyPushbackToBoth>;

    struct Command
    {
        CommandType type{};
        CommandPayload payload{};
    };

    struct FsmOutput
    {
        ActionState state = ActionState::Idle;
        ActionFlags flags{};
        std::vector<Command> commands;
        bool attackRestarted = false;
        bool parryRecoverToIdle = false;
    };

    struct ResolveOutput
    {
        std::vector<Command> immediate;
        std::vector<CombatEvent> deferred;
    };
}
