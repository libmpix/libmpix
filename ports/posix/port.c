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

void *mpix_port_alloc(size_t size)
{
	return malloc(size);
}

void mpix_port_free(void *mem)
{
	free(mem);
}
