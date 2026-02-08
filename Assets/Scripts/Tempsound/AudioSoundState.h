#pragma once

#include <cstdint>

namespace Alice
{
    /// 보스 공격 상태 (델리게이트: OnBossAttackSfxRequest)
    enum class BossAttackState : std::uint8_t
    {
        None = 0,
        AttackAlarm,
        Attack1,
        Attack2,
        Attack3,
        AttackABC,
        SoulSwordCharge,
        SoulSwordAttack,
        SideAttack,
        DashAttack,
        Count
    };


    /// 보스 움직임 상태 (델리게이트: OnBossMovementSfxRequest)
    enum class BossMovementState : std::uint8_t
    {
        None = 0,
        DashAttack,
        Walk,
        Rotate,
        Count
    };

    /// 보스 나머지 상태 (델리게이트: OnBossOtherSfxRequest)
    enum class BossOtherState : std::uint8_t
    {
        None = 0,
        GroggyEnter,
        Roar,
        Hit,
        Death,
        Count
    };

    /// 플레이어 공격 상태 (델리게이트: OnPlayerAttackSfxRequest)
    enum class PlayerAttackState : std::uint8_t
    {
        None = 0,
        HeavyAttack,
        Attack1,
        Attack2,
        Attack3,
        Guard,
        Parry,
        Count
    };

    /// 플레이어 움직임 상태 (델리게이트: OnPlayerMovementSfxRequest)
    enum class PlayerMovementState : std::uint8_t
    {
        None = 0,
        Roll,
        Run,
        Dash,
        Stop,
        HitRoll,
        Count
    };

    /// 플레이어 나머지 상태 (델리게이트: OnPlayerOtherSfxRequest)
    enum class PlayerOtherState : std::uint8_t
    {
        None = 0,
        GuardBreakAlarm,
        GuardBreak,
        EgoCombine,
        Heal,
        Death,
        Count
    };
}
