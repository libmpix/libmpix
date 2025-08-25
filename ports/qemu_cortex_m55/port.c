/* SPDX-License-Identifier: Apache-2.0 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <mpix/port.h>

/* Simple heap management for embedded systems */
#ifndef MPIX_HEAP_SIZE
#define MPIX_HEAP_SIZE (64 * 1024) // 64KB heap for testing
#endif

static uint8_t heap_memory[MPIX_HEAP_SIZE] __attribute__((aligned(8)));
static size_t heap_offset = 0;

void *mpix_port_alloc(size_t size)
{
    // Align size to 8-byte boundary
    size = (size + 7) & ~7;
    
    if (heap_offset + size > MPIX_HEAP_SIZE) {
        return NULL; // Out of memory
    }
    
    void *ptr = &heap_memory[heap_offset];
    heap_offset += size;
    return ptr;
}

void mpix_port_free(void *mem)
{
    // Simple allocator doesn't support free
    // In real embedded system, might use a more sophisticated allocator
    (void)mem;
}

/* System timer implementation for QEMU */
static uint64_t start_time = 0;

uint32_t mpix_port_get_uptime_us(void)
{
    // Use DWT cycle counter if available, otherwise fallback to simple counter
    #ifdef DWT
    if (start_time == 0) {
        start_time = DWT->CYCCNT;
    }
    // Assume 100 MHz CPU clock for QEMU Cortex-M55
    uint64_t cycles = DWT->CYCCNT - start_time;
    return (uint32_t)(cycles / 100); // Convert to microseconds
    #else
    // Fallback implementation for QEMU without DWT
    static uint32_t counter = 0;
    return ++counter * 1000; // Simple incrementing microseconds
    #endif
}

void mpix_port_printf(const char *fmt, ...)
{
    va_list ap;
    
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fflush(stdout);
}

/* Dummy implementation for camera exposure control */
int mpix_port_init_exposure(void *dev, int32_t *def, int32_t *max)
{
    (void)dev;
    if (def) *def = 1000;  // Default exposure
    if (max) *max = 10000; // Maximum exposure
    return 0;
}

int mpix_port_set_exposure(void *dev, int32_t val)
{
    (void)dev;
    (void)val;
    return 0;
}

/* Memory utilities for ARM Cortex-M55 */
void mpix_port_memory_info(void)
{
    printf("QEMU Cortex-M55 Memory Info:\n");
    printf("  Heap size: %u bytes\n", MPIX_HEAP_SIZE);
    printf("  Heap used: %zu bytes\n", heap_offset);
    printf("  Heap free: %zu bytes\n", MPIX_HEAP_SIZE - heap_offset);
    
    #ifdef __ARM_FEATURE_MVE
    printf("  ARM Helium MVE: Available\n");
    #else
    printf("  ARM Helium MVE: Not available\n");
    #endif
}

/* System initialization for QEMU environment */
void mpix_port_init(void)
{
    // Initialize heap
    heap_offset = 0;
    memset(heap_memory, 0, MPIX_HEAP_SIZE);
    
    // Enable DWT cycle counter if available
    #ifdef DWT
    if (!(DWT->CTRL & DWT_CTRL_CYCCNTENA_Msk)) {
        DWT->CYCCNT = 0;
        DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    }
    #endif
    
    printf("libmpix QEMU Cortex-M55 port initialized\n");
    mpix_port_memory_info();
}
