cmake_minimum_required(VERSION 3.10)
project(toy_forth C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

# Default build type must be set before the first target is defined
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Choose the build type: Release, LeakCheck, Profile." FORCE)
endif()

include_directories(${PROJECT_SOURCE_DIR}/include)
file(GLOB SOURCES "${PROJECT_SOURCE_DIR}/src/*.c")
add_executable(toy_forth ${SOURCES})

# ── Compiler baseline flags ───────────────────────────────────────────────────

if(MSVC)
    # Override CMake's default per-config flag sets so our custom build types
    # don't inherit conflicting options (e.g. /RTC1 or /MDd).
    set(CMAKE_C_FLAGS_DEBUG          "/MTd /Zi /Ob0 /Od /RTC1")
    set(CMAKE_C_FLAGS_RELEASE        "/MT /O2 /Oi /Ot /GL")
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "/MT /Zi /O2 /Oi /Ot /GL")
    set(CMAKE_C_FLAGS_MINSIZEREL     "/MT /O1")

    target_compile_options(toy_forth PRIVATE /W3)
    target_compile_definitions(toy_forth PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    # GCC / Clang on Linux, macOS, or Windows (MinGW)
    target_compile_options(toy_forth PRIVATE -Wall -Wextra -Wpedantic)
    if(WIN32)
        target_compile_definitions(toy_forth PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
endif()

# ── Per-build-type flags ──────────────────────────────────────────────────────

string(TOUPPER "${CMAKE_BUILD_TYPE}" UPPER_BUILD_TYPE)

if(UPPER_BUILD_TYPE STREQUAL "RELEASE")
    if(MSVC)
        # CMAKE_C_FLAGS_RELEASE already sets /O2 /Oi /Ot /GL via the flag sets above.
        target_link_options(toy_forth PRIVATE /LTCG)
    else()
        target_compile_options(toy_forth PRIVATE -O3)
    endif()

elseif(UPPER_BUILD_TYPE STREQUAL "LEAKCHECK")
    if(MINGW)
        # MinGW has no reliable ASan support; fall back to stb_leakcheck.
        target_compile_definitions(toy_forth PRIVATE STB_LEAKCHECK)
        target_compile_options(toy_forth PRIVATE -O0 -g)
    elseif(MSVC)
        # MSVC supports ASan natively since VS 2019 16.9.
        target_compile_options(toy_forth PRIVATE /fsanitize=address /Od /Zi)
        # ASan on MSVC is incompatible with /RTC1 and incremental linking.
        target_link_options(toy_forth PRIVATE /INCREMENTAL:NO)
    else()
        # GCC / Clang on Linux or macOS.
        target_compile_options(toy_forth PRIVATE -fsanitize=address -fno-omit-frame-pointer -O0 -g)
        target_link_options(toy_forth PRIVATE -fsanitize=address)
    endif()

elseif(UPPER_BUILD_TYPE STREQUAL "PROFILE")
    if(MINGW)
        message(FATAL_ERROR "Profiling with MinGW is not supported. Use MSVC or Clang.")
    elseif(MSVC)
    	target_compile_options(toy_forth PRIVATE /O2 /Zi)
    	target_link_options(toy_forth PRIVATE /DEBUG)
    else()
        target_compile_options(toy_forth PRIVATE -O2 -g)
    endif()

else()
    message(STATUS "Unknown build type '${CMAKE_BUILD_TYPE}' — no extra flags applied.")
endif()
