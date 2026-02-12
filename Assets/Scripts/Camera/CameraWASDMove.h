#pragma once

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"

namespace Alice
{
    class CameraWASDMove : public IScript
    {
        ALICE_BODY(CameraWASDMove);

    public:
        void Update(float deltaTime) override;

    private:
        // Units per second.
        ALICE_PROPERTY(float, m_moveSpeed, 8.0f);
        // World-units moved per mouse pixel while dragging.
        ALICE_PROPERTY(float, m_dragMoveSpeed, 0.02f);
    };
}
