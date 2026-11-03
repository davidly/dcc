"""Verify the aggregate AST/MIR proof runner's public gate inventory."""

from pathlib import Path
import shutil
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[2]
PWSH = shutil.which("pwsh")


@unittest.skipUnless(PWSH, "PowerShell 7 is required")
class MirProofSuiteTests(unittest.TestCase):
    def test_default_clang_prefers_newest_versioned_compiler(self):
        command = r'''
$ErrorActionPreference = 'Stop'
$tokens = $null; $errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path (Get-Location) 'scripts/run-mir-proof-suite.ps1'),
    [ref]$tokens, [ref]$errors)
$function = $ast.Find({ param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Resolve-AvailableClang'
}, $true)
. ([scriptblock]::Create($function.Extent.Text))
$script:versioned = @(
    [pscustomobject]@{ Name = 'clang-14'; Source = 'older-compiler' },
    [pscustomobject]@{ Name = 'clang-18'; Source = 'newer-compiler' },
    [pscustomobject]@{ Name = 'clang-format-99'; Source = 'not-a-compiler' }
)
$script:unversioned = [pscustomobject]@{ Source = 'default-compiler' }
function Get-Command {
    param($Name, $ErrorAction)
    if ($Name -eq 'clang-*') { return $script:versioned }
    if ($Name -eq 'clang') { return $script:unversioned }
}
if ((Resolve-AvailableClang) -ne 'newer-compiler') {
    throw 'Did not prefer the newest versioned compiler'
}
$script:versioned = @()
if ((Resolve-AvailableClang) -ne 'default-compiler') {
    throw 'Did not fall back to unversioned clang'
}
$script:unversioned = $null
if (Resolve-AvailableClang) { throw 'Unexpected compiler resolution' }
'''
        result = subprocess.run(
            [PWSH, "-NoLogo", "-NoProfile", "-Command", command],
            cwd=ROOT, text=True, capture_output=True, timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_llvm_version_accepts_profdata_banner_and_rejects_bad_commands(self):
        command = r'''
$ErrorActionPreference = 'Stop'
$tokens = $null; $errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path (Get-Location) 'scripts/run-mir-proof-suite.ps1'),
    [ref]$tokens, [ref]$errors)
$function = $ast.Find({ param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Get-LlvmMajor'
}, $true)
. ([scriptblock]::Create($function.Extent.Text))
function Test-Clang { 'Ubuntu clang version 18.1.8'; $global:LASTEXITCODE = 0 }
function Test-Profdata { 'llvm-profdata'; 'LLVM version 18.1.8'; $global:LASTEXITCODE = 0 }
function Test-Unknown { 'unknown'; $global:LASTEXITCODE = 0 }
function Test-Failed { 'LLVM version 18.1.8'; $global:LASTEXITCODE = 2 }
function llvm-profdata-14 {
    param($First, $Second)
    if ($First -eq 'merge' -and $Second -eq '--version') {
        'Ubuntu LLVM version 14.0.0'; $global:LASTEXITCODE = 0
    } else { 'Unknown command!'; $global:LASTEXITCODE = 1 }
}
if ((Get-LlvmMajor 'llvm-profdata-14') -ne 14) {
    throw 'Could not read legacy llvm-profdata version'
}
foreach ($command in @('Test-Clang', 'Test-Profdata')) {
    if ((Get-LlvmMajor $command) -ne 18) { throw "Incorrect version for $command" }
}
foreach ($command in @('Test-Unknown', 'Test-Failed')) {
    $rejected = $false
    try { Get-LlvmMajor $command } catch { $rejected = $true }
    if (-not $rejected) { throw "Accepted invalid version for $command" }
}
'''
        result = subprocess.run(
            [PWSH, "-NoLogo", "-NoProfile", "-Command", command],
            cwd=ROOT, text=True, capture_output=True, timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_unversioned_clang_discovers_versioned_llvm_companions(self):
        command = r'''
$ErrorActionPreference = 'Stop'
$tokens = $null; $errors = $null
$ast = [System.Management.Automation.Language.Parser]::ParseFile(
    (Join-Path (Get-Location) 'scripts/run-mir-proof-suite.ps1'),
    [ref]$tokens, [ref]$errors)
$function = $ast.Find({ param($node)
    $node -is [System.Management.Automation.Language.FunctionDefinitionAst] -and
    $node.Name -eq 'Resolve-LlvmPeer'
}, $true)
. ([scriptblock]::Create($function.Extent.Text))
$directory = Join-Path ([IO.Path]::GetTempPath()) 'fake-llvm'
$script:compiler = Join-Path $directory 'clang'
$script:available = @{}
function Get-Command {
    param($Name, $ErrorAction)
    if ($Name -eq $script:compiler) { return [pscustomobject]@{ Source = $script:compiler } }
    if ($script:available.ContainsKey($Name)) {
        return [pscustomobject]@{ Source = $script:available[$Name] }
    }
    return $null
}
function Get-LlvmMajor { param($Command) return 18 }
foreach ($name in @('clang', 'clang-18', 'clang-cl-18')) {
    $script:compiler = Join-Path $directory $name
    foreach ($tool in @('llvm-cov', 'llvm-profdata')) {
        $script:available = @{}
        $peer = Join-Path $directory "$tool-18"
        $script:available[$peer] = $peer
        if ((Resolve-LlvmPeer $script:compiler $tool) -ne $peer) {
            throw "Could not resolve versioned $tool for $name"
        }
        $script:available = @{}
        $peer = Join-Path $directory $tool
        $script:available[$peer] = $peer
        if ((Resolve-LlvmPeer $script:compiler $tool) -ne $peer) {
            throw "Could not resolve unversioned $tool for $name"
        }
        $script:available = @{}
        if (Resolve-LlvmPeer $script:compiler $tool) {
            throw "Missing $tool unexpectedly resolved for $name"
        }
    }
}
Write-Output 'LLVM peer discovery checks=18 failures=0'
'''
        result = subprocess.run(
            [PWSH, "-NoLogo", "-NoProfile", "-Command", command],
            cwd=ROOT, text=True, capture_output=True, timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn("LLVM peer discovery checks=18 failures=0", result.stdout)

    def test_cmake_builds_do_not_replace_the_canonical_compiler(self):
        runner = (ROOT / "scripts/run-mir-proof-suite.ps1").read_text(
            encoding="utf-8"
        )

        self.assertEqual(runner.count("-DDCC_RUNTIME_OUTPUT_DIRECTORY="), 3)
        self.assertIn('"-DCMAKE_C_COMPILER=$coverageCompiler"', runner)

    def test_runner_sanitizes_ambient_controls_and_bounds_parallel_groups(self):
        runner = (ROOT / "scripts/run-mir-proof-suite.ps1").read_text(
            encoding="utf-8"
        )

        self.assertIn("Clear-AmbientProofControls", runner)
        self.assertIn('Environment = @{ CC = $coverageCompiler }', runner)
        self.assertIn(
            '$env:PATH = $repoRoot + [System.IO.Path]::PathSeparator + $env:PATH',
            runner,
        )
        self.assertIn(
            "[System.StringComparison]::OrdinalIgnoreCase",
            runner,
        )
        self.assertIn(
            ") -MaxConcurrency ([Math]::Min(6, $Jobs))",
            runner,
        )
        self.assertIn(
            ") -MaxConcurrency ([Math]::Min(2, $Jobs))",
            runner,
        )
        self.assertIn(
            "[Math]::Floor($Jobs / [Math]::Min(4, $Jobs))",
            runner,
        )
        self.assertIn("[switch]$RequireComplete", runner)
        self.assertIn("[switch]$All", runner)
        self.assertIn(
            'DCC_COVERAGE_REQUIRE_COMPLETE = "1"',
            runner,
        )

    def test_parallel_release_and_windows_coverage_paths_are_isolated(self):
        runner = (ROOT / "scripts/run-mir-proof-suite.ps1").read_text(
            encoding="utf-8"
        )
        runall = (ROOT / "scripts/runall.ps1").read_text(encoding="utf-8")

        self.assertIn("function Convert-ToShellPath", runner)
        self.assertIn('cygpath -u -- "$1"', runner)
        self.assertIn(
            "DCC_COVERAGE_BUILD_DIR = Convert-ToShellPath $coverageOutput",
            runner,
        )
        self.assertEqual(
            runall.count('-BuildDir (Join-Path $BuildDir "diagnostics")'),
            2,
        )

    def test_fuzz_generator_and_receipt_are_part_of_the_aggregate_runner(self):
        runner = (ROOT / "scripts/run-mir-proof-suite.ps1").read_text(
            encoding="utf-8"
        )

        self.assertIn("scripts/test-mir-fuzz-source.ps1", runner)
        self.assertIn('Name = "MIR fuzz generator proof"', runner)
        self.assertIn('Join-Path $outputRoot "receipt.json"', runner)
        self.assertIn("ConvertTo-Json -Depth 6", runner)
        self.assertIn("Expected versus found:", runner)
        self.assertIn(
            "export CC=/path/to/clang-18 LLVM_COV=/path/to/llvm-cov-18 LLVM_PROFDATA=/path/to/llvm-profdata-18",
            runner,
        )

    def test_lists_all_gates_without_running_them(self):
        result = subprocess.run(
            [
                PWSH,
                "-NoLogo",
                "-NoProfile",
                "-File",
                "scripts/run-mir-proof-suite.ps1",
                "-List",
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=30,
        )

        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(
            result.stdout.splitlines(),
            [
                "1. canonical tool build",
                "2. script tests and static audits",
                "3. independent release CMake build",
                "4. normal MIR host tests",
                "5. ASan/UBSan MIR host tests",
                "6. debugger-host tests",
                "7. isolated compiler mutation campaign",
                "8. strict stack release suite",
                "9. strict no-stack release suite",
                "10. extended generated-MIR census",
                "11. instrumented compiler coverage and mutation campaigns",
            ],
        )

    def test_all_alias_keeps_the_same_gate_inventory(self):
        result = subprocess.run(
            [
                PWSH,
                "-NoLogo",
                "-NoProfile",
                "-File",
                "scripts/run-mir-proof-suite.ps1",
                "-All",
                "-List",
            ],
            cwd=ROOT,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            timeout=30,
        )

        self.assertEqual(result.returncode, 0, result.stdout)
        self.assertEqual(
            result.stdout.splitlines(),
            [
                "1. canonical tool build",
                "2. script tests and static audits",
                "3. independent release CMake build",
                "4. normal MIR host tests",
                "5. ASan/UBSan MIR host tests",
                "6. debugger-host tests",
                "7. isolated compiler mutation campaign",
                "8. strict stack release suite",
                "9. strict no-stack release suite",
                "10. extended generated-MIR census",
                "11. instrumented compiler coverage and mutation campaigns",
            ],
        )


if __name__ == "__main__":
    unittest.main()
