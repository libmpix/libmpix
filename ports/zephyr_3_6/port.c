/* SPDX-License-Identifier: Apache-2.0 */

#include <stdarg.h>
#include <stdio.h>

#include <zephyr/kernel.h>
#include <zephyr/drivers/video.h>
#include <zephyr/drivers/video-controls.h>

#include <mpix/port.h>

K_HEAP_DEFINE(mpix_heap, CONFIG_MPIX_HEAP_SIZE);

void *mpix_port_alloc(size_t size, enum mpix_mem_source mem_source)
{
	(void)mem_source;
	return k_heap_alloc(&mpix_heap, size, K_NO_WAIT);
}

void mpix_port_free(void *mem, enum mpix_mem_source mem_source)
{
	(void)mem_source;
	return k_heap_free(&mpix_heap, mem);
}

uint32_t mpix_port_get_uptime_us(void)
{
	return k_cycle_get_64() * 1000 * 1000 / sys_clock_hw_cycles_per_sec();
}

void mpix_port_printf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vprintf(fmt, ap);
	va_end(ap);
}
