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

// 게임 플레이 전용 엔트리 포인트입니다.
// - 에디터 UI(ImGui Docking)는 표시하지 않고,
//   뷰포트에 보이던 게임 화면만 전체 창으로 실행합니다.
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    ImGui_ImplWin32_EnableDpiAwareness();
    ConfigureDllSearchPath();

    // 공용 로거 초기화
    Alice::Logger::Initialize();

    Alice::CommandLineOptions options{};
    std::string optionError;
    if (!Alice::ParseProcessCommandLine(options, optionError))
    {
        MessageBoxA(nullptr, optionError.c_str(), "AliceGame command line", MB_OK | MB_ICONERROR);
        Alice::Logger::Shutdown();
        return -1;
    }

    const bool benchRequested = options.benchRequested;
    // editorMode=false → 게임 전용 모드
    Alice::Engine engine(false, std::move(options));

    if (!engine.Initialize(hInstance, nCmdShow))
    {
        if (!benchRequested)
            MessageBoxW(nullptr, L"엔진 초기화에 실패했습니다.", L"AliceGame", MB_OK | MB_ICONERROR);
        Alice::Logger::Shutdown();
        return -1;
    }

    int result = engine.Run();

    Alice::Logger::Shutdown();
    return result;
}



