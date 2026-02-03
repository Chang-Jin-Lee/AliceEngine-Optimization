#pragma once

#include <string>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    class RootMotionKeyTester : public IScript
    {
        ALICE_BODY(RootMotionKeyTester);

    public:
        void Start() override;
        void Update(float deltaTime) override;

    private:
        void ApplyKey(int keyIndex);

        ALICE_PROPERTY(bool, m_forceDisableUpperAdditive, true);
        ALICE_PROPERTY(bool, m_forceRestartOnKey, true);
        ALICE_PROPERTY(bool, m_loop, true);
        ALICE_PROPERTY(float, m_playSpeed, 1.0f);

        /// @brief Key1: Play clip with current AdvancedAnimationComponent settings.
        /// @details Root motion behavior follows rootMotionUnlock in the component.
        ALICE_PROPERTY(std::string, m_clipKey1, std::string(""));

        /// @brief Key2: Play clip; if rootMotionUnlock=true, actor follows root bone.
        /// @details Use this as the main root-motion-on test key.
        ALICE_PROPERTY(std::string, m_clipKey2, std::string(""));

        /// @brief Key3: Play clip (no special root motion overrides).
        /// @details Root motion behavior follows rootMotionUnlock in the component.
        ALICE_PROPERTY(std::string, m_clipKey3, std::string(""));

        /// @brief Key4: Play clip (useful to compare with Key2).
        /// @details Root motion behavior follows rootMotionUnlock in the component.
        ALICE_PROPERTY(std::string, m_clipKey4, std::string(""));

        /// @brief Key5: Play clip (optional extra slot).
        /// @details Root motion behavior follows rootMotionUnlock in the component.
        ALICE_PROPERTY(std::string, m_clipKey5, std::string(""));
    };
}
