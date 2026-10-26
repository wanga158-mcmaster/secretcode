@echo off
rem Repo launcher for the arena toolkit (python tools/arena ...), using the pinned Python runtime.
setlocal
set "ARENA_PY=%USERPROFILE%\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe"
if not exist "%ARENA_PY%" set "ARENA_PY=python"
"%ARENA_PY%" -B "%~dp0tools\arena" %*
