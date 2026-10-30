#requires -Version 7.0
<#
.SYNOPSIS
Amalgamates the src/ unity fragments into the single-file Windhawk mod.

.DESCRIPTION
Reads the entry file and replaces each top-level `#include "<path>"` line (path relative to
the source root) with a `// src/<path>` marker line followed by the fragment's bytes. System
`#include <...>` lines are left untouched. A "generated, do not edit" note follows the entry's
`// ==/WindhawkMod==` line. Output is UTF-8 without BOM with LF line endings, and is
byte-for-byte deterministic for a given source tree.

Input rules (violations fail with exit code 1):
- Entry and fragments: valid UTF-8, no BOM, no CR characters, non-empty, and exactly one
  trailing LF (last byte LF, the byte before it not LF).
- Local includes appear only in the entry, outside comments and preprocessor conditionals,
  in the exact form `#include "dir/file.h"`; each path is relative, uses `/`, has no `.` or
  `..` segments, stays under the source root, exists, and is included once.
- Every file under the source root other than the entry is included.
- Fragments contain no local includes.

.PARAMETER SourceRoot
Directory holding the entry and fragments. Default: src next to this script.

.PARAMETER Entry
Entry file, relative to SourceRoot. Default: taskbar-ai-quota.cpp.

.PARAMETER Output
Generated mod file. Default: local@taskbar-ai-quota.wh.cpp next to this script.

.PARAMETER Check
Regenerate in memory and compare with Output; never writes. Exit 1 on mismatch.

