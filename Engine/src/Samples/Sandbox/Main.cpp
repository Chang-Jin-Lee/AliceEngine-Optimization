#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <filesystem>

#include "Runtime/Engine/Engine.h"
#include "Runtime/Foundation/Logger.h"

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

// WinMain: 프로그램의 진입점입니다.
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    ConfigureDllSearchPath();

    // 공용 로거 초기화 (실행 파일 옆 Logs 디렉터리에 로그 파일 생성)
    Alice::Logger::Initialize();

    Alice::Engine engine;

    // 1) 엔진 초기화 (윈도우 + D3D11 렌더 디바이스)
    if (!engine.Initialize(hInstance, nCmdShow))
    {
        MessageBoxW(nullptr, L"엔진 초기화에 실패했습니다.", L"AliceRenderer", MB_OK | MB_ICONERROR);
        Alice::Logger::Shutdown();
        return -1;
    }

    // 2) 메인 루프 실행
    int result = engine.Run();

    // 3) 로그 파일 정리
    Alice::Logger::Shutdown();

    return result;
}


