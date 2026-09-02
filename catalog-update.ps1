[CmdletBinding()]
param(
    [string]$Source = (Join-Path $PSScriptRoot 'local@taskbar-ai-quota.wh.cpp'),
    [string]$Worktree = (Join-Path $env:LOCALAPPDATA 'windhawk-taskbar-ai-quota\windhawk-mods'),
    [string]$Branch,
    [switch]$Verify
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'
$catalogPath = 'mods/taskbar-ai-quota.wh.cpp'

& (Join-Path $PSScriptRoot 'check-mod.ps1') -Source $Source
if ($LASTEXITCODE -ne 0) {
    throw "Mod validation failed with exit code $LASTEXITCODE"
}

$sourcePath = (Resolve-Path -LiteralPath $Source).Path
$sourceText = [System.IO.File]::ReadAllText($sourcePath)
$sourceVersionMatch = [regex]::Match($sourceText, '(?m)^//\s+@version\s+(\d+\.\d+\.\d+)\s*$')
if (-not $sourceVersionMatch.Success) {
    throw 'Could not read source @version'
}
$sourceVersion = [version]$sourceVersionMatch.Groups[1].Value

if (-not (Get-Command git -ErrorAction SilentlyContinue)) {
    throw 'git is not available'
}

if (-not $Verify) {
    if (Test-Path -LiteralPath $Worktree) {
        if (-not (Test-Path -LiteralPath (Join-Path $Worktree '.git'))) {
            throw "Catalog worktree is not a Git clone: $Worktree"
        }
        $dirty = @(git -C $Worktree status --porcelain=v1)
        if ($LASTEXITCODE -ne 0) {
            throw "Could not inspect catalog worktree (exit $LASTEXITCODE)"
        }
        if ($dirty.Count -ne 0) {
            throw "Catalog worktree has uncommitted changes: $Worktree"
        }
    } else {
        $worktreeParent = Split-Path -Parent $Worktree
        if (-not (Test-Path -LiteralPath $worktreeParent -PathType Container)) {
            New-Item -ItemType Directory -Path $worktreeParent | Out-Null
        }
        git clone --single-branch --branch main https://github.com/Cleroth/windhawk-mods.git $Worktree
        if ($LASTEXITCODE -ne 0) {
            throw "Could not clone the catalog fork (exit $LASTEXITCODE)"
        }
    }
} elseif (-not (Test-Path -LiteralPath (Join-Path $Worktree '.git'))) {
    throw "Catalog worktree is not available: $Worktree"
}

git -C $Worktree fetch origin
if ($LASTEXITCODE -ne 0) {
    throw "Could not fetch catalog fork (exit $LASTEXITCODE)"
}
git -C $Worktree fetch https://github.com/ramensoftware/windhawk-mods.git `
    main:refs/remotes/upstream/main
if ($LASTEXITCODE -ne 0) {
    throw "Could not fetch upstream catalog main (exit $LASTEXITCODE)"
}

if (-not $Verify) {
    git -C $Worktree checkout main
    if ($LASTEXITCODE -ne 0) {
        throw "Could not check out catalog main (exit $LASTEXITCODE)"
    }
    git -C $Worktree merge --ff-only origin/main
    if ($LASTEXITCODE -ne 0) {
        throw "Could not update catalog main from the fork (exit $LASTEXITCODE)"
    }
    git -C $Worktree merge --ff-only upstream/main
    if ($LASTEXITCODE -ne 0) {
        throw "Could not update catalog main from upstream (exit $LASTEXITCODE)"
    }

    if ([string]::IsNullOrWhiteSpace($Branch)) {
        $Branch = "taskbar-ai-quota-$sourceVersion"
    }
    git -C $Worktree check-ref-format --branch $Branch | Out-Null
    if ($LASTEXITCODE -ne 0) {
        throw "Invalid catalog branch name: $Branch"
    }
    $localBranch = @(git -C $Worktree branch --list $Branch)
    if ($LASTEXITCODE -ne 0) {
        throw "Could not inspect local catalog branches (exit $LASTEXITCODE)"
    }
    $remoteBranch = @(git -C $Worktree branch --remotes --list "origin/$Branch")
    if ($LASTEXITCODE -ne 0) {
        throw "Could not inspect remote catalog branches (exit $LASTEXITCODE)"
    }
    if ($localBranch.Count -ne 0) {
        git -C $Worktree checkout $Branch
        if ($LASTEXITCODE -ne 0) {
            throw "Could not check out catalog branch $Branch (exit $LASTEXITCODE)"
        }
        if ($remoteBranch.Count -ne 0) {
            git -C $Worktree pull --ff-only origin $Branch
            if ($LASTEXITCODE -ne 0) {
                throw "Could not update catalog branch $Branch (exit $LASTEXITCODE)"
            }
        }
    } elseif ($remoteBranch.Count -ne 0) {
        git -C $Worktree checkout --track "origin/$Branch"
        if ($LASTEXITCODE -ne 0) {
            throw "Could not track catalog branch $Branch (exit $LASTEXITCODE)"
        }
    } else {
        git -C $Worktree checkout -b $Branch
        if ($LASTEXITCODE -ne 0) {
            throw "Could not create catalog branch $Branch (exit $LASTEXITCODE)"
        }
    }

    $destination = Join-Path $Worktree ($catalogPath -replace '/', '\')
    Copy-Item -LiteralPath $sourcePath -Destination $destination -Force
}

$catalogFile = Join-Path $Worktree ($catalogPath -replace '/', '\')
if (-not (Test-Path -LiteralPath $catalogFile -PathType Leaf)) {
    throw "Catalog file not found: $catalogFile"
}
$sourceHash = (Get-FileHash -LiteralPath $sourcePath -Algorithm SHA256).Hash
$catalogHash = (Get-FileHash -LiteralPath $catalogFile -Algorithm SHA256).Hash
if ($sourceHash -ne $catalogHash) {
    throw 'Source and catalog files are not byte-identical'
}

$catalogMainText = (git -C $Worktree show "upstream/main:$catalogPath") -join "`n"
if ($LASTEXITCODE -ne 0) {
    throw "Could not read the catalog main version (exit $LASTEXITCODE)"
}
$catalogVersionMatch = [regex]::Match(
    $catalogMainText, '(?m)^//\s+@version\s+(\d+\.\d+\.\d+)\s*$')
if (-not $catalogVersionMatch.Success) {
    throw 'Could not read catalog main @version'
}
$catalogVersion = [version]$catalogVersionMatch.Groups[1].Value
if ($sourceVersion -le $catalogVersion) {
    throw "Source version $sourceVersion must be newer than catalog version $catalogVersion"
}

$statusLines = @(git -C $Worktree status --porcelain=v1)
if ($LASTEXITCODE -ne 0) {
    throw "Could not inspect catalog status (exit $LASTEXITCODE)"
}
$targetIsStaged = $false
foreach ($line in $statusLines) {
    if ($line.Length -lt 4 -or $line.Substring(3).Trim() -ne $catalogPath) {
        throw "Unexpected catalog worktree change: $line"
    }
    if ($line[0] -notin @(' ', '?')) {
        $targetIsStaged = $true
    }
}
if ($targetIsStaged) {
    $sourceBlob = git -C $Worktree hash-object --path=$catalogPath -- $sourcePath
    if ($LASTEXITCODE -ne 0) {
        throw "Could not hash the source for index verification (exit $LASTEXITCODE)"
    }
    $indexBlob = git -C $Worktree rev-parse ":$catalogPath"
    if ($LASTEXITCODE -ne 0 -or $indexBlob -ne $sourceBlob) {
        throw 'The staged catalog file does not match the source; restage it before continuing'
    }
}

$changedFiles = @(git -C $Worktree diff --name-only upstream/main --)
if ($LASTEXITCODE -ne 0) {
    throw "Could not inspect catalog diff (exit $LASTEXITCODE)"
}
if ($changedFiles.Count -ne 1 -or $changedFiles[0] -ne $catalogPath) {
    throw "Catalog update must change exactly $catalogPath; found: $($changedFiles -join ', ')"
}

$currentBranch = git -C $Worktree branch --show-current
if ($LASTEXITCODE -ne 0 -or [string]::IsNullOrWhiteSpace($currentBranch)) {
    throw "Could not read the current catalog branch (exit $LASTEXITCODE)"
}
"Catalog update verified: $catalogVersion -> $sourceVersion"
"Worktree: $Worktree"
"Branch: $currentBranch"
"Changed file: $catalogPath"
if (-not $Verify) {
    'No commit, push, or PR action was performed.'
}
