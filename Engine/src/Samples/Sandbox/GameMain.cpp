#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

#include "Runtime/Engine/Engine.h"
#include "Runtime/Foundation/Logger.h"

// 게임 플레이 전용 엔트리 포인트입니다.
// - 에디터 UI(ImGui Docking)는 표시하지 않고,
//   뷰포트에 보이던 게임 화면만 전체 창으로 실행합니다.
int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int nCmdShow)
{
    // 공용 로거 초기화
    Alice::Logger::Initialize();

    // editorMode=false → 게임 전용 모드
    Alice::Engine engine(false);

    if (!engine.Initialize(hInstance, nCmdShow))
    {
        MessageBoxW(nullptr, L"엔진 초기화에 실패했습니다.", L"AliceGame", MB_OK | MB_ICONERROR);
        Alice::Logger::Shutdown();
        return -1;
    }

    int result = engine.Run();

    Alice::Logger::Shutdown();
    return result;
}



