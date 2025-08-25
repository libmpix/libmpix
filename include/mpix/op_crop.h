/**
 * SPDX-License-Identifier: Apache-2.0
 * @defgroup mpix_op_crop_h mpix/op_crop.h
 * @brief Low-level cropping operations
 * @{
 */
#ifndef MPIX_OP_CROP_H
#define MPIX_OP_CROP_H

#include <stdint.h>

#include <mpix/op.h>

/**
 * Image cropping operation.
 * @internal
 */
struct mpix_crop_op {
	/** Fields common to all operations. */
	struct mpix_base_op base;
	/** X offset of the crop region */
	uint16_t x_offset;
	/** Y offset of the crop region */
	uint16_t y_offset;
	/** Width of the crop region */
	uint16_t crop_width;
	/** Height of the crop region */
	uint16_t crop_height;
};

/**
 * @brief Define a crop operation.
 *
 * Invoking this macro suffices for @ref mpix_image_crop() to include the extra format.
 *
 * @param id Short identifier to differentiate operations of the same type.
 * @param fn Function converting one input line.
 * @param fmt The pixel format of the data cropped.
 */
#define MPIX_REGISTER_CROP_OP(id, fn, fmt)                                                         \
	const struct mpix_crop_op mpix_crop_op_##id = {                                            \
		.base.name = "crop_" #id,                                                          \
		.base.fourcc_src = MPIX_FMT_##fmt,                                                 \
		.base.fourcc_dst = MPIX_FMT_##fmt,                                                 \
		.base.window_size = 1,                                                             \
		.base.run = fn,                                                                    \
	}

/**
 * @brief Crop a 24-bit per pixel frame.
 *
 * @param src_buf Input buffer to crop
 * @param src_width Width of the input in number of pixels.
 * @param src_height Height of the input in number of pixels.
 * @param dst_buf Output buffer in which the cropped image is stored.
 * @param x_offset X coordinate of the top-left corner of the crop region.
 * @param y_offset Y coordinate of the top-left corner of the crop region.
 * @param crop_width Width of the crop region in pixels.
 * @param crop_height Height of the crop region in pixels.
 */
void mpix_crop_frame_raw24(const uint8_t *src_buf, size_t src_width, size_t src_height,
			   uint8_t *dst_buf, size_t x_offset, size_t y_offset,
			   size_t crop_width, size_t crop_height);

/**
 * @brief Crop a 16-bit per pixel frame.
 * @copydetails mpix_crop_frame_raw24()
 */
void mpix_crop_frame_raw16(const uint8_t *src_buf, size_t src_width, size_t src_height,
			   uint8_t *dst_buf, size_t x_offset, size_t y_offset,
			   size_t crop_width, size_t crop_height);

/**
 * @brief Crop a 8-bit per pixel frame.
 * @copydetails mpix_crop_frame_raw24()
 */
void mpix_crop_frame_raw8(const uint8_t *src_buf, size_t src_width, size_t src_height,
			  uint8_t *dst_buf, size_t x_offset, size_t y_offset,
			  size_t crop_width, size_t crop_height);

#endif /** @} */
