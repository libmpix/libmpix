# QEMU RISCV Port for libmpix

This port provides support for running libmpix on QEMU's RISCV emulation.

## Requirements

- RISCV GNU toolchain at `/opt/risv/riscv64-unknown-elf/bin` 
- QEMU with RISCV support
- CMake 3.20 or later

**Note**: The `riscv` toolchain should be build with `-mcmodel=medany`. Sample build is 
```sh
./configure --prefix=/opt/riscv/ --with-arch=rv64gc --with-abi=lp64 --with-cmodel=medany
```

## Features

- Able to run all libmpx tests on the riscv environment

```sh
cmake -DCMAKE_TOOLCHAIN_FILE=$(pwd)/ports/qemu_riscv64/toolchain-riscv64.cmake -B build ports/qemu_riscv64
```

- Run the various tests in qemu
```sh
qemu-system-riscv64 -machine virt -m 256 -nographic -bios none -kernel build/test_stats.elf
```