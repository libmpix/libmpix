/* SPDX-License-Identifier: Apache-2.0 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

int main(void)
{
    printf("RISCV Test Program\n");
    printf("======================================\n");
    
    #ifdef __riscv_vector
    printf("RISCV Vector Extension: AVAILABLE\n");
    #else
    printf("RISCV Vector Extension: NOT AVAILABLE\n");
    #endif
    
    #ifdef CONFIG_MPIX_SIMD_RISCV_VECTOR
    printf("libmpix RISCV Vector Extension: ENABLED\n");
    #else
    printf("libmpix RISCV Vector Extension: DISABLED\n");
    #endif
    
    
    // Exit cleanly for QEMU
    while(1) {
        __asm("wfi"); // Wait for interrupt
    }
    
    return 0;
}
