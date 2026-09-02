[CmdletBinding()]
param(
    [int]$PrNumber = 0,
    [string]$Repository = 'ramensoftware/windhawk-mods',
    [string]$Owner = 'Cleroth',
    [string]$ModId = 'taskbar-ai-quota'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

if (-not (Get-Command gh -ErrorAction SilentlyContinue)) {
    throw 'gh is not available'
}

if ($PrNumber -eq 0) {
    $pullRequestsJson = gh pr list -R $Repository --state open --author $Owner `
        --search "$ModId in:title" `
        --json number,title,url,updatedAt
    if ($LASTEXITCODE -ne 0) {
        throw "Could not list pull requests (exit $LASTEXITCODE)"
    }
    $expectedTitlePrefix = "Update ${ModId}:"
    $pullRequests = @($pullRequestsJson | ConvertFrom-Json | Where-Object {
        $_.title.StartsWith($expectedTitlePrefix, [System.StringComparison]::OrdinalIgnoreCase)
    })
    if ($pullRequests.Count -eq 0) {
        throw "No open $ModId pull request by $Owner was found"
    }
    if ($pullRequests.Count -gt 1) {
        $matches = ($pullRequests | ForEach-Object { "#$($_.number) $($_.title)" }) -join '; '
        throw "Multiple matching pull requests found; pass -PrNumber. $matches"
    }
    $PrNumber = $pullRequests[0].number
}

$pullRequestJson = gh pr view $PrNumber -R $Repository `
    --json number,title,url,state,isDraft,headRefOid,statusCheckRollup,comments
if ($LASTEXITCODE -ne 0) {
    throw "Could not read pull request #$PrNumber (exit $LASTEXITCODE)"
}
$pullRequest = $pullRequestJson | ConvertFrom-Json

$checks = @($pullRequest.statusCheckRollup | ForEach-Object {
    if ($_.__typename -eq 'StatusContext') {
        [pscustomobject]@{
            name = $_.context
            status = if ($_.state -in @('PENDING', 'EXPECTED')) { 'PENDING' } else { 'COMPLETED' }
            conclusion = $_.state
        }
    } else {
        [pscustomobject]@{
            name = $_.name
            status = $_.status
            conclusion = $_.conclusion
        }
    }
})
$pendingChecks = @($checks | Where-Object { $_.status -ne 'COMPLETED' })
$failedChecks = @($checks | Where-Object {
    $_.status -eq 'COMPLETED' -and
    $_.conclusion -notin @('SUCCESS', 'SKIPPED', 'NEUTRAL')
})
$successfulChecks = @($checks | Where-Object { $_.conclusion -eq 'SUCCESS' })

$reviewComments = @($pullRequest.comments | Where-Object {
    $_.author.login -eq 'windhawk-reviewer' -and
    $_.body -match '<!--\s*ai-review\s+sha=([0-9a-f]{40})\s*-->'
} | Sort-Object createdAt)
$reviewSha = $null
$reviewCreatedAt = $null
if ($reviewComments.Count -ne 0) {
    $latestReview = $reviewComments[-1]
    $reviewMatch = [regex]::Match(
        $latestReview.body, '<!--\s*ai-review\s+sha=([0-9a-f]{40})\s*-->')
    $reviewSha = $reviewMatch.Groups[1].Value
    $reviewCreatedAt = [datetimeoffset]$latestReview.createdAt
}

$limitComments = @($pullRequest.comments | Where-Object {
    $_.author.login -eq 'windhawk-reviewer' -and
    $_.body -match 'Comment `/ai-review` again after (\d{4}-\d{2}-\d{2} \d{2}:\d{2} UTC)'
} | Sort-Object createdAt)
$retryText = $null
if ($limitComments.Count -ne 0) {
    $latestLimit = $limitComments[-1]
    $limitCreatedAt = [datetimeoffset]$latestLimit.createdAt
    if ($null -eq $reviewCreatedAt -or $limitCreatedAt -gt $reviewCreatedAt) {
        $retryMatch = [regex]::Match(
            $latestLimit.body,
            'Comment `/ai-review` again after (\d{4}-\d{2}-\d{2} \d{2}:\d{2} UTC)')
        if ($retryMatch.Success) {
            $retryDeadline = [datetimeoffset]::ParseExact(
                $retryMatch.Groups[1].Value,
                "yyyy-MM-dd HH:mm 'UTC'",
                [System.Globalization.CultureInfo]::InvariantCulture,
                [System.Globalization.DateTimeStyles]::AssumeUniversal)
            $retryText = if ($retryDeadline -gt [datetimeoffset]::UtcNow) {
                $retryDeadline.ToString('yyyy-MM-dd HH:mm UTC')
            } else {
                'available now'
            }
        }
    }
}

$reviewMatchesHead = $reviewSha -eq $pullRequest.headRefOid
$checksReady = $checks.Count -ne 0 -and $successfulChecks.Count -ne 0 -and
               $pendingChecks.Count -eq 0 -and $failedChecks.Count -eq 0
$ready = $pullRequest.state -eq 'OPEN' -and -not $pullRequest.isDraft -and
         $checksReady -and $reviewMatchesHead

"PR #$($pullRequest.number): $($pullRequest.title)"
"URL: $($pullRequest.url)"
"State: $($pullRequest.state)"
"Draft: $($pullRequest.isDraft.ToString().ToLowerInvariant())"
"Head: $($pullRequest.headRefOid)"
if ($checks.Count -eq 0) {
    'Checks: none reported'
} elseif ($failedChecks.Count -ne 0) {
    "Checks: failed ($($failedChecks.name -join ', '))"
} elseif ($pendingChecks.Count -ne 0) {
    "Checks: pending ($($pendingChecks.name -join ', '))"
} elseif ($successfulChecks.Count -eq 0) {
    'Checks: completed without a successful check'
} else {
    'Checks: passed'
}

if ($null -eq $reviewSha) {
    'AI review: none'
} elseif ($reviewMatchesHead) {
    "AI review: current ($reviewSha)"
} else {
    "AI review: stale ($reviewSha)"
}
if ($retryText) {
    "AI review retry: $retryText"
}
"Ready for human review: $($ready.ToString().ToLowerInvariant())"
