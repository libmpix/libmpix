# SPDX-License-Identifier: Apache-2.0

# Example CMake project for using libmpix on a POSIX system such as Linux, BSD, Mac OSX...

cmake_minimum_required(VERSION 3.20)

project(app LANGUAGES C)
set(CMAKE_BUILD_TYPE Debug)

add_executable(libmpix_test main.c)

add_subdirectory(../../ports/posix libmpix)
target_link_libraries(libmpix_test PUBLIC libmpix)
libmpix_init(libmpix_test)
