@echo off
setlocal

rem 1. 서브모듈 업데이트
git -C .. submodule sync --recursive
git -C .. submodule update --init --recursive
if errorlevel 1 goto :Error

rem 2. 메인 프로젝트 생성
cmake -S .. -B ..\build -G "Visual Studio 17 2022"
if errorlevel 1 goto :Error

rem 3. ScriptsBuild 하위 프로젝트 생성
cmake -S ..\ScriptsBuild -B ..\ScriptsBuild\build -G "Visual Studio 17 2022"
if errorlevel 1 goto :Error

echo [OK] 모든 솔루션 생성이 완료되었습니다.
goto :End

:Error
echo.
echo [FAIL] 프로젝트 생성 중 오류가 발생했습니다. 위 로그를 확인하세요.

:End
endlocal
pause