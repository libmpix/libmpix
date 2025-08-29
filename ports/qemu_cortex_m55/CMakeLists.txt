# SPDX-License-Identifier: Apache-2.0

# CMake rules to build libmpix for QEMU Cortex-M55 with ARM Helium SIMD support

cmake_minimum_required(VERSION 3.20)

project(libmpix_qemu_cortex_m55 LANGUAGES C)

# Cortex-M55 specific flags
set(CORTEX_M55_FLAGS 
    "-mcpu=cortex-m55" 
    "-mfloat-abi=hard" 
    "-mfpu=auto" 
)

# Common compiler flags
set(COMMON_FLAGS
    "-Wall"
    "-Wextra"
    "-Og"
    "-g3"
    "-ffunction-sections"
    "-fdata-sections"
    "-fstack-usage"
    "--specs=nano.specs"
    "-u _printf_float"
    "-u _scanf_float"
    "-Wconversion"
    "-Wsign-conversion"
    "-Wincompatible-pointer-types"
    "-flax-vector-conversions"
    "-std=gnu11"
)

# Set up paths
file(REAL_PATH ../.. LIBMPIX_DIR)

# Include directories
set(INCLUDE_DIRS
    ${LIBMPIX_DIR}/include
    ${LIBMPIX_DIR}/ports/qemu_cortex_m55
    ${LIBMPIX_DIR}/CMSIS_6/CMSIS/Core/Include/
)

# Generate genlist.h
set(GENLIST_H ${CMAKE_CURRENT_BINARY_DIR}/include/mpix/genlist.h)
file(MAKE_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/include/mpix)

# Collect all operation source files for genlist.py
file(GLOB LIBMPIX_OP_SOURCES ${LIBMPIX_DIR}/src/op_*.c)

# Run the genlist.py script to generate the list definitions
execute_process(
    COMMAND python3 ${LIBMPIX_DIR}/scripts/genlist.py ${LIBMPIX_OP_SOURCES}
    OUTPUT_FILE ${GENLIST_H}
    WORKING_DIRECTORY ${LIBMPIX_DIR}
    RESULT_VARIABLE GENLIST_RESULT
)

list(APPEND INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR}/include)


# Common source files
set(LIBMPIX_COMMON_SRC
    ${LIBMPIX_DIR}/src/JPEGENC.c
    ${LIBMPIX_DIR}/src/formats.c
    ${LIBMPIX_DIR}/src/image.c
    ${LIBMPIX_DIR}/src/print.c
    ${LIBMPIX_DIR}/src/str.c
    ${LIBMPIX_DIR}/src/utils.c
)

# Check for ARM-specific implementations and use them if available
# Otherwise fall back to CPU implementations
set(LIBMPIX_OPERATION_SRC)

# Handle debayer operation - use ARM Helium if available, otherwise CPU
if(EXISTS ${LIBMPIX_DIR}/src/arch/arm/op_debayer_helium.c)
    list(APPEND LIBMPIX_OPERATION_SRC ${LIBMPIX_DIR}/src/arch/arm/op_debayer_helium.c)
    message(STATUS "Using ARM Helium MVE debayer implementation")
endif()
# Always include base debayer implementation for the main API functions
list(APPEND LIBMPIX_OPERATION_SRC ${LIBMPIX_DIR}/src/op_debayer.c)

# Convert operations: prefer ARM if available, otherwise use CPU
if(EXISTS ${LIBMPIX_DIR}/src/arch/arm/op_convert_helium.c)
    list(APPEND LIBMPIX_OPERATION_SRC ${LIBMPIX_DIR}/src/arch/arm/op_convert_helium.c)
    message(STATUS "Using ARM Helium MVE convert implementation")
else()
    list(APPEND LIBMPIX_OPERATION_SRC ${LIBMPIX_DIR}/src/op_convert.c)
    message(STATUS "Using CPU convert implementation")
endif()

