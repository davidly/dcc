# Modular dccpeep host optimizer.
cmake_minimum_required(VERSION 3.10)
project(dccpeep C)
if(CMAKE_VERSION VERSION_LESS 3.12)
    find_package(PythonInterp 3 REQUIRED)
    set(DCC_PYTHON ${PYTHON_EXECUTABLE})
else()
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(DCC_PYTHON ${Python3_EXECUTABLE})
endif()

add_custom_target(dcc_version_header
    COMMAND ${DCC_PYTHON} ${CMAKE_CURRENT_SOURCE_DIR}/../../scripts/generate-version.py
            --output ${CMAKE_CURRENT_BINARY_DIR}/dcc_version.h
    BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/dcc_version.h
    VERBATIM)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

file(GLOB DCCPEEP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
add_executable(dccpeep ${DCCPEEP_SOURCES} dccpeep_internal.h)
add_dependencies(dccpeep dcc_version_header)
target_include_directories(dccpeep PRIVATE ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_BINARY_DIR})
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(dccpeep PRIVATE -Wall -Wextra -O2)
endif()
set_target_properties(dccpeep PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../..)
