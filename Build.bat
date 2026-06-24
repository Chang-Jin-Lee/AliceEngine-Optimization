@echo off
setlocal EnableExtensions EnableDelayedExpansion

echo ========================================================
echo [Build.bat] Engine 스마트 빌드 시스템 (Monorepo)
echo 목표: ThirdParty 서브모듈 동기화 + 라이브러리 셋업 + 빌드
echo ========================================================

set "EXIT_CODE=0"
set "ENGINE_DIR=%~dp0Engine"

where git >nul 2>nul
if %errorlevel% neq 0 (
    echo [FAIL] Git이 없습니다.
    set "EXIT_CODE=1"
    goto :End
)

if not exist "%ENGINE_DIR%\" (
    echo [FAIL] Engine 폴더가 없습니다.
    set "EXIT_CODE=1"
    goto :End
)

echo.
echo [STEP 1] ThirdParty 서브모듈 업데이트 중...
git submodule update --init --recursive
if errorlevel 1 (
    echo [FAIL] 서브모듈 업데이트 실패.
    set "EXIT_CODE=1"
    goto :End
)

echo.
echo [STEP 2] Engine Setup (라이브러리 설정)
pushd "%ENGINE_DIR%"
if not exist "Setup.bat" (
    echo [FAIL] Setup.bat 파일이 없습니다.
    popd
    set "EXIT_CODE=1"
    goto :End
)
set "CALLED_FROM_BUILD=1"
call Setup.bat
set "CALLED_FROM_BUILD="
if errorlevel 1 (
    echo [FAIL] Setup.bat 실행 실패
    popd
    set "EXIT_CODE=1"
    goto :End
)

echo.
echo [STEP 3] 솔루션 생성 (build_msvc.cmd)
if not exist "build_msvc.cmd" (
    echo [FAIL] build_msvc.cmd 파일이 없습니다.
    popd
    set "EXIT_CODE=1"
    goto :End
)
call build_msvc.cmd
if errorlevel 1 (
    echo [FAIL] build_msvc.cmd 실행 실패
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
if "%EXIT_CODE%"=="0" ( echo [RESULT] SUCCESS ) else ( echo [RESULT] FAIL )
echo.
pause
