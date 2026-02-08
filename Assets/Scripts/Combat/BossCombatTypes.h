#pragma once
/*
* Boss combat intent/signal/output types for simplified boss session flow.
*/

#include <string>

#include "C_CombatContracts.h"

namespace Alice::Combat
{
    struct BossIntent
    {
        Vec2 move{};
        bool attackRequested = false;
        bool chargeActive = false;
        int chargeLevel = 0;
        bool wantsFaceTarget = false;
    };

    struct BossSignals
    {
        bool hitThisFrame = false;
        float hitstopSec = 0.0f;
        bool wasAttacking = false;
        bool groggyTriggered = false;
        float groggyExtendSec = 0.0f;
        bool groggyHold = false;
        bool dead = false;
    };

    struct BossOutput
    {
        ActionState state = ActionState::Idle;
        ActionFlags flags{};
        BossIntent intent{};
        std::string attackClip{};
        bool wantsFaceTarget = false;
        bool hitstopActive = false;
        bool hitReactActive = false;
        bool groggyRecoverActive = false;
    };
}
