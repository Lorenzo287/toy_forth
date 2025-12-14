cmake_minimum_required(VERSION 3.10)
project(toy_forth)

include_directories(${PROJECT_SOURCE_DIR}/include)

file(GLOB SOURCES "${PROJECT_SOURCE_DIR}/src/*.c")

add_executable(toy_forth ${SOURCES})

target_compile_options(toy_forth PRIVATE
    -Wall
    -W
    -O2
)
