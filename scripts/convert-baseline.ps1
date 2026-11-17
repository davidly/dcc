#Requires -Version 7
<#
.SYNOPSIS
Split the legacy concatenated baseline (baseline_test_dcc.txt) into per-app
baseline files under tests/baselines/.

.DESCRIPTION
The original baseline is a single text file where each app's expected output is
preceded by a "test <app>" header line, in a specific run order (APPLIST). A
naive "split on every line starting with 'test '" is unsafe because some app
output legitimately contains such lines (e.g. tstdc prints
"test tstdc completed with great success").

This converter uses the authoritative ordered app list to split the baseline
correctly: a "test <app>" line is only treated as a header when <app> matches
the next expected app in the list. Each app's exact output is written to
tests/baselines/<app>.txt, with no escaping or transformation, so a plain
file comparison reproduces the original diff-based verification.

.PARAMETER InputFile
  Legacy baseline text file (default: "baseline_test_dcc.txt").

.PARAMETER OutputDir
  Directory to write per-app baseline files (default: "tests/baselines").

.PARAMETER AppList
  Required complete app list in the original run order. The current Python
  runner discovers tests dynamically and cannot reconstruct this historical order.

.EXAMPLE
  pwsh ./scripts/convert-baseline.ps1 -AppList (Get-Content legacy-app-order.txt)
  pwsh ./scripts/convert-baseline.ps1 -InputFile baseline_test_dcc.txt -OutputDir tests/baselines -AppList (Get-Content legacy-app-order.txt)

.NOTES
  Output: one file per app, e.g. tests/baselines/sieve.txt, containing the exact
  expected stdout for that app (excluding the "test <app>" header line).
#>

param(
    [string]$InputFile = "baseline_test_dcc.txt",
    [string]$OutputDir = "tests/baselines",
    [string[]]$AppList
)

$ErrorActionPreference = "Stop"

if (-not (Test-Path $InputFile)) {
    Write-Error "Input baseline not found: $InputFile"
    exit 1
}

# Historical split order must be supplied explicitly; discovery order in
# the current runner is not the order of the legacy concatenated baseline.
if (-not $AppList -or $AppList.Count -eq 0) {
    Write-Error "Provide -AppList in the complete original run order (for example, -AppList (Get-Content legacy-app-order.txt))."
    exit 1
}

Write-Host "Using ordered app list of $($AppList.Count) apps" -ForegroundColor Cyan

# Read the baseline as raw text, normalized to LF. Some app output has no
# trailing newline, so a "test <app>" header can be glued onto the previous
# output line (e.g. "...319test bint"). A line-based split therefore misses
# headers. Instead, slice the raw text by locating each expected
# "test <app>\n" marker in sequence (order is authoritative), which reproduces
# every app's output byte-for-byte.
$raw = (Get-Content -Path $InputFile -Raw) -replace "`r`n", "`n"

# Locate markers in order. For each app, find "test <app>\n" at/after the cursor.
$markers = [System.Collections.Generic.List[object]]::new()
$missing = [System.Collections.Generic.List[string]]::new()
$cursor = 0
foreach ($app in $AppList) {
    $pattern = "test " + [regex]::Escape($app) + "`n"
    $m = [regex]::Match($raw.Substring($cursor), $pattern)
    if ($m.Success) {
        $markerStart = $cursor + $m.Index
        $contentStart = $markerStart + $m.Length
        $markers.Add([pscustomobject]@{ App = $app; ContentStart = $contentStart; MarkerStart = $markerStart })
        $cursor = $contentStart
    }
    else {
        $missing.Add($app)
    }
}

if ($markers.Count -eq 0) {
    Write-Error "No app markers were found in $InputFile"
    exit 1
}

# Slice content: each app's output runs from its ContentStart to the next
# marker's MarkerStart (or end of file for the last app).
$script:result = [ordered]@{}
for ($i = 0; $i -lt $markers.Count; $i++) {
    $start = $markers[$i].ContentStart
    $end = if ($i + 1 -lt $markers.Count) { $markers[$i + 1].MarkerStart } else { $raw.Length }
    $script:result[$markers[$i].App] = $raw.Substring($start, $end - $start)
}

# Ensure output directory exists.
if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

# Write per-app files (exact bytes; LF line endings preserved as captured).
$written = 0
foreach ($app in $script:result.Keys) {
    $outPath = Join-Path $OutputDir "$app.txt"
    [System.IO.File]::WriteAllText($outPath, $script:result[$app])
    $written++
}

Write-Host "Wrote $written per-app baseline files to $OutputDir/" -ForegroundColor Green

# Report any expected apps that were not found in the baseline.
if ($missing.Count -gt 0) {
    Write-Host "Apps in list but absent from baseline ($($missing.Count)):" -ForegroundColor Yellow
    $missing | ForEach-Object { Write-Host "  - $_" -ForegroundColor Yellow }
}
