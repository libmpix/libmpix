/* SPDX-License-Identifier: Apache-2.0 */

/* Startup code for QEMU Cortex-M55 */

#include <stdint.h>
#include "system_cortex_m55.h"

/* Stack pointer initialization */
extern uint32_t _estack;

/* External references */
extern uint32_t _sdata, _edata, _sidata;
extern uint32_t _sbss, _ebss;

/* Function prototypes */
void Reset_Handler(void);
void Default_Handler(void);
int main(void);

/* System exception handlers */
void NMI_Handler(void)                  __attribute__((weak, alias("Default_Handler")));
void HardFault_Handler(void)            __attribute__((weak, alias("Default_Handler")));
void MemManage_Handler(void)            __attribute__((weak, alias("Default_Handler")));
void BusFault_Handler(void)             __attribute__((weak, alias("Default_Handler")));
void UsageFault_Handler(void)           __attribute__((weak, alias("Default_Handler")));
void SecureFault_Handler(void)          __attribute__((weak, alias("Default_Handler")));
void SVC_Handler(void)                  __attribute__((weak, alias("Default_Handler")));
void DebugMon_Handler(void)             __attribute__((weak, alias("Default_Handler")));
void PendSV_Handler(void)               __attribute__((weak, alias("Default_Handler")));
void SysTick_Handler(void)              __attribute__((weak, alias("Default_Handler")));

/* IRQ handlers */
void WWDG_IRQHandler(void)              __attribute__((weak, alias("Default_Handler")));
void PVD_PVM_IRQHandler(void)           __attribute__((weak, alias("Default_Handler")));
void RTC_TAMP_LSECSS_IRQHandler(void)   __attribute__((weak, alias("Default_Handler")));
void RTC_WKUP_IRQHandler(void)          __attribute__((weak, alias("Default_Handler")));
void FLASH_IRQHandler(void)             __attribute__((weak, alias("Default_Handler")));
void RCC_IRQHandler(void)               __attribute__((weak, alias("Default_Handler")));
void EXTI0_IRQHandler(void)             __attribute__((weak, alias("Default_Handler")));
void EXTI1_IRQHandler(void)             __attribute__((weak, alias("Default_Handler")));

/* Vector table */
__attribute__((section(".isr_vector")))
const void* vector_table[] = {
    &_estack,                   // 0x00: Stack pointer
    Reset_Handler,              // 0x04: Reset handler
    NMI_Handler,                // 0x08: NMI handler
    HardFault_Handler,          // 0x0C: Hard fault handler
    MemManage_Handler,          // 0x10: Memory management fault handler
    BusFault_Handler,           // 0x14: Bus fault handler
    UsageFault_Handler,         // 0x18: Usage fault handler
    SecureFault_Handler,        // 0x1C: Secure fault handler
    0,                          // 0x20: Reserved
    0,                          // 0x24: Reserved
    0,                          // 0x28: Reserved
    SVC_Handler,                // 0x2C: SVCall handler
    DebugMon_Handler,           // 0x30: Debug monitor handler
    0,                          // 0x34: Reserved
    PendSV_Handler,             // 0x38: PendSV handler
    SysTick_Handler,            // 0x3C: SysTick handler
    
    /* External interrupts */
    WWDG_IRQHandler,            // 0x40: Window WatchDog
    PVD_PVM_IRQHandler,         // 0x44: PVD/PVM through EXTI Line detection
    RTC_TAMP_LSECSS_IRQHandler, // 0x48: RTC Tamper and TimeStamps through the EXTI line
    RTC_WKUP_IRQHandler,        // 0x4C: RTC Wakeup through the EXTI line
    FLASH_IRQHandler,           // 0x50: FLASH
    RCC_IRQHandler,             // 0x54: RCC
    EXTI0_IRQHandler,           // 0x58: EXTI Line0
    EXTI1_IRQHandler,           // 0x5C: EXTI Line1
    // Add more IRQ handlers as needed
};

/* Reset handler implementation */
void Reset_Handler(void)
{
    uint32_t *src, *dest;
    
    /* Copy data from FLASH to SRAM */
    src = &_sidata;
    dest = &_sdata;
    while (dest < &_edata) {
        *dest++ = *src++;
    }
    
    /* Clear BSS section */
    dest = &_sbss;
    while (dest < &_ebss) {
        *dest++ = 0;
    }
    
    /* Enable FPU if available */
    #ifdef __ARM_FEATURE_FMA
    /* Set CP10 and CP11 for full access */
    SCB->CPACR |= ((3UL << 20) | (3UL << 22));
    #endif
    
    /* Enable MVE if available */
    #ifdef __ARM_FEATURE_MVE
    /* Enable MVE and FP access */
    SCB->CPACR |= ((3UL << 20) | (3UL << 22));
    #endif
    
    /* Call main function */
    main();
    
    /* If main returns, loop forever */
    while (1) {
        __asm("nop");
    }
}

/* Default handler implementation */
void Default_Handler(void)
{
    while (1) {
        __asm("nop");
    }
}
