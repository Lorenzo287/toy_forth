cmake_minimum_required(VERSION 3.10)
project(toy_forth C)

set(CMAKE_C_STANDARD 11)

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

include_directories(${PROJECT_SOURCE_DIR}/include)

file(GLOB SOURCES "${PROJECT_SOURCE_DIR}/src/*.c")

add_executable(toy_forth ${SOURCES})

option(ENABLE_LEAKCHECK "Enable memory leak detection with stb_leakcheck" OFF)
if(ENABLE_LEAKCHECK)
    target_compile_definitions(toy_forth PRIVATE STB_LEAKCHECK)
endif()

if(MSVC)
    target_compile_options(toy_forth PRIVATE /W4)
else()
    target_compile_options(toy_forth PRIVATE 
        -Wall 
        -Wextra 
        -Wpedantic
        -O3
    )
    # Add debug symbols if specifically asked for Debug build
    target_compile_options(toy_forth PRIVATE $<$<CONFIG:Debug>:-g -O0>)
endif()
