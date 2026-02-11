#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/Input/InputTypes.h"

namespace Alice
{
    class TimeStopScript : public IScript
    {
        ALICE_BODY(TimeStopScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        void SetTimeStop(bool stopped);
        ALICE_FUNC(SetTimeStop);

        // Inspector controls
        ALICE_PROPERTY(int, toggleKey, static_cast<int>(KeyCode::Escape));
        ALICE_PROPERTY(bool, pauseAudioOnStop, true);
        ALICE_PROPERTY(bool, timeStopped, false);

    private:
        bool m_prevTimeStopped = false;
    };
}
