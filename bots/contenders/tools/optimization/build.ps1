[CmdletBinding()]
param(
    [string]$Compiler = '',
    [string]$Output = '',
    [string]$Python = '',
    [switch]$Dynamic
)
$ErrorActionPreference = 'Stop'
$engineRoot = $PSScriptRoot
$repoRoot = [IO.Path]::GetFullPath((Join-Path $engineRoot '../../../..'))
if (-not $Compiler) {
    if ($env:CXX) { $Compiler = $env:CXX }
    elseif (Test-Path -LiteralPath 'C:\msys64\ucrt64\bin\g++.exe') {
        $Compiler = 'C:\msys64\ucrt64\bin\g++.exe'
    } else {
        $candidate = Get-Command g++ -ErrorAction SilentlyContinue
        if ($candidate) { $Compiler = $candidate.Source }
    }
}
if (-not $Compiler -or -not (Get-Command $Compiler -ErrorAction SilentlyContinue)) {
    throw 'A native C++20 compiler is required. Set CXX or pass -Compiler PATH (the WASI SDK compiler is not a native compiler).'
}
if (-not $Output) { $Output = Join-Path $engineRoot 'bin/optimizer.exe' }
$Output = [IO.Path]::GetFullPath($Output)
$sourceFiles = @(Get-ChildItem -LiteralPath (Join-Path $engineRoot 'src') -Filter '*.cpp' -File -Recurse | Sort-Object FullName | ForEach-Object FullName)
if (-not $sourceFiles.Count) { throw 'No engine sources found in src/.' }
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Output)) | Out-Null
$buildCache = if (Test-Path -LiteralPath (Join-Path $repoRoot 'arena.cmd')) {
    Join-Path $repoRoot '.cache/contenders/optimization/runtime-build'
} else { Join-Path $engineRoot '.cache/runtime-build' }
$stagingDirectory = Join-Path $buildCache ([Guid]::NewGuid().ToString('N'))
[IO.Directory]::CreateDirectory($stagingDirectory) | Out-Null
$stagingExecutable = Join-Path $stagingDirectory 'optimizer.exe'
$compilerArgs = @('-std=c++20', '-O2', '-DNDEBUG', '-Wall', '-Wextra', '-Wpedantic', '-pthread', '-I', (Join-Path $engineRoot 'include'))
if ($env:OS -eq 'Windows_NT') { $compilerArgs += '-municode' }
if (-not $Dynamic) { $compilerArgs += @('-static', '-static-libgcc', '-static-libstdc++') }
$compilerArgs += $sourceFiles
$compilerArgs += @('-o', $stagingExecutable)
$compilerSearchPath = $env:PATH
try {
    $compilerDirectory = Split-Path -Parent (Get-Command $Compiler).Source
    $env:PATH = $compilerDirectory + [IO.Path]::PathSeparator + $env:PATH
    if (Test-Path -LiteralPath (Join-Path $repoRoot 'tools/arena/core.py')) {
        if (-not $Python) {
            $pythonCandidates = @($env:OPTIMIZER_PYTHON, (Join-Path $repoRoot 'runtime/python/python.exe'), (Join-Path $repoRoot '.tools/python/python.exe'))
            if ($env:USERPROFILE) { $pythonCandidates += Join-Path $env:USERPROFILE '.cache/codex-runtimes/codex-primary-runtime/dependencies/python/python.exe' }
            $Python = $pythonCandidates | Where-Object { $_ -and (Test-Path -LiteralPath $_) } | Select-Object -First 1
        }
        if (-not $Python) { throw 'Python is required to participate in the repository slot pool. Pass -Python PATH.' }
        & $Python '-B' '-I' '-X' 'utf8' (Join-Path $engineRoot 'adapters/build_host.py') '--repo' $repoRoot '--' $Compiler @compilerArgs
    } else {
        # Standalone source packets have no shared arena scheduler. One foreground
        # compiler process is the local fallback, never a reserved campaign worker.
        & $Compiler @compilerArgs
    }
    if ($LASTEXITCODE -ne 0) { throw "Native engine build failed (exit $LASTEXITCODE)." }
} finally { $env:PATH = $compilerSearchPath }
Copy-Item -LiteralPath $stagingExecutable -Destination $Output -Force
Write-Output "Built $Output"
