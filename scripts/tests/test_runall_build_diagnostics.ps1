#Requires -Version 7
# Exercise the actual serial/parallel worker functions with a successful fake
# build and an existing COM file, so exit status/artifact checks cannot mask
# a missing diagnostic check. No compiler or emulator is launched.
$ErrorActionPreference = 'Stop'
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot '../..')).Path
foreach ($entry in @(
    @('scripts/runall.ps1', 'Invoke-DccMakeBuild'),
    @('scripts/runall-extended.ps1', 'Invoke-ExtendedTest')
)) {
    $tokens = $null
    $errors = $null
    $ast = [System.Management.Automation.Language.Parser]::ParseFile(
        (Join-Path $repoRoot $entry[0]), [ref]$tokens, [ref]$errors)
    if ($errors.Count) { throw "Parse errors in $($entry[0]): $errors" }
    $name = $entry[1]
    $definition = $ast.Find({ param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq $name
    }, $true)
    . ([scriptblock]::Create($definition.Extent.Text))
}

function Get-DccMakeCommand { 'fake-dccmake' }
function Test-MatchesExpected { param($Actual, $Expected) $Actual -eq $Expected }
function Invoke-ProcessWithTimeout {
    param($FilePath, $Arguments, $WorkingDirectory, $TimeoutSeconds)
    if ($FilePath -ne 'fake-dccmake') { $script:emulatorCalls++ }
    [pscustomobject]@{
        ExitCode = 0
        TimedOut = $false
        Output = if ($FilePath -eq 'fake-dccmake') { $script:buildOutput } else { 'ok' }
    }
}

$scratch = Join-Path ([IO.Path]::GetTempPath()) ('dcc-build-diagnostics-' + [guid]::NewGuid())
New-Item -ItemType Directory -Path $scratch | Out-Null
try {
    $source = Join-Path $scratch 'fixture.c'
    $expected = Join-Path $scratch 'fixture.txt'
    Set-Content $source 'int main(void) { return 0; }'
    Set-Content $expected 'ok' -NoNewline
    Set-Content (Join-Path $scratch 'FIXTURE.COM') 'artifact'
    $case = [pscustomobject]@{
        Name = 'fixture'; SourcePath = $source; ExpectedPath = $expected
        DccFloatio = $null; DccLongio = $null; DccArgs = ''; ExpectedExitCode = 0
    }
    $checks = 0
    foreach ($sample in @(
        @{ Text = '; MIR assignment_name: instruction 68 has invalid PHI predecessors'; Pass = $false },
        @{ Text = '; MIR f: invalid function dimensions'; Pass = $false },
        @{ Text = '; MIR f: argument 2 does not dominate call 7'; Pass = $false },
        @{ Text = '; MIR f: instruction 3 uses non-dominating v1'; Pass = $false },
        @{ Text = '%Undefined symbol'; Pass = $false },
        @{ Text = "No Fatal error(s)`r`n; MIR f blocks=3 values=4"; Pass = $true }
    )) {
        $script:buildOutput = $sample.Text
        foreach ($mode in @('fast', 'nopeep', 'full')) {
            # These diagnostics are injected failure oracles, not actual
            # compiler errors. Hide Write-Host output from the worker while
            # retaining its return value and unexpected errors.
            $actual = Invoke-DccMakeBuild -Name fixture -SourcePath $source -BuildDir $scratch -Mode $mode -Quiet 6>$null
            if ($actual -ne $sample.Pass) { throw "Main suite accepted/rejected incorrectly: $mode $($sample.Text)" }
            $checks++
        }
        $script:emulatorCalls = 0
        $result = Invoke-ExtendedTest -Case $case -Modes @('fast', 'nopeep') -BuildDir $scratch -RepoRoot $repoRoot -Emulator fake-emulator -RunTimeout 1
        if ($result.Passed -ne $sample.Pass) { throw "Extended suite accepted/rejected incorrectly: $($sample.Text)" }
        if (!$sample.Pass -and $script:emulatorCalls) { throw 'Extended suite ran a rejected build' }
        $checks++
    }
    Write-Host "Build diagnostic checks=$checks failures=0"
}
finally {
    Remove-Item -LiteralPath $scratch -Recurse -Force
}
