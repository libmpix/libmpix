/* SPDX-License-Identifier: Apache-2.0 */

#ifndef SYSTEM_CORTEX_M55_H
#define SYSTEM_CORTEX_M55_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

/* System Core Clock */
extern uint32_t SystemCoreClock;

/* SCB - System Control Block */
typedef struct {
    volatile uint32_t CPUID;   /* 0x00: CPUID Base Register */
    volatile uint32_t ICSR;    /* 0x04: Interrupt Control and State Register */
    volatile uint32_t VTOR;    /* 0x08: Vector Table Offset Register */
    volatile uint32_t AIRCR;   /* 0x0C: Application Interrupt and Reset Control Register */
    volatile uint32_t SCR;     /* 0x10: System Control Register */
    volatile uint32_t CCR;     /* 0x14: Configuration Control Register */
    volatile uint8_t  SHP[12]; /* 0x18: System Handlers Priority Registers */
    volatile uint32_t SHCSR;   /* 0x24: System Handler Control and State Register */
    volatile uint32_t CFSR;    /* 0x28: Configurable Fault Status Register */
    volatile uint32_t HFSR;    /* 0x2C: HardFault Status Register */
    volatile uint32_t DFSR;    /* 0x30: Debug Fault Status Register */
    volatile uint32_t MMFAR;   /* 0x34: MemManage Fault Address Register */
    volatile uint32_t BFAR;    /* 0x38: BusFault Address Register */
    volatile uint32_t AFSR;    /* 0x3C: Auxiliary Fault Status Register */
    volatile uint32_t PFR[2];  /* 0x40: Processor Feature Register */
    volatile uint32_t DFR;     /* 0x48: Debug Feature Register */
    volatile uint32_t ADR;     /* 0x4C: Auxiliary Feature Register */
    volatile uint32_t MMFR[4]; /* 0x50: Memory Model Feature Register */
    volatile uint32_t ISAR[6]; /* 0x60: Instruction Set Attributes Register */
    uint32_t RESERVED0[1];
    volatile uint32_t CLIDR;   /* 0x78: Cache Level ID register */
    volatile uint32_t CTR;     /* 0x7C: Cache Type register */
    volatile uint32_t CCSIDR;  /* 0x80: Cache Size ID Register */
    volatile uint32_t CSSELR;  /* 0x84: Cache Size Selection Register */
    volatile uint32_t CPACR;   /* 0x88: Coprocessor Access Control Register */
    volatile uint32_t NSACR;   /* 0x8C: Non-Secure Access Control Register */
} SCB_Type;

/* SCB CPACR Register Definitions */
#define SCB_CPACR_CP10_Pos          20U
#define SCB_CPACR_CP10_Msk          (3UL << SCB_CPACR_CP10_Pos)
#define SCB_CPACR_CP11_Pos          22U
#define SCB_CPACR_CP11_Msk          (3UL << SCB_CPACR_CP11_Pos)

/* DWT - Data Watchpoint and Trace Unit */
typedef struct {
    volatile uint32_t CTRL;     /* 0x00: Control Register */
    volatile uint32_t CYCCNT;   /* 0x04: Cycle Count Register */
    volatile uint32_t CPICNT;   /* 0x08: CPI Count Register */
    volatile uint32_t EXCCNT;   /* 0x0C: Exception Overhead Count Register */
    volatile uint32_t SLEEPCNT; /* 0x10: Sleep Count Register */
    volatile uint32_t LSUCNT;   /* 0x14: LSU Count Register */
    volatile uint32_t FOLDCNT;  /* 0x18: Folded-instruction Count Register */
    volatile uint32_t PCSR;     /* 0x1C: Program Counter Sample Register */
} DWT_Type;

/* DWT Control Register Definitions */
#define DWT_CTRL_CYCCNTENA_Pos      0U
#define DWT_CTRL_CYCCNTENA_Msk      (0x1UL << DWT_CTRL_CYCCNTENA_Pos)

/* Memory mapping */
#define SCB_BASE            (0xE000ED00UL)
#define DWT_BASE            (0xE0001000UL)

#define SCB                 ((SCB_Type*)SCB_BASE)
#define DWT                 ((DWT_Type*)DWT_BASE)

/* System functions */
void SystemInit(void);
void SystemCoreClockUpdate(void);

#ifdef __cplusplus
}
#endif

#endif /* SYSTEM_CORTEX_M55_H */
