#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <filesystem>

#include "Runtime/Engine/Engine.h"
#include "Runtime/Foundation/Logger.h"
#include "Runtime/Engine/CommandLineOptions.h"
#include "imgui_impl_win32.h"

static void ConfigureDllSearchPath()
{
    wchar_t exePath[MAX_PATH] = {};
    const DWORD len = GetModuleFileNameW(nullptr, exePath, MAX_PATH);
    if (len == 0 || len >= MAX_PATH)
    {
        return;
    }

    const std::filesystem::path exeDir = std::filesystem::path(exePath).parent_path();
    const std::filesystem::path dllDir = exeDir / L"dll";
    SetDllDirectoryW(dllDir.c_str());
}

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    ImGui_ImplWin32_EnableDpiAwareness();
    ConfigureDllSearchPath();
    Alice::Logger::Initialize();

    Alice::CommandLineOptions options{};
    std::string optionError;
    if (!Alice::ParseProcessCommandLine(options, optionError))
    {
        MessageBoxA(nullptr, optionError.c_str(), "AliceRenderer command line", MB_OK | MB_ICONERROR);
        Alice::Logger::Shutdown();
        return -1;
    }

    const bool benchRequested = options.benchRequested;
    Alice::Engine engine(true, std::move(options));
    if (!engine.Initialize(hInstance, nCmdShow))
    {
        if (!benchRequested)
            MessageBoxW(nullptr, L"Failed to initialize engine.", L"AliceRenderer", MB_OK | MB_ICONERROR);
        Alice::Logger::Shutdown();
        return -1;
    }

    const int result = engine.Run();
    Alice::Logger::Shutdown();
    return result;
}

