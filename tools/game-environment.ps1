# Current mutable environment authority. Historical campaigns retain their own pins.
function Get-GameVersionManifest {
    param([string] $ProjectRoot)
    $manifestPath = Join-Path $ProjectRoot 'docs/reference/current-version.json'
    if (-not (Test-Path -LiteralPath $manifestPath -PathType Leaf)) { throw 'Current game manifest missing; complete the coordinated version reconciliation.' }
    $manifest = Get-Content -LiteralPath $manifestPath -Raw | ConvertFrom-Json
    if ($manifest.schemaVersion -ne 1 -or $manifest.event -ne 'cambc2026a' -or
        $manifest.cli.version -notmatch '^\d+\.\d+\.\d+$' -or
        $manifest.cli.windowsX64.sha256 -notmatch '^[a-f0-9]{64}$') {
        throw "Invalid current game manifest: $manifestPath"
    }
    return $manifest
}

function Assert-ExpectedGameFile {
    param([string] $Path, [string] $Sha256, [string] $Meaning = 'game input')
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing $Meaning`: $Path" }
    if ((Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash -ine $Sha256) {
        throw "Changed $Meaning`: $Path. Reconcile its version instead of reusing old qualification."
    }
}

function Assert-CurrentGameSdk {
    param([string] $ProjectRoot, $Manifest)
    if (-not $Manifest) { $Manifest = Get-GameVersionManifest -ProjectRoot $ProjectRoot }
    $config = Get-Content -LiteralPath (Join-Path $ProjectRoot 'cambc.toml') -Raw
    if ($config -notmatch '(?m)^\s*sdk_version\s*=\s*"([^"]+)"' -or $Matches[1] -ne $Manifest.sdk.version) {
        throw 'cambc.toml disagrees with the current SDK manifest.'
    }
    Assert-ExpectedGameFile -Path (Join-Path $ProjectRoot $Manifest.sdk.cppHeader.path) -Sha256 $Manifest.sdk.cppHeader.sha256 -Meaning 'SDK header'
    foreach ($entry in @($Manifest.sdk.files) + @($Manifest.cmake.files)) {
        Assert-ExpectedGameFile -Path (Join-Path $ProjectRoot $entry.path) -Sha256 $entry.sha256 -Meaning 'SDK/build input'
    }
}

function Get-PinnedCambc {
    param([string] $ProjectRoot, $Manifest)
    if (-not $Manifest) { $Manifest = Get-GameVersionManifest -ProjectRoot $ProjectRoot }
    # A stale local executable is an error. Never silently select .cache or PATH.
    $cliPath = Join-Path $ProjectRoot '.tools/cambc.exe'
    Assert-ExpectedGameFile -Path $cliPath -Sha256 $Manifest.cli.windowsX64.sha256 -Meaning 'pinned CLI (run tools/bootstrap.ps1 if absent)'
    $reported = (& $cliPath --version | Out-String).Trim()
    if ($LASTEXITCODE -ne 0 -or $reported -ne "cambc $($Manifest.cli.version)") {
        throw "Pinned CLI version mismatch: $reported"
    }
    return $cliPath
}

function Get-OfficialGamePool {
    param([string] $ProjectRoot, $Manifest)
    if (-not $Manifest) { $Manifest = Get-GameVersionManifest -ProjectRoot $ProjectRoot }
    $poolPath = Join-Path $ProjectRoot $Manifest.mapPool.path
    Assert-ExpectedGameFile -Path $poolPath -Sha256 $Manifest.mapPool.sha256 -Meaning 'official map-pool metadata'
    $pool = Get-Content -LiteralPath $poolPath -Raw | ConvertFrom-Json
    if (@($pool.maps).Count -ne $Manifest.mapPool.count) { throw 'Current pool count disagrees with its manifest.' }
    Assert-ExpectedGameFile -Path (Join-Path $ProjectRoot $Manifest.mapPool.fileIndex.path) -Sha256 $Manifest.mapPool.fileIndex.sha256 -Meaning 'official map filename index'
    return $pool
}

function Resolve-OfficialGameMap {
    param([string] $ProjectRoot, [string] $Name, $Manifest, $Pool)
    if (-not $Manifest) { $Manifest = Get-GameVersionManifest -ProjectRoot $ProjectRoot }
    if (-not $Pool) { $Pool = Get-OfficialGamePool -ProjectRoot $ProjectRoot -Manifest $Manifest }
    if ($Name -notmatch '^[A-Za-z0-9_-]+$') { throw "Use an exact current pool name or UUID: $Name" }
    $entries = @($Pool.maps | Where-Object { $_.name -ceq $Name -or $_.id -ceq $Name })
    if ($entries.Count -ne 1) { throw "Map is not uniquely in the current official pool: $Name. Historical filenames are not current aliases." }
    $entry = $entries[0]
    $indexPath = Join-Path $ProjectRoot $Manifest.mapPool.fileIndex.path
    Assert-ExpectedGameFile -Path $indexPath -Sha256 $Manifest.mapPool.fileIndex.sha256 -Meaning 'official map filename index'
    $index = Get-Content -LiteralPath $indexPath -Raw | ConvertFrom-Json
    $files = @($index.maps | Where-Object { $_.id -ceq $entry.id -and $_.sha256 -ceq $entry.sha256 })
    if ($files.Count -ne 1 -or $files[0].file -notmatch '^[A-Za-z0-9_-]+\.map26a$') { throw "Invalid official filename mapping: $Name" }
    $mapPath = Join-Path $ProjectRoot "maps/$($files[0].file)"
    Assert-ExpectedGameFile -Path $mapPath -Sha256 $entry.sha256 -Meaning 'official map payload'
    if ($entry.size -and (Get-Item -LiteralPath $mapPath).Length -ne $entry.size) { throw "Official map size mismatch: $Name" }
    return [pscustomobject]@{ Name = $entry.name; Id = $entry.id; Path = $mapPath; Sha256 = $entry.sha256 }
}
