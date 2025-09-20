/**
 * SPDX-License-Identifier: Apache-2.0
 * @defgroup mpix_sample_h mpix/sample.h
 * @brief Sampling of image pixels
 * @{
 */
#ifndef MPIX_SAMPLE_H
#define MPIX_SAMPLE_H

#include <stdint.h>
#include <stddef.h>

#include <mpix/types.h>

/**
 * @brief Collect a pixel at a random location from the input buffer in RGB24 format.
 *
 * @param buf Buffer of pixels from which collect the sample.
 * @param fmt Resolution of the source and destination, and format of the destination.
 * @param dst Buffer for one pixel filled by this function.
 * @return 0 on success or negative error code.
 */
int mpix_sample_random_rgb(const uint8_t *buf, const struct mpix_format *fmt, uint8_t *dst);

#endif /** @} */