# Handle kernel operation - use ARM Helium if available, otherwise CPU
if(EXISTS ${LIBMPIX_DIR}/src/arch/arm/op_kernel_helium.c)
    list(APPEND LIBMPIX_OPERATION_SRC ${LIBMPIX_DIR}/src/arch/arm/op_kernel_helium.c)
    message(STATUS "Using ARM Helium MVE kernel implementation")
endif()
# Always include base kernel implementation for the main API functions
list(APPEND LIBMPIX_OPERATION_SRC ${LIBMPIX_DIR}/src/op_kernel.c)

# Handle palettize operation - use ARM Helium if available, otherwise CPU
if(EXISTS ${LIBMPIX_DIR}/src/arch/arm/op_palettize_helium.c)
    list(APPEND LIBMPIX_OPERATION_SRC ${LIBMPIX_DIR}/src/arch/arm/op_palettize_helium.c)
    message(STATUS "Using ARM Helium MVE palettize implementation")
endif()


# Handle JPEG operation - use ARM Helium if available, otherwise CPU
if(EXISTS ${LIBMPIX_DIR}/src/arch/arm/op_jpeg_helium.c)
    list(APPEND LIBMPIX_OPERATION_SRC ${LIBMPIX_DIR}/src/arch/arm/op_jpeg_helium.c)
    list(APPEND LIBMPIX_OPERATION_SRC ${LIBMPIX_DIR}/src/arch/arm/JPEGENC_helium.c)
    message(STATUS "Using ARM Helium MVE JPEG implementation")
endif()




# Add other operations if needed - you can extend this pattern
# Always include the CPU version if no ARM version exists
set(CPU_ONLY_OPERATIONS
    op_correction.c
    op_jpeg.c
    op_kernel.c
    op_palettize.c
    op_qoi.c
    op_resize.c
    op_debayer.c
    sample.c
    stats.c
)

foreach(op ${CPU_ONLY_OPERATIONS})
    if(EXISTS ${LIBMPIX_DIR}/src/arch/arm/${op})
        list(APPEND LIBMPIX_OPERATION_SRC ${LIBMPIX_DIR}/src/arch/arm/${op})
        message(STATUS "Using ARM implementation for ${op}")
    else()
        if(EXISTS ${LIBMPIX_DIR}/src/${op})
            list(APPEND LIBMPIX_OPERATION_SRC ${LIBMPIX_DIR}/src/${op})
            message(STATUS "Using CPU implementation for ${op}")
        endif()
    endif()
endforeach()

# Add ARM Helium correction implementation if present (custom filename)
if(EXISTS ${LIBMPIX_DIR}/src/arch/arm/op_correction_helium.c)
    list(APPEND LIBMPIX_OPERATION_SRC ${LIBMPIX_DIR}/src/arch/arm/op_correction_helium.c)
    message(STATUS "Using ARM Helium MVE correction implementation")
endif()

# Port-specific sources
set(LIBMPIX_PORT_SRC
    ${LIBMPIX_DIR}/ports/qemu_cortex_m55/port.c
    ${LIBMPIX_DIR}/ports/qemu_cortex_m55/system_cortex_m55.c
    ${LIBMPIX_DIR}/ports/qemu_cortex_m55/startup_cortex_m55.c
    ${LIBMPIX_DIR}/ports/qemu_cortex_m55/syscall_stubs.c
)

# Create the main library
add_library(mpix_core STATIC
    ${LIBMPIX_COMMON_SRC}
    ${LIBMPIX_OPERATION_SRC}
    ${LIBMPIX_PORT_SRC}
)

target_include_directories(mpix_core PUBLIC ${INCLUDE_DIRS})

target_compile_options(mpix_core PRIVATE 
    ${CORTEX_M55_FLAGS}
    ${COMMON_FLAGS}
    "-DCONFIG_MPIX_SIMD_ARM_HELIUM=1"
    "-DCONFIG_MPIX_LOG_LEVEL=4"
    "-DQEMU_CORTEX_M55=1"
    "-DARM_MATH_HELIUM=1"
    "-DARM_MATH_CM55=1"
    "-D__DSP_PRESENT=1"
    "-D__FPU_PRESENT=1"
)

