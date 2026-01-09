# SPDX-License-Identifier: Apache-2.0

# CMake rules to build libmpix for QEMU RISCV with Vector Extension support

cmake_minimum_required(VERSION 3.20)

project(libmpix_qemu_riscv64 LANGUAGES C ASM)

# Common compiler flags
set(COMMON_FLAGS
    "-Wall"
    "-Wextra"
    "-Og"
    "-g3"
)

# Set up paths
file(REAL_PATH ../.. LIBMPIX_DIR)

# Include directories
set(INCLUDE_DIRS
    ${LIBMPIX_DIR}/include
    ${LIBMPIX_DIR}/ports/qemu_riscv64
)

# Generate genlist.h
set(GENLIST_H ${CMAKE_CURRENT_BINARY_DIR}/include/mpix/genlist.h)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/mpix)

# Collect all operation source files for genlist.py
file(GLOB LIBMPIX_OP_SOURCES ${LIBMPIX_DIR}/src/op_*.c)

# Run the genlist.py script to generate the list definitions
execute_process(
    COMMAND sh ${LIBMPIX_DIR}/scripts/genlist.sh ${LIBMPIX_OP_SOURCES}
    OUTPUT_FILE ${GENLIST_H}
    WORKING_DIRECTORY ${LIBMPIX_DIR}
    RESULT_VARIABLE GENLIST_RESULT
)

list(APPEND INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/include)

