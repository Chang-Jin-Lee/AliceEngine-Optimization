#include "VisibleMouseScript.h"
#include "Runtime/Scripting/ScriptFactory.h"

namespace Alice
{
    REGISTER_SCRIPT(VisibleMouseScript);

    void VisibleMouseScript::Start()
    {
        if (auto* input = Input())
        {
            input->SetCursorVisible(true);
            input->SetCursorLocked(false);
        }
    }
}
