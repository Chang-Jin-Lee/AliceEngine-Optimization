@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Generator is chosen by Build.bat (STEP 3) and handed over in ALICE_VS_GENERATOR.
rem Run directly without it and CMake falls back to its own default, which is the
rem newest Visual Studio it can find.
set "GEN_ARG="
if defined ALICE_VS_GENERATOR (
    set "GEN_ARG=-G "%ALICE_VS_GENERATOR%""
    echo [INFO] Generator: %ALICE_VS_GENERATOR%
) else (
    echo [INFO] ALICE_VS_GENERATOR not set - letting CMake pick its default.
)

rem 1. submodule update
git -C .. submodule sync --recursive
git -C .. submodule update --init --recursive
if errorlevel 1 goto :Error

rem 2. Engine project cmake configure
rem A CMake cache is locked to the generator that produced it, so switching
rem Visual Studio versions has to drop the old build directory first.
call :DropIfGeneratorChanged "..\build"
cmake -S .. -B ..\build %GEN_ARG%
if errorlevel 1 goto :Error

rem 3. ScriptsBuild cmake configure (stale cache auto-clean)
set "SB_DIR=..\EngineSource\ScriptsBuild\build"
set "SB_CACHE=%SB_DIR%\CMakeCache.txt"
if exist "%SB_CACHE%" (
    findstr /c:"EngineSource/ScriptsBuild" "%SB_CACHE%" >nul 2>&1
    if errorlevel 1 (
        echo [INFO] Stale ScriptsBuild cache detected - cleaning...
        powershell -NoProfile -Command "Remove-Item -Recurse -Force '..\EngineSource\ScriptsBuild\build'"
    )
)
call :DropIfGeneratorChanged "%SB_DIR%"
cmake -S ..\EngineSource\ScriptsBuild -B %SB_DIR% %GEN_ARG%
if errorlevel 1 goto :Error

echo [OK] All cmake projects configured successfully.
goto :End

rem ----------------------------------------------------------------
rem :DropIfGeneratorChanged <build-dir>
rem Removes the build directory when its cached generator differs from the
rem requested one. CMake refuses to reconfigure across generators, and leftover
rem .vcxproj files from the previous one would point at a toolset that may no
rem longer be installed.
rem ----------------------------------------------------------------
:DropIfGeneratorChanged
set "CACHE_DIR=%~1"
if not defined ALICE_VS_GENERATOR goto :eof
if not exist "%CACHE_DIR%\CMakeCache.txt" goto :eof
set "CACHED_GEN="
for /f "usebackq tokens=1,* delims==" %%a in (`findstr /b /c:"CMAKE_GENERATOR:INTERNAL=" "%CACHE_DIR%\CMakeCache.txt"`) do set "CACHED_GEN=%%b"
if not defined CACHED_GEN goto :eof
if /i "!CACHED_GEN!"=="%ALICE_VS_GENERATOR%" goto :eof
echo [INFO] Generator changed: "!CACHED_GEN!" -^> "%ALICE_VS_GENERATOR%"
echo        Removing stale build directory: %CACHE_DIR%
rmdir /s /q "%CACHE_DIR%"
goto :eof

:Error
echo.
echo [FAIL] cmake configure failed. Check the log above.
endlocal
exit /b 1

:End
endlocal
pause
