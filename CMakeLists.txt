cmake_minimum_required(VERSION 3.10)
project(toy_forth C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
include_directories(${PROJECT_SOURCE_DIR}/include)
file(GLOB SOURCES "${PROJECT_SOURCE_DIR}/src/*.c")

add_executable(toy_forth ${SOURCES})

# Options
option(ENABLE_LEAKCHECK "Enable memory leak detection with stb_leakcheck" OFF)
option(ENABLE_ASAN "Enable AddressSanitizer (Linux/WSL only)" OFF)

# Compiler-specific flags
if(MSVC)
    target_compile_options(toy_forth PRIVATE /W4)
    target_compile_definitions(toy_forth PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(toy_forth PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Configuration-specific flags
if(ENABLE_LEAKCHECK)
    target_compile_definitions(toy_forth PRIVATE STB_LEAKCHECK)
    target_compile_options(toy_forth PRIVATE -O0 -g)
elseif(ENABLE_ASAN)
    target_compile_options(toy_forth PRIVATE -fsanitize=address -fno-omit-frame-pointer -O0 -g)
    target_link_options(toy_forth PRIVATE -fsanitize=address)
else()
    target_compile_options(toy_forth PRIVATE -O3)
endif()
