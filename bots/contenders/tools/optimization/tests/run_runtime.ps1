[CmdletBinding()]
param(
    [string]$Compiler = 'C:\msys64\ucrt64\bin\g++.exe',
    [string]$Scratch = '',
    [string]$Python = ''
)
$ErrorActionPreference = 'Stop'
$engineRoot = Split-Path -Parent $PSScriptRoot
$repoRoot = [IO.Path]::GetFullPath((Join-Path $engineRoot '../../../..'))
if (-not $Scratch) {
    $Scratch = if (Test-Path -LiteralPath (Join-Path $repoRoot 'arena.cmd')) {
        Join-Path $repoRoot '.cache/contenders/optimization/runtime-tests'
    } else { Join-Path $engineRoot '.cache/runtime-tests' }
}
$Scratch = [IO.Path]::GetFullPath($Scratch)
[IO.Directory]::CreateDirectory($Scratch) | Out-Null
$testExecutable = Join-Path $Scratch 'runtime-tests.exe'
$compilerArgs = @('-std=c++20', '-O2', '-Wall', '-Wextra', '-Wpedantic', '-pthread', '-static', '-static-libgcc', '-static-libstdc++', '-I', (Join-Path $engineRoot 'include'), (Join-Path $engineRoot 'src/common.cpp'), (Join-Path $PSScriptRoot 'runtime.cpp'), '-o', $testExecutable)
if ($env:OS -eq 'Windows_NT') { $compilerArgs += '-municode' }
$compilerSearchPath = $env:PATH
try {
    $compilerDirectory = Split-Path -Parent (Get-Command $Compiler).Source
    $env:PATH = $compilerDirectory + [IO.Path]::PathSeparator + $env:PATH
    if (Test-Path -LiteralPath (Join-Path $repoRoot 'tools/arena/core.py')) {
        if (-not $Python -and $env:USERPROFILE) { $Python = Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' }
        if (-not $Python -or -not (Test-Path -LiteralPath $Python)) { throw 'Pass -Python PATH for the repository slot pool.' }
        & $Python '-B' '-I' '-X' 'utf8' (Join-Path $engineRoot 'adapters/build_host.py') '--repo' $repoRoot '--' $Compiler @compilerArgs
    } else { & $Compiler @compilerArgs }
    if ($LASTEXITCODE -ne 0) { throw "Runtime test compilation failed (exit $LASTEXITCODE)." }
} finally { $env:PATH = $compilerSearchPath }
& $testExecutable '--scratch' $Scratch
if ($LASTEXITCODE -ne 0) { throw "Runtime tests failed (exit $LASTEXITCODE)." }
