#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "Engine/Engine.h"
#include "Core/Logger.h"

// WinMain: 프로그램의 진입점입니다.
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
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


