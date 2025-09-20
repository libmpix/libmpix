/**
 * SPDX-License-Identifier: Apache-2.0
 * @defgroup mpix_port_h mpix/port.h
 * @brief Low-level functions to implement new libmpix ports
 *
 * @{
 */
#ifndef MPIX_PORT_H
#define MPIX_PORT_H

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Get the uptime in microsecond, used to compute performance statistics
 *
 * Counter overflows are not fatal to the system as this is only for benchmarking purpose.
 *
 * @return The 32-bit counter of microsecond since boot
 */
uint32_t mpix_port_get_uptime_us(void);

/**
 * @brief Allocate a buffer to use with libmpix
 *
 * This will be used to allocate the small intermediate buffers present between the operations.
 *
 * @param size Number of bytes available in the buffer
 * @return The pointer to the new buffer or NULL if allocation failed.
 */
void *mpix_port_alloc(size_t size);

/**
 * @brief Free a buffer allocated with @ref mpix_port_alloc().
 *
 * This will be used to free the buffers that were allocated after all processing.
 *
 * @param mem Pointer to the buffer to free.
 */
void mpix_port_free(void *mem);

/**
 * @brief Initialize the exposure control with min/max values and current value.
 *
 * @param dev Device on which apply the exposure, as a port-specific type.
 * @param def Default exposure level, set by querying the device for its default.
 * @param max Maximum exposure level, set by querying the device for its maximum.
 * @return 0 on success or negative error code.
 */
int mpix_port_init_exposure(void *dev, int32_t *def, int32_t *max);

/**
 * @brief Write the exposure level of the device.
 *
 * @param dev Device for which to set the exposure level.
 * @param val The new exposure value.
 * @return 0 on success or negative error code.
 */
int mpix_port_set_exposure(void *dev, int32_t val);

#endif /** @} */
