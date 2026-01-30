@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM ----------------------------------------------------------------------
REM [창 제어 설정]
REM /c : 스크립트 실행이 끝나면 창을 닫습니다. (단, 마지막 pause 대기 후)
REM ----------------------------------------------------------------------
if /i "%~1" neq "__INVOKED" (
    cmd /c ""%~f0" __INVOKED"
    exit /b
)

echo ========================================================
echo [Build.bat] Engine 스마트 빌드 시스템 (Monorepo)
echo 목표: ThirdParty 서브모듈 동기화 + 라이브러리 셋업 + 빌드
echo ========================================================

set "EXIT_CODE=0"
set "ENGINE_DIR=%~dp0Engine"

REM -----------------------------------------------------------
REM 1. Git 설치 확인
REM -----------------------------------------------------------
where git >nul 2>nul
if %errorlevel% neq 0 (
    echo [FAIL] Git이 없습니다.
    set "EXIT_CODE=1"
    goto :End
)

REM -----------------------------------------------------------
REM 2. Engine 폴더 존재 확인
REM (이제 서브모듈이 아니므로 폴더가 없으면 진행 불가)
REM -----------------------------------------------------------
if not exist "%ENGINE_DIR%\" (
    echo.
    echo [FAIL] 'Engine' 폴더가 없습니다!
    echo 이 배치 파일과 같은 위치에 Engine 폴더가 있는지 확인해주세요.
    set "EXIT_CODE=1"
    goto :End
)

REM -----------------------------------------------------------
REM 3. ThirdParty 서브모듈 업데이트
REM Engine 내부의 서브모듈(.gitmodules에 정의된 것들)을 가져옵니다.
REM -----------------------------------------------------------
echo.
echo [STEP 1] ThirdParty 서브모듈 업데이트 (rttr, imgui 등)...
git submodule update --init --recursive
if errorlevel 1 (
    echo [FAIL] 서브모듈 업데이트 실패.
    echo 인터넷 연결을 확인하거나, .gitmodules 설정을 확인하세요.
    set "EXIT_CODE=1"
    goto :End
)

REM -----------------------------------------------------------
REM 4. Setup 및 CMake 빌드 (중간 멈춤 방지 적용)
REM -----------------------------------------------------------
:BuildSequence
echo.
echo [STEP 2] Engine Setup (라이브러리 설정)
pushd "%ENGINE_DIR%"

if exist "Setup.bat" (
    REM [핵심] echo. | call ... 
    REM Setup.bat 내부의 pause를 엔터 입력으로 스킵
    echo. | call Setup.bat
    if errorlevel 1 (
        echo [FAIL] Setup.bat 실행 실패
        popd
        set "EXIT_CODE=1"
        goto :End
    )
) else (
    echo [FAIL] Setup.bat 파일이 없습니다.
    popd
    set "EXIT_CODE=1"
    goto :End
)

echo.
echo [STEP 3] 솔루션 생성 (build_msvc.cmd)
if exist "build_msvc.cmd" (
    REM [핵심] echo. | call ...
    REM build_msvc.cmd 내부의 pause를 엔터 입력으로 스킵
    echo. | call build_msvc.cmd
    if errorlevel 1 (
        echo [FAIL] build_msvc.cmd 실행 실패
        popd
        set "EXIT_CODE=1"
        goto :End
    )
) else (
    echo [FAIL] build_msvc.cmd 파일이 없습니다.
    popd
    set "EXIT_CODE=1"
    goto :End
)

popd

echo.
echo ========================================================
echo [SUCCESS] 모든 작업이 완료되었습니다.
echo Build 폴더에서 솔루션 파일을 확인하세요.
echo ========================================================

:End
echo.
if "%EXIT_CODE%"=="0" (
    echo [RESULT] SUCCESS
) else (
    echo [RESULT] FAIL ^(ExitCode=%EXIT_CODE%^)
)

echo.
echo (아무 키나 누르면 창이 닫힙니다.)
pause
exit /b %EXIT_CODE%