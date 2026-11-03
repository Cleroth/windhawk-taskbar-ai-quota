#Requires -Version 7
<#
.SYNOPSIS
Read-only audit of the mod's stored state: snapshot it, or compare a new snapshot with an older one.

.DESCRIPTION
snapshot: saves a JSON snapshot to -Folder. It reads Windhawk's LocalStorage registry key for the
mod (settings_v1 parsed, settings_v1_len, hiddenAccounts, any other value by hash, and every auth_*
sign-in as presence + SHA-256 of its stored encrypted string; tokens are never decrypted, copied or
printed), the per-user RecordHistory consent value, and the history journal's files, header and
per-day / per-history-ID sample counts. It writes nothing else.

compare: takes and saves a new snapshot (or loads -Current) and classifies every difference from -Baseline as
expected (settings fields named in -Edited, accounts labelled in -Disposable), tolerated (a sign-in
hash changed while present, journal growth, retention pruning) or unexpected (anything else).
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
    # fields as <label>.<field> (O1.hidden), 'accountOrder' and 'consent'.
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
# own offset basis), history\journal.h (journal path, header, consent key) and history\types.h
# (240-byte samples: account ID at 0, time in Unix ms at 8).
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
    $snapshot = [ordered]@{
        format = 1
        createdUtc = $now.ToString('o')
        computer = $env:COMPUTERNAME
        user = [Environment]::UserName
        modId = $ModId
    }

    # Windhawk mod storage: its ini names the registry root (Engine\ModsWritable\<mod>\LocalStorage).
    if (-not (Test-Path -LiteralPath $WindhawkIni -PathType Leaf)) { throw "Windhawk ini not found: $WindhawkIni" }
    $ini = @{}
    $section = ''
    foreach ($line in Get-Content -LiteralPath $WindhawkIni) {
        if ($line -match '^\s*\[(.+)\]\s*$') { $section = $Matches[1] }
        elseif ($section -eq 'Storage' -and $line -match '^\s*([^=;]+?)\s*=\s*(.*?)\s*$') { $ini[$Matches[1]] = $Matches[2] }
    }
    if ($ini.Portable -ne '0') { throw "Portable Windhawk storage (Portable=$($ini.Portable)) isn't supported by this audit" }
    $hiveName, $rootPath = $ini.RegistryKey -split '\\', 2
    $hive = switch ($hiveName) {
        { $_ -in 'HKLM', 'HKEY_LOCAL_MACHINE' } { [Microsoft.Win32.RegistryHive]::LocalMachine }
        { $_ -in 'HKCU', 'HKEY_CURRENT_USER' } { [Microsoft.Win32.RegistryHive]::CurrentUser }
        default { throw "Unsupported Windhawk registry key: $($ini.RegistryKey)" }
    }
    $storagePath = "$rootPath\Engine\ModsWritable\$ModId\LocalStorage"
    $storage = [ordered]@{ key = "$hiveName\$storagePath"; present = $false; values = @() }
    $settingsText = $null
    $authValues = @{}
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
                $entry = [ordered]@{ name = $name; kind = "$kind"; bytes = $bytes.Length; sha256 = & $sha256 $bytes }
                if ($name -match '^auth_[0-9a-f]{16}$') {
                    $authValues[$name] = $entry  # The encrypted value itself is never kept.
                } elseif ($name -eq 'settings_v1') {
                    $settingsText = [string]$value
                    $entry.chars = $settingsText.Length  # UTF-16 units, like settings_v1_len.
                } elseif ($kind -in 'DWord', 'QWord' -or $name -eq 'hiddenAccounts') {
                    $entry.value = $value
                }
                $storage.values += $entry
            }
        } finally {
            $key.Close()
        }
    }
    $root.Close()

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

    # Per-user history consent: missing = on (recording is on until the user turns it off).
    $consentPath = "Software\Windhawk\TaskbarAIQuota\$ModId"
    $consent = [ordered]@{ key = "HKCU\$consentPath"; present = $false; kind = $null; value = $null; recording = $true }
    $user = [Microsoft.Win32.RegistryKey]::OpenBaseKey([Microsoft.Win32.RegistryHive]::CurrentUser, [Microsoft.Win32.RegistryView]::Registry64)
    $consentKey = $user.OpenSubKey($consentPath, $false)
    if ($consentKey) {
        try {
            if ($consentKey.GetValueNames() -contains 'RecordHistory') {
                $consent.present = $true
                $consent.kind = "$($consentKey.GetValueKind('RecordHistory'))"
                $consent.value = $consentKey.GetValue('RecordHistory')
                $consent.recording = $consent.kind -eq 'DWord' -and $consent.value -eq 1
            }
        } finally {
            $consentKey.Close()
        }
    }
    $user.Close()
    $snapshot.consent = $consent

    # History journal. Opened with full sharing so the mod's appends and atomic replace never fail.
    $directory = Join-Path ([Environment]::GetFolderPath('LocalApplicationData')) "TaskbarAIQuota\$ModId"
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
                $ids[$id] = 1 + [int]$ids[$id]
                if ($null -eq $first -or $time -lt $first) { $first = $time }
                if ($null -eq $last -or $time -gt $last) { $last = $time }
            }
            $journal.invalidTimes = $invalid
            $journal.firstUtc = if ($null -ne $first) { [DateTimeOffset]::FromUnixTimeMilliseconds([long]$first).ToString('o') } else { $null }
            $journal.lastUtc = if ($null -ne $last) { [DateTimeOffset]::FromUnixTimeMilliseconds([long]$last).ToString('o') } else { $null }
            $journal.days = $days
            $journal.historyIds = [ordered]@{}
            foreach ($id in $ids.Keys | Sort-Object) { $journal.historyIds[$id] = $ids[$id] }
        }
    }
    $snapshot.journal = $journal

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
    "Consent: $(if ($consent.present) { "RecordHistory=$($consent.value)" } else { 'not set' }) (recording $(if ($consent.recording) { 'on' } else { 'off' }))"
    "Journal: $journalText"
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

