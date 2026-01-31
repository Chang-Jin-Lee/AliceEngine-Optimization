#pragma once

#include <vector>

namespace Alice
{
	struct CombatHitEvent;
}

namespace Alice
{
	class World;

	class WeaponTraceSystem
	{
	public:
		void Update(World& world, float dtSec, std::vector<CombatHitEvent>* outHits);
	};
}
