# Modular dccpeep host optimizer.
cmake_minimum_required(VERSION 3.10)
project(dccpeep C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

file(GLOB DCCPEEP_SOURCES CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/*.c")
add_executable(dccpeep ${DCCPEEP_SOURCES} dccpeep_internal.h)
target_include_directories(dccpeep PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(dccpeep PRIVATE -Wall -Wextra -O2)
endif()
set_target_properties(dccpeep PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../..)