# Create test executable for kernel
add_executable(test_kernel_helium
    ${LIBMPIX_DIR}/tests/kernel/main.c
)

target_link_libraries(test_kernel_helium 
    -Wl,--whole-archive mpix_core -Wl,--no-whole-archive
    -lm
)

# Create test executable for palettize
add_executable(test_palettize_helium
    ${LIBMPIX_DIR}/tests/palettize/main.c
)

target_link_libraries(test_palettize_helium 
    -Wl,--whole-archive mpix_core -Wl,--no-whole-archive
    -lm
)

target_compile_options(test_palettize_helium PRIVATE 
    ${CORTEX_M55_FLAGS}
    ${COMMON_FLAGS}
    "-DCONFIG_MPIX_SIMD_ARM_HELIUM=1"
    "-DCONFIG_MPIX_LOG_LEVEL=4"
    "-DQEMU_CORTEX_M55=1"
)

target_link_options(test_palettize_helium PRIVATE
    ${CORTEX_M55_FLAGS}
    ${COMMON_FLAGS}
    "-Wl,--gc-sections"
    "-Wl,--print-memory-usage"
    "--specs=nano.specs"
    "-u _printf_float"
    "-u _scanf_float"
    "-T${LIBMPIX_DIR}/ports/qemu_cortex_m55/cortex_m55.ld"
    "-Wl,-Map=test_palettize_helium.map"
)

set_target_properties(test_palettize_helium PROPERTIES OUTPUT_NAME "test_palettize_helium.elf")

target_compile_options(test_kernel_helium PRIVATE 
    ${CORTEX_M55_FLAGS}
    ${COMMON_FLAGS}
    "-DCONFIG_MPIX_SIMD_ARM_HELIUM=1"
    "-DCONFIG_MPIX_LOG_LEVEL=4"
    "-DQEMU_CORTEX_M55=1"
)

# Linker flags for kernel test
target_link_options(test_kernel_helium PRIVATE
    ${CORTEX_M55_FLAGS}
    ${COMMON_FLAGS}
    "-Wl,--gc-sections"
    "-Wl,--print-memory-usage"
    "--specs=nano.specs"
    "-u _printf_float"
    "-u _scanf_float"
    "-T${LIBMPIX_DIR}/ports/qemu_cortex_m55/cortex_m55.ld"
    "-Wl,-Map=test_kernel_helium.map"
)

# Set output name for kernel test
set_target_properties(test_kernel_helium PROPERTIES OUTPUT_NAME "test_kernel_helium.elf")

# Create test executable for debayer
add_executable(test_debayer_helium
    ${LIBMPIX_DIR}/tests/debayer/main.c
)

target_link_libraries(test_debayer_helium 
    -Wl,--whole-archive mpix_core -Wl,--no-whole-archive
    -lm
)

target_compile_options(test_debayer_helium PRIVATE 
    ${CORTEX_M55_FLAGS}
    ${COMMON_FLAGS}
    "-DCONFIG_MPIX_SIMD_ARM_HELIUM=1"
    "-DCONFIG_MPIX_LOG_LEVEL=4"
    "-DQEMU_CORTEX_M55=1"
)

# Linker flags
target_link_options(test_debayer_helium PRIVATE
    ${CORTEX_M55_FLAGS}
    ${COMMON_FLAGS}
    "-Wl,--gc-sections"
    "-Wl,--print-memory-usage"
    "--specs=nano.specs"
    "-u _printf_float"
    "-u _scanf_float"
    "-T${LIBMPIX_DIR}/ports/qemu_cortex_m55/cortex_m55.ld"
    "-Wl,-Map=test_debayer_helium.map"
)

# Create test executable for correction
add_executable(test_correction_helium
    ${LIBMPIX_DIR}/tests/correction/main.c
)

