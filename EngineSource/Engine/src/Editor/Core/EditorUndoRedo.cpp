#include "Editor/Core/EditorCore.h"
#include "Editor/Core/EditorUndoRedo.h"
#include "Editor/Core/EditorUIState.h"

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace Alice
{
	namespace
	{
		// Undo/Redo 스택 관리
		std::vector<std::unique_ptr<ICommand>> g_UndoStack;
		std::vector<std::unique_ptr<ICommand>> g_RedoStack;
		constexpr size_t MAX_UNDO_STACK_SIZE = 50;
	}

	bool ExecuteUndo(World& world, EntityId& selectedEntity)
	{
		if (g_UndoStack.empty())
			return false;

		auto cmd = std::move(g_UndoStack.back());
		g_UndoStack.pop_back();

		cmd->Undo(world, selectedEntity);

		// Redo 지원하는 커맨드만 Redo 스택에 추가
		if (cmd->SupportsRedo())
		{
			g_RedoStack.push_back(std::move(cmd));
			if (g_RedoStack.size() > MAX_UNDO_STACK_SIZE)
			{
				g_RedoStack.erase(g_RedoStack.begin());
			}
		}
		// else: Redo 불가 커맨드는 버림

		g_SceneDirty = true;
		return true;
	}

	bool ExecuteRedo(World& world, EntityId& selectedEntity)
	{
		if (g_RedoStack.empty())
			return false;

		auto cmd = std::move(g_RedoStack.back());
		g_RedoStack.pop_back();

		cmd->Execute(world, selectedEntity);

		// Undo 스택에 다시 추가
		g_UndoStack.push_back(std::move(cmd));
		if (g_UndoStack.size() > MAX_UNDO_STACK_SIZE)
		{
			g_UndoStack.erase(g_UndoStack.begin());
		}

		g_SceneDirty = true;
		return true;
	}

	void ClearUndoStack()
	{
		g_UndoStack.clear();
		g_RedoStack.clear();
	}

	void EditorCore::PushCommand(std::unique_ptr<ICommand> cmd)
	{
		// 새로운 액션이 들어오면 Redo 스택 클리어 (일반적인 Undo/Redo 동작)
		g_RedoStack.clear();

		g_UndoStack.push_back(std::move(cmd));
		if (g_UndoStack.size() > MAX_UNDO_STACK_SIZE)
		{
			g_UndoStack.erase(g_UndoStack.begin());
		}
	}
}