file(GLOB SOURCE_LIST ${LIBMPIX_DIR}/src/*.c)

# All source files
set(LIBMPIX_SRC
    ${SOURCE_LIST}
)

# Port-specific sources
set(LIBMPIX_PORT_SRC
    ${LIBMPIX_DIR}/ports/qemu_riscv64/port.c
    ${LIBMPIX_DIR}/ports/qemu_riscv64/syscall_stubs.c
    ${LIBMPIX_DIR}/ports/qemu_riscv64/startup.S
)

# Create the main library
add_library(mpix_core STATIC
    ${LIBMPIX_SRC}
    ${LIBMPIX_PORT_SRC}
)

target_include_directories(mpix_core PUBLIC ${INCLUDE_DIRS})

target_compile_options(mpix_core PRIVATE 
    ${COMMON_FLAGS}
    "-DCONFIG_MPIX_LOG_LEVEL=4"
)

# Create test executable for convert
add_executable(test_convert
    ${LIBMPIX_DIR}/tests/convert.on/main.c
)

target_link_libraries(test_convert
    -Wl,--whole-archive mpix_core -Wl,--no-whole-archive
    -lm
)

target_compile_options(test_convert PRIVATE 
    ${COMMON_FLAGS}
    "-DCONFIG_MPIX_LOG_LEVEL=4"
)

target_link_options(test_convert PRIVATE
    ${COMMON_FLAGS}
    "-Wl,--gc-sections"
    "-Wl,--print-memory-usage"
    "--specs=nano.specs"
    "-u _printf_float"
    "-u _scanf_float"
    "-T${LIBMPIX_DIR}/ports/qemu_riscv64/riscv64.ld"
    "-Wl,-Map=test_convert.map"
)

set_target_properties(test_convert PROPERTIES OUTPUT_NAME "test_convert.elf")

# Create test executable for correction
add_executable(test_correction
    ${LIBMPIX_DIR}/tests/correction.on/main.c
)

target_link_libraries(test_correction
    -Wl,--whole-archive mpix_core -Wl,--no-whole-archive
    -lm
)

target_compile_options(test_correction PRIVATE 
    ${COMMON_FLAGS}
    "-DCONFIG_MPIX_LOG_LEVEL=4"
)

target_link_options(test_correction PRIVATE
    ${COMMON_FLAGS}
    "-Wl,--gc-sections"
    "-Wl,--print-memory-usage"
    "--specs=nano.specs"
    "-u _printf_float"
    "-u _scanf_float"
    "-T${LIBMPIX_DIR}/ports/qemu_riscv64/riscv64.ld"
    "-Wl,-Map=test_correction.map"
)

set_target_properties(test_correction PROPERTIES OUTPUT_NAME "test_correction.elf")

# Create test executable for crop
add_executable(test_crop
    ${LIBMPIX_DIR}/tests/crop.on/main.c
)

target_link_libraries(test_crop
    -Wl,--whole-archive mpix_core -Wl,--no-whole-archive
    -lm
)

target_compile_options(test_crop PRIVATE 
    ${COMMON_FLAGS}
    "-DCONFIG_MPIX_LOG_LEVEL=4"
)

target_link_options(test_crop PRIVATE
    ${COMMON_FLAGS}
    "-Wl,--gc-sections"
    "-Wl,--print-memory-usage"
    "--specs=nano.specs"
    "-u _printf_float"
    "-u _scanf_float"
    "-T${LIBMPIX_DIR}/ports/qemu_riscv64/riscv64.ld"
    "-Wl,-Map=test_crop.map"
)

set_target_properties(test_crop PROPERTIES OUTPUT_NAME "test_crop.elf")

# Create test executable for image
add_executable(test_image
    ${LIBMPIX_DIR}/tests/image.on/main.c
)

target_link_libraries(test_image
    -Wl,--whole-archive mpix_core -Wl,--no-whole-archive
    -lm
)

target_compile_options(test_image PRIVATE 
    ${COMMON_FLAGS}
    "-DCONFIG_MPIX_LOG_LEVEL=4"
)

target_link_options(test_image PRIVATE
    ${COMMON_FLAGS}
    "-Wl,--gc-sections"
    "-Wl,--print-memory-usage"
    "--specs=nano.specs"
    "-u _printf_float"
    "-u _scanf_float"
    "-T${LIBMPIX_DIR}/ports/qemu_riscv64/riscv64.ld"
    "-Wl,-Map=test_image.map"
)

set_target_properties(test_image PROPERTIES OUTPUT_NAME "test_image.elf")

# Create test executable for palettize
add_executable(test_palettize
    ${LIBMPIX_DIR}/tests/palettize.on/main.c
)

target_link_libraries(test_palettize
    -Wl,--whole-archive mpix_core -Wl,--no-whole-archive
    -lm
)

target_compile_options(test_palettize PRIVATE 
    ${COMMON_FLAGS}
    "-DCONFIG_MPIX_LOG_LEVEL=4"
)

target_link_options(test_palettize PRIVATE
    ${COMMON_FLAGS}
    "-Wl,--gc-sections"
    "-Wl,--print-memory-usage"
    "--specs=nano.specs"
    "-u _printf_float"
    "-u _scanf_float"
    "-T${LIBMPIX_DIR}/ports/qemu_riscv64/riscv64.ld"
    "-Wl,-Map=test_palettize.map"
)

set_target_properties(test_palettize PROPERTIES OUTPUT_NAME "test_palettize.elf")

# Create test executable for kernel
add_executable(test_kernel
    ${LIBMPIX_DIR}/tests/kernel.on/main.c
)

target_link_libraries(test_kernel
    -Wl,--whole-archive mpix_core -Wl,--no-whole-archive
    -lm
)

target_compile_options(test_kernel PRIVATE 
    ${COMMON_FLAGS}
    "-DCONFIG_MPIX_LOG_LEVEL=4"
)

# Linker flags for kernel test
target_link_options(test_kernel PRIVATE
    ${COMMON_FLAGS}
    "-Wl,--gc-sections"
    "-Wl,--print-memory-usage"
    "--specs=nano.specs"
    "-u _printf_float"
    "-u _scanf_float"
    "-T${LIBMPIX_DIR}/ports/qemu_riscv64/riscv64.ld"
    "-Wl,-Map=test_kernel.map"
)

# Set output name for kernel test
set_target_properties(test_kernel PROPERTIES OUTPUT_NAME "test_kernel.elf")

# Create test executable for debayer
add_executable(test_debayer
    ${LIBMPIX_DIR}/tests/debayer.on/main.c
)

target_link_libraries(test_debayer 
    -Wl,--whole-archive mpix_core -Wl,--no-whole-archive
    -lm
)

target_compile_options(test_debayer PRIVATE 
    ${COMMON_FLAGS}
    "-DCONFIG_MPIX_LOG_LEVEL=4"
)

# Linker flags for kernel test
target_link_options(test_debayer PRIVATE
    ${COMMON_FLAGS}
    "-Wl,--gc-sections"
    "-Wl,--print-memory-usage"
    "--specs=nano.specs"
    "-u _printf_float"
    "-u _scanf_float"
    "-T${LIBMPIX_DIR}/ports/qemu_riscv64/riscv64.ld"
    "-Wl,-Map=test_debayer.map"
)

# Create simple test executable
add_executable(simple_test
    ${LIBMPIX_DIR}/ports/qemu_riscv64/main.c
)

target_link_libraries(simple_test 
    -Wl,--whole-archive mpix_core -Wl,--no-whole-archive
    -lm
)

target_compile_options(simple_test PRIVATE 
    ${COMMON_FLAGS}
    "-DCONFIG_MPIX_LOG_LEVEL=4"
)

target_link_options(simple_test PRIVATE
    ${COMMON_FLAGS}
    "-Wl,--gc-sections"
    "-Wl,--print-memory-usage"
    "--specs=nano.specs"
    "-u _printf_float"
    "-u _scanf_float"
    "-T${LIBMPIX_DIR}/ports/qemu_riscv64/riscv64.ld"
    "-Wl,-Map=simple_test.map"
)

set_target_properties(simple_test PROPERTIES OUTPUT_NAME "simple_test.elf")

# Create test executable for print
add_executable(test_print
    ${LIBMPIX_DIR}/tests/print.on/main.c
)

target_link_libraries(test_print
    -Wl,--whole-archive mpix_core -Wl,--no-whole-archive
    -lm
)

target_compile_options(test_print PRIVATE 
    ${COMMON_FLAGS}
    "-DCONFIG_MPIX_LOG_LEVEL=4"
)

target_link_options(test_print PRIVATE
    ${COMMON_FLAGS}
    "-Wl,--gc-sections"
    "-Wl,--print-memory-usage"
    "--specs=nano.specs"
    "-u _printf_float"
    "-u _scanf_float"
    "-T${LIBMPIX_DIR}/ports/qemu_riscv64/riscv64.ld"
    "-Wl,-Map=test_print.map"
)

set_target_properties(test_print PROPERTIES OUTPUT_NAME "test_print.elf")

# Create test executable for resize
add_executable(test_resize
    ${LIBMPIX_DIR}/tests/resize.on/main.c
)

target_link_libraries(test_resize
    -Wl,--whole-archive mpix_core -Wl,--no-whole-archive
    -lm
)

target_compile_options(test_resize PRIVATE 
    ${COMMON_FLAGS}
    "-DCONFIG_MPIX_LOG_LEVEL=4"
)

target_link_options(test_resize PRIVATE
    ${COMMON_FLAGS}
    "-Wl,--gc-sections"
    "-Wl,--print-memory-usage"
    "--specs=nano.specs"
    "-u _printf_float"
    "-u _scanf_float"
    "-T${LIBMPIX_DIR}/ports/qemu_riscv64/riscv64.ld"
    "-Wl,-Map=test_resize.map"
)

set_target_properties(test_resize PROPERTIES OUTPUT_NAME "test_resize.elf")

# Create test executable for stats
add_executable(test_stats
    ${LIBMPIX_DIR}/tests/stats.on/main.c
)

target_link_libraries(test_stats
    -Wl,--whole-archive mpix_core -Wl,--no-whole-archive
    -lm
)

target_compile_options(test_stats PRIVATE 
    ${COMMON_FLAGS}
    "-DCONFIG_MPIX_LOG_LEVEL=4"
)

target_link_options(test_stats PRIVATE
    ${COMMON_FLAGS}
    "-Wl,--gc-sections"
    "-Wl,--print-memory-usage"
    "--specs=nano.specs"
    "-u _printf_float"
    "-u _scanf_float"
    "-T${LIBMPIX_DIR}/ports/qemu_riscv64/riscv64.ld"
    "-Wl,-Map=test_stats.map"
)

set_target_properties(test_stats PROPERTIES OUTPUT_NAME "test_stats.elf")

# Show build information
message(STATUS "Building for RISCV")