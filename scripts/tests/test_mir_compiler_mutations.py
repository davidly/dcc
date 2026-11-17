"""Real native fixture builds exercise isolated Python mutation scheduling."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
import uuid
from scripts import mir_compiler_mutations as mutations
from scripts.tests.test_process_supervision_python import process_alive

ROOT=Path(__file__).resolve().parents[2]


@unittest.skipUnless(shutil.which('cmake'), 'requires CMake and a native compiler')
class CompilerMutationTests(unittest.TestCase):
    maxDiff=None
    @classmethod
    def setUpClass(cls):
        cls.mutations=mutations.mutations()

    def setUp(self):
        self.workspace = Path(tempfile.mkdtemp(prefix="dcc-mut-")).resolve()
        self.repo = self.workspace / "fixture repo"
        self.source = self.repo / "src/dcc"
        self.source.mkdir(parents=True)
        (self.repo / "scripts").mkdir()
        (self.repo / "tests/host").mkdir(parents=True)
        self.trace = self.workspace / "trace"
        self.trace.mkdir()
        for name in ("run-mir-compiler-mutations.py",
                     "run-mir-compiler-mutation-worker.py",
                     "mir_compiler_mutations.py", "mir-compiler-mutations.json",
                     "test-mir-matcher-restoration.py",
                     "test-mir-paired-byte-restoration.py",
                     "process_supervision.py"):
            shutil.copyfile(ROOT / "scripts" / name, self.repo / "scripts" / name)
        (self.repo / "scripts/new-mir-fuzz-source.py").write_text(
            'def generate(seed, programs): return "/* probe */"\n')
        self.source_text = {}
        for mutation in self.mutations:
            if "Before" not in mutation:
                continue
            source = mutation.get("Source", "src/dcc/dcc_mir.c")
            self.source_text.setdefault(source, []).append(mutation["Before"])
        self.source_text["src/dcc/dcc_mir.c"].append("""
static int mir_promote_objects(void)
mir_invalidate_use_cache();
mir_invalidate_use_cache();
mir_invalidate_use_cache();
struct MirAllocationSummary
""")
        for relative, fragments in self.source_text.items():
            path = self.repo / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("\n".join(fragments))
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
        reads = []
        for index, relative in enumerate(self.source_text):
            reads.append(
                f'file(READ "${{CMAKE_CURRENT_SOURCE_DIR}}/'
                f'{Path(relative).name}" source{index})\n'
                f'string(APPEND text "${{source{index}}}\\n")\n'
            )
        (self.source / "CMakeLists.txt").write_text(f"""
cmake_minimum_required(VERSION 3.10)
project(mutation_fixture C)
set(text "")
{''.join(reads)}
set(mutation "baseline")
set(diagnostic "")
{''.join(tests)}
file(WRITE "{self.trace.as_posix()}/${{mutation}}.start"
    "${{CMAKE_CURRENT_SOURCE_DIR}}\\n${{CMAKE_CURRENT_BINARY_DIR}}\\n${{DCC_RUNTIME_OUTPUT_DIRECTORY}}\\n")
configure_file(configuration.h.in configuration.h @ONLY)
if(NOT EXISTS "${{CMAKE_CURRENT_SOURCE_DIR}}/../../tests/host/mir_scalar_dag.c")
    message(FATAL_ERROR "Auxiliary host test was not copied")
