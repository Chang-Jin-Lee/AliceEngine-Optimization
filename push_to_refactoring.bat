@echo off
setlocal enabledelayedexpansion
chcp 65001 >nul
REM ============================================================
REM  EGOSIS  ->  EGOSIS_Refactoring  백업 푸시 스크립트
REM  사용법: 이 파일을 더블클릭하거나, 터미널에서
REM          D:\Github\EGOSIS 에서  push_to_refactoring.bat  실행
REM  사전 조건: GitHub에 빈 레포(EGOSIS_Refactoring)가 만들어져 있어야 함
REM             (README/라이선스 없이 생성)
REM ============================================================

cd /d "%~dp0"

set "REMOTE_URL=https://github.com/Chang-Jin-Lee/EGOSIS_Refactoring.git"
set "BRANCH=dev"
REM 100MB 초과 스카이박스(HDR/MDR .dds, 합계 ~515MB)를 LFS로 포함하려면 1, 제외하려면 0
set "INCLUDE_SKYBOX=1"

echo === [1/6] Git / Git LFS 확인 ===
where git >nul 2>&1 || (echo [오류] git 이 설치되어 있지 않습니다. & pause & exit /b 1)
git lfs version >nul 2>&1
if errorlevel 1 (
  echo [오류] Git LFS 가 없습니다. https://git-lfs.com 에서 설치 후 다시 실행하세요.
  pause & exit /b 1
)
git lfs install

if "%INCLUDE_SKYBOX%"=="1" (
  echo === [2/6] 100MB 초과 스카이박스 파일 LFS 추적 ===
  git lfs track "Resource/Skybox/Sample/BakerSampleEnvHDR.dds"
  git lfs track "Resource/Skybox/Sample/BakerSampleEnvMDR.dds"
  git lfs track "Resource/Skybox/darkenv/darkEnvHDR.dds"
  git add .gitattributes
) else (
  echo === [2/6] 스카이박스 제외 모드 - LFS 건너뜀 ===
)

echo === [3/6] 변경사항 스테이징 ===
git add -A
if "%INCLUDE_SKYBOX%"=="1" (
  REM .gitignore 에 막혀 있으므로 강제 추가 (LFS 필터가 자동 적용됨)
  git add -f "Resource/Skybox/Sample/BakerSampleEnvHDR.dds"
  git add -f "Resource/Skybox/Sample/BakerSampleEnvMDR.dds"
  git add -f "Resource/Skybox/darkenv/darkEnvHDR.dds"
)

echo === [4/6] 커밋 ===
git commit -m "[chore] 백업: 코드/문서/위키/포트폴리오 (+스카이박스 LFS)" || echo (커밋할 변경이 없습니다 - 계속 진행)

echo === [5/6] 원격 backup 설정 ===
git remote get-url backup >nul 2>&1 && (git remote set-url backup "%REMOTE_URL%") || (git remote add backup "%REMOTE_URL%")

echo === [6/6] 푸시 (%BRANCH% -^> backup) ===
git push -u backup %BRANCH%
if errorlevel 1 (
  echo.
  echo [실패] 푸시 중 오류. 자주 있는 원인:
  echo   - 레포가 비어있지 않음(README 등). 빈 레포로 다시 만들거나 강제푸시 필요
  echo   - 로그인/권한 문제. GitHub 자격 증명을 확인하세요
  pause & exit /b 1
)

echo.
echo === 완료! https://github.com/Chang-Jin-Lee/EGOSIS_Refactoring 에서 확인하세요 ===
pause
endlocal
