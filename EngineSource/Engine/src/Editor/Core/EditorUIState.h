#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "Runtime/Rendering/Components/MaterialComponent.h"
#include "Runtime/UI/UICurveAsset.h"

namespace Alice
{
	// Scene dirty flag (editor-wide)
	extern bool g_SceneDirty;

	// Current scene path state
	extern bool g_HasCurrentScenePath;
	extern std::filesystem::path g_CurrentScenePath;

	// Tool window toggles
	extern bool g_ShowBuildGameWindow;
	extern bool g_ShowPvdSettingsWindow;

	// Scene load flow state
	extern bool g_RequestSceneLoad;
	extern std::filesystem::path g_NextScenePath;
	extern bool g_ShowSceneLoadError;
	extern std::string g_SceneLoadErrorMsg;

	// Material asset editor state
	extern bool g_MaterialEditorOpen;
	extern std::filesystem::path g_MaterialEditorPath;
	extern MaterialComponent g_MaterialEditorData;

	// UI curve asset editor state
	extern bool g_UICurveEditorOpen;
	extern std::filesystem::path g_UICurveEditorPath;
	extern UICurveAsset g_UICurveEditorData;
	extern int g_UICurveEditorSelected;

	// Preload asset editor state
	extern bool g_PreloadEditorOpen;
	extern std::filesystem::path g_PreloadEditorPath;
	extern std::vector<std::string> g_PreloadEditorItems;
	extern int g_PreloadEditorSelected;
}
