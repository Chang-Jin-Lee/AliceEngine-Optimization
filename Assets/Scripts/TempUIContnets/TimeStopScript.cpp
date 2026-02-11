#include "TimeStopScript.h"

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/Input/Input.h"

namespace Alice
{
    REGISTER_SCRIPT(TimeStopScript);

    void TimeStopScript::Start()
    {
        m_prevTimeStopped = Get_timeStopped();
        if (Get_pauseAudioOnStop())
        {
            if (auto* audio = Audio())
            {
                audio->PauseAll(m_prevTimeStopped);
                audio->PauseBGM(m_prevTimeStopped);
            }
        }
    }

    void TimeStopScript::Update(float /*deltaTime*/)
    {
        if (auto* input = Input())
        {
            const KeyCode key = static_cast<KeyCode>(Get_toggleKey());
            if (input->GetKeyDown(key))
            {
                SetTimeStop(!Get_timeStopped());
                return;
            }
        }

        // If audio system initializes after Start(), keep enforcing pause while timeStopped.
        if (Get_pauseAudioOnStop() && Get_timeStopped())
        {
            if (auto* audio = Audio())
            {
                audio->PauseAll(true);
                audio->PauseBGM(true);
            }
        }

        const bool current = Get_timeStopped();
        if (current == m_prevTimeStopped)
            return;

        m_prevTimeStopped = current;
        if (Get_pauseAudioOnStop())
        {
            if (auto* audio = Audio())
            {
                audio->PauseAll(current);
                audio->PauseBGM(current);
            }
        }
    }

    void TimeStopScript::SetTimeStop(bool stopped)
    {
        Set_timeStopped(stopped);
        if (Get_pauseAudioOnStop())
        {
            if (auto* audio = Audio())
            {
                audio->PauseAll(stopped);
                audio->PauseBGM(stopped);
            }
        }
        m_prevTimeStopped = stopped;
    }
}