.PARAMETER Locate
Map a 1-based line of the generated file to `src/<file>:<line>`.
#>
[CmdletBinding(DefaultParameterSetName = 'Generate')]
param(
    [string]$SourceRoot = (Join-Path $PSScriptRoot 'src'),
    [string]$Entry = 'taskbar-ai-quota.cpp',
    [string]$Output = (Join-Path $PSScriptRoot 'local@taskbar-ai-quota.wh.cpp'),
    [Parameter(ParameterSetName = 'Check', Mandatory)][switch]$Check,
    [Parameter(ParameterSetName = 'Locate', Mandatory)][ValidateRange(1, [int]::MaxValue)][int]$Locate
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$utf8 = [System.Text.UTF8Encoding]::new($false, $true)

# Reads a source file and enforces the encoding and line-ending rules.
function Read-Source([string]$path, [string]$label) {
    if (-not [System.IO.File]::Exists($path)) { throw "${label}: file not found" }
    $bytes = [System.IO.File]::ReadAllBytes($path)
    if ($bytes.Length -ge 3 -and $bytes[0] -eq 0xEF -and $bytes[1] -eq 0xBB -and $bytes[2] -eq 0xBF) {
        throw "${label}: starts with a UTF-8 BOM"
    }
    try { $text = $utf8.GetString($bytes) } catch { throw "${label}: not valid UTF-8" }
    $cr = $text.IndexOf("`r")
    if ($cr -ge 0) { throw "${label}:$(($text.Substring(0, $cr) -split "`n").Count): contains a CR character" }
    if ($text.Length -eq 0) { throw "${label}: empty file" }
    if (-not $text.EndsWith("`n")) { throw "${label}: missing trailing newline" }
    if ($text.EndsWith("`n`n")) { throw "${label}: trailing blank line(s) after the final newline" }
    return $text
}

# Splits text (ending in LF) into lines without terminators.
function Split-Lines([string]$text) {
    $lines = $text.Split("`n")
    return , [string[]]$lines[0..($lines.Count - 2)]
}

# Returns @{ Line = <1-based>; Text = <line> } for every preprocessor directive line, skipping
# lines that start inside a block comment or raw string literal.
function Get-Directives([string[]]$lines, [string]$label) {
    $result = [System.Collections.Generic.List[object]]::new()
    $inBlock = $false
    $rawEnd = $null
    for ($n = 0; $n -lt $lines.Count; $n++) {
        $line = $lines[$n]
        $startsInCode = -not $inBlock -and $null -eq $rawEnd
        if ($startsInCode -and $line -match '^\s*#') {
            $result.Add(@{ Line = $n + 1; Text = $line })
            continue
        }
        $i = 0
        while ($i -lt $line.Length) {
            if ($inBlock) {
                $e = $line.IndexOf('*/', $i)
                if ($e -lt 0) { $i = $line.Length; break }
                $inBlock = $false; $i = $e + 2; continue
            }
            if ($null -ne $rawEnd) {
                $e = $line.IndexOf($rawEnd, $i)
                if ($e -lt 0) { $i = $line.Length; break }
                $i = $e + $rawEnd.Length; $rawEnd = $null; continue
            }
            $c = $line[$i]
            if ($c -eq '/' -and $i + 1 -lt $line.Length -and $line[$i + 1] -eq '/') { break }
            if ($c -eq '/' -and $i + 1 -lt $line.Length -and $line[$i + 1] -eq '*') { $inBlock = $true; $i += 2; continue }
            if ($c -eq '"' -or $c -eq "'") {
                # The identifier-like token directly before the quote decides raw strings,
                # encoding prefixes, and digit separators (1'000).
                $s = $i
                while ($s -gt 0 -and ([char]::IsLetterOrDigit($line[$s - 1]) -or $line[$s - 1] -eq '_' -or $line[$s - 1] -eq "'")) { $s-- }
                $prefix = $line.Substring($s, $i - $s)
                if ($c -eq '"' -and $prefix -cin 'R', 'LR', 'uR', 'UR', 'u8R') {
                    $open = $line.IndexOf('(', $i + 1)
                    if ($open -lt 0) { throw "${label}:$($n + 1): unsupported raw string literal" }
                    $rawEnd = ')' + $line.Substring($i + 1, $open - $i - 1) + '"'
                    $i = $open + 1; continue
                }
                if ($c -eq "'" -and $prefix -match '^[0-9]') { $i++; continue }
                $j = $i + 1
                while ($j -lt $line.Length -and $line[$j] -ne $c) { if ($line[$j] -eq '\') { $j++ }; $j++ }
                $i = $j + 1; continue
            }
            $i++
        }
    }
    if ($inBlock) { throw "${label}: unterminated block comment" }
    if ($null -ne $rawEnd) { throw "${label}: unterminated raw string literal" }
    return , $result
}

function Build-Mod {
    $root = [System.IO.Path]::GetFullPath($SourceRoot, $PWD.ProviderPath).TrimEnd('\', '/')
    if (-not [System.IO.Directory]::Exists($root)) { throw "source root not found: $root" }
    $entryLabel = "src/$Entry"
    $entryText = Read-Source (Join-Path $root $Entry) $entryLabel
    $entryLines = Split-Lines $entryText

    $includes = @{}   # lower-case path -> entry line
    $includeAt = @{}  # entry line -> path
    $depth = 0
    foreach ($d in (Get-Directives $entryLines $entryLabel)) {
        $where = "${entryLabel}:$($d.Line)"
        if ($d.Text -match '^\s*#\s*(if|ifdef|ifndef)\b') { $depth++; continue }
        if ($d.Text -match '^\s*#\s*endif\b') { $depth--; continue }
        if ($d.Text -notmatch '^\s*#\s*include\s*"') { continue }
        if ($d.Text -notmatch '^#include "([^"]+)"$') { throw "${where}: local include must be exactly '#include `"<path>`"'" }
        $rel = $Matches[1]
        if ($depth -gt 0) { throw "${where}: local include inside a preprocessor conditional" }
        $segments = $rel.Split('/')
        if ($rel.Contains('\') -or $rel.Contains(':') -or $rel.StartsWith('/') -or
            ($segments | Where-Object { $_ -eq '' -or $_ -eq '.' -or $_ -eq '..' })) {
            throw "${where}: include path '$rel' must be relative, use '/', and have no '.' or '..' segments"
        }
        $full = [System.IO.Path]::GetFullPath((Join-Path $root $rel))
        if (-not $full.StartsWith($root + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "${where}: include path '$rel' escapes the source root"
        }
        if ($rel -ieq $Entry) { throw "${where}: entry includes itself" }
        $key = $rel.ToLowerInvariant()
        if ($includes.ContainsKey($key)) { throw "${where}: duplicate include of '$rel' (first at line $($includes[$key]))" }
        $includes[$key] = $d.Line
        $includeAt[$d.Line] = $rel
    }
    if ($depth -ne 0) { throw "${entryLabel}: unbalanced preprocessor conditionals" }

    $metadataEnd = '// ==/WindhawkMod=='
    if (@($entryLines | Where-Object { $_ -ceq $metadataEnd }).Count -ne 1) {
        throw "${entryLabel}: expected exactly one '$metadataEnd' line"
    }
    $note = @(
        ''
        '// GENERATED from src/ by generate-mod.ps1 - do not edit this file.'
        '// Source: https://github.com/Cleroth/windhawk-taskbar-ai-quota (edit src/, then regenerate).'
    )

    $out = [System.Text.StringBuilder]::new()
    $map = [System.Collections.Generic.List[string]]::new()
    for ($n = 0; $n -lt $entryLines.Count; $n++) {
        $line = $entryLines[$n]
        if ($includeAt.ContainsKey($n + 1)) {
            $rel = $includeAt[$n + 1]
            $label = "src/$rel"
            $text = Read-Source (Join-Path $root $rel) $label
            $fragLines = Split-Lines $text
            foreach ($d in (Get-Directives $fragLines $label)) {
                if ($d.Text -match '^\s*#\s*include\s*"') { throw "${label}:$($d.Line): fragments must not contain local includes" }
            }
            [void]$out.Append("// $label`n").Append($text)
            $map.Add("${entryLabel}:$($n + 1)")
            for ($k = 1; $k -le $fragLines.Count; $k++) { $map.Add("${label}:$k") }
        } else {
            [void]$out.Append($line).Append("`n")
            $map.Add("${entryLabel}:$($n + 1)")
            if ($line -ceq $metadataEnd) {
                foreach ($noteLine in $note) {
                    [void]$out.Append($noteLine).Append("`n")
                    $map.Add('(generated note, not in src/)')
                }
            }
        }
    }

    $entryKey = $Entry.Replace('\', '/').ToLowerInvariant()
    foreach ($file in [System.IO.Directory]::EnumerateFiles($root, '*', [System.IO.SearchOption]::AllDirectories)) {
        $rel = [System.IO.Path]::GetRelativePath($root, $file).Replace('\', '/')
        $key = $rel.ToLowerInvariant()
        if ($key -ne $entryKey -and -not $includes.ContainsKey($key)) { throw "src/${rel}: not included by $entryLabel" }
    }
    return @{ Text = $out.ToString(); Map = $map }
}

try {
    $result = Build-Mod
    $outPath = [System.IO.Path]::GetFullPath($Output, $PWD.ProviderPath)
    $bytes = $utf8.GetBytes($result.Text)
    $existing = if ([System.IO.File]::Exists($outPath)) { [System.IO.File]::ReadAllBytes($outPath) } else { $null }
    $same = $null -ne $existing -and [System.Linq.Enumerable]::SequenceEqual([byte[]]$existing, [byte[]]$bytes)

    switch ($PSCmdlet.ParameterSetName) {
        'Locate' {
            if ($Locate -gt $result.Map.Count) { throw "line $Locate is past the end of the generated file ($($result.Map.Count) lines)" }
            if (-not $same) { [Console]::Error.WriteLine("warning: $outPath is not up to date with the sources; mapping reflects the sources") }
            $result.Map[$Locate - 1]
        }
        'Check' {
            if ($same) { "up to date: $outPath"; exit 0 }
            if ($null -eq $existing) { [Console]::Error.WriteLine("missing: $outPath"); exit 1 }
            $old = ($utf8.GetString($existing)).Split("`n")
            $new = $result.Text.Split("`n")
            $k = 0
            while ($k -lt $old.Count -and $k -lt $new.Count -and $old[$k] -ceq $new[$k]) { $k++ }
            $at = if ($k -lt $result.Map.Count) { " ($($result.Map[$k]))" } else { '' }
            [Console]::Error.WriteLine("out of date: $outPath first differs at line $($k + 1)$at; run generate-mod.ps1")
            exit 1
        }
        default {
            if (-not $same) { [System.IO.File]::WriteAllBytes($outPath, $bytes) }
            "generated: $outPath ($($result.Map.Count) lines)"
        }
    }
    exit 0
} catch {
    [Console]::Error.WriteLine("generate-mod: $($_.Exception.Message)")
    exit 1
}
