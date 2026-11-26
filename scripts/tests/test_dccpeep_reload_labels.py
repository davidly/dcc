"""Assembly checks for reload-skip labels surviving optimizer iterations."""
from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[2]
FIXTURE = ROOT / 'tests/dccpeep/skip-long-reload-label.in.mac'


@unittest.skipUnless(all((ROOT / tool).is_file() for tool in ('dccpeep', 'm80c')),
                     'built dccpeep and m80c required')
class ReloadSkipLabelTests(unittest.TestCase):
    def test_existing_skip_label_keeps_its_target(self):
        with tempfile.TemporaryDirectory(prefix='dcc-reload-label-', dir=ROOT / 'build') as directory:
            folder = Path(directory)
            source, output = folder / 'IN.MAC', folder / 'OUT.MAC'
            source.write_text(FIXTURE.read_text())
            for iteration in range(2):
                with self.subTest(iteration=iteration):
                    result = subprocess.run(
                        [str(ROOT / 'dccpeep'), str(source), str(output)],
                        capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    text = output.read_text()
                    labels = [line.lower() for line in text.splitlines() if line.endswith(':')]
                    self.assertEqual(len(labels), len(set(labels)), text)
                    self.assertIn('skip_ix_reload_across_label', text)
                    self.assertIn('Lskrl_4:\n\tld a,1\n\tout (7),a', text)
                    result = subprocess.run(
                        [str(ROOT / 'm80c'), '=OUT.MAC', '/X', '/O', '/Z', '/L'],
                        cwd=folder, capture_output=True, text=True, timeout=30)
                    listing = folder / 'OUT.PRN'
                    self.assertEqual(result.returncode, 0,
                                     result.stdout + result.stderr + listing.read_text())
                    self.assertTrue((folder / 'OUT.REL').is_file())
                    source.write_text(text)
