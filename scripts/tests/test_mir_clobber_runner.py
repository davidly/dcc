"""Exercise data-only clobber inventory and isolated process-shard contracts."""

from pathlib import Path
import shutil
import subprocess
import unittest


@unittest.skipUnless(shutil.which("pwsh"), "PowerShell 7 is required")
class MirClobberRunnerTests(unittest.TestCase):
    def test_build_timeout_has_a_distinct_failure_message(self):
        root = Path(__file__).resolve().parents[2]
        command = r'''
$ErrorActionPreference = 'Stop'
$repoRoot = (Get-Location).Path
$tempRoot = [IO.Path]::GetTempPath()
$dccmake = 'unused-build-command'
$BuildTimeout = 17
$RunTimeout = 3
$collectExecutions = $false
$tokens = $null; $errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path $repoRoot 'scripts/run-mir-clobber-tests.ps1'),
    [ref]$tokens, [ref]$errors)
foreach ($name in @('Set-ProcessEnvironment', 'Assert-RunCase')) {
    $definition = $ast.Find({ param($node)
        $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
        $node.Name -eq $name
    }, $true)
    . ([scriptblock]::Create($definition.Extent.Text))
}
function Invoke-WithTimeout {
    param($FilePath, $Arguments, $WorkingDirectory, $TimeoutSeconds)
    if ($TimeoutSeconds -ne 17) { throw 'Build used the wrong timeout' }
    [pscustomobject]@{ TimedOut = $true; ExitCode = -1; Output = 'partial compiler output' }
}
$message = ''
try {
    Assert-RunCase -Name probe -Sources @('input.c') -Peep $false -StackCheck $true
} catch { $message = $_.Exception.Message }
if ($message -notmatch 'probe build timed out after 17s \(stack-nopeep\)' -or
    $message -notmatch 'partial compiler output') {
    throw "Unexpected build timeout diagnostic: $message"
}
'''
        result = subprocess.run(
            ["pwsh", "-NoProfile", "-Command", command],
            cwd=root, text=True, capture_output=True, timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_runner_contracts(self):
        root = Path(__file__).resolve().parents[2]
        result = subprocess.run(
            ["pwsh", "-NoProfile", "-File", "scripts/tests/test_mir_clobber_runner.ps1"],
            cwd=root,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=120,
        )
        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertIn("runner harness passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
