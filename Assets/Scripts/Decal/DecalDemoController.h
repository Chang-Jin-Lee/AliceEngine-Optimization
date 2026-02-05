#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    // Simple decal demo controller: spins the decal and scrolls UVs.
    class DecalDemoController : public IScript
    {
        ALICE_BODY(DecalDemoController);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        ALICE_PROPERTY(float, m_spinSpeed, 0.6f);
        ALICE_PROPERTY(float, m_uvScrollSpeed, 0.1f);
        ALICE_PROPERTY(bool, m_pulseOpacity, true);
        ALICE_PROPERTY(float, m_opacityMin, 0.35f);
        ALICE_PROPERTY(float, m_opacityPulseSpeed, 1.5f);

    private:
        float m_timeSec = 0.0f;
    };
}
