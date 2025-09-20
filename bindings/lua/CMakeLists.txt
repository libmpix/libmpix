# SPDX-License-Identifier: Apache-2.0

# Example CMake project for using libmpix on a POSIX system such as Linux, BSD, Mac OSX...

cmake_minimum_required(VERSION 3.20)
find_package(PkgConfig)
pkg_check_modules(LUA REQUIRED lua)

project(app LANGUAGES C)
set(CMAKE_BUILD_TYPE Debug)

add_compile_options(-Wall -Wextra -Wno-unused-parameter)
add_executable(mpix-lua-demo main.c lua_mpix.c)
add_subdirectory(../../ports/posix libmpix)
target_link_libraries(mpix-lua-demo PUBLIC libmpix)
target_link_libraries(mpix-lua-demo PUBLIC ${LUA_LIBRARIES})
target_include_directories(mpix-lua-demo PUBLIC ${LUA_INCLUDE_DIRS})
target_compile_options(mpix-lua-demo PUBLIC ${LUA_CFLAGS_OTHER})

libmpix_add_genlist_source(${CMAKE_CURRENT_LIST_DIR}/lua_mpix.c)
libmpix_init(mpix-lua-demo)

install(TARGETS mpix-lua-demo)
