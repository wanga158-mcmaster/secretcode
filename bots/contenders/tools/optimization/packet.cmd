@echo off
setlocal
if /i "%~1"=="verify" if exist "%~dp0bin\optimizer.exe" goto native_verify
set "PACKET_PYTHON=%OPT_PYTHON%"
if not defined PACKET_PYTHON if exist "%~dp0..\..\..\..\runtime\python\python.exe" set "PACKET_PYTHON=%~dp0..\..\..\..\runtime\python\python.exe"
if not defined PACKET_PYTHON if exist "%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe" set "PACKET_PYTHON=%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
if not defined PACKET_PYTHON set "PACKET_PYTHON=python3"
"%PACKET_PYTHON%" -B -I -X utf8 "%~dp0packet.py" %*
exit /b %errorlevel%

:native_verify
if "%~2"=="" (
  echo Usage: packet.cmd verify DIRECTORY [--strict] 1>&2
  exit /b 2
)
"%~dp0bin\optimizer.exe" verify-packet --repo "%~2" %3 %4 %5 %6 %7 %8 %9
exit /b %errorlevel%
