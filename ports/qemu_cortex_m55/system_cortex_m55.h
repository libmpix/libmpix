/* SPDX-License-Identifier: Apache-2.0 */

#ifndef SYSTEM_CORTEX_M55_H
#define SYSTEM_CORTEX_M55_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* ========================================================================= */
/* ============           Interrupt Number Definition           ============ */
/* ========================================================================= */

typedef enum IRQn
{
/* ================     Cortex-M Core Exception Numbers     ================ */
  Reset_IRQn             = -15,  /*  1 Reset Vector, invoked on Power up and warm reset */
  NonMaskableInt_IRQn    = -14,  /*  2 Non maskable Interrupt, cannot be stopped or preempted */
  HardFault_IRQn         = -13,  /*  3 Hard Fault, all classes of Fault */
  MemoryManagement_IRQn  = -12,  /*  4 Memory Management, MPU mismatch, including Access Violation and No Match */
  BusFault_IRQn          = -11,  /*  5 Bus Fault, Pre-Fetch-, Memory Access, other address/memory Fault */
  UsageFault_IRQn        = -10,  /*  6 Usage Fault, i.e. Undef Instruction, Illegal State Transition */
  SecureFault_IRQn       =  -9,  /*  7 Secure Fault Interrupt */
  SVCall_IRQn            =  -5,  /* 11 System Service Call via SVC instruction */
  DebugMonitor_IRQn      =  -4,  /* 12 Debug Monitor */
  PendSV_IRQn            =  -2,  /* 14 Pendable request for system service */
  SysTick_IRQn           =  -1,  /* 15 System Tick Timer */

/* ================     QEMU Cortex-M55 Interrupt Numbers   ================ */
  /* Basic QEMU MPS3 board interrupts for Cortex-M55 */
  UART0_IRQn             =  0,   /* UART 0 */
  UART1_IRQn             =  1,   /* UART 1 */
  UART2_IRQn             =  2,   /* UART 2 */
  UART3_IRQn             =  3,   /* UART 3 */
  UART4_IRQn             =  4,   /* UART 4 */
  UART5_IRQn             =  5,   /* UART 5 */
  TIMER0_IRQn            =  6,   /* Timer 0 */
  TIMER1_IRQn            =  7,   /* Timer 1 */
  TIMER2_IRQn            =  8,   /* Timer 2 */
  TIMER3_IRQn            =  9,   /* Timer 3 */
  GPIO0_IRQn             = 10,   /* GPIO 0 */
  GPIO1_IRQn             = 11,   /* GPIO 1 */
  GPIO2_IRQn             = 12,   /* GPIO 2 */
  GPIO3_IRQn             = 13,   /* GPIO 3 */
  I2C0_IRQn              = 14,   /* I2C 0 */
  I2C1_IRQn              = 15,   /* I2C 1 */
  SPI0_IRQn              = 16,   /* SPI 0 */
  SPI1_IRQn              = 17,   /* SPI 1 */
  SPI2_IRQn              = 18,   /* SPI 2 */
  WDT_IRQn               = 19,   /* Watchdog Timer */
  RTC_IRQn               = 20,   /* Real Time Clock */
  AUDIO_IRQn             = 21,   /* Audio */
  DMA0_IRQn              = 22,   /* DMA 0 */
  DMA1_IRQn              = 23,   /* DMA 1 */
  DMA2_IRQn              = 24,   /* DMA 2 */
  DMA3_IRQn              = 25,   /* DMA 3 */
  ETH_IRQn               = 26,   /* Ethernet */
  USB_IRQn               = 27,   /* USB */
  CAN_IRQn               = 28,   /* CAN */
  SDIO_IRQn              = 29,   /* SDIO */
  LCD_IRQn               = 30,   /* LCD Controller */
  CAMERA_IRQn            = 31,   /* Camera Interface */
} IRQn_Type;

/* ========================================================================= */
/* ============      Processor and Core Peripheral Section      ============ */
/* ========================================================================= */

/* --------  Configuration of Core Peripherals  ----------------------------------- */
#define __CM55_REV                0x0001U   /* Core revision r0p1 */
#define __SAUREGION_PRESENT       1U        /* SAU regions present */
#define __MPU_PRESENT             1U        /* MPU present */
#define __VTOR_PRESENT            1U        /* VTOR present */
#define __NVIC_PRIO_BITS          4U        /* Number of Bits used for Priority Levels */
#define __Vendor_SysTickConfig    0U        /* Set to 1 if different SysTick Config is used */
#ifndef __FPU_PRESENT
#define __FPU_PRESENT             1U        /* FPU present */
#endif
#define __FPU_DP                  1U        /* Double Precision FPU */
#ifndef __DSP_PRESENT
#define __DSP_PRESENT             1U        /* DSP extension present */
#endif
#define __ICACHE_PRESENT          1U        /* Instruction Cache present */
#define __DCACHE_PRESENT          1U        /* Data Cache present */


/* System Core Clock */
extern uint32_t SystemCoreClock;

/* System functions */
void SystemInit(void);
void SystemCoreClockUpdate(void);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_CORTEX_M55_H */
