[CmdletBinding()]
param(
    [Parameter(Position = 0)][string]$Engine = 'help',
    [double]$Hours = 24,
    [Alias('Input')][string]$InputPath = '',
    [string]$Out = '',
    [string]$Repo = '',
    [UInt64]$Iterations = 100000,
    [UInt64]$Seed = 1,
    [UInt32]$Threads = 1,
    [switch]$Resume,
    [switch]$Build,
    [string]$Compiler = '',
    [string]$Portfolio = '',
    [string]$Worker = '',
    [Parameter(ValueFromRemainingArguments = $true)][string[]]$EngineArgs
)
$ErrorActionPreference = 'Stop'
$engineRoot = $PSScriptRoot
if ($Worker -and -not $Portfolio) { $Portfolio = Join-Path $engineRoot 'examples/portfolio-three-workers.json' }
if ($Portfolio) {
    if (-not [IO.Path]::IsPathRooted($Portfolio) -and -not (Test-Path -LiteralPath $Portfolio)) { $Portfolio = Join-Path $engineRoot $Portfolio }
    $portfolioDefinition = Get-Content -LiteralPath $Portfolio -Raw | ConvertFrom-Json
    if ($portfolioDefinition.schema -ne 'optimizer-portfolio/v1') { throw 'Unsupported portfolio schema.' }
    if (-not $Worker) { throw 'Choose one portfolio worker with -Worker A, B or C; workers run independently.' }
    $workerDefinition = $portfolioDefinition.workers | Where-Object { $_.id -eq $Worker } | Select-Object -First 1
    if (-not $workerDefinition) { throw "Portfolio worker not found: $Worker" }
    $Engine = [string]$workerDefinition.engine
    if (-not $PSBoundParameters.ContainsKey('Hours') -and $workerDefinition.PSObject.Properties['hours']) { $Hours = [double]$workerDefinition.hours }
    if (-not $PSBoundParameters.ContainsKey('Iterations') -and $workerDefinition.PSObject.Properties['iterations']) { $Iterations = [UInt64]$workerDefinition.iterations }
    if (-not $PSBoundParameters.ContainsKey('Seed') -and $workerDefinition.PSObject.Properties['seed']) { $Seed = [UInt64]$workerDefinition.seed }
    if (-not $PSBoundParameters.ContainsKey('Threads') -and $workerDefinition.PSObject.Properties['threads']) { $Threads = [UInt32]$workerDefinition.threads }
    if (-not $InputPath -and $workerDefinition.input) { $InputPath = [string]$workerDefinition.input }
    if (-not $Out -and $workerDefinition.out) { $Out = [string]$workerDefinition.out }
}
$Engine = $Engine.ToLowerInvariant()
if ($Engine -eq 'micro') { $Engine = 'c12' }
if ($Hours -lt 0 -or [double]::IsNaN($Hours) -or [double]::IsInfinity($Hours)) { throw '-Hours must be a finite nonnegative number; zero means no time cutoff.' }
if ($Threads -eq 0) { throw '-Threads must be positive.' }
if (-not $Repo) {
    $candidateRoot = [IO.DirectoryInfo]::new($engineRoot)
    while ($candidateRoot) {
        if ((Test-Path -LiteralPath (Join-Path $candidateRoot.FullName 'packet-manifest.json')) -or
            (Test-Path -LiteralPath (Join-Path $candidateRoot.FullName 'arena.cmd'))) {
            $Repo = $candidateRoot.FullName
            break
        }
        $candidateRoot = $candidateRoot.Parent
    }
    if (-not $Repo) { $Repo = [IO.Path]::GetFullPath((Join-Path $engineRoot '../../../..')) }
}
$Repo = [IO.Path]::GetFullPath($Repo)
$binary = Join-Path $engineRoot 'bin/optimizer.exe'
if ($Build -or -not (Test-Path -LiteralPath $binary)) {
    $buildArguments = @{}
    if ($Compiler) { $buildArguments.Compiler = $Compiler }
    & (Join-Path $engineRoot 'build.ps1') @buildArguments
}
if (-not (Test-Path -LiteralPath $binary)) { throw "Missing executable: $binary" }

