@echo off
setlocal

rem 1. ������ ������Ʈ
git -C .. submodule sync --recursive
git -C .. submodule update --init --recursive
if errorlevel 1 goto :Error

rem 2. ���� ������Ʈ ����
cmake -S .. -B ..\build -G "Visual Studio 17 2022"
if errorlevel 1 goto :Error

rem 3. ScriptsBuild ���� ������Ʈ ����
cmake -S ..\EngineSource\ScriptsBuild -B ..\EngineSource\ScriptsBuild\build -G "Visual Studio 17 2022"
if errorlevel 1 goto :Error

echo [OK] ��� �ַ�� ������ �Ϸ�Ǿ����ϴ�.
goto :End

:Error
echo.
echo [FAIL] ������Ʈ ���� �� ������ �߻��߽��ϴ�. �� �α׸� Ȯ���ϼ���.

:End
endlocal
pause