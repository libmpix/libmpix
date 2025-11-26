# SPDX-License-Identifier: Apache-2.0

# Example CMake project for using libmpix on a POSIX system such as Linux, BSD, Mac OSX...

cmake_minimum_required(VERSION 3.20)

project(app LANGUAGES C)
set(CMAKE_BUILD_TYPE Debug)

FILE(GLOB app_sources src/*.c)

add_executable(libmpix_benchmark ${app_sources})

target_compile_definitions(libmpix_benchmark PRIVATE CONFIG_WIDTH=320 CONFIG_HEIGHT=240)

add_subdirectory(../../ports/posix libmpix)
target_link_libraries(libmpix_benchmark PUBLIC libmpix)
libmpix_init(libmpix_benchmark)