$entry = $null
$registry = Join-Path $engineRoot 'engines.json'
if (Test-Path -LiteralPath $registry) {
    $catalog = Get-Content -LiteralPath $registry -Raw | ConvertFrom-Json
    if ($catalog.engines -is [System.Array]) { $entry = $catalog.engines | Where-Object { $_.id -eq $Engine -or $_.mode -eq $Engine } | Select-Object -First 1 }
    elseif ($catalog.engines -and $catalog.engines.PSObject.Properties[$Engine]) { $entry = $catalog.engines.$Engine }
    elseif ($catalog.PSObject.Properties[$Engine]) { $entry = $catalog.$Engine }
}
if ($Engine -notin @('help', 'list', 'status')) {
    if (-not $InputPath) {
        if ($entry -and $entry.default_config) { $InputPath = [string]$entry.default_config }
        else {
            $configFiles = @(Get-ChildItem -LiteralPath (Join-Path $engineRoot 'examples') -Filter "$Engine-*.json" -File | Sort-Object Name)
            $preferred = $configFiles | Where-Object { $_.BaseName -notmatch 'smoke|request|fixture|test|portfolio' } | Select-Object -First 1
            if (-not $preferred) { $preferred = $configFiles | Select-Object -First 1 }
            if ($preferred) { $InputPath = $preferred.FullName }
        }
        if ($InputPath -and -not [IO.Path]::IsPathRooted($InputPath)) {
            $fromEngine = Join-Path $engineRoot $InputPath
            $InputPath = if (Test-Path -LiteralPath $fromEngine) { $fromEngine } else { Join-Path $Repo $InputPath }
        }
    }
    if ($InputPath -and -not [IO.Path]::IsPathRooted($InputPath) -and -not (Test-Path -LiteralPath $InputPath)) { $InputPath = Join-Path $engineRoot $InputPath }
    if (-not $InputPath -or -not (Test-Path -LiteralPath $InputPath -PathType Leaf)) { throw "No configuration found for $Engine. Supply -Input PATH." }
    $InputPath = [IO.Path]::GetFullPath($InputPath)
    if (-not $Out) { $Out = Join-Path $Repo "results/contenders/optimization/campaigns/$Engine" }
    elseif (-not [IO.Path]::IsPathRooted($Out)) { $Out = Join-Path $Repo $Out }
    $Out = [IO.Path]::GetFullPath($Out)
    $packetManifest = Join-Path $Repo 'packet-manifest.json'
    if ((Test-Path -LiteralPath $packetManifest) -and $entry) {
        $packet = Get-Content -LiteralPath $packetManifest -Raw | ConvertFrom-Json
        $runtimeFlavor = if ($entry.runtime) { [string]$entry.runtime } elseif ($entry.runtime_flavor) { [string]$entry.runtime_flavor } else { '' }
        if ($packet.kind -eq 'model' -and $runtimeFlavor -match 'native') {
            throw "$Engine needs native game runtimes. Export a native packet, or select an explicitly model/data configuration."
        }
    }
}
if ($Engine -eq 'status') {
    if (-not $Out) { throw 'status requires -Out DIRECTORY.' }
    if (-not [IO.Path]::IsPathRooted($Out)) { $Out = Join-Path $Repo $Out }
    $Out = [IO.Path]::GetFullPath($Out)
}

$environmentNames = @('LOCALAPPDATA', 'PATH', 'ARENA_NODE', 'ARENA_CLANG', 'OPT_NODE', 'OPT_GAME_ENGINE', 'CAMBC_DATA', 'CAMBC_CACHE', 'CAMBC_ASSET_CACHE', 'CAMBC_CONFIG', 'CAMBC_OFFLINE', 'CAMBC_NO_UPDATE_CHECK')
$savedEnvironment = @{}
foreach ($environmentName in $environmentNames) { $savedEnvironment[$environmentName] = [Environment]::GetEnvironmentVariable($environmentName, 'Process') }
try {
    $packetLocal = Join-Path $Repo 'runtime/localappdata'
    if (Test-Path -LiteralPath $packetLocal -PathType Container) {
        $env:LOCALAPPDATA = $packetLocal
        $packetCache = Join-Path $packetLocal 'cambc'
        $env:CAMBC_DATA = $packetCache
        $env:CAMBC_CACHE = $packetCache
        $env:CAMBC_ASSET_CACHE = Join-Path $packetCache 'assets'
        $env:CAMBC_CONFIG = Join-Path $Repo 'runtime/config'
        $env:CAMBC_OFFLINE = 'true'
        $env:CAMBC_NO_UPDATE_CHECK = 'true'
        $env:ARENA_CLANG = Join-Path $packetCache 'wasi-sdk-33/bin/clang++.exe'
    }
    $pythonExecutable = Join-Path $Repo 'runtime/python/python.exe'
    if (Test-Path -LiteralPath $pythonExecutable) { $env:PATH = (Split-Path -Parent $pythonExecutable) + [IO.Path]::PathSeparator + $env:PATH }
    $nodeExecutable = Join-Path $Repo 'runtime/node/bin/node.exe'
    if (Test-Path -LiteralPath $nodeExecutable) { $env:ARENA_NODE = $nodeExecutable; $env:OPT_NODE = $nodeExecutable }
    $gameplayEngine = Join-Path $engineRoot 'assets/oracle/gameplay.wasm'
    if (Test-Path -LiteralPath $gameplayEngine) { $env:OPT_GAME_ENGINE = $gameplayEngine }
    $commandArguments = @($Engine)
    if ($Engine -eq 'status') {
        $commandArguments += @('--out', $Out, '--repo', $Repo)
    } elseif ($Engine -notin @('help', 'list')) {
        $seconds = ($Hours * 3600).ToString('R', [Globalization.CultureInfo]::InvariantCulture)
        $commandArguments += @('--input', $InputPath, '--out', $Out, '--repo', $Repo,
            '--iterations', $Iterations.ToString(), '--seconds', $seconds, '--seed', $Seed.ToString(), '--threads', $Threads.ToString())
        if ($Resume) { $commandArguments += '--resume' }
        if (Test-Path -LiteralPath $pythonExecutable) { $commandArguments += @('--python', $pythonExecutable) }
    }
    if ($EngineArgs) { $commandArguments += $EngineArgs }
    & $binary @commandArguments
    $engineExitCode = $LASTEXITCODE
} finally {
    foreach ($environmentName in $environmentNames) { [Environment]::SetEnvironmentVariable($environmentName, $savedEnvironment[$environmentName], 'Process') }
}
exit $engineExitCode
