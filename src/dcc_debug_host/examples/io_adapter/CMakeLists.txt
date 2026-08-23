add_library(dcc-debug-io-adapter-example SHARED
    dcc_debug_io_adapter_example.c
)

target_include_directories(dcc-debug-io-adapter-example PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../../include
)
target_compile_definitions(dcc-debug-io-adapter-example PRIVATE
    DCC_DEBUG_IO_ADAPTER_BUILD=1
)
set_target_properties(dcc-debug-io-adapter-example PROPERTIES
    C_VISIBILITY_PRESET hidden
    OUTPUT_NAME dcc-debug-io-adapter-example
)

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(dcc-debug-io-adapter-example PRIVATE -Wall -Wextra)
elseif(MSVC)
    target_compile_options(dcc-debug-io-adapter-example PRIVATE /W4)
    target_compile_definitions(dcc-debug-io-adapter-example PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

if(BUILD_TESTING)
    add_executable(dcc-debug-io-adapter-example-test
        dcc_debug_io_adapter_example_test.c
    )
    target_include_directories(dcc-debug-io-adapter-example-test PRIVATE
        ${CMAKE_CURRENT_LIST_DIR}/../../include
    )
    target_link_libraries(dcc-debug-io-adapter-example-test PRIVATE
        dcc-debug-io-adapter-example
    )
    add_test(NAME dcc-debug-io-adapter-example
             COMMAND dcc-debug-io-adapter-example-test)
endif()
