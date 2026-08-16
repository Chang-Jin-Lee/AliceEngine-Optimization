@echo off
setlocal EnableExtensions

REM ================================================================
REM 005 - A/B capture
REM
REM Two measurement runs (no --frames) plus two video-source runs
REM (with --frames). Dumping PNGs distorts frame time, so the two
REM purposes are never mixed: only runs 1-2 produce the numbers,
REM the CSVs of runs 3-4 are discarded.
REM
REM Under --camera-replay both --warmup and --duration advance in take
REM time (frame index), so legacy and current record the exact same
REM frame range.
REM
REM ASCII only on purpose: cmd reads .bat in the OEM codepage, and
REM non-ASCII text here breaks line parsing on some machines.
REM ================================================================

set "REPO=%~dp0.."
set "RUNDIR=%REPO%\build\bin\Release"
set "EXE=%RUNDIR%\Launch.exe"
set "TAKE=%REPO%\Bench\take01.json"
set "OUT=%REPO%\Artifacts"

if not exist "%EXE%" (
    echo [FAIL] Release build not found: %EXE%
    pause
    exit /b 1
)
if not exist "%TAKE%" (
    echo [FAIL] camera take not found: %TAKE%
    echo.
    echo   Run Scripts\record_take.bat first and fly for 20+ seconds.
    echo   Without a take the two runs show different frames and the
    echo   comparison does not hold.
    pause
    exit /b 1
)

REM Full exe path: with NoDefaultCurrentDirectoryInExePath set, cmd does
REM not search the current directory and a bare Launch.exe exits 9009.
REM --debug-draw=off : the editor wireframes cover the view and add draw
REM calls. Applied to legacy and current alike, so the A/B is unaffected.
set "COMMON=--camera-replay=%TAKE% --vsync=off --debug-draw=off --width=1920 --height=1080 --warmup=5 --duration=15"

if not exist "%OUT%" mkdir "%OUT%"

echo ========================================================
echo  A/B capture - 4 runs, this takes several minutes
echo ========================================================
echo   take   : %TAKE%
echo   output : %OUT%
echo.

pushd "%RUNDIR%"

echo [1/4] metrics - legacy
"%EXE%" %COMMON% --legacy --csv="%OUT%\bench_legacy.csv"
if errorlevel 1 goto :Failed

echo [2/4] metrics - current
"%EXE%" %COMMON%          --csv="%OUT%\bench_current.csv"
if errorlevel 1 goto :Failed

echo [3/4] video frames - legacy
"%EXE%" %COMMON% --legacy --frames="%OUT%\legacy\%%06d.png"  --frame-stride=1
if errorlevel 1 goto :Failed

echo [4/4] video frames - current
"%EXE%" %COMMON%          --frames="%OUT%\current\%%06d.png" --frame-stride=1
if errorlevel 1 goto :Failed

popd

echo.
echo ========================================================
echo [OK] capture complete
echo.
if exist "%OUT%\bench_legacy.csv"  for %%A in ("%OUT%\bench_legacy.csv")  do echo   bench_legacy.csv  : %%~zA bytes
if exist "%OUT%\bench_current.csv" for %%A in ("%OUT%\bench_current.csv") do echo   bench_current.csv : %%~zA bytes
echo.
echo  Next: verify the metric gates, then encode with ffmpeg.
echo ========================================================
echo.
pause
exit /b 0

:Failed
popd
echo.
echo [FAIL] capture failed. Check the newest log in %RUNDIR%\Logs\
echo.
pause
exit /b 1
