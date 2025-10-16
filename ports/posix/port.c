/* SPDX-License-Identifier: Apache-2.0 */

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include <mpix/port.h>

uint32_t mpix_port_get_uptime_us(void)
{
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1000 * 1000 + ts.tv_nsec / 1000;
}

void *mpix_port_alloc(size_t size, enum mpix_mem_source mem_source)
{
	(void)mem_source;
	return malloc(size);
}

void mpix_port_free(void *mem, enum mpix_mem_source mem_source)
{
	(void)mem_source;
	free(mem);
}

void mpix_port_printf(const char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}
