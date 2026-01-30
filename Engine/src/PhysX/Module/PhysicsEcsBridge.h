#pragma once

#include <vector>

#include "PhysicsScene.h"

// ============================================================
// PhysicsEcsBridge
// - Small helper for fixed-step driving + ECS sync.
// - Does NOT assume your ECS types.
//   You provide callbacks that:
//     - push game->physics (kinematics, teleports, forces) before Step
//     - apply physics->game (active transforms) after Step
//     - handle events
//
// Usage pattern per scene:
//   bridge.Tick(scene, dt,
//     [&](IPhysicsWorld& w){ ...push...; },
//     [&](void* userData, const Vec3& p, const Quat& q){ ...apply...; },
//     [&](const PhysicsEvent& e){ ...handle...; }
//   );
// ============================================================

class PhysicsEcsBridge
{
public:
    struct Desc
    {
        float fixedDt = 1.0f / 60.0f;
        int   maxSubsteps = 4; // clamp to avoid spiral-of-death
    };

    explicit PhysicsEcsBridge(const Desc& desc = Desc{})
        : m_fixedDt(desc.fixedDt)
        , m_maxSubsteps(desc.maxSubsteps)
    {
    }

    void Reset() { m_accum = 0.0f; }

    template<class PushToPhysicsFn, class ApplyTransformFn, class EventFn>
    void Tick(
        PhysicsScene& scene,
        float dt,
        PushToPhysicsFn&& pushToPhysics,
        ApplyTransformFn&& applyTransform,
        EventFn&& onEvent)
    {
        if (!scene.IsValid()) return;
        if (m_fixedDt <= 0.0f) return;

        m_accum += dt;

        int steps = 0;
        while (m_accum >= m_fixedDt && steps < m_maxSubsteps)
        {
            // 1) game -> physics (kinematics, teleports, forces)
            pushToPhysics(scene.World());

            // 2) simulate
            scene.Step(m_fixedDt);

            // 3) physics -> game (moved bodies only)
            m_tmpTransforms.clear();
            scene.ConsumeActiveTransforms(m_tmpTransforms);
            for (const auto& t : m_tmpTransforms)
                applyTransform(t.userData, t.position, t.rotation);

            // 4) events
            m_tmpEvents.clear();
            scene.ConsumeEvents(m_tmpEvents);
            for (const auto& e : m_tmpEvents)
                onEvent(e);

            m_accum -= m_fixedDt;
            ++steps;
        }

        // If we clamped, drop leftover time to avoid huge catch-up hitches.
        if (steps == m_maxSubsteps)
            m_accum = 0.0f;
    }

private:
    float m_fixedDt = 1.0f / 60.0f;
    int   m_maxSubsteps = 4;
    float m_accum = 0.0f;

    std::vector<ActiveTransform> m_tmpTransforms;
    std::vector<PhysicsEvent>    m_tmpEvents;
};
