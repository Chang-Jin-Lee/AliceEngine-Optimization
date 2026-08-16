@echo off
setlocal EnableExtensions

REM ================================================================
REM 004 - Camera take recording (a human flies it once)
REM
REM One take is replayed by both the legacy and the current run, so the
REM two videos show the same frames. Flying by hand twice would give two
REM different paths and the comparison would not hold up.
REM
REM ASCII only on purpose: cmd reads .bat in the OEM codepage, and
REM non-ASCII text here breaks line parsing on some machines.
REM ================================================================

set "REPO=%~dp0.."
set "EXE=%REPO%\build\bin\Release\Launch.exe"
set "TAKE=%REPO%\Bench\take01.json"

REM Densest tile scene. The original RenderDoc scene is
REM DuellumCycli/PrototypeDungeon.scene, but its tile mesh is 20000 units
REM while the grid spacing is 2, so scene and assets have drifted apart.
set "SCENE=#01PrototypeMap.scene"

if not exist "%EXE%" (
    echo [FAIL] Release build not found: %EXE%
    echo        Run Build.bat, then build Release ^> Launch in the solution.
    pause
    exit /b 1
)

if not exist "%REPO%\Bench" mkdir "%REPO%\Bench"

echo ========================================================
echo  Camera take recording
echo ========================================================
echo.
echo   scene  : %SCENE%   (tiles 407 / walls 552 / entities 1142)
echo   output : %TAKE%
echo.
echo   Controls
echo    - right-drag to look, WASD to move, wheel to change speed
echo    - fly around the dense tile area for about 20 seconds
echo    - the first 5 seconds are warmup and get discarded, so start slow
echo    - close the window when done; the take is saved on exit
echo.
echo   Scene loading takes 30+ seconds. It is not frozen.
echo.
echo  Press any key to start.
pause >nul

REM Output goes through an absolute path. A relative path would resolve
REM against the exe working directory and land under build\, not Bench\.
REM The exe itself is called by full path too: with
REM NoDefaultCurrentDirectoryInExePath set, cmd does not search the
REM current directory and a bare "Launch.exe" fails with exit 9009.
pushd "%REPO%\build\bin\Release"
"%EXE%" --scene=%SCENE% --camera-record="%TAKE%" --vsync=off --debug-draw=off
set "EL=%errorlevel%"
popd

echo.
if exist "%TAKE%" (
    echo [OK] saved: %TAKE%
    for %%A in ("%TAKE%") do echo      size: %%~zA bytes
    echo.
    echo  Next: run Scripts\run_bench.bat for the A/B capture.
) else (
    echo [FAIL] take was not saved. exit=%EL%
    echo        Check the newest log in build\bin\Release\Logs\ for [Bench] lines.
)
echo.
pause