target_link_libraries(test_correction_helium 
    -Wl,--whole-archive mpix_core -Wl,--no-whole-archive
    -lm
)

target_compile_options(test_correction_helium PRIVATE 
    ${CORTEX_M55_FLAGS}
    ${COMMON_FLAGS}
    "-DCONFIG_MPIX_SIMD_ARM_HELIUM=1"
    "-DCONFIG_MPIX_LOG_LEVEL=4"
    "-DQEMU_CORTEX_M55=1"
)

target_link_options(test_correction_helium PRIVATE
    ${CORTEX_M55_FLAGS}
    ${COMMON_FLAGS}
    "-Wl,--gc-sections"
    "-Wl,--print-memory-usage"
    "--specs=nano.specs"
    "-u _printf_float"
    "-u _scanf_float"
    "-T${LIBMPIX_DIR}/ports/qemu_cortex_m55/cortex_m55.ld"
    "-Wl,-Map=test_correction_helium.map"
)

set_target_properties(test_correction_helium PROPERTIES OUTPUT_NAME "test_correction_helium.elf")

# Set output name
set_target_properties(test_debayer_helium PROPERTIES OUTPUT_NAME "test_debayer_helium.elf")

# Create simple test executable
add_executable(simple_test
    ${LIBMPIX_DIR}/ports/qemu_cortex_m55/main.c
)

target_link_libraries(simple_test 
    -Wl,--whole-archive mpix_core -Wl,--no-whole-archive
    -lm
)

target_compile_options(simple_test PRIVATE 
    ${CORTEX_M55_FLAGS}
    ${COMMON_FLAGS}
    "-DCONFIG_MPIX_SIMD_ARM_HELIUM=1"
    "-DCONFIG_MPIX_LOG_LEVEL=4"
    "-DQEMU_CORTEX_M55=1"
)

target_link_options(simple_test PRIVATE
    ${CORTEX_M55_FLAGS}
    ${COMMON_FLAGS}
    "-Wl,--gc-sections"
    "-Wl,--print-memory-usage"
    "--specs=nano.specs"
    "-u _printf_float"
    "-u _scanf_float"
    "-T${LIBMPIX_DIR}/ports/qemu_cortex_m55/cortex_m55.ld"
    "-Wl,-Map=simple_test.map"
)

set_target_properties(simple_test PROPERTIES OUTPUT_NAME "simple_test.elf")

# Create JPEG test executable (validates Helium quantize vs baseline)
add_executable(test_jpeg_helium
    ${LIBMPIX_DIR}/tests/jpeg/main.c
)

target_link_libraries(test_jpeg_helium 
    -Wl,--whole-archive mpix_core -Wl,--no-whole-archive
    -lm
)

target_compile_options(test_jpeg_helium PRIVATE 
    ${CORTEX_M55_FLAGS}
    ${COMMON_FLAGS}
    "-DCONFIG_MPIX_SIMD_ARM_HELIUM=1"
    "-DCONFIG_MPIX_LOG_LEVEL=4"
    "-DQEMU_CORTEX_M55=1"
)

target_link_options(test_jpeg_helium PRIVATE
    ${CORTEX_M55_FLAGS}
    ${COMMON_FLAGS}
    "-Wl,--gc-sections"
    "-Wl,--print-memory-usage"
    "--specs=nano.specs"
    "-u _printf_float"
    "-u _scanf_float"
    "-T${LIBMPIX_DIR}/ports/qemu_cortex_m55/cortex_m55.ld"
    "-Wl,-Map=test_jpeg_helium.map"
)

set_target_properties(test_jpeg_helium PROPERTIES OUTPUT_NAME "test_jpeg_helium.elf")

# Show build information
message(STATUS "Building for Cortex-M55 with ARM Helium MVE support")
message(STATUS "ARM Helium debayer source: ${LIBMPIX_DIR}/src/arch/arm/op_debayer_helium.c")