#Requires -Version 7
<#
.SYNOPSIS
Read-only audit of the mod's stored state: snapshot it, or compare a new snapshot with an older one.

.DESCRIPTION
snapshot: saves a JSON snapshot to -Folder. It finds Windhawk's storage as its engine does
(windhawk.ini names the engine folder, whose engine.ini names the data folder and, unless portable,
the registry key) and reads the mod's local storage there, from the registry or, in a portable
install, from ModsWritable\<mod ID>.ini: settings_v1 parsed, settings_v1_len, hiddenAccounts, any
other value by hash, and every auth_* sign-in as presence + SHA-256 of its stored encrypted string
(tokens are never decrypted, copied or printed). Then this user's recording consent and language
(recordHistory_<SID>, language_<SID>, and the HKCU RecordHistory and Language values older versions
used), the journal-move provenance (historyMigratedFrom_<SID>), and the history journals' files,
headers and per-day / per-history-ID / per-day Antigravity sample counts: the current one in
ModsWritable\mod-storage\<mod ID>\<SID>, and the one older versions kept in
%LOCALAPPDATA%\TaskbarAIQuota\<mod ID>. It writes nothing else.

compare: takes and saves a new snapshot (or loads -Current) and classifies every difference from -Baseline as
expected (settings fields named in -Edited, accounts labelled in -Disposable), tolerated (a sign-in
hash changed while present, journal growth, retention pruning, Antigravity thinning, the journal
moving from the older location) or unexpected (anything else). Consent, language and journal
compare by their effective values, so a move between locations alone is reported as information.
Exits 1 when anything is unexpected.

Audit only, not a backup: see "Storage audit" in AGENTS.md for restore limits.
#>
[CmdletBinding()]
param(
    [Parameter(Mandatory, Position = 0)][ValidateSet('snapshot', 'compare')][string]$Mode,
    # Where snapshots go (created if missing); keep it outside the repo.
    [string]$Folder = (Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) 'TaskbarAIQuota-audit'),
    [string]$ModId = 'local@taskbar-ai-quota',
    # compare: the older snapshot file.
    [string]$Baseline,
    # compare: a newer snapshot file instead of a new snapshot.
    [string]$Current,
    # compare: settings you changed on purpose: top-level settings_v1 fields (pollMinutes), account
    # fields as <label>.<field> (O1.hidden), 'accountOrder', 'consent' and 'language'.
    [string[]]$Edited = @(),
    # compare: labels of disposable test accounts; any change to them (add, rename, remove, sign-in,
    # sign-out, history ID) is expected.
    [string[]]$Disposable = @(),
    [string]$WindhawkIni = (Join-Path $env:ProgramFiles 'Windhawk\windhawk.ini')
)

$ErrorActionPreference = 'Stop'
if ($Mode -eq 'compare' -and -not $Baseline) { throw 'compare needs -Baseline <snapshot.json>' }
# `pwsh storage-audit.ps1 -Edited a,b` passes one comma-joined string; split it.
$Edited = @($Edited | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })
$Disposable = @($Disposable | ForEach-Object { $_ -split ',' } | ForEach-Object { $_.Trim() } | Where-Object { $_ })

# Mirrors the mod's sources: auth\token-store.h (auth_<identity hash>), core\model.h
# (AccountIdentityHash: FNV-1a 64 over the UTF-16 bytes of provider + "\n" + label, with the mod's
# own offset basis), core\user-storage.h (the per-user folder, value names and the older HKCU key),
# history\journal.h (journal file, header, the older folder), history\thinning.h (Antigravity
# readings older than 15 days thinned) and history\types.h (240-byte samples: account ID at 0, time
# in Unix ms at 8, provider at 32, Antigravity 3).
$offsetBasis = [System.Numerics.BigInteger]::Parse('1469598103934665603')
$wordSpace = [System.Numerics.BigInteger]::Pow(2, 64)
$journalMagic = [uint64]0x3154534948514941
$sampleBytes = 240
$sampleCap = 250000
$sha256 = { param([byte[]]$bytes) [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($bytes)).ToLowerInvariant() }
$utf8 = [Text.Encoding]::UTF8

