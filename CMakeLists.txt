cmake_minimum_required(VERSION 3.10)
project(toy_forth C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

set(BUILD_MODE "None" CACHE STRING "None | LeakCheck | Profile")
set_property(CACHE BUILD_MODE PROPERTY STRINGS None LeakCheck Profile)
message(STATUS "Build mode: ${BUILD_MODE}")

include_directories(${PROJECT_SOURCE_DIR}/include)
file(GLOB SOURCES "${PROJECT_SOURCE_DIR}/src/*.c")
add_executable(toy_forth ${SOURCES})

# ── Baseline warnings ─────────────────────────────────────────────────────────

if(MSVC)
    target_compile_options(toy_forth PRIVATE /W3)
    target_compile_definitions(toy_forth PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    target_compile_options(toy_forth PRIVATE -Wall -Wextra -Wpedantic)
    if(WIN32 AND NOT MINGW)
        target_compile_definitions(toy_forth PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
endif()

# ── LeakCheck / Profile override CMAKE_BUILD_TYPE ────────────────────────────

if(BUILD_MODE STREQUAL "LeakCheck")
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
    if(WIN32)
        target_compile_definitions(toy_forth PRIVATE STB_LEAKCHECK)
    else()
        target_compile_options(toy_forth PRIVATE -fsanitize=leak -fno-omit-frame-pointer)
        target_link_options(toy_forth PRIVATE -fsanitize=leak)
    endif()

elseif(BUILD_MODE STREQUAL "Profile")
    if(MINGW)
        message(FATAL_ERROR "Profiling with MinGW is not supported.")
    endif()
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE) # -O2 -g built-in
    if(MSVC)
        target_compile_options(toy_forth PRIVATE /Oy-)
        target_link_options(toy_forth PRIVATE /DEBUG)
    else()
        target_compile_options(toy_forth PRIVATE -fno-omit-frame-pointer)
    endif()
endif()
