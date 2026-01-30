@echo off
setlocal ENABLEDELAYEDEXPANSION

REM =========================================================================
REM [설정] 설치 경로 지정 (이 부분만 수정하면 됩니다)
REM 1. "AUTO" 로 설정 시: D드라이브가 있으면 D:\vcpkg, 없으면 C:\vcpkg 자동 선택
REM 2. 특정 경로 지정 시: 예) set "USER_DEFINED_PATH=E:\MyLibs\vcpkg"
REM =========================================================================
set "USER_DEFINED_PATH=AUTO"

REM ===============================================
REM AliceRenderer vcpkg 셋업 스크립트 (통합본)
REM ===============================================

echo [AliceRenderer] vcpkg 셋업을 시작합니다.

REM -----------------------------------------------------------
REM [1] Git 설치 여부 확인
REM -----------------------------------------------------------
where git >nul 2>nul
if %errorlevel% neq 0 (
    echo [오류] Git이 설치되어 있지 않거나 PATH에 없습니다.
    echo Git을 먼저 설치해주세요: https://git-scm.com/
    pause
    exit /b 1
)

REM -----------------------------------------------------------
REM [2] 설치 경로 결정 (우선순위: 사용자지정 > 환경변수 > 자동감지)
REM -----------------------------------------------------------

REM 1. 스크립트 상단 사용자 지정 경로 확인
if /i "%USER_DEFINED_PATH%" neq "AUTO" (
    set "TARGET_ROOT=%USER_DEFINED_PATH%"
    echo  - 스크립트 상단에 지정된 경로를 사용합니다: !TARGET_ROOT!
) else (
    REM 2. 시스템 환경변수 확인
    if defined VCPKG_ROOT (
        set "TARGET_ROOT=%VCPKG_ROOT%"
        echo  - 시스템 환경변수 VCPKG_ROOT를 사용합니다: !TARGET_ROOT!
    ) else (
        REM 3. 자동 감지 (D드라이브 유무)
        if exist "D:\" (
            set "TARGET_ROOT=D:\vcpkg"
            echo  - D드라이브가 감지되었습니다. 설치 경로: !TARGET_ROOT!
        ) else (
            set "TARGET_ROOT=C:\vcpkg"
            echo  - D드라이브가 없습니다. C드라이브에 설치합니다: !TARGET_ROOT!
        )
    )
)

set "VCPKG_EXE=%TARGET_ROOT%\vcpkg.exe"

REM -----------------------------------------------------------
REM [3] vcpkg 클론 및 부트스트랩
REM -----------------------------------------------------------

REM 폴더 자체가 없으면 클론
if not exist "%TARGET_ROOT%\.git" (
    echo.
    echo [1/5] vcpkg 저장소를 클론합니다...
    
    REM 폴더가 없으면 생성
    if not exist "%TARGET_ROOT%" mkdir "%TARGET_ROOT%"
    
    git clone https://github.com/microsoft/vcpkg.git "%TARGET_ROOT%"
    if !errorlevel! neq 0 (
        echo [오류] git clone 실패. 해당 폴더가 이미 존재하고 비어있지 않은지 확인하세요.
        pause
        exit /b 1
    )
) else (
    echo  - vcpkg 저장소가 이미 존재합니다. git pull로 업데이트를 시도합니다.
    pushd "%TARGET_ROOT%"
    git pull
    popd
)

REM vcpkg.exe가 없으면 빌드(bootstrap)
if not exist "%VCPKG_EXE%" (
    echo.
    echo [2/5] bootstrap-vcpkg.bat 실행 중...
    pushd "%TARGET_ROOT%"
    call bootstrap-vcpkg.bat
    if !errorlevel! neq 0 (
        echo [오류] bootstrap 실패.
        popd
        pause
        exit /b 1
    )
    popd
)

REM -----------------------------------------------------------
REM [4] 라이브러리 설치
REM -----------------------------------------------------------
echo.
echo [3/5] 필수 라이브러리 설치 (시간이 걸립니다)...

