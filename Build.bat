@echo off
setlocal EnableExtensions EnableDelayedExpansion

echo ========================================================
echo [Build.bat] Engine Build System (Monorepo)
echo ========================================================

set "EXIT_CODE=0"
set "SCRIPTS_DIR=%~dp0Scripts"
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
set "VS_PRESELECT=%~1"

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
REM [STEP 3] Visual Studio Selection
REM ================================================================
REM The engine is a CMake project, so the .sln/.vcxproj are generated - there is
REM no PlatformToolset pinned in the repo. Which VS gets used is decided here and
REM passed to CMake as the generator, so any installed VS works.
echo.
echo [STEP 3] Detecting Visual Studio...

REM VSWHERE lives under "Program Files (x86)". Those parentheses terminate a
REM for /f IN-clause if they are substituted while the line is parsed, so this
REM path is always referenced through delayed expansion.
if not exist "!VSWHERE!" (
    echo [FAIL] vswhere.exe not found. Install Visual Studio 2019 or later.
    echo        Expected: !VSWHERE!
    set "EXIT_CODE=1"
    goto :End
)

REM Only installations that actually carry the C++ toolset are candidates.
REM The three queries use identical filters, so their output order matches and
REM the index lines up across path / version / name.
set "VS_FILTER=-all -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64"
set "VS_COUNT=0"
for /f "usebackq delims=" %%p in (`"!VSWHERE!" !VS_FILTER! -property installationPath`) do (
    set /a VS_COUNT+=1
    set "VS_PATH_!VS_COUNT!=%%p"
)
set "VS_I=0"
for /f "usebackq delims=" %%v in (`"!VSWHERE!" !VS_FILTER! -property installationVersion`) do (
    set /a VS_I+=1
    set "VS_VER_!VS_I!=%%v"
)
set "VS_I=0"
for /f "usebackq delims=" %%n in (`"!VSWHERE!" !VS_FILTER! -property displayName`) do (
    set /a VS_I+=1
    set "VS_NAME_!VS_I!=%%n"
)

if %VS_COUNT% equ 0 (
    echo [FAIL] No Visual Studio with C++ build tools found.
    echo        Install the "Desktop development with C++" workload.
    set "EXIT_CODE=1"
    goto :End
)

REM Map the installation major version onto the matching CMake generator.
for /l %%i in (1,1,%VS_COUNT%) do (
    set "VS_GEN_%%i="
    for /f "tokens=1 delims=." %%a in ("!VS_VER_%%i!") do (
        if "%%a"=="18" set "VS_GEN_%%i=Visual Studio 18 2026"
        if "%%a"=="17" set "VS_GEN_%%i=Visual Studio 17 2022"
        if "%%a"=="16" set "VS_GEN_%%i=Visual Studio 16 2019"
    )
)

echo.
echo  Installed Visual Studio (C++ toolset):
for /l %%i in (1,1,%VS_COUNT%) do (
    if defined VS_GEN_%%i (
        echo    [%%i] !VS_NAME_%%i!  ^(!VS_VER_%%i!^)
        echo        generator: !VS_GEN_%%i!
    ) else (
        echo    [%%i] !VS_NAME_%%i!  ^(!VS_VER_%%i!^)  - unsupported, no CMake generator
    )
)
echo.

set "VS_CHOICE="
set /a VS_TRIES=0

REM Every label below sits at top level on purpose. A label placed inside a
REM parenthesised block and jumped back into with goto breaks the block, which
REM turns the retry prompt into an infinite loop.
if defined VS_PRESELECT goto :PreselectVS
if %VS_COUNT% equ 1 goto :AutoSelectVS
goto :AskVS

REM Non-interactive selection: Build.bat 2026 / 2022 / 2019, or an index.
:PreselectVS
for /l %%i in (1,1,%VS_COUNT%) do (
    if not defined VS_CHOICE if "%VS_PRESELECT%"=="%%i" set "VS_CHOICE=%%i"
)
if not defined VS_CHOICE (
    for /l %%i in (1,1,%VS_COUNT%) do (
        if not defined VS_CHOICE (
            echo !VS_GEN_%%i! | findstr /c:"%VS_PRESELECT%" >nul 2>nul
            if not errorlevel 1 set "VS_CHOICE=%%i"
        )
    )
)
if not defined VS_CHOICE (
    echo [FAIL] Requested Visual Studio "%VS_PRESELECT%" is not installed.
    set "EXIT_CODE=1"
    goto :End
)
echo  Selected by argument: [!VS_CHOICE!]
goto :VSSelected

