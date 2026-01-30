#pragma once

namespace Alice
{
    class World;
    class SkinnedMeshRegistry;

    class SocketWorldUpdateSystem
    {
    public:
        explicit SocketWorldUpdateSystem(SkinnedMeshRegistry& registry);

        void Update(World& world);

    private:
        SkinnedMeshRegistry& m_registry;
    };
}