"%VCPKG_EXE%" install directxtk:x64-windows-static-md
"%VCPKG_EXE%" install directxtex[dx11]:x64-windows-static-md
"%VCPKG_EXE%" install imgui[dx11-binding]:x64-windows-static-md
"%VCPKG_EXE%" install imgui[win32-binding]:x64-windows-static-md --recurse
"%VCPKG_EXE%" install assimp:x64-windows
"%VCPKG_EXE%" install physx:x64-windows

REM -----------------------------------------------------------
REM [5] 스카이박스 리소스 다운로드 (GitHub Direct Link)
REM -----------------------------------------------------------
echo.
echo [4/5] 스카이박스 리소스 확인 및 다운로드...

REM GitHub Releases 링크
set "DOWNLOAD_URL=https://github.com/Chang-Jin-Lee/D3D11-AliceTutorial/releases/download/Skybox_2/Skybox.7z"

REM 현재 배치 파일이 있는 위치 기준으로 Resource 폴더 경로 설정
set "RES_ROOT=%~dp0..\Resource\Skybox"
set "TEMP_ARC=skybox_temp.7z"

REM 검사할 하위 폴더들
set "CHECK_DIR_1=%RES_ROOT%\Bridge"
set "CHECK_DIR_2=%RES_ROOT%\Sample"
set "CHECK_DIR_3=%RES_ROOT%\Indoor"

REM 세 폴더가 모두 존재하는지 확인
if exist "%CHECK_DIR_1%" (
    if exist "%CHECK_DIR_2%" (
        if exist "%CHECK_DIR_3%" (
            echo  - 이미 스카이박스 리소스가 존재합니다. 다운로드를 건너뜁니다.
            goto SKIP_RESOURCE_DOWNLOAD
        )
    )
)

echo  - 리소스가 누락되었습니다. 다운로드를 시작합니다.
if not exist "%RES_ROOT%" mkdir "%RES_ROOT%"

REM 1. 7zip 압축 해제용 툴(Standalone Console Version) 임시 다운로드
echo  - 압축 해제 도구(7zr.exe) 다운로드 중...
curl -L -o 7zr.exe https://www.7-zip.org/a/7zr.exe >nul 2>&1
if not exist "7zr.exe" (
    echo [오류] 7zr.exe 다운로드 실패. 인터넷 연결을 확인하세요.
    goto SKIP_RESOURCE_DOWNLOAD
)

REM 2. 파일 다운로드 (curl -L 옵션으로 리다이렉트 자동 처리)
echo  - 리소스 파일 다운로드 중... 
echo    URL: %DOWNLOAD_URL%
curl -L -o "%TEMP_ARC%" "%DOWNLOAD_URL%"

REM 파일 유효성 검사 (다운로드 실패 체크)
if not exist "%TEMP_ARC%" (
    echo [오류] 다운로드 파일이 생성되지 않았습니다.
    goto CLEANUP_AND_SKIP
)

REM 3. 압축 해제
echo  - 압축 해제 중...
7zr.exe x "%TEMP_ARC%" -o"%RES_ROOT%" -y >nul
if %errorlevel% neq 0 (
    echo [오류] 압축 해제 중 에러가 발생했습니다.
    goto CLEANUP_AND_SKIP
)

echo  - 리소스 설치 완료!

:CLEANUP_AND_SKIP
REM 4. 임시 파일 정리
echo  - 임시 파일 정리 중...
if exist 7zr.exe del 7zr.exe
if exist "%TEMP_ARC%" del "%TEMP_ARC%"

:SKIP_RESOURCE_DOWNLOAD

REM -----------------------------------------------------------
REM [6] Visual Studio 통합(삭제)
REM -----------------------------------------------------------
echo.
echo [5/5] Visual Studio 통합 설정 (User-wide)
REM "%VCPKG_EXE%" integrate install
"%VCPKG_EXE%" integrate remove

echo.
echo ========================================================
echo [완료] 모든 셋업이 끝났습니다.
echo Visual Studio를 재시작하면 라이브러리를 사용할 수 있습니다.
echo 설치 위치: %TARGET_ROOT%
echo ========================================================
echo.
pause
exit /b 0