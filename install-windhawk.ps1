[CmdletBinding()]
param(
    [string]$Source = (Join-Path $PSScriptRoot 'local@taskbar-ai-quota.wh.cpp'),
    [string]$ModsSource = 'C:\ProgramData\Windhawk\ModsSource',
    [string]$StatePath = (Join-Path $env:LOCALAPPDATA 'windhawk-taskbar-ai-quota\install-state.json'),
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
    throw "Source file not found: $Source"
}

if (-not (Test-Path -LiteralPath $ModsSource -PathType Container)) {
    throw "Windhawk ModsSource folder not found: $ModsSource"
}

$sourcePath = (Resolve-Path -LiteralPath $Source).Path
if ($sourcePath -ieq (Join-Path $PSScriptRoot 'local@taskbar-ai-quota.wh.cpp')) {
    & (Join-Path $PSScriptRoot 'generate-mod.ps1') -Check
    if ($LASTEXITCODE -ne 0) {
        throw 'The generated mod is stale or src/ is invalid; run generate-mod.ps1'
    }
}

$destination = Join-Path (Resolve-Path -LiteralPath $ModsSource).Path (Split-Path -Leaf $Source)

# The state file records the SHA256 last installed per destination, so an installed copy edited
# in Windhawk's editor is never overwritten silently.
$state = @{}
if (Test-Path -LiteralPath $StatePath -PathType Leaf) {
    $state = Get-Content -LiteralPath $StatePath -Raw | ConvertFrom-Json -AsHashtable
    if ($state -isnot [System.Collections.IDictionary]) {
        throw "Invalid install state file: $StatePath"
    }
}
$stateKey = $destination.ToLowerInvariant()
$sourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
if (-not $Force -and (Test-Path -LiteralPath $destination -PathType Leaf)) {
    $destinationHash = (Get-FileHash -LiteralPath $destination -Algorithm SHA256).Hash
    if ($destinationHash -ne $sourceHash -and $destinationHash -ne $state[$stateKey]) {
        throw ("Refusing to overwrite $destination; it differs from the last copy installed from " +
            "this repo (edited in Windhawk's editor?). Port those edits into src/ and run " +
            'generate-mod.ps1, or rerun with -Force to discard them.')
    }
}

try {
    Copy-Item -LiteralPath $sourcePath -Destination $destination -Force
}
catch [System.UnauthorizedAccessException] {
    throw "Access denied installing to $ModsSource. Run PowerShell as Administrator."
}

$state[$stateKey] = $sourceHash
$stateDirectory = Split-Path -Parent $StatePath
if ($stateDirectory -and -not (Test-Path -LiteralPath $stateDirectory -PathType Container)) {
    New-Item -ItemType Directory -Path $stateDirectory | Out-Null
}
$state | ConvertTo-Json | Set-Content -LiteralPath $StatePath -Encoding utf8NoBOM
"Installed: $destination"
