<#
.SYNOPSIS
Seeds a local build's Windhawk symbol cache from the published mod's online cache.

.DESCRIPTION
Windhawk skips its online symbol cache for a local build (a local@ ID), which then needs
Microsoft's PDB and stays unloaded after a Windows update whose PDB isn't published yet. This
copies the published mod's online cache entry for the installed taskbar.dll into the local build's
cache, as Windhawk's own firewall workaround does. Disable and re-enable the mod afterwards.

The entry lists only the symbols the published version hooks: a local build that requires another
one still needs the PDB.

An entry already cached is kept (it may hold more symbols) unless -Force; a cached load error is
always replaced. -WhatIf reports without writing.

.PARAMETER Module
The hooked module. Default: taskbar.dll in System32.

.PARAMETER PublishedId
The published mod's ID, which names its online cache.

.PARAMETER SymbolCacheKey
The local build's symbol cache in the registry (a non-portable Windhawk install).
#>
[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$Module = (Join-Path $env:SystemRoot 'System32\taskbar.dll'),
    [string]$PublishedId = 'taskbar-ai-quota',
    [string]$SymbolCacheKey = 'HKLM:\SOFTWARE\Windhawk\Engine\ModsWritable\local@taskbar-ai-quota\SymbolCache',
    [switch]$Force
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# The cache key Windhawk's engine derives: the PDB's GUID and age (hex, unpadded).
$stream = [System.IO.File]::Open($Module, 'Open', 'Read', 'ReadWrite')
try {
    $pe = [System.Reflection.PortableExecutable.PEReader]::new($stream)
    $codeView = @($pe.ReadDebugDirectory() | Where-Object {
        $_.Type -eq [System.Reflection.PortableExecutable.DebugDirectoryEntryType]::CodeView
    })
    if ($codeView.Count -eq 0) {
        throw "No PDB reference in $Module"
    }
    $pdb = $pe.ReadCodeViewDebugDirectoryData($codeView[0])
} finally {
    $stream.Dispose()
}
$cacheKey = 'pdb_' + $pdb.Guid.ToString('N').ToUpperInvariant() + $pdb.Age.ToString('x')

$url = "https://ramensoftware.github.io/windhawk-mod-symbol-cache/$PublishedId/$cacheKey.txt"
$response = Invoke-WebRequest -Uri $url -SkipHttpErrorCheck -TimeoutSec 30
if ($response.StatusCode -eq 404) {
    throw "The published mod has no online cache entry for this $(Split-Path -Leaf $Module) yet: $url"
}
if ($response.StatusCode -ne 200) {
    throw "Online cache server returned status $($response.StatusCode): $url"
}
$entry = ([string]$response.Content).Trim()
$fields = $entry -split '#'
if ($fields.Count -lt 5 -or $fields[0] -ne '1' -or $entry -match '[\r\n]') {
    throw "Unexpected online cache entry format: $url"
}
"Module: $Module ($cacheKey)"
"Online entry: $([int](($fields.Count - 3) / 2)) symbols, $($fields[2])"

$existing = $null
if (Test-Path -LiteralPath $SymbolCacheKey) {
    $existing = (Get-Item -LiteralPath $SymbolCacheKey).GetValue($cacheKey)
}
if ($existing -eq $entry) {
    'The local cache already holds this entry.'
    return
}
if ($existing -and -not $existing.StartsWith('error:') -and -not $Force) {
    'The local cache already has an entry for this module; kept. Rerun with -Force to replace it.'
    return
}

if ($PSCmdlet.ShouldProcess("$SymbolCacheKey\$cacheKey", 'Write the online cache entry')) {
    if (-not (Test-Path -LiteralPath $SymbolCacheKey)) {
        New-Item -Path $SymbolCacheKey -Force | Out-Null
    }
    Set-ItemProperty -LiteralPath $SymbolCacheKey -Name $cacheKey -Value $entry -Type String
    'Seeded. Disable and re-enable the mod in Windhawk to load it.'
}
