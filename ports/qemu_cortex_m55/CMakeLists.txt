# SPDX-License-Identifier: Apache-2.0

# CMake rules to build libmpix for QEMU Cortex-M55 with ARM Helium SIMD support

cmake_minimum_required(VERSION 3.20)

# Set up cross-compilation environment BEFORE project() call
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Use the specified GCC ARM toolchain
set(CMAKE_C_COMPILER /opt/gcc-arm-none-eabi/bin/arm-none-eabi-gcc)
set(CMAKE_CXX_COMPILER /opt/gcc-arm-none-eabi/bin/arm-none-eabi-g++)
set(CMAKE_ASM_COMPILER /opt/gcc-arm-none-eabi/bin/arm-none-eabi-gcc)
set(CMAKE_OBJCOPY /opt/gcc-arm-none-eabi/bin/arm-none-eabi-objcopy)
set(CMAKE_OBJDUMP /opt/gcc-arm-none-eabi/bin/arm-none-eabi-objdump)
set(CMAKE_SIZE /opt/gcc-arm-none-eabi/bin/arm-none-eabi-size)

# Skip compiler tests for cross-compilation
set(CMAKE_C_COMPILER_WORKS 1)
set(CMAKE_CXX_COMPILER_WORKS 1)

project(libmpix_qemu_cortex_m55 LANGUAGES C)

# Cortex-M55 specific flags
set(CORTEX_M55_FLAGS 
    "-mcpu=cortex-m55"
    "-mthumb"
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
)

# Set up paths
file(REAL_PATH ../.. LIBMPIX_DIR)

# Include directories
set(INCLUDE_DIRS
    ${LIBMPIX_DIR}/include
    ${LIBMPIX_DIR}/ports/qemu_cortex_m55
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
)

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

# Show build information
message(STATUS "Building for Cortex-M55 with ARM Helium MVE support")
message(STATUS "ARM Helium debayer source: ${LIBMPIX_DIR}/src/arch/arm/op_debayer_helium.c")