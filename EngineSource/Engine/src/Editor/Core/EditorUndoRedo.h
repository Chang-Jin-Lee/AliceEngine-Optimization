#pragma once

#include "Runtime/ECS/Entity.h"

namespace Alice
{
	class World;

	bool ExecuteUndo(World& world, EntityId& selectedEntity);
	bool ExecuteRedo(World& world, EntityId& selectedEntity);
	void ClearUndoStack();
}
