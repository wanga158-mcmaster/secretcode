@echo off
rem Build and run the opponent-context native test (llvm-mingw); output under .cache\main\zero\context\.
setlocal
set ROOT=%~dp0..\..\..\..
set CXX=%ROOT%\.tools\toolchain\llvm-mingw-20261020-ucrt-x86_64\bin\clang++.exe
set OUT=%ROOT%\.cache\main\zero\context
set SRC=%~dp0..\src\core
if not exist "%OUT%" mkdir "%OUT%"
"%CXX%" -std=c++20 -O2 -static -Wall -Wextra -UNDEBUG "%~dp0context_test.cpp" "%SRC%\context.cpp" "%SRC%\catalog.cpp" -o "%OUT%\context_test.exe" || exit /b 1
"%OUT%\context_test.exe"
