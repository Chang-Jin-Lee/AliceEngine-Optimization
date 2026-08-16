@echo off
setlocal EnableExtensions

REM ================================================================
REM 004 - 카메라 테이크 녹화 (사람이 직접 날아서 1회 녹화)
REM
REM 이 테이크 하나를 legacy / current 두 실행이 똑같이 재생하므로
REM 두 영상의 장면이 프레임 단위로 같아진다. 손으로 두 번 날면
REM 구간이 달라져서 "조건이 다른데 비교가 되냐"는 반박을 막을 수 없다.
REM ================================================================

set "REPO=%~dp0.."
set "EXE=%REPO%\build\bin\Release\Launch.exe"
REM 타일이 가장 밀집한 씬. 렌더독 기록의 원본 장면은
REM DuellumCycli/PrototypeDungeon.scene 이지만 씬과 애셋이 어긋나 있어
REM (타일 메시가 20000단위인데 격자 간격은 2) 촬영에는 이 씬을 쓴다.
set "SCENE=#01PrototypeMap.scene"
set "TAKE=%REPO%\Bench\take01.json"

if not exist "%EXE%" (
    echo [FAIL] Release 빌드가 없습니다: %EXE%
    echo        Build.bat 실행 후 솔루션에서 Release ^> Launch 를 빌드하십시오.
    pause
    exit /b 1
)

echo ========================================================
echo  카메라 테이크 녹화
echo ========================================================
echo.
echo   씬   : %SCENE%   ^(타일 407 / 벽 552 / 엔티티 1142^)
echo   출력 : %TAKE%
echo.
echo   [조작]
echo    - 우클릭 드래그로 시선, WASD 로 이동, 휠로 이동속도 조절
echo    - 타일이 많이 보이는 구간을 15초 정도 날아 주십시오
echo    - 앞 5초는 워밍업으로 버려지므로 시작은 천천히
echo    - 다 날았으면 창을 닫으면 저장됩니다
echo.
echo   씬 로딩에 30초 이상 걸립니다. 멈춘 것처럼 보여도 기다리십시오.
echo.
echo  아무 키나 누르면 시작합니다.
pause >nul

REM 출력은 절대 경로로 넘긴다. 상대 경로는 exe 작업 디렉터리 기준이라
REM 저장소의 Bench\ 가 아니라 build 폴더 안에 떨어진다.
pushd "%REPO%\build\bin\Release"
Launch.exe --scene=%SCENE% --camera-record="%TAKE%" --vsync=off --debug-draw=off
set "EL=%errorlevel%"
popd

echo.
if exist "%TAKE%" (
    echo [OK] 저장됨: %TAKE%
    for %%A in ("%TAKE%") do echo      크기: %%~zA bytes
    echo.
    echo  다음: Scripts\run_bench.bat 으로 A/B 캡처를 진행하십시오.
) else (
    echo [FAIL] 테이크가 저장되지 않았습니다. exit=%EL%
    echo        build\bin\Release\Logs\ 의 최신 로그에서 [Bench] 줄을 확인하십시오.
)
echo.
pause
