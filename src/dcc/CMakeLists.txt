# CMakeLists.txt - modularised dcc compiler (separate compilation).
#
# Compiles each module translation unit (dcc.c and the dcc_*.c files, all of
# which include the umbrella header dcc.h) and links them into the `dcc`
# executable.
#
# Typical use from the repo root:
#   cmake -S src/dcc -B build/dcc
#   cmake --build build/dcc
# By default the binary is placed at the repository root as `dcc`, matching the
# toolchain conventions (ma.sh / runall.sh expect ./dcc).

cmake_minimum_required(VERSION 3.10)
project(dcc C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Module translation units. Each is compiled separately and linked together;
# all of them #include the umbrella header dcc.h.
set(DCC_SOURCES
    dcc.c               # driver + main()
    dcc_state.c         # definitions of the shared globals
    dcc_asmname.c
    dcc_diag_emit.c
    dcc_preproc.c
    dcc_pp_expr.c      # preprocessor #if/#elif expression evaluator
    dcc_global_scan.c  # whole-file global write/address pre-scan
    dcc_types.c
    dcc_constexpr.c
    dcc_symbols.c
    dcc_fold.c
    dcc_expr.c
    dcc_cmp.c
    dcc_ops.c
    dcc_assign.c
    dcc_stmt_fast.c
    dcc_decl.c
    dcc_stmt.c
    dcc_func.c
    dcc_global_init.c   # file-scope object initializer parsing (split from dcc_func.c)
    dcc_regalloc.c      # speculative no-IX / BC-regalloc codegen (split from dcc_func.c)
    dcc_loop_regalloc.c # loop-scoped BC register allocation
    dcc_array_narrow.c  # conservative byte-narrowing proof
    dcc_licm.c          # loop-invariant code motion / CSE
    dcc_data.c
    dcc_ast.c           # function-local AST (arena + nodes)
    dcc_ast_build.c     # function-local AST builder
    dcc_ast_gen.c           # AST classifiers / type & lvalue resolvers
    dcc_ast_gen_support.c   # AST support dispatch, calls/structs/folds
    dcc_ast_gen_expr.c      # AST expression emitters
    dcc_ast_gen_cond.c      # AST condition and branch emitters
    dcc_ast_gen_stmt.c      # AST statement/switch/for emitters
    dcc_mir.c               # analysis-only virtual-register MIR prototype
    dcc_mir_emit_common.c
    dcc_mir_homed_cfg.c
    dcc_mir_machine_emit.c # scheduled Z80 machine-template emission
    dcc_mir_machine_attention.c # strict attention-kernel schedules
    dcc_mir_machine_float_reports.c # strict float report/check schedules
    dcc_mir_machine_scanners.c # strict scanner/parser schedules
    dcc_mir_select.c
    dcc_mir_schedule.c      # shadow sparse live-segment scheduler
    dcc_mir_spilled_cfg.c
    dcc_mir_target.c        # shadow Z80 constraints and template costs
)

add_executable(dcc ${DCC_SOURCES} dcc.h dcc_ast_gen_internal.h
                   dcc_preproc_internal.h dcc_regalloc_internal.h dcc_mir.h
                   dcc_mir_machine_internal.h)
target_include_directories(dcc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(dcc PRIVATE -Wall -Wextra -O2)
endif()

# Place the built binary at the repository root as ./dcc so the existing
# ma.sh / runall.sh harness (which calls ./dcc) keeps working unchanged.
set_target_properties(dcc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../..)
