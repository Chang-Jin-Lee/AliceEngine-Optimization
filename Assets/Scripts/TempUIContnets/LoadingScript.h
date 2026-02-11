#pragma once

#include <string>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    class LoadingScript : public IScript
    {
        ALICE_BODY(LoadingScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        // Scene to move to after delay.
        ALICE_PROPERTY(std::string, targetScenePath, "");
        // Delay seconds before scene change.
        ALICE_PROPERTY(float, delaySec, 1.0f);
        // If true, start timer on Start().
        ALICE_PROPERTY(bool, autoStart, true);

        // Optional fade control.
        ALICE_PROPERTY(std::string, fadeEntityName, "");
        // Fade in (to clear) immediately on start.
        ALICE_PROPERTY(bool, fadeInOnStart, true);
        // After this delay, trigger fade out (to black).
        ALICE_PROPERTY(float, fadeOutDelaySec, 2.0f);
        // Enable timed fade out.
        ALICE_PROPERTY(bool, enableFadeOut, true);

        void Trigger();

    private:
        bool m_pending{ false };
        float m_timer{ 0.0f };
        float m_fadeTimer{ 0.0f };
        bool m_fadeOutTriggered{ false };
        class FadeInOutScript* m_fade{ nullptr };
    };
}
