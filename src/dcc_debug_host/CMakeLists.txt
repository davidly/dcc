cmake_minimum_required(VERSION 3.16)
project(dcc_debug_host C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DCC_DEBUG_HOST_BUILD_EXAMPLES "Build debugger I/O adapter examples" ON)

find_package(Threads REQUIRED)

set(HARDWARE_DIR ${CMAKE_CURRENT_LIST_DIR}/hardware)
set(ASSETS_DIR ${CMAKE_CURRENT_LIST_DIR}/assets)
set(IO_ADAPTER_INCLUDE_DIR ${CMAKE_CURRENT_LIST_DIR}/include)
get_filename_component(DEBUGGER_ROOT ${CMAKE_CURRENT_LIST_DIR} REALPATH)

function(assert_debugger_local_path PATH_TO_CHECK)
    get_filename_component(RESOLVED_PATH ${PATH_TO_CHECK} REALPATH
                           BASE_DIR ${CMAKE_CURRENT_LIST_DIR})
    string(FIND "${RESOLVED_PATH}" "${DEBUGGER_ROOT}/" ROOT_PREFIX_POSITION)
    if(NOT ROOT_PREFIX_POSITION EQUAL 0)
        message(FATAL_ERROR
            "dcc_debug_host target path escapes the standalone project: ${RESOLVED_PATH}")
    endif()
endfunction()

foreach(REQUIRED_STANDALONE_PATH
        ${HARDWARE_DIR}/memory.c
    ${IO_ADAPTER_INCLUDE_DIR}/dcc_debug_io_adapter.h
        ${ASSETS_DIR}/disks/cpm63k.dsk
        ${ASSETS_DIR}/disks/disk_c_blank.dsk
        ${ASSETS_DIR}/disks/disk_d_blank.dsk)
    if(NOT EXISTS ${REQUIRED_STANDALONE_PATH})
        message(FATAL_ERROR "dcc_debug_host standalone asset is missing: ${REQUIRED_STANDALONE_PATH}")
    endif()
endforeach()

file(READ ${CMAKE_CURRENT_LIST_DIR}/x80.cxx DEBUGGER_X80_SOURCE)
file(READ ${CMAKE_CURRENT_LIST_DIR}/x80.hxx DEBUGGER_X80_HEADER)
foreach(FORBIDDEN_Z80_TOKEN "i8080_" "fZ80Mode" "Z80Mode")
    string(FIND "${DEBUGGER_X80_SOURCE}${DEBUGGER_X80_HEADER}"
           "${FORBIDDEN_Z80_TOKEN}" FORBIDDEN_Z80_POSITION)
    if(NOT FORBIDDEN_Z80_POSITION EQUAL -1)
        message(FATAL_ERROR
            "dcc_debug_host Z80 engine contains forbidden 8080-mode token: ${FORBIDDEN_Z80_TOKEN}")
    endif()
endforeach()
unset(DEBUGGER_X80_SOURCE)
unset(DEBUGGER_X80_HEADER)

set(DEBUGGER_HOST_SOURCES
    dcc_host.cpp
    dcc_host_full_cpm.cpp
    dcc_host_fixture_publish.cpp
    directory_disk.cpp
    dcc_host_debug_metadata.cpp
    dcc_host_debug_evaluator.cpp
    dcc_host_io_adapter_loader.cpp
    dcc_host_target_terminal.cpp
    dcc_host_mi_server.cpp
    ${HARDWARE_DIR}/interrupt_controller.c
    ${HARDWARE_DIR}/universal_88dcdd.c
    ${HARDWARE_DIR}/memory.c
    x80.cxx
    cpu_x80_adapter.cpp
)

foreach(DEBUGGER_SOURCE ${DEBUGGER_HOST_SOURCES})
    assert_debugger_local_path(${DEBUGGER_SOURCE})
endforeach()