$snapshot = $null
if ($Mode -eq 'snapshot' -or -not $Current) {
    $now = [DateTimeOffset]::UtcNow
    $sid = [Security.Principal.WindowsIdentity]::GetCurrent().User.Value
    $snapshot = [ordered]@{
        format = 2
        createdUtc = $now.ToString('o')
        computer = $env:COMPUTERNAME
        user = [Environment]::UserName
        sid = $sid
        modId = $ModId
    }

    # Windhawk's storage as its engine sees it: windhawk.ini names the engine folder, whose
    # engine.ini names the data folder (relative paths from the engine folder) and, unless portable,
    # the registry root.
    $readIni = {
        param($path, $wanted)
        $values = @{}
        $section = ''
        foreach ($line in Get-Content -LiteralPath $path) {
            if ($line -match '^\s*\[(.+)\]\s*$') { $section = $Matches[1] }
            elseif ($section -eq $wanted -and $line -match '^\s*([^=;]+?)\s*=\s*(.*?)\s*$') {
                $value = $Matches[2]
                if ($value.Length -ge 2 -and $value[0] -eq $value[-1] -and $value[0] -in '"', "'") { $value = $value.Substring(1, $value.Length - 2) }
                $values[$Matches[1]] = $value
            }
        }
        $values
    }
    if (-not (Test-Path -LiteralPath $WindhawkIni -PathType Leaf)) { throw "Windhawk ini not found: $WindhawkIni" }
    $ini = & $readIni $WindhawkIni 'Storage'
    $enginePath = [IO.Path]::GetFullPath([IO.Path]::Combine((Split-Path -Parent $WindhawkIni), [Environment]::ExpandEnvironmentVariables("$($ini.EnginePath)")))
    $engineIni = Join-Path $enginePath 'engine.ini'
    if (-not (Test-Path -LiteralPath $engineIni -PathType Leaf)) { throw "Windhawk engine ini not found: $engineIni" }
    $engine = & $readIni $engineIni 'Storage'
    $dataPath = [IO.Path]::GetFullPath([IO.Path]::Combine($enginePath, [Environment]::ExpandEnvironmentVariables("$($engine.AppDataPath)")))
    $portable = $engine.Portable -and $engine.Portable -ne '0'

    # The mod's local storage: each value with its kind, size and hash.
    $storage = [ordered]@{ portable = $portable; key = $null; present = $false; values = @() }
    $settingsText = $null
    $authValues = @{}
    $stored = @{}  # Raw values of this user's preferences and the journal-move provenance.
    $userValue = "^(recordHistory|language|historyMigratedFrom)_$([regex]::Escape($sid))$"
    $addValue = {
        param($name, $kind, $value, [byte[]]$bytes)
        $entry = [ordered]@{ name = $name; kind = "$kind"; bytes = $bytes.Length; sha256 = & $sha256 $bytes }
        if ($name -match '^auth_[0-9a-f]{16}$') {
            $authValues[$name] = $entry  # The encrypted value itself is never kept.
        } elseif ($name -eq 'settings_v1') {
            $script:settingsText = [string]$value
            $entry.chars = $script:settingsText.Length  # UTF-16 units, like settings_v1_len.
        } elseif ($kind -in 'DWord', 'QWord' -or $name -eq 'hiddenAccounts' -or $name -match $userValue) {
            $entry.value = $value
        }
        if ($name -match $userValue) { $stored[$Matches[1]] = [ordered]@{ kind = "$kind"; value = $value } }
        $storage.values += $entry
    }
    if ($portable) {
        # Every value a string, as written: binary in hex, quoted when it would read back trimmed.
        $iniPath = Join-Path $dataPath "ModsWritable\$ModId.ini"
        $storage.key = "$iniPath [LocalStorage]"
        if (Test-Path -LiteralPath $iniPath -PathType Leaf) {
            $values = & $readIni $iniPath 'LocalStorage'
            $storage.present = $values.Count -gt 0
            foreach ($name in $values.Keys | Sort-Object) { & $addValue $name 'String' $values[$name] $utf8.GetBytes($values[$name]) }
        }
    } else {
        $hiveName, $rootPath = "$($engine.RegistryKey)" -split '\\', 2
        $hive = switch ($hiveName) {
            { $_ -in 'HKLM', 'HKEY_LOCAL_MACHINE' } { [Microsoft.Win32.RegistryHive]::LocalMachine }
            { $_ -in 'HKCU', 'HKEY_CURRENT_USER' } { [Microsoft.Win32.RegistryHive]::CurrentUser }
            { $_ -in 'HKU', 'HKEY_USERS' } { [Microsoft.Win32.RegistryHive]::Users }
            default { throw "Unsupported Windhawk registry key: $($engine.RegistryKey)" }
        }
        $storagePath = "$rootPath\ModsWritable\$ModId\LocalStorage"
        $storage.key = "$hiveName\$storagePath"
        $root = [Microsoft.Win32.RegistryKey]::OpenBaseKey($hive, [Microsoft.Win32.RegistryView]::Registry64)
        $key = $root.OpenSubKey($storagePath, $false)
        if ($key) {
            try {
                $storage.present = $true
                foreach ($name in $key.GetValueNames() | Sort-Object) {
                    $kind = $key.GetValueKind($name)
                    $value = $key.GetValue($name, $null, 'DoNotExpandEnvironmentNames')
                    $bytes = switch ($kind) {
                        'String' { $utf8.GetBytes($value) }
                        'ExpandString' { $utf8.GetBytes($value) }
                        'DWord' { [BitConverter]::GetBytes([int]$value) }
                        'QWord' { [BitConverter]::GetBytes([long]$value) }
                        'Binary' { [byte[]]$value }
                        default { $utf8.GetBytes(($value | Out-String)) }
                    }
                    if ($kind -eq 'Binary') { $value = [Convert]::ToHexString([byte[]]$value) }
                    & $addValue $name $kind $value $bytes
                }
            } finally {
                $key.Close()
            }
        }
        $root.Close()
    }

    # settings_v1 and its accounts, with each account's sign-in identity.
    $settings = $null
    if ($settingsText) {
        try { $settings = $settingsText | ConvertFrom-Json -AsHashtable }
        catch { $storage.settingsError = "settings_v1 isn't valid JSON: $($_.Exception.Message)" }
    }
    $storage.settings = $settings
    $accounts = @()
    $index = 0
    foreach ($account in @($settings.accounts)) {
        if ($null -eq $account) { continue }
        $hash = $offsetBasis
        foreach ($byte in [Text.Encoding]::Unicode.GetBytes("$($account.provider)`n$($account.label)")) {
            $hash = ([System.Numerics.BigInteger]::op_ExclusiveOr($hash, [System.Numerics.BigInteger]$byte) * 1099511628211) % $wordSpace
        }
        $authName = 'auth_' + ([uint64]$hash).ToString('x16')
        $accounts += [ordered]@{
            index = $index++
            provider = $account.provider
            label = $account.label
            historyId = $account.historyId
            hidden = $account.hidden
            recordHistory = $account.recordHistory
            signIn = $authName
            signedIn = $authValues.ContainsKey($authName)
        }
    }
    $snapshot.storage = $storage
    $snapshot.accounts = $accounts
    $snapshot.signIns = @($authValues.Keys | Sort-Object | ForEach-Object {
        $name = $_
        $owner = $accounts | Where-Object { $_.signIn -eq $name } | Select-Object -First 1
        [ordered]@{
            name = $name
            bytes = $authValues[$name].bytes
            sha256 = $authValues[$name].sha256
            account = if ($owner) { "$($owner.provider)/$($owner.label)" } else { $null }
        }
    })

    # This user's history consent and language: the Windhawk values, else the HKCU ones older
    # versions wrote (which the mod reads until it moves them), else the defaults. Consent: missing =
    # on (recording is on until the user turns it off). Language: a tag; missing, empty or `auto` =
    # Automatic (the mod also reads a tag it doesn't know as Automatic).
    $legacyPath = "Software\Windhawk\TaskbarAIQuota\$ModId"
    $legacy = @{}
    $user = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser, [Microsoft.Win32.RegistryView]::Registry64)
    $legacyKey = $user.OpenSubKey($legacyPath, $false)
    if ($legacyKey) {
        try {
            foreach ($name in 'RecordHistory', 'Language') {
                if ($legacyKey.GetValueNames() -contains $name) {
                    $legacy[$name] = [ordered]@{ kind = "$($legacyKey.GetValueKind($name))"; value = $legacyKey.GetValue($name) }
                }
            }
        } finally {
            $legacyKey.Close()
        }
    }
    $user.Close()
    $describe = { param($found) if ($found) { [ordered]@{ present = $true; kind = $found.kind; value = $found.value } } else { [ordered]@{ present = $false } } }
    $consent = [ordered]@{
        recording = $true; source = 'default'
        windhawk = [ordered]@{ name = "recordHistory_$sid" } + (& $describe $stored.recordHistory)
        legacy = [ordered]@{ key = "HKCU\$legacyPath\RecordHistory" } + (& $describe $legacy.RecordHistory)
    }
    $storedConsent = if ($stored.recordHistory) { $parsed = 0; if ([int]::TryParse("$($stored.recordHistory.value)", [ref]$parsed)) { $parsed } else { -1 } } else { -1 }
    if ($storedConsent -ne -1) {
        $consent.recording = $storedConsent -eq 1; $consent.source = 'windhawk'
    } elseif ($legacy.RecordHistory) {
        $consent.recording = $legacy.RecordHistory.kind -eq 'DWord' -and $legacy.RecordHistory.value -eq 1; $consent.source = 'legacy'
    }
    $language = [ordered]@{
        tag = ''; source = 'default'
        windhawk = [ordered]@{ name = "language_$sid" } + (& $describe $stored.language)
        legacy = [ordered]@{ key = "HKCU\$legacyPath\Language" } + (& $describe $legacy.Language)
    }
    if ($stored.language -and "$($stored.language.value)") {
        $language.tag = if ("$($stored.language.value)" -eq 'auto') { '' } else { "$($stored.language.value)" }; $language.source = 'windhawk'
    } elseif ($legacy.Language) {
        $language.tag = if ($legacy.Language.kind -eq 'String') { "$($legacy.Language.value)" } else { '' }; $language.source = 'legacy'
    }
    $snapshot.consent = $consent
    $snapshot.language = $language
    $snapshot.journalMove = [ordered]@{ name = "historyMigratedFrom_$sid" } + (& $describe $stored.historyMigratedFrom)

    # History journals: this user's folder in the mod's storage, and the folder older versions used.
    # Opened with full sharing so the mod's appends and atomic replace never fail.
    $readJournal = {
        param($directory)
        $journal = [ordered]@{ directory = $directory; files = @(); present = $false }
        if (Test-Path -LiteralPath $directory -PathType Container) {
            $journal.files = @(Get-ChildItem -LiteralPath $directory -File -Force | Sort-Object Name | ForEach-Object {
                [ordered]@{ name = $_.Name; bytes = $_.Length; lastWriteUtc = $_.LastWriteTimeUtc.ToString('o') }
            })
        }
        $journalPath = Join-Path $directory 'usage-history-v1.bin'
        if (Test-Path -LiteralPath $journalPath -PathType Leaf) {
            $stream = [IO.File]::Open($journalPath, 'Open', 'Read', [IO.FileShare]'ReadWrite, Delete')
            try {
                $data = [byte[]]::new($stream.Length)
                $read = 0
                while ($read -lt $data.Length) {
                    $got = $stream.Read($data, $read, $data.Length - $read)
                    if ($got -le 0) { break }
                    $read += $got
                }
            } finally {
                $stream.Dispose()
            }
            $journal.present = $true
            $journal.bytes = $read
            if ($read -ge 16) {
                $journal.header = [ordered]@{
                    magicOk = [BitConverter]::ToUInt64($data, 0) -eq $journalMagic
                    version = [BitConverter]::ToUInt32($data, 8)
                    recordBytes = [BitConverter]::ToUInt32($data, 12)
                }
                $count = [int][Math]::Floor(($read - 16) / $sampleBytes)
                $journal.samples = $count
                $journal.tailBytes = ($read - 16) % $sampleBytes
                $days = [ordered]@{}
                $antigravityDays = [ordered]@{}
                $ids = @{}
                $invalid = 0
                $first = $null
                $last = $null
                for ($i = 0; $i -lt $count; $i++) {
                    $offset = 16 + $i * $sampleBytes
                    $id = [BitConverter]::ToUInt64($data, $offset).ToString('x16')
                    $time = [BitConverter]::ToUInt64($data, $offset + 8)
                    if ($time -eq 0 -or $time -ge 100000000000000) { $invalid++; continue }
                    $day = [DateTimeOffset]::FromUnixTimeMilliseconds([long]$time).UtcDateTime.ToString('yyyy-MM-dd')
                    $days[$day] = 1 + [int]$days[$day]
                    if ([BitConverter]::ToUInt32($data, $offset + 32) -eq 3) { $antigravityDays[$day] = 1 + [int]$antigravityDays[$day] }
                    $ids[$id] = 1 + [int]$ids[$id]
                    if ($null -eq $first -or $time -lt $first) { $first = $time }
                    if ($null -eq $last -or $time -gt $last) { $last = $time }
                }
                $journal.invalidTimes = $invalid
                $journal.firstUtc = if ($null -ne $first) { [DateTimeOffset]::FromUnixTimeMilliseconds([long]$first).ToString('o') } else { $null }
                $journal.lastUtc = if ($null -ne $last) { [DateTimeOffset]::FromUnixTimeMilliseconds([long]$last).ToString('o') } else { $null }
                $journal.days = $days
                $journal.antigravityDays = $antigravityDays
                $journal.historyIds = [ordered]@{}
                foreach ($id in $ids.Keys | Sort-Object) { $journal.historyIds[$id] = $ids[$id] }
            }
        }
        $journal
    }
    $storageJournal = & $readJournal (Join-Path $dataPath "ModsWritable\mod-storage\$ModId\$sid")
    $older = & $readJournal (Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) "TaskbarAIQuota\$ModId")
    # The journal the mod loads: the one in its storage, else the older one it would move.
    $location = if ($storageJournal.present -or -not $older.present) { 'storage' } else { 'older' }
    $journal = if ($location -eq 'storage') { $storageJournal } else { $older }
    $journal.location = $location
    $snapshot.journal = $journal
    $snapshot.olderJournal = $older

    if (-not (Test-Path -LiteralPath $Folder -PathType Container)) {
        $parent = Split-Path -Parent $Folder
        if (-not (Test-Path -LiteralPath $parent -PathType Container)) { throw "Parent folder not found: $parent" }
        New-Item -ItemType Directory -Path $Folder | Out-Null
    }
    $path = Join-Path $Folder ('snapshot-{0}.json' -f $now.ToString('yyyyMMddTHHmmssZ'))
    $snapshot | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $path -Encoding utf8NoBOM
    "Snapshot: $path"
    $journalText = if ($journal.present) {
        "$($journal.samples) samples, $($journal.bytes) bytes, $($journal.firstUtc) to $($journal.lastUtc)"
    } else { 'none' }
    $signedIn = @($accounts | Where-Object { $_.signedIn }).Count
    $orphans = @($snapshot.signIns | Where-Object { -not $_.account }).Count
    "Storage: $(if ($storage.present) { $storage.key } else { "missing ($($storage.key))" }); settings_v1 $(if ($settings) { 'parsed' } else { 'missing or invalid' })"
    "Accounts: $($accounts.Count) ($signedIn with a stored sign-in); stored sign-ins without an account: $orphans"
    $from = @{ windhawk = 'Windhawk storage'; legacy = 'older HKCU value'; default = 'not set' }
    "Consent: recording $(if ($consent.recording) { 'on' } else { 'off' }) ($($from[$consent.source]))"
    "Language: $(if ($language.tag) { $language.tag } else { 'Automatic' }) ($($from[$language.source]))"
    "Journal: $journalText$(if ($journal.present) { " ($(if ($location -eq 'storage') { $storageJournal.directory } else { "older folder $($older.directory), not moved yet" }))" })"
    if ($location -eq 'storage' -and ($older.present -or $older.files.Count)) {
        "Older folder: $($older.directory) ($(if ($older.present) { "journal kept: $($older.samples) samples" } else { "$($older.files.Count) leftover file(s)" }))"
    }
}
if ($Mode -eq 'snapshot') { exit 0 }

