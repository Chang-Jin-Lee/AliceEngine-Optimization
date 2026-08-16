@echo off
setlocal EnableExtensions EnableDelayedExpansion

REM ================================================================
REM 005 - A/B 캡처
REM
REM 계측용 2회(--frames 없이) + 영상 소재용 2회(--frames 붙여서).
REM PNG 덤프가 프레임 시간을 왜곡하므로 두 목적을 섞지 않는다.
REM 계측 수치는 1~2번 실행의 CSV만 쓰고, 3~4번 실행의 CSV는 쓰지 않는다.
REM
REM 재생 모드에서는 warmup/duration이 모두 테이크 시간(프레임 인덱스) 기준이라
REM legacy와 current가 정확히 같은 프레임 구간을 기록한다.
REM ================================================================

set "REPO=%~dp0.."
set "RUNDIR=%REPO%\build\bin\Release"
set "EXE=%RUNDIR%\Launch.exe"
set "TAKE=%REPO%\Bench\take01.json"
set "OUT=%REPO%\Artifacts"

if not exist "%EXE%" (
    echo [FAIL] Release 빌드가 없습니다: %EXE%
    pause
    exit /b 1
)
if not exist "%TAKE%" (
    echo [FAIL] 카메라 테이크가 없습니다: %TAKE%
    echo.
    echo   Scripts\record_take.bat 을 먼저 실행해 15초 이상 녹화하십시오.
    echo   테이크 없이 손으로 두 번 날면 두 실행의 장면이 달라져 비교가 성립하지 않습니다.
    pause
    exit /b 1
)

REM --debug-draw=off : 에디터 디버그 와이어프레임은 화면을 가리고 드로우콜에도 얹힌다.
REM legacy/current 양쪽에 동일하게 적용되므로 A/B 비교는 왜곡되지 않는다.
set "COMMON=--camera-replay=%TAKE% --vsync=off --debug-draw=off --width=1920 --height=1080 --warmup=5 --duration=15"

if not exist "%OUT%" mkdir "%OUT%"

echo ========================================================
echo  A/B 캡처 (4회 실행, 수 분 걸립니다)
echo ========================================================
echo   테이크 : %TAKE%
echo   출력   : %OUT%
echo.

pushd "%RUNDIR%"

echo [1/4] 계측 - legacy
Launch.exe %COMMON% --legacy --csv="%OUT%\bench_legacy.csv"
if errorlevel 1 goto :Failed

echo [2/4] 계측 - current
Launch.exe %COMMON%          --csv="%OUT%\bench_current.csv"
if errorlevel 1 goto :Failed

echo [3/4] 영상 소재 - legacy
Launch.exe %COMMON% --legacy --frames="%OUT%\legacy\%%06d.png"  --frame-stride=1
if errorlevel 1 goto :Failed

echo [4/4] 영상 소재 - current
Launch.exe %COMMON%          --frames="%OUT%\current\%%06d.png" --frame-stride=1
if errorlevel 1 goto :Failed

popd

echo.
echo ========================================================
echo [OK] 캡처 완료
echo.
for %%F in ("%OUT%\bench_legacy.csv" "%OUT%\bench_current.csv") do (
    if exist %%F (
        for /f %%C in ('find /c /v "" ^< %%F') do echo   %%~nxF : %%C lines
    )
)
echo.
echo  다음: 수치 게이트 검증 후 ffmpeg 인코딩
echo ========================================================
echo.
pause
exit /b 0

:Failed
popd
echo.
echo [FAIL] 캡처 중 실패했습니다. %RUNDIR%\Logs\ 의 최신 로그를 확인하십시오.
echo.
pause
exit /b 1
