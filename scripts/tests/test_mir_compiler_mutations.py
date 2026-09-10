"""Exercise the real process scheduler with tiny, independently built controls."""

import json
import os
from pathlib import Path
import shutil
import subprocess
import unittest
import uuid


ROOT = Path(__file__).resolve().parents[2]
PWSH = shutil.which("pwsh")


@unittest.skipUnless(PWSH and shutil.which("cmake"), "requires PowerShell and CMake")
class CompilerMutationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        command = (
            f"Import-Module '{ROOT.as_posix()}/scripts/mir-compiler-mutations.psm1'; "
            "ConvertTo-Json -InputObject @(Get-MirCompilerMutations)"
        )
        cls.mutations = json.loads(subprocess.check_output(
            [PWSH, "-NoProfile", "-Command", command], text=True))

    def setUp(self):
        self.workspace = ROOT / "build" / ("mutation-runner-test-" + uuid.uuid4().hex)
        self.repo = self.workspace / "fixture repo"
        self.source = self.repo / "src/dcc"
        self.source.mkdir(parents=True)
        (self.repo / "scripts").mkdir()
        (self.repo / "tests/host").mkdir(parents=True)
        self.trace = self.workspace / "trace"
        self.trace.mkdir()
        for name in ("run-mir-compiler-mutations.ps1",
                     "run-mir-compiler-mutation-worker.ps1",
                     "mir-compiler-mutations.psm1"):
            shutil.copyfile(ROOT / "scripts" / name, self.repo / "scripts" / name)
        (self.repo / "scripts/new-mir-fuzz-source.ps1").write_text(
            'param($OutputPath, $Seed, $Programs)\n'
            'Set-Content -LiteralPath $OutputPath -Value "/* probe */"\n')
        self.anchor_text = "\n".join(
            m["Before"] for m in self.mutations if "Before" in m
        ) + """
static int mir_promote_objects(void)
mir_invalidate_use_cache();
mir_invalidate_use_cache();
mir_invalidate_use_cache();
struct MirAllocationSummary
"""
        (self.source / "dcc_mir.c").write_text(self.anchor_text)
        self.configure_fixture()

    def tearDown(self):
        shutil.rmtree(self.workspace)

    def configure_fixture(self, mode="normal"):
        tests = []
        for mutation in self.mutations[1:]:
            after = mutation.get("After", "(void)0;")
            diagnostic = mutation.get("ExpectedFailure", "")
            tests.append(f"""
string(FIND "${{text}}" [==[{after}]==] found)
if(NOT found EQUAL -1)
    if(NOT mutation STREQUAL "baseline")
        message(FATAL_ERROR "Multiple mutations in one source copy")
    endif()
    set(mutation "{mutation['Name']}")
    set(diagnostic "{diagnostic}")
endif()
""")
        (self.source / "CMakeLists.txt").write_text(f"""
cmake_minimum_required(VERSION 3.10)
project(mutation_fixture C)
file(READ "${{CMAKE_CURRENT_SOURCE_DIR}}/dcc_mir.c" text)
set(mutation "baseline")
set(diagnostic "")
{''.join(tests)}
file(WRITE "{self.trace.as_posix()}/${{mutation}}.start"
    "${{CMAKE_CURRENT_SOURCE_DIR}}\\n${{CMAKE_CURRENT_BINARY_DIR}}\\n${{DCC_RUNTIME_OUTPUT_DIRECTORY}}\\n")
configure_file(configuration.h.in configuration.h @ONLY)
add_executable(dcc ../../tests/host/mir_verify.c)
set_target_properties(dcc PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${{DCC_RUNTIME_OUTPUT_DIRECTORY}}")
add_executable(mir-verify-test ../../tests/host/mir_verify.c)
target_compile_definitions(mir-verify-test PRIVATE VERIFIER)
target_include_directories(dcc PRIVATE "${{CMAKE_CURRENT_BINARY_DIR}}")
target_include_directories(mir-verify-test PRIVATE "${{CMAKE_CURRENT_BINARY_DIR}}")
if("{mode}" STREQUAL "mixed" AND mutation STREQUAL "callback-identity")
    target_compile_definitions(dcc PRIVATE BROKEN)
endif()
""")
        (self.source / "configuration.h.in").write_text(
            '#define MUTATION "@mutation@"\n#define DIAGNOSTIC "@diagnostic@"\n'
            f'#define MODE "{mode}"\n#define TRACE "{self.trace.as_posix()}"\n')
        (self.repo / "tests/host/mir_verify.c").write_text(r'''
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "configuration.h"
#ifdef BROKEN
#error deliberate invalid build
#endif
static void done(void) {
    FILE *f = fopen(TRACE "/" MUTATION ".done", "w");
    if (!f) exit(9);
    fclose(f);
}
int main(int argc, char **argv) {
    const char *profile = getenv("LLVM_PROFILE_FILE");
    const char *cache = getenv("DCC_MIR_CACHE_VERIFY");
    if (getenv("DCC_MIR_SELECT_CANDIDATE") || !profile ||
        strstr(profile, "forbidden-normal-coverage") || !strstr(profile, "profiles"))
        return 10;
#ifdef VERIFIER
    if (!cache || strcmp(cache, "1")) return 11;
    done();
    if (!strcmp(MUTATION, "baseline")) {
        if (!strcmp(MODE, "baseline-test-failure")) {
            puts("MIR verifier failures=1"); return 1;
        }
        puts("MIR verifier failures=0"); return 0;
    }
    if (!strcmp(MODE, "mixed") && !strcmp(MUTATION, "dominance")) {
        puts("MIR verifier failures=0"); return 0;
    }
    if (!strcmp(MODE, "mixed") && !strcmp(MUTATION, "call-arity"))
        puts("FAIL unrelated assertion");
    else puts(DIAGNOSTIC);
    puts("MIR verifier failures=1");
    if (!strcmp(MODE, "mixed") && !strcmp(MUTATION, "argument-abi")) return 134;
    return 1;
#else
    int i;
    if (!cache || strcmp(cache, "1")) return 12;
    if (!strcmp(MODE, "baseline-compile-failure") && !strcmp(MUTATION, "baseline"))
        return 1;
    if (!strcmp(MUTATION, "promotion-cache")) {
        done();
        puts("; MIR CACHE MISMATCH mir_definition function=probe value=1 cached=2 uncached=-1");
        puts("dcc: fatal: MIR use-cache mismatch");
        return 1;
    }
    for (i = 1; i + 1 < argc; ++i) {
        if (!strcmp(argv[i], "-o")) {
            FILE *f = fopen(argv[i + 1], "w");
            if (!f) return 13;
            fputs("; probe", f); fclose(f);
            return 0;
        }
    }
    return 14;
#endif
}
''')

    def run_fixture(self, jobs=None, expected_exit=0):
        output = self.workspace / "output with spaces"
        command = [PWSH, "-NoLogo", "-NoProfile", "-File",
                   str(self.repo / "scripts/run-mir-compiler-mutations.ps1"),
                   "-OutputDirectory", str(output)]
        if jobs is not None:
            command += ["-Jobs", str(jobs), "-BuildJobs", "2"]
        environment = dict(os.environ, DCC_MIR_SELECT_CANDIDATE="poison",
                           DCC_MIR_CACHE_VERIFY="poison",
                           LLVM_PROFILE_FILE=str(self.workspace / "forbidden-normal-coverage"))
        completed = subprocess.run(command, cwd=self.repo, env=environment,
                                   capture_output=True, text=True, timeout=180)
        if expected_exit == 0:
            self.assertEqual(completed.returncode, 0, completed.stdout + completed.stderr)
        else:
            self.assertNotEqual(completed.returncode, 0)
        results = json.loads((output / "results.json").read_text())
        self.assertEqual([r["mutation"] for r in results],
                         [m["Name"] for m in self.mutations])
        self.assertFalse(list(output.glob("work-*")))
        self.assertFalse((self.workspace / "forbidden-normal-coverage").exists())
        self.assertEqual((self.source / "dcc_mir.c").read_text(), self.anchor_text)
        return results

    def test_default_serial_and_parallel_are_equal_and_bounded(self):
        serial = self.run_fixture()
        self.assertEqual([r["outcome"] for r in serial], ["passed"] + ["killed"] * 9)
        self.assert_concurrency(1)
        for path in self.trace.iterdir():
            path.unlink()
        parallel = self.run_fixture(jobs=2)
        self.assertEqual(serial, parallel)
        self.assert_concurrency(2)

    def assert_concurrency(self, limit):
        events = []
        baseline_end = (self.trace / "baseline.done").stat().st_mtime_ns
        paths = []
        for mutation in self.mutations:
            name = mutation["Name"]
            start = self.trace / f"{name}.start"
            paths.extend(start.read_text().splitlines())
            if name == "baseline":
                continue
            self.assertGreaterEqual(start.stat().st_mtime_ns, baseline_end)
            events.append((start.stat().st_mtime_ns, 1))
            events.append(((self.trace / f"{name}.done").stat().st_mtime_ns, -1))
        self.assertEqual(len(paths), len(set(paths)), "shared source/build/bin trees")
        count = peak = 0
        for _, delta in sorted(events):
            count += delta
            peak = max(peak, count)
        self.assertEqual(count, 0)
        self.assertEqual(peak, limit)

    def test_baseline_failures_do_not_schedule_mutants(self):
        for mode in ("baseline-compile-failure", "baseline-test-failure"):
            with self.subTest(mode=mode):
                self.configure_fixture(mode)
                results = self.run_fixture(jobs=2, expected_exit=1)
                self.assertTrue(all(r["outcome"] == "invalid" for r in results))
                self.assertTrue(all(r["phase"] == "not-run" for r in results[1:]))
                self.assertEqual([p.name for p in self.trace.glob("*.start")],
                                 ["baseline.start"])

    def test_survivors_invalid_exits_builds_and_worker_errors_are_recorded(self):
        self.configure_fixture("mixed")
        self.anchor_text += "\nphi_value = phi->dst;\n"
        (self.source / "dcc_mir.c").write_text(self.anchor_text)
        worker = self.repo / "scripts/run-mir-compiler-mutation-worker.ps1"
        worker.write_text(worker.read_text().replace(
            '$ErrorActionPreference = "Stop"',
            '$ErrorActionPreference = "Stop"\n'
            'if ($Name -eq "indirect-callee") {\n'
            '    Set-Content -LiteralPath "$OutputDirectory/result.json" -Value "{"\n'
            '    exit 0\n}\n'
            'if ($Name -eq "phi-edge-liveness") {\n'
            '    Remove-Item -LiteralPath "$OutputDirectory/result.json"\n'
            '    exit 0\n}\n'))
        results = self.run_fixture(jobs=2, expected_exit=1)
        outcomes = {r["mutation"]: r["outcome"] for r in results}
        self.assertEqual(outcomes, {
            "baseline": "passed", "dominance": "survived", "argument-abi": "invalid",
            "call-arity": "invalid", "indirect-callee": "invalid",
            "callback-identity": "invalid", "phi-edge-liveness": "invalid",
            "call-argument-liveness": "killed", "phi-consumer-value": "invalid",
            "promotion-cache": "killed",
        })

    def test_classifier_requires_exact_diagnostic_exit_and_completion(self):
        module = (ROOT / "scripts/mir-compiler-mutations.psm1").as_posix()
        command = f"Import-Module '{module}'; " + r'''
$hostMutation = @(Get-MirCompilerMutations)[1]
$cacheMutation = @(Get-MirCompilerMutations)[9]
$hostLog = "FAIL branch value cannot escape join`nMIR verifier failures=1`n"
$cacheLog = "; MIR CACHE MISMATCH mir_definition function=f value=1 cached=2 uncached=-1`ndcc: fatal: MIR use-cache mismatch`n"
foreach ($case in @(
    @($hostMutation, $hostLog, 1, $false, "killed"),
    @($hostMutation, $hostLog, 8, $false, "invalid"),
    @($hostMutation, $hostLog, 1, $true, "invalid"),
    @($hostMutation, $hostLog.Replace("join", "join extra"), 1, $false, "invalid"),
    @($hostMutation, "MIR verifier failures=0`n", 0, $false, "survived"),
    @($hostMutation, "", 0, $false, "invalid"),
    @($cacheMutation, $cacheLog, 1, $false, "killed"),
    @($cacheMutation, $cacheLog, 134, $false, "invalid"),
    @($cacheMutation, $cacheLog, 1, $true, "invalid"),
    @($cacheMutation, $cacheLog.Replace("mir_definition", "other"), 1, $false, "invalid"),
    @($cacheMutation, "", 0, $false, "survived")
)) {
    $actual = Get-MirMutationOutcome @{
        Output = $case[1]; ExitCode = $case[2]; TimedOut = $case[3]
    } $case[0]
    if ($actual -ne $case[4]) { throw "Expected $($case[4]), got $actual" }
}
'''
        completed = subprocess.run([PWSH, "-NoProfile", "-Command", command],
                                   capture_output=True, text=True, timeout=30)
        self.assertEqual(completed.returncode, 0, completed.stderr)

    def test_worker_and_build_limits_reject_zero(self):
        for parameter in ("Jobs", "BuildJobs"):
            completed = subprocess.run(
                [PWSH, "-NoProfile", "-File",
                 str(self.repo / "scripts/run-mir-compiler-mutations.ps1"),
                 f"-{parameter}", "0"], capture_output=True, text=True, timeout=30)
            self.assertNotEqual(completed.returncode, 0)
            self.assertIn(parameter, completed.stderr)

    def test_child_timeout_and_environment_isolation(self):
        command = (
            f"Import-Module '{ROOT.as_posix()}/scripts/mir-compiler-mutations.psm1'; "
            f"$directory = '{self.workspace.as_posix()}'; "
        ) + r'''
$pwsh = (Get-Process -Id $PID).Path
$env:DCC_MIR_CACHE_VERIFY = "parent-cache"
$env:LLVM_PROFILE_FILE = "parent-profile"
$child = Start-MirMutationProcess $pwsh @(
    "-NoProfile", "-Command",
    'if ($env:DCC_MIR_CACHE_VERIFY) { exit 9 }; Write-Output $env:LLVM_PROFILE_FILE'
) $directory "$directory/environment.log"
$result = Complete-MirMutationProcess $child
if ($result.ExitCode -ne 0 -or $result.Output -notmatch 'profiles') {
    throw "Child environment not isolated"
}
if ($env:DCC_MIR_CACHE_VERIFY -ne "parent-cache" -or
    $env:LLVM_PROFILE_FILE -ne "parent-profile") { throw "Parent environment changed" }
$child = Start-MirMutationProcess $pwsh @(
    "-NoProfile", "-Command", 'Write-Output started; Start-Sleep -Seconds 30'
) $directory "$directory/timeout.log"
$result = Complete-MirMutationProcess $child 1
if (-not $result.TimedOut -or $result.Output -notmatch 'started') {
    throw "Child timeout did not preserve its diagnostic"
}
'''
        completed = subprocess.run([PWSH, "-NoProfile", "-Command", command],
                                   capture_output=True, text=True, timeout=15)
        self.assertEqual(completed.returncode, 0, completed.stderr)


if __name__ == "__main__":
    unittest.main()