# Storage values other than settings, its length sentinel, hiddenAccounts and sign-ins.
$valuesOf = { param($snap) $map = @{}; foreach ($v in @($snap.storage.values)) { if ($v) { $map[$v.name] = $v } }; $map }
$baseValues = & $valuesOf $base
$currentValues = & $valuesOf $latest
foreach ($name in @($baseValues.Keys) + @($currentValues.Keys) | Sort-Object -Unique) {
    if ($name -match '^auth_' -or $name -in 'settings_v1', 'settings_v1_len', 'hiddenAccounts') { continue }
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

# Consent.
if (-not (& $same $base.consent $latest.consent)) {
    $text = "History consent: recording $($base.consent.recording) (value $($base.consent.value)) -> $($latest.consent.recording) (value $($latest.consent.value))"
    if ($Edited -ccontains 'consent') { $found.expected.Add($text) } else { $found.unexpected.Add($text) }
}

# Journal: growth and pruning older than the retention window are tolerated; anything removed inside
# it (except storage-cap compaction) or added before the baseline's last day is unexpected. Kept
# forever in both snapshots, nothing may be pruned or compacted.
$bj = $base.journal; $cj = $latest.journal
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
    $grown = 0; $pruned = 0; $compacted = 0
    foreach ($day in @($bj.days.Keys) + @($cj.days.Keys) | Sort-Object -Unique) {
        $before = [int]$bj.days[$day]; $after = [int]$cj.days[$day]
        if ($after -gt $before) {
            if ($day -ge $baseLastDay) { $grown += $after - $before }
            else { $found.unexpected.Add("Journal day $day gained $($after - $before) samples before the baseline's newest day") }
        } elseif ($after -lt $before) {
            if ($day -le $prunable) { $pruned += $before - $after }
            elseif ($capped) { $compacted += $before - $after }
            elseif ($retention) { $found.unexpected.Add("Journal day $day lost $($before - $after) of $before samples inside the $retention-day retention window") }
            else { $found.unexpected.Add("Journal day $day lost $($before - $after) of $before samples while history is kept forever") }
        }
    }
    if ($grown) { $found.tolerated.Add("Journal grew by $grown samples") }
    if ($pruned) { $found.tolerated.Add("Retention pruning removed $pruned samples older than $retention days") }
    if ($compacted) { $found.tolerated.Add("Storage-cap compaction removed $compacted samples") }
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
