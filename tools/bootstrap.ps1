# Pin both the CLI version and bytes. SDK and map sources are in the repository;
# this does not run init, overwrite the SDK, authenticate, or submit a bot.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$projectRoot = Split-Path $PSScriptRoot -Parent
. (Join-Path $PSScriptRoot 'game-environment.ps1')
$manifest = Get-GameVersionManifest -ProjectRoot $projectRoot
$version = $manifest.cli.version
$expectedHash = $manifest.cli.windowsX64.sha256
$targetDirectory = Join-Path $projectRoot '.tools'
$destination = Join-Path $targetDirectory 'cambc.exe'

function Test-PinnedCli([string] $Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { return $false }
    if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ine $expectedHash) { return $false }
    $reportedVersion = (& $Path --version | Out-String).Trim()
    return $LASTEXITCODE -eq 0 -and $reportedVersion -eq "cambc $version"
}

if (Test-PinnedCli $destination) {
    Write-Host "CAMBC $version is already installed and verified."
    exit 0
}

New-Item -ItemType Directory -Path $targetDirectory -Force | Out-Null
$temporary = Join-Path $targetDirectory ('cambc-' + [guid]::NewGuid().ToString('N') + '.exe')
try {
    $cached = Join-Path $projectRoot ".cache/cambc/cambc-$version.exe"
    if (Test-PinnedCli $cached) {
        Copy-Item -LiteralPath $cached -Destination $temporary
    }
    else {
        if (-not [Environment]::Is64BitOperatingSystem -or
            $env:PROCESSOR_ARCHITECTURE -eq 'ARM64') {
            throw 'This bootstrap pins the official Windows x64 distribution only.'
        }
        $metadata = Invoke-RestMethod -Uri 'https://2026a.battlecode.co.uk/api/v1/distribution'
        $published = @($metadata.platforms | Where-Object platform -eq 'windows-x64')[0].published
        if ($published.version -ne $version -or $published.sha256 -ine $expectedHash) {
            throw "The current distribution is not pinned CAMBC $version. Review an SDK/CLI upgrade explicitly before updating this pin."
        }
        $download = [uri] $published.downloadUrl
        if ($download.Scheme -ne 'https' -or $download.Host -ne '2026a.battlecode.co.uk') {
            throw 'The official distribution API returned an unexpected download origin.'
        }
        Write-Host "Downloading official CAMBC $version (Windows x64)..."
        Invoke-WebRequest -Uri $download.AbsoluteUri -OutFile $temporary
    }
    if (-not (Test-PinnedCli $temporary)) {
        throw 'CLI SHA256 mismatch; downloaded bytes were not installed.'
    }
    Move-Item -LiteralPath $temporary -Destination $destination -Force
}
finally {
    if (Test-Path -LiteralPath $temporary) { Remove-Item -LiteralPath $temporary }
}

Write-Host "Installed and verified CAMBC $version at $destination"
