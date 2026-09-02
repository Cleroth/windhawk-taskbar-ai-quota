[CmdletBinding()]
param(
    [string]$Source = (Join-Path $PSScriptRoot 'local@taskbar-ai-quota.wh.cpp'),
    [string]$Compiler = 'C:\Program Files\Windhawk\Compiler\bin\clang++.exe',
    [string]$IncludeDirectory = 'C:\Program Files\Windhawk\Compiler\include'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
    throw "Source file not found: $Source"
}
if ((Split-Path -Leaf $Source) -ne 'local@taskbar-ai-quota.wh.cpp') {
    throw "Unexpected source filename: $Source"
}
if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) {
    throw "Windhawk compiler not found: $Compiler"
}
if (-not (Test-Path -LiteralPath $IncludeDirectory -PathType Container)) {
    throw "Windhawk include directory not found: $IncludeDirectory"
}

$sourcePath = (Resolve-Path -LiteralPath $Source).Path
$sourceText = [System.IO.File]::ReadAllText($sourcePath)
$expectedMetadata = [ordered]@{
    id = 'taskbar-ai-quota'
    github = 'https://github.com/Cleroth'
    license = 'MIT'
}

foreach ($entry in $expectedMetadata.GetEnumerator()) {
    $pattern = '(?m)^//\s+@' + [regex]::Escape($entry.Key) + '\s+(\S+)\s*$'
    $match = [regex]::Match($sourceText, $pattern)
    if (-not $match.Success) {
        throw "Missing @$($entry.Key) metadata"
    }
    if ($match.Groups[1].Value -ne $entry.Value) {
        throw "Unexpected @$($entry.Key): $($match.Groups[1].Value)"
    }
}

$versionMatch = [regex]::Match($sourceText, '(?m)^//\s+@version\s+(\S+)\s*$')
if (-not $versionMatch.Success -or $versionMatch.Groups[1].Value -notmatch '^\d+\.\d+\.\d+$') {
    throw 'Missing or invalid @version metadata'
}

$compilerArguments = @(
    '--target=x86_64-w64-mingw32'
    '-std=c++23'
    '-DWH_EDITING'
    '-DWH_MOD'
    '-DUNICODE'
    '-D_UNICODE'
    '-municode'
    '-Wall'
    "-I$IncludeDirectory"
    '-fsyntax-only'
    $sourcePath
)

& $Compiler @compilerArguments
if ($LASTEXITCODE -ne 0) {
    throw "Windhawk clang check failed with exit code $LASTEXITCODE"
}

"Validated $sourcePath (@version $($versionMatch.Groups[1].Value))"