endif()
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
    if ((getenv("DCC_MIR_SELECT_CANDIDATE") &&
         strcmp(getenv("DCC_MIR_SELECT_CANDIDATE"), "regional")) || !profile ||
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
    if (!strcmp(MUTATION, "global-field-vn-cache")) {
        puts("; MIR CACHE MISMATCH mir_definition function=probe value=3 cached=4 uncached=-1");
        puts("dcc: fatal: MIR use-cache mismatch");
        return 1;
    }
    if (!strcmp(MODE, "mixed") && !strcmp(MUTATION, "call-arity"))
        puts("FAIL unrelated assertion");
    else puts(DIAGNOSTIC);
    puts("MIR verifier failures=1");
    if (!strcmp(MODE, "mixed") && !strcmp(MUTATION, "argument-abi")) return 134;
    return 1;
#else
    int gap = 0;
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
    if (getenv("DCC_MIR_MACHINE_MUTATE")) {
        done();
        if (!strncmp(MUTATION, "allocation-", 11)) {
            puts("; MIR machine function=main template=allocation-lifetime-runner accept=emitted");
            puts("; MIR selection function=main selector=scheduled-machine-cfg result=mir");
        } else if (strstr(getenv("DCC_MIR_MACHINE_MUTATE"), "memory_size")) {
            puts("; MIR machine function=main template=allocation-lifetime-runner reject=large-writes");
            puts("; MIR selection function=main selector=spilled-scalar-cfg result=mir");
        } else {
            puts("; MIR machine function=main template=allocation-lifetime-runner reject=first-allocation");
            puts("; MIR selection function=main selector=spilled-scalar-cfg result=mir");
        }
    }
    for (i = 1; i < argc; ++i)
        if (strstr(argv[i], "MIR_CLOBBER_PAIRED_GAP=1"))
            gap = 1;
    for (i = 1; i + 1 < argc; ++i) {
        if (!strcmp(argv[i], "-o")) {
            FILE *f = fopen(argv[i + 1], "w");
            if (!f) return 13;
            if (!gap || !strcmp(MUTATION, "paired-byte-adjacency"))
                fputs(";@dcc.mir paired-byte-call\n", f);
            else
                fputs("; generic\n", f);
            fclose(f);
            if (!strcmp(MUTATION, "paired-byte-adjacency"))
                done();
            return 0;
        }
    }
    return 14;
#endif
}
''')
        (self.repo / "tests/host/mir_scalar_dag.c").write_text(
            "/* Auxiliary host test copied into mutation workspaces. */\n"
        )
        (self.repo / "tests/mir-clobber").mkdir(parents=True, exist_ok=True)
        (self.repo / "tests/mir-clobber/pairbyte.c").write_text(
            "int main(void) { return 0; }\n"
        )

    def run_fixture(self, jobs=None, expected_exit=0, names=None):
        output = self.workspace / "output with spaces"
        command = [sys.executable, str(self.repo / "scripts/run-mir-compiler-mutations.py"),
                   "-OutputDirectory", str(output)]
        if jobs is not None:
            command += ["-Jobs", str(jobs), "-BuildJobs", "2"]
        if names is not None:
            command += ["-Names", ",".join(names)]
        environment = dict(os.environ, DCC_MIR_SELECT_CANDIDATE="poison",
                           DCC_MIR_CACHE_VERIFY="poison",
                           LLVM_PROFILE_FILE=str(self.workspace / "forbidden-normal-coverage"))
        # Observe live start/done files instead of comparing wall-clock mtimes,
        # which can move or be coarsely cached on different filesystems.
        log=self.workspace/'campaign-console.log'
        self.observed_peak=0
        premature=False
        began=time.monotonic()
        with log.open('w') as capture:
            process=subprocess.Popen(command,cwd=self.repo,env=environment,stdout=capture,stderr=subprocess.STDOUT)
            while process.poll() is None:
                starts={p.stem for p in self.trace.glob('*.start')}
                dones={p.stem for p in self.trace.glob('*.done')}
                self.observed_peak=max(self.observed_peak,len(starts-dones))
                premature |= bool(starts-{'baseline'}) and 'baseline' not in dones
                if time.monotonic()-began>180:
                    process.kill();process.wait()
                    self.fail('fixture campaign timed out')
                time.sleep(.01)
        completed=subprocess.CompletedProcess(command,process.returncode,log.read_text(),'')
        self.assertFalse(premature,'mutant started before healthy baseline completed')
        if expected_exit == 0:
            diagnostic = completed.stdout + completed.stderr
            if completed.returncode != 0 and (output / "results.json").exists():
                results = json.loads((output / "results.json").read_text())
                for result in results:
                    if result["outcome"] not in ("passed", "killed"):
                        diagnostic += f"\n{result}\n"
                        case_output = output / result["mutation"]
                        for log in sorted(case_output.glob("*.log")):
                            diagnostic += f"\n--- {log.name} ---\n{log.read_text()}"
            self.assertEqual(completed.returncode, 0, diagnostic)
        else:
            self.assertNotEqual(completed.returncode, 0)
        results = json.loads((output / "results.json").read_text())
        self.assertEqual([r["mutation"] for r in results],
                         [m["Name"] for m in self.mutations
                          if names is None or m["Name"] == "baseline" or m["Name"] in names])
        self.assertFalse(list(output.glob("work-*")))
        self.assertFalse((self.workspace / "forbidden-normal-coverage").exists())
        for relative, fragments in self.source_text.items():
            self.assertEqual((self.repo / relative).read_text(), "\n".join(fragments))
        return results

    def test_focused_names_always_include_healthy_baseline(self):
        results = self.run_fixture(
            jobs=2, names=["memory-dominated-type", "memory-endian-shift"])
        self.assertEqual([r["outcome"] for r in results],
                         ["passed", "killed", "killed"])
        self.assertEqual(len(list(self.trace.glob("*.start"))), 3)

    def test_default_serial_and_parallel_are_equal_and_bounded(self):
        serial = self.run_fixture()
        self.assertEqual(
            [r["outcome"] for r in serial],
            ["passed"] + ["killed"] * (len(self.mutations) - 1))
        self.assert_concurrency(1)
        for path in self.trace.iterdir():
            path.unlink()
        parallel = self.run_fixture(jobs=2)
        self.assertEqual(serial, parallel)
        self.assert_concurrency(2)

    def assert_concurrency(self, limit):
        paths=[]
        for mutation in self.mutations:
            start=self.trace/(mutation['Name']+'.start')
            paths.extend(start.read_text().splitlines())
        self.assertEqual(len(paths),len(set(paths)),"shared source/build/bin trees")
        self.assertEqual(self.observed_peak,limit)

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
        self.source_text["src/dcc/dcc_mir.c"].append("phi_value = phi->dst;")
        (self.source / "dcc_mir.c").write_text(
            "\n".join(self.source_text["src/dcc/dcc_mir.c"]))
        worker = self.repo / "scripts/run-mir-compiler-mutation-worker.py"
        worker.write_text(worker.read_text().replace(
            '    worker(args.repo_root',
            '    from pathlib import Path\n'
            '    if args.name == "indirect-callee":\n'
            '        Path(args.output_directory,"result.json").write_text("{")\n'
            '        return\n'
            '    if args.name == "phi-edge-liveness":\n'
            '        Path(args.output_directory,"result.json").unlink()\n'
            '        return\n'
            '    worker(args.repo_root'))
        results = self.run_fixture(jobs=2, expected_exit=1)
        outcomes = {r["mutation"]: r["outcome"] for r in results}
        expected = {
            "baseline": "passed", "dominance": "survived", "argument-abi": "invalid",
            "call-arity": "invalid", "indirect-callee": "invalid",
            "callback-identity": "invalid", "phi-edge-liveness": "invalid",
            "call-argument-liveness": "killed", "phi-consumer-value": "invalid",
            "preprocessor-character-escape": "killed",
            "preprocessor-character-hex": "killed",
            "promotion-cache": "killed",
            "global-field-vn-cache": "killed",
            "global-field-vn-call-barrier": "killed",
            "deferred-call-transaction": "killed",
            "debug-conversion-gate": "killed",
            "deferred-merge-demotion": "killed",
            "phi-call-prototype": "killed",
            "conditional-call-prototype": "killed",
            "conditional-call-compatibility": "killed",
            "call-signature-snapshot": "killed",
            "scalar-call-signature": "killed",
            "call-crossing-allocation": "killed",
            "wide-call-crossing-allocation": "killed",
            "guarded-call-preservation": "killed",
            "wide-guarded-call-preservation": "killed",
            "homed-aggregate-copy-size": "killed",
            "spilled-call-abi": "killed",
            "paired-byte-adjacency": "killed",
            "allocation-first-result": "killed",
            "allocation-store-width": "killed",
        }
        expected.update({m["Name"]: "killed" for m in self.mutations
                         if m["Name"].startswith("memory-")})
        self.assertEqual(outcomes, expected)


    def test_worker_and_build_limits_reject_zero(self):
        for parameter in ('Jobs','BuildJobs','WorkerTimeout'):
            p=subprocess.run([sys.executable,str(self.repo/'scripts/run-mir-compiler-mutations.py'),
                              '-'+parameter,'0'],capture_output=True,text=True,timeout=10)
            self.assertNotEqual(p.returncode,0)
            self.assertIn(parameter,p.stderr)

    def test_child_timeout_and_environment_isolation(self):
        previous=dict(os.environ)
        os.environ.update(DCC_MIR_CACHE_VERIFY='parent-cache',LLVM_PROFILE_FILE='parent-profile')
        try:
            code='import os;assert not os.getenv("DCC_MIR_CACHE_VERIFY");print(os.environ["LLVM_PROFILE_FILE"])'
            child=mutations.start([sys.executable,'-c',code],self.workspace,self.workspace/'environment.log')
            result=child.complete()
            self.assertEqual(result['ExitCode'],0,result)
            self.assertIn('profiles',result['Output'])
            self.assertEqual(os.environ['DCC_MIR_CACHE_VERIFY'],'parent-cache')
            self.assertEqual(os.environ['LLVM_PROFILE_FILE'],'parent-profile')
            child=mutations.start([sys.executable,'-c','import time;print("started",flush=True);time.sleep(30)'],self.workspace)
            result=child.complete(.3)
            self.assertTrue(result['TimedOut']);self.assertIn('started',result['Output'])
        finally:
            os.environ.clear();os.environ.update(previous)

    def test_worker_watchdog_stops_descendants(self):self.check_worker_cleanup(False)
    def test_exited_worker_cleans_nested_process_scopes(self):self.check_worker_cleanup(True)

    def check_worker_cleanup(self,exit_worker):
        worker=self.repo/'scripts/run-mir-compiler-mutation-worker.py'
        marker=self.workspace/'watchdog-child'
        child='import os,pathlib,time;pathlib.Path('+repr(str(marker))+').write_text(str(os.getpid()));time.sleep(30)'
        code=('import os,sys,time\nfrom pathlib import Path\nfrom mir_compiler_mutations import start\n'
              'argv=sys.argv\noutput=Path(argv[argv.index("--output-directory")+1])\n'
              'child=start([sys.executable,"-c",'+repr(child)+'],output,parent_scope=os.environ["DCC_PROCESS_SCOPE"])\n'
              'while not Path('+repr(str(marker))+').exists():time.sleep(.01)\n')
        code+='raise SystemExit(0)\n' if exit_worker else 'time.sleep(30)\n'
        worker.write_text(code)
        output=self.workspace/'watchdog-output'
        began=time.monotonic()
        p=subprocess.run([sys.executable,str(self.repo/'scripts/run-mir-compiler-mutations.py'),
            '--jobs','2','--worker-timeout','2','--output-directory',str(output)],
            capture_output=True,text=True,timeout=10)
        self.assertNotEqual(p.returncode,0)
        self.assertLess(time.monotonic()-began,6)
        results=json.loads((output/'results.json').read_text())
        self.assertEqual(len(results),len(self.mutations))
        self.assertTrue(all(r['outcome']=='invalid' for r in results))
        if not exit_worker:self.assertEqual(results[0]['detail'],'Worker timed out; see worker.log')
        self.assertTrue(all(r['phase']=='not-run' for r in results[1:]))
        pid=int(marker.read_text());end=time.monotonic()+2
        while process_alive(pid) and time.monotonic()<end:time.sleep(.01)
        self.assertFalse(process_alive(pid),'owned descendant survived')
        self.assertFalse(list(output.rglob('process-id')))
        self.assertFalse(list(output.glob('work-*')))



class CampaignPolicyTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):cls.mutations=mutations.mutations()

    def test_liveness_mutants_target_the_shipping_bitset_transfer(self):
        source = (ROOT / "src/dcc/dcc_mir.c").read_text()
        begin = source.index("static int mir_compute_liveness_bitsets(")
        end = source.index("\nint mir_verify_and_dump(", begin)
        bitsets = source[begin:end]
        for name in ("phi-edge-liveness", "call-argument-liveness"):
            mutation = next(item for item in self.mutations if item["Name"] == name)
            with self.subTest(name=name):
                self.assertEqual(source.count(mutation["Before"]), 1)
                self.assertIn(mutation["Before"], bitsets)
                self.assertNotIn(mutation["Before"], source[:begin])


    def test_all_mutations_have_unique_production_anchors(self):
        for mutation in self.mutations:
            if "Before" not in mutation:
                continue
            path = ROOT / mutation.get("Source", "src/dcc/dcc_mir.c")
            source = path.read_text()
            with self.subTest(mutation=mutation["Name"]):
                self.assertEqual(source.count(mutation["Before"]), 1,
                                 f"Mutation anchor changed in {path}")
                self.assertNotEqual(mutation["Before"], mutation["After"])


    def test_branch_manifest_mutants_restore_unique_production_guards(self):
        manifest = json.loads((ROOT / "scripts/compiler-branch-proof.json").read_text())
        for name, label in manifest["mutant_assertions"].items():
            mutation = next(item for item in self.mutations if item["Name"] == name)
            with self.subTest(name=name):
                self.assertEqual(mutation["ExpectedFailure"], label)
                self.assertEqual((ROOT / mutation["Source"]).read_text().count(
                    mutation["Before"]), 1)


    def test_exact_classifier_for_every_mutation(self):
        for mutation in self.mutations[1:]:
            cache=mutation.get('CompileProbe') or mutation.get('CacheVerifier')
            label=('MIR matcher restoration failures' if mutation.get('MatcherProbe') else
                   'MIR paired-byte mutation failures' if mutation.get('PairedByteProbe') else 'MIR verifier failures')
            good=('; MIR CACHE MISMATCH mir_definition function=f value=1 cached=2 uncached=-1\ndcc: fatal: MIR use-cache mismatch\n'
                  if cache else mutation['ExpectedFailure']+'\n'+label+'=1\n')
            cases=[(good,1,False,'killed'),(good,134,False,'invalid'),(good,1,True,'invalid'),
                   ('FAIL unrelated assertion\n'+label+'=1\n',1,False,'invalid'),
                   ('prefix '+good,1,False,'invalid'),
                   ('' if cache else label+'=0\n',0,False,'survived')]
            for text,code,timed,expected in cases:
                with self.subTest(mutation=mutation['Name'],expected=expected,text=text):
                    self.assertEqual(mutations.classify({'Output':text,'ExitCode':code,'TimedOut':timed},mutation),expected)


    def test_unknown_names_do_not_touch_existing_evidence(self):
        with tempfile.TemporaryDirectory() as folder:
            output=Path(folder)
            evidence=output/'results.json';evidence.write_text('existing evidence')
            with self.assertRaisesRegex(ValueError,'Unknown compiler mutation'):
                mutations.campaign(output,names=['not-a-mutation'])
            self.assertEqual(evidence.read_text(),'existing evidence')

    def test_output_lock_refuses_competing_campaign_without_overwriting(self):
        with tempfile.TemporaryDirectory() as folder:
            output=Path(folder)
            evidence=output/'results.json';evidence.write_text('existing evidence')
            with mutations.OutputLock(output/'.lock'):
                with self.assertRaises(OSError):mutations.campaign(output,names=['baseline'])
            self.assertEqual(evidence.read_text(),'existing evidence')

if __name__=='__main__':unittest.main()
