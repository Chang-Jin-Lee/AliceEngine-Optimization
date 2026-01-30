#include "SocketSetupExample.h"

#include "Core/ScriptFactory.h"
#include "Core/GameObject.h"
#include "Components/SocketComponent.h"

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

