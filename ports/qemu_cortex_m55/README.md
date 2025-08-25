# QEMU Cortex-M55 Port for libmpix

This port provides support for running libmpix on QEMU's Cortex-M55 emulation with ARM Helium SIMD acceleration.

## Requirements

- ARM GCC toolchain at `/opt/gcc-arm-none-eabi/bin/`
- QEMU with Cortex-M55 support
- CMake 3.20 or later

## Features

- ARM Cortex-M55 CPU support
- ARM Helium (MVE) SIMD acceleration
- Hardware floating-point support
- 512KB RAM, 1MB Flash memory layout
- DWT cycle counter for performance measurement
- Simple heap allocator for embedded environment

## Building Tests

To build and run tests for specific operations:

```bash
cd tests/<operation_name>  # e.g., tests/convert
mkdir build_cortex_m55
cd build_cortex_m55

# Configure CMake for Cortex-M55
cmake -DCMAKE_TOOLCHAIN_FILE=/home/baozhu/storage/libmpix/ports/qemu_cortex_m55/toolchain-arm-cortex-m55.cmake -B build ports/qemu_cortex_m55
# Build the test
make

# The resulting binary can be run in QEMU
timeout 15 qemu-system-arm -machine mps3-an547 -cpu cortex-m55 -kernel test_debayer_helium.elf -semihosting -nographic
```

## Running with QEMU

Example QEMU command to run a test binary:

```bash
# Basic execution
qemu-system-arm -M mps3-an547 -kernel test.elf -nographic -semihosting

# With debugging support
qemu-system-arm -M mps3-an547 -kernel test.elf -nographic -semihosting -s -S

# Connect GDB for debugging
arm-none-eabi-gdb test.elf
(gdb) target remote :1234
(gdb) continue
```

## SIMD Support

This port automatically enables ARM Helium SIMD support when the toolchain supports it:

- `CONFIG_MPIX_SIMD_ARM_HELIUM=1` is defined
- Helium-optimized functions are included from `src/arch/arm/helium/`
- MVE instructions are available for accelerated image processing

## Memory Configuration

- **Flash**: 1MB starting at 0x00000000
- **RAM**: 512KB starting at 0x20000000  
- **Heap**: 64KB configurable via `MPIX_HEAP_SIZE`
- **Stack**: Minimum 1KB

## Supported Operations

All libmpix operations are supported, with hardware-accelerated versions for:

- Format conversion (RGB24, YUV, etc.)
- Debayer operations
- Kernel operations (blur, sharpen, edge detection)
- Color correction
- Image resizing
- Statistics collection

## Debugging

The port includes:

- Debug symbols (`-g3`)
- Stack usage reporting (`-fstack-usage`)
- Memory usage reporting at link time
- DWT cycle counter for performance profiling

## Customization

Key configuration options in `CMakeLists.txt`:

- `CORTEX_M55_FLAGS`: CPU-specific compilation flags
- `MPIX_HEAP_SIZE`: Heap size in `port.c`
- Optimization level and debug flags

The linker script `cortex_m55.ld` can be modified for different memory layouts.