# Compare ----------------------------------------------------------------------------------------
$base = Get-Content -LiteralPath $Baseline -Raw | ConvertFrom-Json -AsHashtable
$latest = if ($Current) { Get-Content -LiteralPath $Current -Raw | ConvertFrom-Json -AsHashtable }
           else { $snapshot | ConvertTo-Json -Depth 12 | ConvertFrom-Json -AsHashtable }
$found = [ordered]@{ unexpected = [Collections.Generic.List[string]]::new(); expected = [Collections.Generic.List[string]]::new();
                     tolerated = [Collections.Generic.List[string]]::new(); info = [Collections.Generic.List[string]]::new() }
$same = { param($a, $b) ($a | ConvertTo-Json -Compress -Depth 8) -ceq ($b | ConvertTo-Json -Compress -Depth 8) }
$isDisposable = { param($label) $null -ne $label -and $Disposable -ccontains $label }

if ($base.modId -ne $latest.modId) { $found.info.Add("Different mod IDs: $($base.modId) -> $($latest.modId)") }

# Storage values other than settings, its length sentinel, hiddenAccounts, sign-ins and the per-user
# values (compared below by their effect).
$valuesOf = { param($snap) $map = @{}; foreach ($v in @($snap.storage.values)) { if ($v) { $map[$v.name] = $v } }; $map }
$baseValues = & $valuesOf $base
$currentValues = & $valuesOf $latest
foreach ($name in @($baseValues.Keys) + @($currentValues.Keys) | Sort-Object -Unique) {
    if ($name -match '^auth_' -or $name -in 'settings_v1', 'settings_v1_len', 'hiddenAccounts') { continue }
    if ($name -match '^(recordHistory|language|historyMigratedFrom)_S-[0-9-]+$') { continue }
    $b = $baseValues[$name]; $c = $currentValues[$name]
    if (-not $b) { $found.unexpected.Add("Storage value $name appeared") }
    elseif (-not $c) { $found.unexpected.Add("Storage value $name disappeared") }
    elseif ($b.sha256 -ne $c.sha256) { $found.unexpected.Add("Storage value $name changed") }
}
foreach ($snap in $base, $latest) {
    $length = $snap.storage.values | Where-Object { $_.name -eq 'settings_v1_len' } | Select-Object -First 1
    $text = $snap.storage.values | Where-Object { $_.name -eq 'settings_v1' } | Select-Object -First 1
    if ($length -and $text -and $length.value -ne $text.chars) {
        $found.info.Add("settings_v1_len ($($length.value)) differs from the settings length ($($text.chars)) in the snapshot of $($snap.createdUtc)")
    }
}