:AutoSelectVS
set "VS_CHOICE=1"
echo  Only one installation found - selecting it automatically.
goto :VSSelected

:AskVS
set /a VS_TRIES+=1
REM set /p leaves the variable untouched at end-of-input, so a non-interactive
REM run would otherwise spin here forever.
if !VS_TRIES! gtr 10 (
    echo [FAIL] No valid selection after 10 attempts.
    echo        Run non-interactively instead, e.g. Build.bat 2026
    set "EXIT_CODE=1"
    goto :End
)
set "VS_INPUT="
set /p "VS_INPUT=Which Visual Studio should build the engine? [1-%VS_COUNT%]: "
if not defined VS_INPUT goto :AskVS
for /l %%i in (1,1,%VS_COUNT%) do (
    if "!VS_INPUT!"=="%%i" set "VS_CHOICE=%%i"
)
if defined VS_CHOICE goto :VSSelected
echo  Enter a number between 1 and %VS_COUNT%.
goto :AskVS

:VSSelected
for %%i in (!VS_CHOICE!) do (
    set "ALICE_VS_GENERATOR=!VS_GEN_%%i!"
    set "ALICE_VS_PATH=!VS_PATH_%%i!"
    set "ALICE_VS_NAME=!VS_NAME_%%i!"
)

if not defined ALICE_VS_GENERATOR (
    echo [FAIL] "!ALICE_VS_NAME!" has no matching CMake generator.
    echo        Supported: Visual Studio 2019 / 2022 / 2026.
    set "EXIT_CODE=1"
    goto :End
)

echo [OK] Using !ALICE_VS_NAME!
echo      Generator: !ALICE_VS_GENERATOR!

REM ================================================================
REM [STEP 4] CMake Check & Auto-Install
REM ================================================================
echo.
echo [STEP 4] Checking CMake...

where cmake >nul 2>nul
if %errorlevel% equ 0 (
    echo [OK] CMake found in PATH.
    for /f "tokens=3" %%v in ('cmake --version 2^>^&1 ^| findstr /i "cmake version"') do echo        Version: %%v
    goto :CMAKE_READY
)

REM CMake bundled with the Visual Studio picked above (C++ CMake tools workload)
set "VS_CMAKE=!ALICE_VS_PATH!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin"
if exist "!VS_CMAKE!\cmake.exe" (
    echo [OK] CMake found in !ALICE_VS_NAME!.
    set "PATH=!VS_CMAKE!;%PATH%"
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
REM [STEP 5] CMake Configure (generate VS solution)
REM ================================================================
echo.
echo [STEP 5] Generating VS Solution...
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

REM VS 2022 generates a classic .sln, VS 2026 generates the XML .slnx format.
REM Report whichever one actually landed instead of guessing.
set "ENGINE_SLN=build\AliceRenderer.sln"
if exist "%~dp0build\AliceRenderer.slnx" set "ENGINE_SLN=build\AliceRenderer.slnx"
set "SCRIPTS_SLN=EngineSource\ScriptsBuild\build\AliceUserScripts.sln"
if exist "%~dp0EngineSource\ScriptsBuild\build\AliceUserScripts.slnx" set "SCRIPTS_SLN=EngineSource\ScriptsBuild\build\AliceUserScripts.slnx"

echo.
echo ========================================================
echo [SUCCESS] All steps completed.
echo.
echo  Built with: !ALICE_VS_NAME!
echo.
echo  [1] Editor / Game build:
echo      !ENGINE_SLN!  ^> Build Launch or AlicePlayer
echo.
echo  [2] User script build (hot-reload):
echo      !SCRIPTS_SLN!
echo      ^> Build AliceScripts ^> build\bin\{Config}\dll\AliceScripts.dll
echo ========================================================

:End
echo.
if "%EXIT_CODE%"=="0" ( echo [RESULT] SUCCESS ) else ( echo [RESULT] FAIL )
echo.
pause
