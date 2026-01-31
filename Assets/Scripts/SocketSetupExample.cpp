#include "SocketSetupExample.h"

#include "Runtime/Scripting/ScriptFactory.h"
#include "Runtime/ECS/GameObject.h"
#include "Runtime/Gameplay/Sockets/SocketComponent.h"

namespace Alice
{
    REGISTER_SCRIPT(SocketSetupExample);

    void SocketSetupExample::Start()
    {
        auto go = gameObject();
        auto* sc = go.GetComponent<SocketComponent>();
        if (!sc)
            sc = &go.AddComponent<SocketComponent>();

        for (const auto& s : sc->sockets)
            if (s.name == socketName)
                return;

        SocketDef s;
        s.name = socketName;
        s.parentBone = parentBone;
        s.position = position;
        s.rotation = rotation;
        s.scale = scale;
        sc->sockets.push_back(std::move(s));
    }
}

