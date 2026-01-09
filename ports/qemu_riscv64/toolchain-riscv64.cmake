# SPDX-License-Identifier: Apache-2.0
# CMake toolchain file for RISC-V 64-bit cross-compilation

set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR riscv64)

# Toolchain paths
set(CMAKE_MAKE_PROGRAM make)
set(TOOLCHAIN_PREFIX "riscv64-unknown-elf-")

# Specify the cross compiler
set(CMAKE_C_COMPILER ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_CXX_COMPILER ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_ASM_COMPILER ${TOOLCHAIN_PREFIX}gcc)

# Utility tools
set(CMAKE_OBJCOPY ${TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_OBJDUMP ${TOOLCHAIN_PREFIX}objdump)
set(CMAKE_SIZE ${TOOLCHAIN_PREFIX}size)
set(CMAKE_DEBUGGER ${TOOLCHAIN_PREFIX}gdb)
set(CMAKE_CPPFILT ${TOOLCHAIN_PREFIX}c++filt)

# We are cross compiling so we don't want to run any programs
set(CMAKE_CROSSCOMPILING TRUE)

# Skip compiler tests
set(CMAKE_C_COMPILER_FORCED TRUE)
set(CMAKE_CXX_COMPILER_FORCED TRUE)

# Search for libraries and headers in the target directories only
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# RISC-V 64-bit specific flags (matching your Makefile)
set(CPU_FLAGS
    "-march=rv64imac_zicsr"
    "-mabi=lp64"
    "-mcmodel=medany"
)

# Common flags for both C and CXX
set(COMMON_FLAGS
    "-Wall"
    "-Wextra"
    "-g3"
    "-O2"
    "-nostartfiles"
)

# Combine CPU and common flags
string(REPLACE ";" " " CPU_FLAGS_STR "${CPU_FLAGS}")
string(REPLACE ";" " " COMMON_FLAGS_STR "${COMMON_FLAGS}")

# Set the compiler flags
set(CMAKE_C_FLAGS_INIT "${CPU_FLAGS_STR} ${COMMON_FLAGS_STR}")
set(CMAKE_CXX_FLAGS_INIT "${CPU_FLAGS_STR} ${COMMON_FLAGS_STR}")
set(CMAKE_ASM_FLAGS_INIT "${CPU_FLAGS_STR} ${COMMON_FLAGS_STR}")

# Linker flags (matching your Makefile)
set(CMAKE_EXE_LINKER_FLAGS_INIT "${CPU_FLAGS_STR} -specs=nosys.specs -Wl,--gc-sections")

# Set the executable suffix
set(CMAKE_EXECUTABLE_SUFFIX ".elf")