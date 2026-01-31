#pragma once

namespace Alice
{
    class World;
    class SkinnedMeshRegistry;

    class AttackDriverSystem
    {
    public:
        void SetSkinnedMeshRegistry(SkinnedMeshRegistry* registry) { m_registry = registry; }
        void PreUpdate(World& world);
        void PostUpdate(World& world);

    private:
        SkinnedMeshRegistry* m_registry = nullptr;
    };
}
