"""Keep obsolete direct body emission out of the maintained compiler."""
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[2]
COMPILER = ROOT / "src/dcc"
NON_CODE = re.compile(
    r'/\*.*?\*/|//[^\n]*|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'',
    re.DOTALL,
)
REMOVED_MODULES = ("dcc_assign.c", "dcc_cmp.c", "dcc_ops.c", "dcc_stmt_fast.c")
RENAMED_MODULES = {
    "dcc_ast_gen.c": "dcc_ast_classify.c",
    "dcc_ast_gen_support.c": "dcc_ast_support.c",
    "dcc_ast_gen_cond.c": "dcc_ast_stmt_classify.c",
    "dcc_ast_gen_expr.c": "dcc_ast_capture.c",
    "dcc_ast_gen_internal.h": "dcc_ast_internal.h",
}
OBSOLETE_FRONTEND_NAMES = {
    "DCC_AST_GEN_INTERNAL_H", "ast_gen_supported", "ast_gen_supported_uncached",
    "ast_emit_init_expr", "ast_emit_discarded_expr", "ast_emit_struct_init_expr_assign",
    "gen_local_decl_after_type", "gen_statement", "emit_param_vla_bound_expressions",
    "emit_vla_save_sp", "emit_vla_restore_sp", "emit_vla_restore_for_flow",
    "emit_vla_restore_to_label_scope", "emit_extrn_if_needed",
    "emit_store_const_to_local_array_elem", "emit_store_const_to_local_offset",
    "emit_store_expr_to_local_offset", "emit_store_expr_to_local_array_elem",
    "emit_zero_local_bytes", "emit_init_auto_char_array_at_offset_from_string",
    "emit_init_auto_struct_type", "emit_init_auto_struct_chained_designator",
    "emit_init_auto_struct_scalar", "emit_init_auto_struct_array_leaf",
    "emit_init_auto_struct_array_field_level", "emit_init_auto_struct_array_field",
    "emit_init_auto_struct_array", "emit_init_auto_struct_from_list",
    "emit_init_auto_struct_array_from_list", "emit_init_auto_array_scalar",
    "emit_init_auto_array_level", "emit_init_auto_array_from_list", "emit_vla_alloc",
    "xstrdup2", "append_global_zero_bytes", "append_global_char_array_string",
    "parse_global_init_type",
}
REMOVED_APIS = {
    "gen_expr", "gen_expr_no_comma", "gen_unary", "gen_binop", "gen_binop_typed",
    "gen_binop32", "gen_binop32_typed", "gen_cmp", "gen_cmp_typed", "gen_cmp32",
    "gen_signed_divmod16", "gen_snippet_expr", "gen_snippet_lvalue_addr",
    "gen_post_update_symbol_addr_value", "gen_post_update_from_addr",
    "emit_load_from_hl", "emit_store_de_to_addr_hl", "emit_bool_normalize_hl",
    "emit_copy_de_to_hl_bytes", "emit_push_struct_arg_from_hl",
    "emit_load_hl_from_sp_offset", "emit_incdec_addr", "emit_incdec_value_in_dehl",
    "emit_pre_incdec_lvalue", "emit_incdec_sym_direct",
    "emit_init_auto_char_array_from_string", "emit_load_float_bits",
    "emit_load_const_sym_value", "emit_const_value", "emit_global_char_index_addr",
    "emit_test_global_char_index_zero", "emit_global_byte_array_index_addr",
    "emit_runtime_extrn_if_needed", "emit_runtime_call", "emit_load_frame_addr_hl",
    "emit_load_sym_addr", "emit_load_global_word_direct", "emit_store_global_word_direct",
    "emit_load_sym_value_direct", "emit_load_sym_low_byte_and_const",
    "emit_load_sym_byte_to_a", "emit_load_sym_de_direct", "emit_store_hl_to_sym_direct",
    "emit_store_hl_direct_at", "try_emit_post_update_sym_direct",
    "emit_promote_byte_to_int", "emit_promote_int_to_long", "emit_convert_int_to_float",
    "emit_convert_float_to_intlike", "emit_extend_to_long", "emit_extend_to_long_typed",
    "emit_cleanup_stack_bytes", "emit_call_hl_from_stack_offset", "emit_extract_bitfield",
    "emit_store_bitfield_de_to_addr_hl", "emit_mul_hl_const", "emit_shift_const_long",
    "emit_shift_loop", "emit_signed_bias_for_relop", "emit_cmp_branch_false",
    "emit_cmp_branch_true", "emit_cmp_branch_false_unsigned", "emit_cmp_branch_true_unsigned",
    "emit_byte_operand_to_a", "emit_cp_byte_operand", "emit_byte_cmp_branch_after_cp",
    "emit_branch_on_bool_hl", "emit_cmp_const_branch_for_signed_local16",
    "emit_ld_de_const", "emit_add_const_to_hl", "emit_label", "emit_jp_label",
    "divide_hl_by_elem_size", "emit_add_field_offset", "emit_and_hl_const",
    "emit_and_long_const", "emit_and_word_const", "emit_arith_shift_right_hl_const",
    "emit_cast_16_to_common", "emit_cmp_subtract_to_bool", "emit_float_compare_call",
    "emit_logical_shift_right_hl_const", "emit_mul_hl_const_general",
    "emit_mul_pow2_long_const", "emit_test_expr_nonzero", "int_log2_pow2",
    "invert_relop_for_swap", "local_offset_can_ix_direct", "mark_user_label_reference",
    "mul_const_op_count", "scale_hl_by_elem_size", "sym_is_direct_byte_fetch",
    "sym_word_load_is_two_byte_fetch", "ulong_log2_pow2",
    "ExprState", "g_expr", "current_field_bit_width", "current_field_bit_shift",
    "current_field_bit_mask",
}


