/* SPDX-License-Identifier: Apache-2.0 */

#include "system_cortex_m55.h"
#include "core_cm55.h"

uint32_t SystemCoreClock = 100000000UL; /* Default to 100 MHz */

void SystemInit(void)
{
    /* Enable FPU and MVE co-processor access */
    /* CP10 and CP11: FPU, CP15: Reserved for future use (MVE uses same CP10/11) */
    SCB->CPACR |= ((3UL << 20) | (3UL << 22));  /* CP10 and CP11 full access */
    __asm volatile ("dsb");
    __asm volatile ("isb");
    
    /* QEMU compatibility: Skip DWT/CoreDebug initialization 
     * These debug features are not fully implemented in QEMU and cause access violations */
#ifndef QEMU_CORTEX_M55
    /* Enable Debug and Trace features for DWT */
    if (CoreDebug) {
        CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    }
    
    /* Enable DWT cycle counter if available */
    if (DWT && (CoreDebug->DEMCR & CoreDebug_DEMCR_TRCENA_Msk)) {
        DWT->CYCCNT = 0;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }
#endif
}

void SystemCoreClockUpdate(void)
{
    /* Update SystemCoreClock variable if needed */
    SystemCoreClock = 100000000UL;
}
