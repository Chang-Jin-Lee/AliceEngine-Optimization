#pragma once

#include "Runtime/ECS/Entity.h"
#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    /// UI/타이틀 씬에서 커서를 보이게 하고 잠금을 푼다. (Combat 등에서 돌아온 뒤 복원용)
    class VisibleMouseScript : public IScript
    {
        ALICE_BODY(VisibleMouseScript);

    public:
        void Start() override;
    };
}