def identifiers(text):
    return set(re.findall(r"\b[A-Za-z_]\w*\b", NON_CODE.sub(" ", text)))


class LegacyEmitterRemovalTests(unittest.TestCase):
    def test_obsolete_modules_are_physically_removed(self):
        cmake = (COMPILER / "CMakeLists.txt").read_text()
        for name in REMOVED_MODULES:
            with self.subTest(module=name):
                self.assertFalse((COMPILER / name).exists())
                self.assertNotIn(name, cmake)

    def test_obsolete_symbol_contracts_are_removed(self):
        for source in sorted(COMPILER.iterdir()):
            if source.suffix in (".c", ".h"):
                with self.subTest(source=source.name):
                    self.assertEqual(
                        identifiers(source.read_text()) &
                        (REMOVED_APIS | OBSOLETE_FRONTEND_NAMES), set())

    def test_ast_frontend_modules_have_explicit_ownership_names(self):
        cmake = (COMPILER / "CMakeLists.txt").read_text()
        for old, new in RENAMED_MODULES.items():
            with self.subTest(module=new):
                self.assertFalse((COMPILER / old).exists())
                self.assertTrue((COMPILER / new).is_file())
                self.assertNotIn(old, cmake)
                self.assertIn(new, cmake)

    def test_declaration_and_expression_frontend_cannot_write_assembly(self):
        for name in ("dcc_decl.c", "dcc_expr.c", "dcc_fold.c"):
            with self.subTest(source=name):
                self.assertEqual(
                    identifiers((COMPILER / name).read_text()) &
                    {"g_emit_sink", "emit", "emit_label", "emit_jp_label"},
                    set(),
                )

    def test_guard_ignores_comments_and_literals_not_real_references(self):
        self.assertEqual(identifiers(
            '/* emit_load_sym_addr */ "gen_expr" // g_expr\nmir_capture_stmt(node);'
        ), {"mir_capture_stmt", "node"})
        self.assertIn("gen_expr", identifiers("gen_expr();"))

    def test_scope_frontend_output_is_only_deferred_externs(self):
        source = (COMPILER / "dcc_symbols.c").read_text()
        externs = re.search(
            r"\bvoid emit_deferred_extrns\(void\)\s*\{.*?^\}",
            source, re.DOTALL | re.MULTILINE,
        )
        self.assertIsNotNone(externs)
        self.assertIn(r"\textrn %s\n", externs.group())
        self.assertEqual(
            identifiers(source[:externs.start()] + source[externs.end():]) &
            {"g_emit_sink", "emit", "fprintf", "fputs", "fwrite", "fputc"},
            set(),
        )


if __name__ == "__main__":
    unittest.main()
