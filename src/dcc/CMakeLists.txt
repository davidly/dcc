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

set(CMAKE_C_STANDARD 89)
set(CMAKE_C_STANDARD_REQUIRED ON)

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
    dcc_data.c
    dcc_ast.c           # function-local AST (arena + nodes)
    dcc_ast_build.c     # function-local AST builder
    dcc_ast_gen.c       # AST-driven codegen walker
)

add_executable(dcc ${DCC_SOURCES} dcc.h)
target_include_directories(dcc PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(dcc PRIVATE -Wall -Wextra -O2)
endif()

# Place the built binary at the repository root as ./dcc so the existing
# ma.sh / runall.sh harness (which calls ./dcc) keeps working unchanged.
set_target_properties(dcc PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../..)
