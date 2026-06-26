@echo off
setlocal

rem 1. submodule update
git -C .. submodule sync --recursive
git -C .. submodule update --init --recursive
if errorlevel 1 goto :Error

rem 2. Engine project cmake configure
cmake -S .. -B ..\build -G "Visual Studio 17 2022"
if errorlevel 1 goto :Error

rem 3. ScriptsBuild cmake configure (stale cache auto-clean)
set "SB_CACHE=..\EngineSource\ScriptsBuild\build\CMakeCache.txt"
if exist "%SB_CACHE%" (
    findstr /c:"EngineSource/ScriptsBuild" "%SB_CACHE%" >nul 2>&1
    if errorlevel 1 (
        echo [INFO] Stale ScriptsBuild cache detected - cleaning...
        powershell -NoProfile -Command "Remove-Item -Recurse -Force '..\EngineSource\ScriptsBuild\build'"
    )
)
cmake -S ..\EngineSource\ScriptsBuild -B ..\EngineSource\ScriptsBuild\build -G "Visual Studio 17 2022"
if errorlevel 1 goto :Error

echo [OK] All cmake projects configured successfully.
goto :End

:Error
echo.
echo [FAIL] cmake configure failed. Check the log above.

:End
endlocal
pause
