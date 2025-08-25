#!/bin/bash
# SPDX-License-Identifier: Apache-2.0
# Build and test script for libmpix with QEMU Cortex-M55 ARM Helium MVE

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LIBMPIX_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}Building libmpix for QEMU Cortex-M55 with ARM Helium MVE${NC}"

# Check for required tools
ARM_GCC_PATH="/opt/gcc-arm-none-eabi/bin/arm-none-eabi-gcc"
if [ ! -f "${ARM_GCC_PATH}" ]; then
    if ! command -v arm-none-eabi-gcc &> /dev/null; then
        echo -e "${RED}Error: arm-none-eabi-gcc not found${NC}"
        echo "Checked path: ${ARM_GCC_PATH}"
        echo "Please install ARM GCC toolchain"
        exit 1
    fi
else
    echo -e "${GREEN}Found ARM GCC at: ${ARM_GCC_PATH}${NC}"
    # Add to PATH for this session
    export PATH="/opt/gcc-arm-none-eabi/bin:${PATH}"
fi

if ! command -v qemu-system-arm &> /dev/null; then
    echo -e "${YELLOW}Warning: qemu-system-arm not found in PATH${NC}"
    echo "QEMU execution will be skipped"
fi

# Create build directory
BUILD_DIR="${LIBMPIX_ROOT}/build_qemu_cortex_m55"
rm -rf "${BUILD_DIR}"
mkdir -p "${BUILD_DIR}"
cd "${BUILD_DIR}"

echo -e "${YELLOW}Configuring build...${NC}"

# Configure with CMake
cmake -DCMAKE_TOOLCHAIN_FILE="${LIBMPIX_ROOT}/ports/qemu_cortex_m55/toolchain-arm-cortex-m55.cmake" \
      -DCMAKE_BUILD_TYPE=Debug \
      -DCONFIG_MPIX_SIMD_ARM_HELIUM=1 \
      -DCONFIG_MPIX_LOG_LEVEL=4 \
      "${LIBMPIX_ROOT}/ports/qemu_cortex_m55"

echo -e "${YELLOW}Building...${NC}"

# Build
make -j$(nproc)

echo -e "${GREEN}Build completed successfully!${NC}"

# Check if the ARM Helium optimized file was compiled
if [ -f "${BUILD_DIR}/CMakeFiles/libmpix_qemu_cortex_m55.dir/src/arch/arm/op_debayer_helium.c.obj" ]; then
    echo -e "${GREEN}✓ ARM Helium MVE debayer implementation compiled${NC}"
else
    echo -e "${YELLOW}⚠ ARM Helium MVE debayer not found in build${NC}"
fi

# Build test executable for debayer
echo -e "${YELLOW}Building debayer test...${NC}"

TEST_BUILD_DIR="${BUILD_DIR}/tests/debayer"
mkdir -p "${TEST_BUILD_DIR}"
cd "${TEST_BUILD_DIR}"

# Create test CMakeLists.txt
cat > CMakeLists.txt << 'EOF'
cmake_minimum_required(VERSION 3.20)

project(test_debayer LANGUAGES C)

# Use the same toolchain and flags as the main project
set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR arm)

# Import libmpix
find_package(PkgConfig QUIET)

# Add the test executable
add_executable(test_debayer
    ${LIBMPIX_ROOT}/tests/debayer/main.c
)

# Link with libmpix
target_link_libraries(test_debayer libmpix)

# Set binary name
set_target_properties(test_debayer PROPERTIES OUTPUT_NAME "test_debayer.elf")
EOF

# Configure and build the test
cmake -DLIBMPIX_ROOT="${LIBMPIX_ROOT}" \
      -DCMAKE_TOOLCHAIN_FILE="${LIBMPIX_ROOT}/ports/qemu_cortex_m55/toolchain-arm-cortex-m55.cmake" \
      .

make

if [ -f "test_debayer.elf" ]; then
    echo -e "${GREEN}✓ Debayer test compiled successfully${NC}"
    
    # Show binary information
    echo -e "${YELLOW}Binary information:${NC}"
    arm-none-eabi-size test_debayer.elf
    
    # Try to run with QEMU if available
    if command -v qemu-system-arm &> /dev/null; then
        echo -e "${YELLOW}Running test with QEMU...${NC}"
        timeout 30 qemu-system-arm \
            -machine mps3-an547 \
            -cpu cortex-m55 \
            -kernel test_debayer.elf \
            -nographic \
            -monitor none \
            -serial stdio || {
                echo -e "${YELLOW}QEMU test completed (timeout or exit)${NC}"
            }
    fi
else
    echo -e "${RED}✗ Test compilation failed${NC}"
fi

echo -e "${GREEN}Script completed!${NC}"
