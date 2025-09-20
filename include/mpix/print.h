/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @defgroup mpix_print_h mpix/print.h
 * @brief Print images and statistics
 * @{
 */
#ifndef MPIX_PRINT_H
#define MPIX_PRINT_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include <mpix/types.h>

/**
 * @brief Print a buffer using terminal escape codes.
 * @param buf Imagme buffer to display in the terminal.
 * @param size Size of the buffer in bytes.
 * @param fmt Image format of the buffer.
 * @param truecolor Use higher quality but slower TRUECOLOR instead of @c 256COLOR escape codes.
 */
void mpix_print_buf(const uint8_t *src, size_t size, const struct mpix_format *fmt, bool truecolor);

/**
 * @brief Print 2 rows of pixels using terminal escape codes.
 * @param top Top row of pixels to print.
 * @param bot Bottom row of pixels to print.
 * @param width Number of bytes to print
 * @param fourcc Pixel format of the @p top and @p bot rows of pixels.
 * @param truecolor Use higher quality but slower TRUECOLOR instead of @c 256COLOR escape codes.
 */
void mpix_print_2_rows(const uint8_t *top, const uint8_t *bot, int16_t width, uint32_t fourcc,
		       bool truecolor);

/**
 * @brief Hexdump a buffer in the specified format.
 * @param buf Input buffer to display in the terminal.
 * @param size Size of the input buffer in bytes.
 * @param fmt Image format of the buffer
 */
void mpix_hexdump_buf(const uint8_t *buf, size_t size, const struct mpix_format *fmt);

/**
 * @brief Hexdump a byte buffer
 * @param buf Input buffer to hexdump in the terminal.
 * @param size Size of the input buffer in bytes.
 */
void mpix_hexdump_raw(const uint8_t *buf, size_t size);

/**
 * @brief Printing RGB histograms to the terminal.
 * @param r_hist Buckets for the red channel.
 * @param g_hist Buckets for the green channel.
 * @param b_hist Buckets for the blue channel.
 * @param size Total number of buckets in total contained within @p rgb24hist all channels included.
 * @param height Desired height of the chart in pixels.
 */
void mpix_print_rgb_hist(const uint16_t *r_hist, const uint16_t *g_hist, const uint16_t *b_hist,
			 size_t size, uint16_t height);

/**
 * @brief Printing Y histograms to the terminal.
 * @param y8hist Buffer storing the histogram for the Y (luma) channel.
 * @param size Total number of buckets in total contained within @p hist.
 * @param height Desired height of the chart in pixels.
 */
void mpix_print_y_hist(const uint16_t *y8hist, size_t size, uint16_t height);

/**
 * @brief Print details about every operation of a pipeline
 * @param op First operation of the pipeline
 */
void mpix_print_pipeline(struct mpix_base_op *op);

/**
 * @brief Print details about a signle operation
 * @param op Operation to print
 */
void mpix_print_op(struct mpix_base_op *op);

/**
 * @brief Print a representation of the statistics in the console for debug purpose.
 *
 * @param stats The statistics to print out.
 */
void mpix_print_stats(struct mpix_stats *stats);

/**
 * @brief Print a summary of available controls an their value
 *
 * @param ctrls Array of control to print
 */
void mpix_print_ctrls(int32_t *ctrls[]);

#endif /** @} */
