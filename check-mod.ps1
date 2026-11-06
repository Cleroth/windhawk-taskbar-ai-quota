[CmdletBinding()]
param(
    [string]$Source = (Join-Path $PSScriptRoot 'local@taskbar-ai-quota.wh.cpp'),
    [string]$Compiler = 'C:\Program Files\Windhawk\Compiler\bin\clang++.exe',
    [string]$IncludeDirectory = 'C:\Program Files\Windhawk\Compiler\include',
    # Also compile and link the mod as Windhawk builds it, per @architecture, against the installed
    # engine's library and the mod's @compilerOptions: catches a lib missing from @compilerOptions
    # or an API the bundled import libraries lack. Takes a minute or two.
    [switch]$Link
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Test-Path -LiteralPath $Source -PathType Leaf)) {
    throw "Source file not found: $Source"
}
if ((Split-Path -Leaf $Source) -ne 'local@taskbar-ai-quota.wh.cpp') {
    throw "Unexpected source filename: $Source"
}

$sourcePath = (Resolve-Path -LiteralPath $Source).Path
if ($sourcePath -ieq (Join-Path $PSScriptRoot 'local@taskbar-ai-quota.wh.cpp')) {
    & (Join-Path $PSScriptRoot 'generate-mod.ps1') -Check
    if ($LASTEXITCODE -ne 0) {
        throw 'The generated mod is stale or src/ is invalid; run generate-mod.ps1'
    }
}

if (-not (Test-Path -LiteralPath $Compiler -PathType Leaf)) {
    throw "Windhawk compiler not found: $Compiler"
}
if (-not (Test-Path -LiteralPath $IncludeDirectory -PathType Container)) {
    throw "Windhawk include directory not found: $IncludeDirectory"
}

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

if ($Link) {
    # Windhawk's build (its catalog's scripts/compile_mod.py): the engine library, the mod's options,
    # its exports. -O0 instead of -O2 keeps it quick; it links at least as many symbols.
    $windhawkRoot = Split-Path (Split-Path (Split-Path $Compiler -Parent) -Parent) -Parent
    $iniPath = Join-Path $windhawkRoot 'windhawk.ini'
    $engineMatch = [regex]::Match([System.IO.File]::ReadAllText($iniPath), '(?m)^EnginePath=(.+?)\s*$')
    if (-not $engineMatch.Success) {
        throw "No EnginePath in $iniPath"
    }
    $enginePath = Join-Path $windhawkRoot $engineMatch.Groups[1].Value
    $modId = [regex]::Match($sourceText, '(?m)^//\s+@id\s+(\S+)').Groups[1].Value
    $options = @([regex]::Matches($sourceText, '(?m)^//\s+@compilerOptions\s+(-.*?)\s*$') |
        ForEach-Object { $_.Groups[1].Value -split '\s+' })
    # Without @architecture Windhawk builds every architecture.
    $architectures = @([regex]::Matches($sourceText, '(?m)^//\s+@architecture\s+(\S+)') |
        ForEach-Object { $_.Groups[1].Value })
    if ($architectures.Count -eq 0) {
        $architectures = @('x86', 'x86-64', 'arm64')
    }
    $targets = @{
        'x86' = @('i686-w64-mingw32', '32')
        'x86-64' = @('x86_64-w64-mingw32', '64')
        'arm64' = @('aarch64-w64-mingw32', 'arm64')
    }
    $outDirectory = Join-Path $PSScriptRoot '.out\link'
    New-Item -ItemType Directory -Force -Path $outDirectory | Out-Null
    foreach ($architecture in $architectures) {
        if (-not $targets.ContainsKey($architecture)) {
            throw "Unknown @architecture: $architecture"
        }
        $target, $libDirectory = $targets[$architecture]
        $engineLibrary = Join-Path $enginePath "$libDirectory\windhawk.lib"
        if (-not (Test-Path -LiteralPath $engineLibrary -PathType Leaf)) {
            throw "Windhawk engine library not found: $engineLibrary"
        }
        $output = Join-Path $outDirectory "$architecture-$PID.dll"
        $linkArguments = @(
            '-std=c++23', '-O0', '-shared', '-s', '-DUNICODE', '-D_UNICODE',
            '-DWINVER=0x0A00', '-D_WIN32_WINNT=0x0A00', '-D_WIN32_IE=0x0A00', '-DNTDDI_VERSION=0x0A000008',
            '-D__USE_MINGW_ANSI_STDIO=0', '-DWH_MOD', "-DWH_MOD_ID=L`"$modId`"",
            "-DWH_MOD_VERSION=L`"$($versionMatch.Groups[1].Value)`"", "-I$IncludeDirectory",
            $engineLibrary, '-x', 'c++', $sourcePath, '-include', 'windhawk_api.h', '-target', $target,
            '-Wl,--export-all-symbols', '-o', $output
        ) + $options
        & $Compiler @linkArguments
        $linkExit = $LASTEXITCODE
        Remove-Item -LiteralPath $output -ErrorAction SilentlyContinue
        if ($linkExit -ne 0) {
            throw "Windhawk build of $architecture failed with exit code $linkExit"
        }
        "Linked $architecture against $engineLibrary"
    }
}

"Validated $sourcePath (@version $($versionMatch.Groups[1].Value))"
