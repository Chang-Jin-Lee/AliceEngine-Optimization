@echo off
setlocal ENABLEDELAYEDEXPANSION

set "USER_DEFINED_PATH=AUTO"

echo [AliceRenderer] vcpkg 설치를 시작합니다.

where git >nul 2>nul
if %errorlevel% neq 0 (
    echo [오류] Git이 없습니다. https://git-scm.com/
    if not defined CALLED_FROM_BUILD pause
    exit /b 1
)

if /i "%USER_DEFINED_PATH%" neq "AUTO" (
    set "TARGET_ROOT=%USER_DEFINED_PATH%"
    echo  - 지정된 경로: !TARGET_ROOT!
) else if defined VCPKG_ROOT (
    set "TARGET_ROOT=%VCPKG_ROOT%"
    echo  - 환경변수 VCPKG_ROOT: !TARGET_ROOT!
) else if exist "D:\" (
    set "TARGET_ROOT=D:\vcpkg"
    echo  - D드라이브 감지. 설치 위치: !TARGET_ROOT!
) else (
    set "TARGET_ROOT=C:\vcpkg"
    echo  - C드라이브에 설치합니다: !TARGET_ROOT!
)

set "VCPKG_EXE=%TARGET_ROOT%\vcpkg.exe"

if not exist "%TARGET_ROOT%\.git" (
    echo.
    echo [1/5] vcpkg 저장소를 클론합니다...
    if not exist "%TARGET_ROOT%" mkdir "%TARGET_ROOT%"
    git clone --depth 1 --single-branch -b master https://github.com/microsoft/vcpkg.git "%TARGET_ROOT%"
    if !errorlevel! neq 0 (
        echo [오류] git clone 실패.
        if not defined CALLED_FROM_BUILD pause
        exit /b 1
    )
) else (
    echo  - vcpkg 저장소가 이미 있습니다. 업데이트 중...
    pushd "%TARGET_ROOT%"
    git fetch --depth 1 && git reset --hard @{u}
    popd
)

if not exist "%VCPKG_EXE%" (
    echo.
    echo [2/5] bootstrap-vcpkg.bat 실행 중...
    pushd "%TARGET_ROOT%"
    call bootstrap-vcpkg.bat
    if !errorlevel! neq 0 (
        echo [오류] bootstrap 실패.
        popd
        if not defined CALLED_FROM_BUILD pause
        exit /b 1
    )
    popd
)

echo.
echo [3/5] 필수 라이브러리 설치 (시간이 걸립니다)...
"%VCPKG_EXE%" install directxtk:x64-windows-static-md
"%VCPKG_EXE%" install directxtex[dx11]:x64-windows-static-md
"%VCPKG_EXE%" install assimp:x64-windows
"%VCPKG_EXE%" install physx:x64-windows

echo.
echo [4/5] 스카이박스 리소스 확인 및 다운로드...
set "DOWNLOAD_URL=https://github.com/Chang-Jin-Lee/D3D11-AliceTutorial/releases/download/Skybox_2/Skybox.7z"
set "RES_ROOT=%~dp0..\Resource\Skybox"
set "TEMP_ARC=skybox_temp.7z"

if exist "%RES_ROOT%\Bridge" if exist "%RES_ROOT%\Sample" if exist "%RES_ROOT%\Indoor" (
    echo  - 이미 스카이박스 리소스가 있습니다. 건너뜁니다.
    goto SKIP_SKYBOX
)
echo  - 리소스 다운로드를 시작합니다.
if not exist "%RES_ROOT%" mkdir "%RES_ROOT%"
rem 7-Zip 탐색: PATH > 고정경로 > winget > curl > PowerShell
set "SEVEN_ZIP="
where 7z.exe  >nul 2>nul
if not errorlevel 1 set "SEVEN_ZIP=7z.exe"
if not defined SEVEN_ZIP if exist "C:\Program Files\7-Zip\7z.exe"         set "SEVEN_ZIP=C:\Program Files\7-Zip\7z.exe"
if not defined SEVEN_ZIP if exist "C:\Program Files (x86)\7-Zip\7z.exe"  set "SEVEN_ZIP=C:\Program Files (x86)\7-Zip\7z.exe"
if not defined SEVEN_ZIP if exist "%LOCALAPPDATA%\Programs\7-Zip\7z.exe" set "SEVEN_ZIP=%LOCALAPPDATA%\Programs\7-Zip\7z.exe"
if not defined SEVEN_ZIP where 7zr.exe >nul 2>nul
if not defined SEVEN_ZIP if not errorlevel 1 (
    7zr.exe i >nul 2>&1
    if not errorlevel 1 set "SEVEN_ZIP=7zr.exe"
)
if not defined SEVEN_ZIP (
    echo  - 7-Zip winget 자동설치 중...
    winget install --id 7zip.7zip --silent --accept-package-agreements --accept-source-agreements >nul 2>&1
    if exist "C:\Program Files\7-Zip\7z.exe"         set "SEVEN_ZIP=C:\Program Files\7-Zip\7z.exe"
    if not defined SEVEN_ZIP if exist "%LOCALAPPDATA%\Programs\7-Zip\7z.exe" set "SEVEN_ZIP=%LOCALAPPDATA%\Programs\7-Zip\7z.exe"
)
if not defined SEVEN_ZIP (
    curl -L --max-time 30 -o 7zr.exe https://www.7-zip.org/a/7zr.exe >nul 2>&1
    if exist "7zr.exe" (
        7zr.exe i >nul 2>&1
        if not errorlevel 1 set "SEVEN_ZIP=7zr.exe"
    )
)
if not defined SEVEN_ZIP (
    powershell -NoProfile -Command "try{Invoke-WebRequest 'https://www.7-zip.org/a/7zr.exe' -OutFile '7zr.exe' -UseBasicParsing -TimeoutSec 30}catch{exit 1}" 2>nul
    if exist "7zr.exe" (
        7zr.exe i >nul 2>&1
        if not errorlevel 1 set "SEVEN_ZIP=7zr.exe"
    )
)
if not defined SEVEN_ZIP ( echo [경고] 7-Zip을 찾을 수 없어 스카이박스 다운로드를 건너뜁니다. & goto SKIP_SKYBOX )
curl -L -o "%TEMP_ARC%" "%DOWNLOAD_URL%"
if not exist "%TEMP_ARC%" ( echo [오류] 다운로드 실패. & goto :CLEANUP_SKYBOX )
"%SEVEN_ZIP%" x "%TEMP_ARC%" -o"%RES_ROOT%" -y >nul
echo  - 리소스 설치 완료!
:CLEANUP_SKYBOX
if exist 7zr.exe erase 7zr.exe
if exist "%TEMP_ARC%" erase "%TEMP_ARC%"
:SKIP_SKYBOX

echo.
echo [5/5] Visual Studio 통합 설정
"%VCPKG_EXE%" integrate remove

echo.
echo ========================================================
echo [완료] 모든 설치가 완료되었습니다.
echo Visual Studio를 재시작하면 라이브러리를 사용할 수 있습니다.
echo 설치 위치: %TARGET_ROOT%
echo ========================================================
echo.
if not defined CALLED_FROM_BUILD pause
goto :eof
