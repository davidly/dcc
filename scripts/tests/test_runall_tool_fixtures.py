"""Negative peephole controls must remain active in the shared Python runner."""
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from scripts import runall


class PeepholeControls(unittest.TestCase):
    def check(self, unsafe=None, drift=False):
        with tempfile.TemporaryDirectory() as folder:
            folder=Path(folder)
            text=(runall.ROOT/'tests/dccpeep/aggregate-swap-spilled-ldir.in.mac').read_text()
            if drift:text=text.replace('ldir\nld hl,(_left)','ldir\nld hl,(_changed)')
            fixture=folder/'aggregate-swap-spilled-ldir.in.mac'
            fixture.write_text(text)
            (folder/'aggregate-swap-spilled-ldir.expected.mac').write_text(text)
            seen=[]
            def run(argv,*args,**kwargs):
                source,output=Path(argv[-2]),Path(argv[-1])
                seen.append(source.name)
                text=source.read_text()
                if source.stem=='swap-'+str(unsafe):text+='\n; peep: aggregate_swap_ldir\n'
                output.write_text(text)
                return 0,False,''
            with patch.object(runall,'run',side_effect=run):
                result=runall.dccpeep_fixtures(1,folder,'fake-peep')
            return result,seen

    def test_safe_controls_and_long_line(self):
        good,seen=self.check()
        self.assertTrue(good)
        for name in ('reload','spill','temporary','size','pointer'):
            self.assertIn('swap-'+name+'.mac',seen)
        self.assertIn('long.in.mac',seen)

    def test_every_unsafe_rewrite_is_rejected(self):
        for name in ('reload','spill','temporary','size','pointer'):
            with self.subTest(name=name):self.assertFalse(self.check(unsafe=name)[0])

    def test_fixture_drift_cannot_silently_remove_control(self):
        self.assertFalse(self.check(drift=True)[0])


if __name__=='__main__':unittest.main()
