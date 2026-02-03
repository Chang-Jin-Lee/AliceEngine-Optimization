#pragma once

#include <string>

#include "Runtime/Scripting/IScript.h"
#include "Runtime/Scripting/ScriptReflection.h"
#include "Runtime/ECS/Entity.h"

namespace Alice
{
    class OptionScript : public IScript
    {
        ALICE_BODY(OptionScript);

    public:
        void Start() override;
        void Update(float deltaTime) override;

        // Editor-exposed example property.
        ALICE_PROPERTY(std::string, rootWidgetName, "");


    private:
        EntityId rootEntity = InvalidEntityId;
        bool childrenVisible = true;
    };
}
