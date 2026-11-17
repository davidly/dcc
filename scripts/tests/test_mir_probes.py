"""Probe inventories, isolation and forced-runtime coverage contracts."""
import os
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from scripts import mir_probes as probes


class ProbePolicyTests(unittest.TestCase):
    def test_pointer_mutation_inventory_and_inactive_controls(self):
        source=(probes.ROOT/'tests/tptrcnd.c').read_text();variants=list(probes.pointer_variants(source))
        self.assertEqual(len(variants),85)
        self.assertEqual([v[0] for v in variants],list(range(85)))
        self.assertEqual(sum(v[2] for v in variants),4)
        self.assertTrue(all(v[1]!=source for v in variants))
        prefix=source[:source.index('int main()')]
        self.assertTrue(all(v[1].startswith(prefix) for v in variants))
        with self.assertRaisesRegex(RuntimeError,'inventory changed'):list(probes.pointer_variants(source+'\n/* >= 100 */'))

    def test_scope_and_matrix_inventories_retain_all_controls(self):
        self.assertEqual(len(probes.SCOPE_DIAGNOSTICS),19)
        self.assertEqual(len(set(probes.SCOPE_DIAGNOSTICS)),19)
        self.assertEqual(len(probes.SCOPE_SOURCES),8)
        source=(probes.ROOT/'tests/tforblk.c').read_text()
        for name,before,after in probes.SCOPE_SOURCES:
            self.assertNotEqual(probes.replace_once(source,before,after,name),source)
        with self.assertRaisesRegex(RuntimeError,'not unique'):probes.replace_once('aa','a','b','probe')
        self.assertEqual(len(probes.CANDIDATES),12)
        self.assertEqual(len(set(probes.CANDIDATES)),12)
        self.assertEqual(len(probes.MATRIX_PROBES),8)
        self.assertEqual(sum(p[3] for p in probes.MATRIX_PROBES),2)
        self.assertTrue(all((probes.ROOT/'tests'/p[1]).is_file() for p in probes.MATRIX_PROBES))

    def test_environment_changes_never_mutate_parent(self):
        original={'DCC_AST_DUMP':'parent','dcc_ast_build':'parent','OTHER':'keep'}
        child=probes.controlled({'DCC_AST_DUMP':'child'},remove=['DCC_AST_BUILD'],base=original)
        self.assertEqual(child,{'DCC_AST_DUMP':'child','OTHER':'keep'})
        self.assertEqual(original,{'DCC_AST_DUMP':'parent','dcc_ast_build':'parent','OTHER':'keep'})

    def test_false_success_without_assembly_is_rejected(self):
        with tempfile.TemporaryDirectory() as folder:
            out=Path(folder)/'missing.mac'
            with patch.object(probes,'execute',return_value={'ExitCode':0,'TimedOut':False,'Output':''}):
                result=probes.compile_file('compiler','source.c',out)
            with self.assertRaisesRegex(RuntimeError,'compile failed'):probes.success(result,'probe')

    def test_forced_lifetime_keeps_both_apps_modes_and_stack_configurations(self):
        calls=[];env_before=dict(os.environ)
        def compile(dcc,source,out,env=None,options=(),**kwargs):
            function=env.get('DCC_MIR_SELECT_FUNCTION')
            text=('MIR cost-selected function='+function+' candidate='+env['DCC_MIR_SELECT_CANDIDATE']+' ' if function else
                'function=parse_signed_byte template=bounded-decimal-parse-schedule reject=argument-conversion')
            self.assertFalse(kwargs['compile_only']);self.assertFalse(kwargs['include_root'])
            return {'ExitCode':0,'TimedOut':False,'Output':text,'Assembly':'asm','Artifact':True}
        def execute(argv,env=None,timeout=60):
            calls.append((argv,env,timeout));return {'ExitCode':0,'TimedOut':False,'Output':''}
        with patch.object(probes,'compile_file',side_effect=compile),patch.object(probes,'execute',side_effect=execute):
            probes.physical_lifetime('compiler',timeout=91)
        self.assertEqual(len(calls),4)
        self.assertEqual([c[0][c[0].index('--apps')+1] for c in calls],['tmirlife','tmirlife','cint','cint'])
        self.assertEqual(['--no-stack-check' in c[0] for c in calls],[False,True,False,True])
        for argv,env,timeout in calls:
            for value in ('--mode','full','--no-ram-disk','--no-perf-check','91'):self.assertIn(value,argv)
            self.assertEqual(env['DCC_MIR_REQUIRE_EMIT'],'1')
            self.assertEqual(env['DCC_MIR_REQUIRE_COMPLETE'],'1')
            self.assertNotIn('DCC_MIR_COST_REPORT',env)
        self.assertEqual(dict(os.environ),env_before)


if __name__=='__main__':unittest.main()