file(GLOB DEBUGGER_ROOT_CODE
    ${CMAKE_CURRENT_LIST_DIR}/*.c
    ${CMAKE_CURRENT_LIST_DIR}/*.h
    ${CMAKE_CURRENT_LIST_DIR}/*.cpp
    ${CMAKE_CURRENT_LIST_DIR}/*.hpp
    ${CMAKE_CURRENT_LIST_DIR}/*.cxx
    ${CMAKE_CURRENT_LIST_DIR}/*.hxx)
file(GLOB_RECURSE DEBUGGER_LOCAL_CODE
    ${HARDWARE_DIR}/*.c ${HARDWARE_DIR}/*.h
    ${HARDWARE_DIR}/*.cxx ${HARDWARE_DIR}/*.hxx)
foreach(DEBUGGER_SOURCE ${DEBUGGER_ROOT_CODE} ${DEBUGGER_LOCAL_CODE})
    file(STRINGS ${DEBUGGER_SOURCE} PARENT_INCLUDE
         REGEX "^[ \t]*#[ \t]*include[ \t]*[<\"]\\.\\./")
    if(PARENT_INCLUDE)
        message(FATAL_ERROR
            "dcc_debug_host source has a parent-directory include: ${DEBUGGER_SOURCE}")
    endif()
endforeach()

add_executable(dcc-debug-host ${DEBUGGER_HOST_SOURCES})

target_include_directories(dcc-debug-host PRIVATE
    .
    ${IO_ADAPTER_INCLUDE_DIR}
    ${HARDWARE_DIR}
    ${HARDWARE_DIR}/x80_compat
)

foreach(DEBUGGER_INCLUDE_DIR
    ${IO_ADAPTER_INCLUDE_DIR}
        ${HARDWARE_DIR}
        ${HARDWARE_DIR}/x80_compat)
    assert_debugger_local_path(${DEBUGGER_INCLUDE_DIR})
endforeach()

target_compile_definitions(dcc-debug-host PRIVATE
    DCC_DEBUG_HOST_ASSETS_DIR="${ASSETS_DIR}"
    TRACK_Z80_R_REGISTER=1
    TRACK_Z80_MEMPTR=1
)

target_link_libraries(dcc-debug-host PRIVATE Threads::Threads ${CMAKE_DL_LIBS})

if(WIN32)
    target_link_libraries(dcc-debug-host PRIVATE ws2_32)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
    target_compile_options(dcc-debug-host PRIVATE
        -Wall
        -Wextra
        -Wno-missing-field-initializers
    )
elseif(MSVC)
    target_compile_options(dcc-debug-host PRIVATE /W4)
    target_compile_definitions(dcc-debug-host PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

set_source_files_properties(
    x80.cxx
    cpu_x80_adapter.cpp
    PROPERTIES COMPILE_OPTIONS "-w"
)

include(CTest)

function(dcc_enable_test_assertions TARGET_NAME)
    if(MSVC)
        target_compile_options(${TARGET_NAME} PRIVATE /UNDEBUG)
    else()
        target_compile_options(${TARGET_NAME} PRIVATE -UNDEBUG)
    endif()
endfunction()

if(DCC_DEBUG_HOST_BUILD_EXAMPLES)
    add_subdirectory(examples/io_adapter)
endif()

if(BUILD_TESTING)
    find_package(Python3 REQUIRED COMPONENTS Interpreter)
    set(DCC_ROOT "" CACHE PATH "Path to a DCC checkout for debugger integration tests")
    if(NOT DCC_ROOT)
        get_filename_component(LOCAL_DCC_ROOT ${CMAKE_CURRENT_LIST_DIR}/../.. REALPATH)
        if(EXISTS "${LOCAL_DCC_ROOT}/dccmake" AND EXISTS "${LOCAL_DCC_ROOT}/DCCRTL.MAC")
            set(DCC_ROOT "${LOCAL_DCC_ROOT}")
        elseif(DEFINED ENV{DCC_DIR})
            set(DCC_ROOT "$ENV{DCC_DIR}")
        endif()
    endif()
    add_executable(debug-metadata-test
        tests/dcc_host_debug_metadata_test.cpp
        dcc_host_debug_metadata.cpp
    )
    target_include_directories(debug-metadata-test PRIVATE .)
    add_test(NAME debug-metadata COMMAND debug-metadata-test)
    add_executable(debug-evaluator-test
        tests/dcc_host_debug_evaluator_test.cpp
        dcc_host_debug_metadata.cpp
        dcc_host_debug_evaluator.cpp
        ${HARDWARE_DIR}/memory.c
    )
    target_include_directories(debug-evaluator-test PRIVATE . ${HARDWARE_DIR})
    add_test(NAME debug-evaluator COMMAND debug-evaluator-test)
    add_executable(directory-disk-test
        tests/directory_disk_test.cpp
        directory_disk.cpp
        ${HARDWARE_DIR}/universal_88dcdd.c
    )
    target_include_directories(directory-disk-test PRIVATE . ${HARDWARE_DIR})
    add_test(NAME directory-disk COMMAND directory-disk-test
             ${ASSETS_DIR}/disks/cpm63k.dsk
             ${ASSETS_DIR}/disks/disk_c_blank.dsk
             ${ASSETS_DIR}/disks/disk_d_blank.dsk)
    add_executable(debugger-z80-core-test
        tests/z80_core_test.cpp
        ${HARDWARE_DIR}/interrupt_controller.c
        ${HARDWARE_DIR}/memory.c
        x80.cxx
        cpu_x80_adapter.cpp
    )
    target_include_directories(debugger-z80-core-test PRIVATE
        .
        ${HARDWARE_DIR}
        ${HARDWARE_DIR}/x80_compat
    )
    target_compile_definitions(debugger-z80-core-test PRIVATE
        DCC_DEBUG_X80_HOST_TEST=1
        TRACK_Z80_R_REGISTER=1
        TRACK_Z80_MEMPTR=1
    )
    set_source_files_properties(
        x80.cxx
        cpu_x80_adapter.cpp
        PROPERTIES COMPILE_OPTIONS "-w"
    )
    add_test(NAME debugger-z80-core COMMAND debugger-z80-core-test)
    add_executable(fixture-publish-test
        tests/dcc_host_fixture_publish_test.cpp
        dcc_host_fixture_publish.cpp
    )
    target_include_directories(fixture-publish-test PRIVATE .)
    add_test(NAME fixture-publish COMMAND fixture-publish-test)
    add_executable(target-terminal-test
        tests/dcc_host_target_terminal_test.cpp
        dcc_host_target_terminal.cpp
    )
    target_include_directories(target-terminal-test PRIVATE .)
    target_link_libraries(target-terminal-test PRIVATE Threads::Threads)
    if(WIN32)
        target_link_libraries(target-terminal-test PRIVATE ws2_32)
    endif()
    add_test(NAME target-terminal COMMAND target-terminal-test)
    foreach(ADAPTER_MODE valid init-failure bad-abi missing-callback oversized)
        if(ADAPTER_MODE STREQUAL "valid")
            set(ADAPTER_MODE_VALUE 0)
        elseif(ADAPTER_MODE STREQUAL "init-failure")
            set(ADAPTER_MODE_VALUE 1)
        elseif(ADAPTER_MODE STREQUAL "bad-abi")
            set(ADAPTER_MODE_VALUE 2)
        elseif(ADAPTER_MODE STREQUAL "missing-callback")
            set(ADAPTER_MODE_VALUE 3)
        else()
            set(ADAPTER_MODE_VALUE 4)
        endif()
        add_library(io-adapter-${ADAPTER_MODE} SHARED tests/io_adapter_fixture.c)
        target_include_directories(io-adapter-${ADAPTER_MODE} PRIVATE ${IO_ADAPTER_INCLUDE_DIR})
        target_compile_definitions(io-adapter-${ADAPTER_MODE} PRIVATE
            DCC_DEBUG_IO_ADAPTER_BUILD=1
            DCC_TEST_ADAPTER_MODE=${ADAPTER_MODE_VALUE}
        )
    endforeach()
    add_library(io-adapter-no-symbol SHARED tests/io_adapter_no_symbol.c)
    add_executable(io-adapter-loader-test
        tests/dcc_host_io_adapter_loader_test.cpp
        dcc_host_io_adapter_loader.cpp
    )
    target_include_directories(io-adapter-loader-test PRIVATE . ${IO_ADAPTER_INCLUDE_DIR})
    target_link_libraries(io-adapter-loader-test PRIVATE ${CMAKE_DL_LIBS})
    add_test(NAME io-adapter-loader COMMAND io-adapter-loader-test
        $<TARGET_FILE:io-adapter-valid>
        $<TARGET_FILE:io-adapter-no-symbol>
        $<TARGET_FILE:io-adapter-init-failure>
        $<TARGET_FILE:io-adapter-bad-abi>
        $<TARGET_FILE:io-adapter-missing-callback>
        $<TARGET_FILE:io-adapter-oversized>
    )
    add_test(
        NAME dcc-debug-host-mi
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tests/dcc_host_mi_test.py
            $<TARGET_FILE:dcc-debug-host>
    )
        set_tests_properties(dcc-debug-host-mi PROPERTIES TIMEOUT 30)
    if(EXISTS "${DCC_ROOT}/dccmake" AND EXISTS "${DCC_ROOT}/DCCRTL.MAC")
        add_test(
                NAME dcc-debug-host-dcc
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_LIST_DIR}/tests/dcc_host_integration.py
                    $<TARGET_FILE:dcc-debug-host> ${DCC_ROOT}
        )
            set_tests_properties(dcc-debug-host-dcc PROPERTIES TIMEOUT 120)
    endif()

    foreach(TEST_TARGET
        debug-metadata-test
        debug-evaluator-test
        directory-disk-test
        debugger-z80-core-test
        fixture-publish-test
        target-terminal-test
        io-adapter-loader-test
        dcc-debug-io-adapter-example-test)
        if(TARGET ${TEST_TARGET})
            dcc_enable_test_assertions(${TEST_TARGET})
        endif()
    endforeach()
endif()
