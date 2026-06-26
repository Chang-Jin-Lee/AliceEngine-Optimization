#pragma once

#include <vector>

namespace Alice
{
    class World;
    struct CombatHitEvent;

    class CombatSystem
    {
    public:
        void BeginFrame(World& world);
        void Update(World& world, float dtSec);
        void ProcessHits(World& world, const std::vector<CombatHitEvent>& hits);
    };
}
