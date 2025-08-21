/* SPDX-License-Identifier: Apache-2.0 */

#include "system_cortex_m55.h"

uint32_t SystemCoreClock = 100000000UL; /* Default to 100 MHz */

void SystemInit(void)
{
    /* Enable FPU */
    #ifdef __ARM_FEATURE_FMA
    SCB->CPACR |= ((3UL << 20) | (3UL << 22));
    __asm volatile ("dsb");
    __asm volatile ("isb");
    #endif
    
    /* Enable MVE */
    #ifdef __ARM_FEATURE_MVE
    SCB->CPACR |= ((3UL << 20) | (3UL << 22));
    __asm volatile ("dsb");
    __asm volatile ("isb");
    #endif
    
    /* Enable DWT cycle counter */
    if (DWT) {
        DWT->CYCCNT = 0;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }
}

void SystemCoreClockUpdate(void)
{
    /* Update SystemCoreClock variable if needed */
    SystemCoreClock = 100000000UL;
}
