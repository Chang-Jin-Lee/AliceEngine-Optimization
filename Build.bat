@echo off
setlocal EnableExtensions EnableDelayedExpansion

echo ========================================================
echo [Build.bat] Engine Build System (Monorepo)
echo ========================================================

set "EXIT_CODE=0"
set "SCRIPTS_DIR=%~dp0Scripts"

REM ================================================================
REM [Pre-Check] Git
REM ================================================================
where git >nul 2>nul
if %errorlevel% neq 0 (
    echo [FAIL] Git not found. Install from https://git-scm.com/
    set "EXIT_CODE=1"
    goto :End
)

if not exist "%SCRIPTS_DIR%\" (
    echo [FAIL] Scripts directory not found.
    set "EXIT_CODE=1"
    goto :End
)

REM ================================================================
REM [STEP 1] Submodule Update
REM ================================================================
echo.
echo [STEP 1] Updating submodules...
git submodule update --init --recursive
if errorlevel 1 (
    echo [FAIL] Submodule update failed.
    set "EXIT_CODE=1"
    goto :End
)

REM ================================================================
REM [STEP 2] Engine Setup (vcpkg + libraries)
REM ================================================================
echo.
echo [STEP 2] Engine Setup (vcpkg + library install)
if not exist "%SCRIPTS_DIR%\Setup.bat" (
    echo [FAIL] Setup.bat not found at %SCRIPTS_DIR%
    set "EXIT_CODE=1"
    goto :End
)
set "CALLED_FROM_BUILD=1"
call "%SCRIPTS_DIR%\Setup.bat"
set "CALLED_FROM_BUILD="
if errorlevel 1 (
    echo [FAIL] Setup.bat failed.
    set "EXIT_CODE=1"
    goto :End
)

REM ================================================================
REM [STEP 3] CMake Check & Auto-Install
REM ================================================================
echo.
echo [STEP 3] Checking CMake...

where cmake >nul 2>nul
if %errorlevel% equ 0 (
    echo [OK] CMake found in PATH.
    for /f "tokens=3" %%v in ('cmake --version 2^>^&1 ^| findstr /i "cmake version"') do echo        Version: %%v
    goto :CMAKE_READY
)

REM Try Visual Studio bundled CMake (when C++ CMake tools workload is installed)
set "VS_CMAKE=C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
if exist "%VS_CMAKE%\cmake.exe" (
    echo [OK] CMake found in Visual Studio 2022.
    set "PATH=%VS_CMAKE%;%PATH%"
    goto :CMAKE_READY
)

REM Try default standalone CMake install location
if exist "C:\Program Files\CMake\bin\cmake.exe" (
    echo [OK] CMake found at Program Files.
    set "PATH=C:\Program Files\CMake\bin;%PATH%"
    goto :CMAKE_READY
)

REM Not found - install via winget
echo [INFO] CMake not found. Attempting auto-install via winget...
where winget >nul 2>nul
if %errorlevel% neq 0 (
    echo [FAIL] winget not found. Install CMake manually: https://cmake.org/download/
    set "EXIT_CODE=1"
    goto :End
)

winget install --id Kitware.CMake --silent --accept-package-agreements --accept-source-agreements
if %errorlevel% neq 0 (
    echo [FAIL] CMake auto-install failed. Install manually: https://cmake.org/download/
    set "EXIT_CODE=1"
    goto :End
)

REM winget installs to this default path
set "PATH=C:\Program Files\CMake\bin;%PATH%"

if not exist "C:\Program Files\CMake\bin\cmake.exe" (
    echo [FAIL] CMake installed but binary not found at expected path.
    echo        Please restart this terminal and run Build.bat again.
    set "EXIT_CODE=1"
    goto :End
)
echo [OK] CMake installed successfully.

:CMAKE_READY
cmake --version 2>&1 | findstr /i "cmake version"

REM ================================================================
REM [STEP 4] CMake Configure (generate VS solution)
REM ================================================================
echo.
echo [STEP 4] Generating VS Solution...
if not exist "%SCRIPTS_DIR%\build_msvc.cmd" (
    echo [FAIL] build_msvc.cmd not found at %SCRIPTS_DIR%
    set "EXIT_CODE=1"
    goto :End
)
pushd "%SCRIPTS_DIR%"
call "%SCRIPTS_DIR%\build_msvc.cmd"
set "BUILD_EL=%errorlevel%"
popd
if %BUILD_EL% neq 0 (
    echo [FAIL] build_msvc.cmd failed.
    set "EXIT_CODE=1"
    goto :End
)

echo.
echo ========================================================
echo [SUCCESS] All steps completed.
echo.
echo  [1] 에디터/게임 빌드:
echo      build\AliceRenderer.sln  ^> Launch 또는 AlicePlayer 빌드
echo.
echo  [2] 유저 스크립트 빌드 (핫리로드용):
echo      EngineSource\ScriptsBuild\build\AliceUserScripts.sln
echo      ^> AliceScripts 빌드 ^> build\bin\{Config}\dll\AliceScripts.dll 생성
echo ========================================================

:End
echo.
if "%EXIT_CODE%"=="0" ( echo [RESULT] SUCCESS ) else ( echo [RESULT] FAIL )
echo.
pause
