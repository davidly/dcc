import unittest
from unittest.mock import patch

from scripts import prepush


class CompilerSelectionTests(unittest.TestCase):
    def test_missing_requested_compiler_falls_back_to_installed_gcc(self):
        def which(candidate):
            return '/usr/bin/gcc' if candidate == 'gcc' else None

        with patch.object(prepush.shutil, 'which', side_effect=which):
            compiler = prepush.select_c_compiler(
                system='posix', environment={'CC': 'clang'})

        self.assertEqual(compiler, '/usr/bin/gcc')