# Top-level settings fields.
$bs = $base.storage.settings; $cs = $latest.storage.settings
if (-not $bs -or -not $cs) {
    if (-not (& $same $bs $cs)) { $found.unexpected.Add('settings_v1 is missing or unreadable in one snapshot') }
} else {
    foreach ($field in @($bs.Keys) + @($cs.Keys) | Sort-Object -Unique) {
        if ($field -eq 'accounts' -or (& $same $bs[$field] $cs[$field])) { continue }
        $text = "Setting $field`: $($bs[$field] | ConvertTo-Json -Compress) -> $($cs[$field] | ConvertTo-Json -Compress)"
        if ($Edited -ccontains $field) { $found.expected.Add($text) } else { $found.unexpected.Add($text) }
    }
}

# Accounts, matched by history ID (kept across rename, reorder and visibility).
$baseAccounts = @(@($bs.accounts) | Where-Object { $_ })
$currentAccounts = @(@($cs.accounts) | Where-Object { $_ })
$accountEdits = $false  # Any expected account change: a visibility toggle rewrites hiddenAccounts from all accounts.
foreach ($b in $baseAccounts) {
    $c = $currentAccounts | Where-Object { $_.historyId -eq $b.historyId } | Select-Object -First 1
    if (-not $c) {
        $sameName = $currentAccounts | Where-Object { $_.provider -eq $b.provider -and $_.label -ceq $b.label } | Select-Object -First 1
        $text = if ($sameName) { "History ID of $($b.provider)/$($b.label) changed: $($b.historyId) -> $($sameName.historyId)" }
                else { "Account $($b.provider)/$($b.label) (history ID $($b.historyId)) removed" }
        if (& $isDisposable $b.label) { $found.expected.Add($text); $accountEdits = $true } else { $found.unexpected.Add($text) }
        continue
    }
    foreach ($field in @($b.Keys) + @($c.Keys) | Sort-Object -Unique) {
        if (& $same $b[$field] $c[$field]) { continue }
        $text = "Account $($b.provider)/$($b.label) $field`: $($b[$field] | ConvertTo-Json -Compress) -> $($c[$field] | ConvertTo-Json -Compress)"
        $ok = (& $isDisposable $b.label) -or (& $isDisposable $c.label) -or $Edited -ccontains "$($b.label).$field" -or
              $Edited -ccontains "$($c.label).$field"
        if ($ok) { $found.expected.Add($text); $accountEdits = $true } else { $found.unexpected.Add($text) }
    }
}
foreach ($c in $currentAccounts) {
    if ($baseAccounts | Where-Object { $_.historyId -eq $c.historyId }) { continue }
    if ($baseAccounts | Where-Object { $_.provider -eq $c.provider -and $_.label -ceq $c.label }) { continue }  # Reported as a history ID change.
    $text = "Account $($c.provider)/$($c.label) (history ID $($c.historyId)) added"
    if (& $isDisposable $c.label) { $found.expected.Add($text); $accountEdits = $true } else { $found.unexpected.Add($text) }
}
$common = { param($list, $other) @($list | Where-Object { $id = $_.historyId; $other | Where-Object { $_.historyId -eq $id } } | ForEach-Object { $_.historyId }) }
if (-not (& $same (& $common $baseAccounts $currentAccounts) (& $common $currentAccounts $baseAccounts))) {
    $text = 'Account order changed'
    if ($Edited -ccontains 'accountOrder') { $found.expected.Add($text) } else { $found.unexpected.Add($text) }
}
foreach ($account in $baseAccounts + $currentAccounts) {
    if ($account.historyId -eq '0000000000000000') { $found.info.Add("$($account.provider)/$($account.label) has no history ID (generation failed)") }
}
if ($baseValues['hiddenAccounts'].sha256 -ne $currentValues['hiddenAccounts'].sha256) {
    $text = 'hiddenAccounts (legacy visibility list) changed'
    if ($accountEdits) { $found.expected.Add($text) } else { $found.unexpected.Add($text) }
}

