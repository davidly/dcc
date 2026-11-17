"""Both application workers must reject diagnostics despite a successful exit."""
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch
from scripts import runall


class BuildDiagnosticsTests(unittest.TestCase):
    def test_invalid_builds_never_reach_emulator_in_either_worker_or_mode(self):
        samples = [
            ('; MIR assignment_name: instruction 68 has invalid PHI predecessors',False),
            ('; MIR f: invalid function dimensions',False),
            ('; MIR f: argument 2 does not dominate call 7',False),
            ('; MIR f: instruction 3 uses non-dominating v1',False),
            ('%Undefined symbol',False),('%Mult. Def. symbol',False),('%Phase error',False),
            ('No Fatal error(s)\r\n; MIR f blocks=3 values=4',True),
        ]
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            args = SimpleNamespace(timeout=1,baseline_dir=root)
            baseline = runall.Baseline(root/'fixture.txt','ok')
            for text,good in samples:
                for mode in ('peep','nopeep'):
                    for kind in ('app','extended'):
                        with self.subTest(text=text,mode=mode,worker=kind), \
                             patch.object(runall,'build',return_value=(0,False,text)), \
                             patch.object(runall,'check_run',return_value=(True,[],{})) as emulator:
                            if kind == 'app':
                                result = runall.app_job('fixture',mode,{}, {},{baseline.path:baseline},root,args)
                            else:
                                result = runall.extended_job(root/'fixture.c',mode,{},baseline,root,args)
                            self.assertEqual(result.passed,good)
                            self.assertEqual(emulator.call_count,int(good))

    def test_timeouts_and_nonzero_build_exits_are_always_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder)
            baseline=runall.Baseline(root/'fixture.txt','ok')
            for code,timed in ((1,False),(0,True)):
                with patch.object(runall,'build',return_value=(code,timed,'')), \
                     patch.object(runall,'check_run') as emulator:
                    result=runall.extended_job(root/'fixture.c','peep',{},baseline,root,SimpleNamespace())
                    self.assertFalse(result.passed)
                    emulator.assert_not_called()


if __name__ == '__main__':
    unittest.main()
