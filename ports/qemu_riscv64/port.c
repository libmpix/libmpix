/* SPDX-License-Identifier: Apache-2.0 */

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <time.h>

#include <mpix/port.h>

#ifndef MPIX_HEAP_SIZE
#define MPIX_HEAP_SIZE (64 * 1024)
#endif

/** TIMER REGISTERS */
#define RISCV_MTIME_ADDR    (0x2000000 + 0xBFF8)

static uint8_t heap_memory[MPIX_HEAP_SIZE] __attribute((aligned(8)));
static size_t heap_offset = 0;

void *mpix_port_alloc(size_t size, enum mpix_mem_source mem_source)
{
	(void)mem_source;

	size = (size + 7) & ~7;

	if (heap_offset + size > MPIX_HEAP_SIZE) {
		return NULL;
	}

	void *ptr = &heap_memory[heap_offset];
	heap_offset += size;
	return ptr;
}

void mpix_port_free(void *mem, enum mpix_mem_source mem_source)
{
	(void)mem_source;
	// Simple allocator doesn't support free
	// In real embedded system, might use a more sophisticated allocator
	(void)mem;
}

uint32_t mpix_port_get_uptime_us(void)
{
	return (*(volatile uint64_t *)RISCV_MTIME_ADDR);
}

void mpix_port_memory_info(void)
{
	printf("QEMU RISCV Memory Info:\n");
	printf("  Heap size: %u bytes\n", MPIX_HEAP_SIZE);
	printf("  Heap used: %zu bytes\n", heap_offset);
	printf("  Heap free: %zu bytes\n", MPIX_HEAP_SIZE - heap_offset);

#ifdef __riscv_vector
	printf("  RISCV Vector Extension: Available\n");
#else
	printf("  RISCV Vector Extension: Not available\n");
#endif
}

void mpix_port_init(void)
{
	heap_offset = 0;
	memset(heap_memory, 0, MPIX_HEAP_SIZE);

	printf("libmpix QEMU RISCV port initialized\n");
	mpix_port_memory_info();
}

void mpix_port_printf(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
	fflush(stdout);
}