# Sign-ins: presence and the hash of the stored encrypted value.
$ownerLabel = { param($snap, $name) ($snap.accounts | Where-Object { $_.signIn -eq $name } | Select-Object -First 1).label }
$baseSignIns = @{}; foreach ($s in @($base.signIns)) { if ($s) { $baseSignIns[$s.name] = $s } }
$currentSignIns = @{}; foreach ($s in @($latest.signIns)) { if ($s) { $currentSignIns[$s.name] = $s } }
foreach ($name in @($baseSignIns.Keys) + @($currentSignIns.Keys) | Sort-Object -Unique) {
    $b = $baseSignIns[$name]; $c = $currentSignIns[$name]
    $owner = & $ownerLabel $latest $name
    if (-not $owner) { $owner = & $ownerLabel $base $name }
    $who = if ($owner) { "account $owner" } else { 'no configured account' }
    if ($b -and $c) {
        if ($b.sha256 -ne $c.sha256) { $found.tolerated.Add("Sign-in $name ($who) changed while present: token refresh or a new sign-in") }
        continue
    }
    $text = if ($c) { "Sign-in $name ($who) appeared" } else { "Sign-in $name ($who) disappeared" }
    if (& $isDisposable $owner) { $found.expected.Add($text) } else { $found.unexpected.Add($text) }
}

# Where a snapshot found consent and language: format 1 read only the older HKCU values.
$sourceOf = { param($snap, $what) if ($snap.format -ge 2) { $snap[$what].source } elseif ($snap[$what].present) { 'legacy' } else { 'default' } }

# Consent, by its effect; a move between locations alone is information.
if ($base.consent.recording -ne $latest.consent.recording) {
    $text = "History consent: recording $($base.consent.recording) -> $($latest.consent.recording)"
    if ($Edited -ccontains 'consent') { $found.expected.Add($text) } else { $found.unexpected.Add($text) }
}
if ((& $sourceOf $base 'consent') -ne (& $sourceOf $latest 'consent')) {
    $found.info.Add("History consent read from: $(& $sourceOf $base 'consent') -> $(& $sourceOf $latest 'consent')")
}

# Language preference, like consent; snapshots older than it have none to compare.
if (-not $base.Contains('language') -or -not $latest.Contains('language')) {
    $found.info.Add('A snapshot predates the language value; it was not compared')
} else {
    $tagOf = { param($snap) if ($snap.format -ge 2) { "$($snap.language.tag)" } elseif ($snap.language.present -and $snap.language.kind -eq 'String') { "$($snap.language.value)" } else { '' } }
    $describe = { param($tag) if ($tag) { "'$tag'" } else { 'Automatic' } }
    if ((& $tagOf $base) -cne (& $tagOf $latest)) {
        $text = "Language preference: $(& $describe (& $tagOf $base)) -> $(& $describe (& $tagOf $latest))"
        if ($Edited -ccontains 'language') { $found.expected.Add($text) } else { $found.unexpected.Add($text) }
    }
    if ((& $sourceOf $base 'language') -ne (& $sourceOf $latest 'language')) {
        $found.info.Add("Language preference read from: $(& $sourceOf $base 'language') -> $(& $sourceOf $latest 'language')")
    }
}
if ($base.Contains('journalMove') -and $latest.Contains('journalMove') -and
    $base.journalMove.present -ne $latest.journalMove.present) {
    $found.info.Add("Journal-move provenance $(if ($latest.journalMove.present) { 'recorded' } else { 'cleared' })")
}

# Journal, the one the mod loads in each snapshot (format 1 knew only the older folder): growth,
# pruning older than the retention window and Antigravity thinning older than 15 days are
# tolerated; anything removed inside it (except storage-cap compaction) or added before the
# baseline's last day is unexpected. Kept forever in both snapshots, nothing may be pruned or
# compacted.
$bj = $base.journal; $cj = $latest.journal
$whereOf = { param($snap) if ($snap.journal.location) { $snap.journal.location } else { 'older' } }
if ($bj.present -and $cj.present -and (& $whereOf $base) -ne (& $whereOf $latest)) {
    $found.tolerated.Add("History journal moved: $(& $whereOf $base) folder -> $(& $whereOf $latest) folder ($($cj.directory))")
}
if ($latest.olderJournal.present -and (& $whereOf $latest) -eq 'storage') {
    $found.info.Add("An older journal stays in $($latest.olderJournal.directory) ($($latest.olderJournal.samples) samples), unmerged")
}
if ($bj.present -and -not $cj.present) {
    $found.unexpected.Add('History journal disappeared')
} elseif (-not $bj.present -and $cj.present) {
    $found.tolerated.Add("History journal created ($($cj.samples) samples)")
} elseif ($bj.present) {
    if (-not (& $same $bj.header $cj.header)) { $found.unexpected.Add('History journal header changed') }
    if ($cj.lastUtc -and $bj.lastUtc -and [DateTimeOffset]$cj.lastUtc -lt [DateTimeOffset]$bj.lastUtc) {
        $found.unexpected.Add("Newest journal sample went back from $($bj.lastUtc) to $($cj.lastUtc)")
    }
    # A day count in either snapshot may have pruned; the current one's wins.
    $limited = @($cs, $bs) | Where-Object { $_ -and $_.historyKeepForever -ne $true } | Select-Object -First 1
    $retention = if (-not $limited) { 0 } elseif ($limited.historyRetentionDays) { [int]$limited.historyRetentionDays } else { 90 }
    # Day keys are UTC dates; pruning may reach one day past the cutoff's date.
    $prunable = if ($retention) { ([DateTimeOffset]$latest.createdUtc).AddDays(-$retention - 1).UtcDateTime.ToString('yyyy-MM-dd') } else { '' }
    $baseLastDay = if ($bj.lastUtc) { ([DateTimeOffset]$bj.lastUtc).UtcDateTime.ToString('yyyy-MM-dd') } else { '' }
    $capped = $retention -and $cj.samples -ge [int]($sampleCap * 0.89)
    # Thinning removes only Antigravity samples, on days reaching back past 15 days before now.
    $thinnable = ([DateTimeOffset]$latest.createdUtc).AddDays(-15).UtcDateTime.ToString('yyyy-MM-dd')
    $grown = 0; $pruned = 0; $compacted = 0; $thinned = 0
    foreach ($day in @($bj.days.Keys) + @($cj.days.Keys) | Sort-Object -Unique) {
        $before = [int]$bj.days[$day]; $after = [int]$cj.days[$day]
        if ($after -gt $before) {
            if ($day -ge $baseLastDay) { $grown += $after - $before }
            else { $found.unexpected.Add("Journal day $day gained $($after - $before) samples before the baseline's newest day") }
        } elseif ($after -lt $before) {
            $antigravity = if ($bj.antigravityDays) { [int]$bj.antigravityDays[$day] } else { 0 }
            if ($day -le $prunable) { $pruned += $before - $after }
            elseif ($capped) { $compacted += $before - $after }
            elseif ($day -le $thinnable -and $before - $after -le $antigravity) { $thinned += $before - $after }
            elseif ($retention) { $found.unexpected.Add("Journal day $day lost $($before - $after) of $before samples inside the $retention-day retention window") }
            else { $found.unexpected.Add("Journal day $day lost $($before - $after) of $before samples while history is kept forever") }
        }
    }
    if ($grown) { $found.tolerated.Add("Journal grew by $grown samples") }
    if ($pruned) { $found.tolerated.Add("Retention pruning removed $pruned samples older than $retention days") }
    if ($compacted) { $found.tolerated.Add("Storage-cap compaction removed $compacted samples") }
    if ($thinned) { $found.tolerated.Add("Antigravity thinning removed $thinned samples older than 15 days") }
    if ($cj.tailBytes) { $found.info.Add("Journal ends with a partial record ($($cj.tailBytes) bytes); the mod repairs it on its next write") }
}

"Compare: $Baseline ($(([DateTimeOffset]$base.createdUtc).ToString('u')))"
"     to: $(if ($Current) { $Current } else { 'a new snapshot' }) ($(([DateTimeOffset]$latest.createdUtc).ToString('u')))"
foreach ($class in 'unexpected', 'expected', 'tolerated', 'info') {
    if (-not $found[$class].Count) { continue }
    "$($class.Substring(0, 1).ToUpper() + $class.Substring(1)) ($($found[$class].Count)):"
    $found[$class] | ForEach-Object { "  - $_" }
}
if ($found.unexpected.Count) {
    "Result: $($found.unexpected.Count) unexpected difference(s)"
    exit 1
}
'Result: no unexpected differences'
exit 0
