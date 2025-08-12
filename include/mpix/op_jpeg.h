/**
 * SPDX-License-Identifier: Apache-2.0
 * @defgroup mpix_op_jpeg_h mpix/op_jpeg.h
 * @brief Low-level JPEG encoding operations [WORK-IN-PROGRESS]
 * @{
 */
#ifndef MPIX_JPEG_H
#define MPIX_JPEG_H

#include <stdlib.h>
#include <stdint.h>

#include <mpix/op.h>
#include <JPEGENC.h>

/**
 * JPEG conversion operation
 * @internal
 */
struct mpix_jpeg_op {
	/** Fields common to all operations. */
	struct mpix_base_op base;

	/** Image context from the JPEGENC library */
	JPEGE_IMAGE image;

	/** Image encoder context from the JPEGENC library */
	JPEGENCODE encoder;
};

/**
 * JPEG quality levels use while encoding an image.
 */
enum mpix_jpeg_quality {
	/** Default quality, placeholder until configurable quality is supported */
	MPIX_JPEG_QUALITY_DEFAULT,
};

/**
 * @brief Define a new encoding operation: from a pixel format to the JPEG format.
 *
 * @param id Short identifier to differentiate operations of the same category.
 * @param op Operation reading input 8 lines at a time.
 * @param fmt_src The source format for that operation.
 * @param fmt_dst The destination format for that operation.
 */
#define MPIX_REGISTER_JPEG_OP(id, op, fmt_src, fmt_dst)                                            \
	const struct mpix_jpeg_op mpix_jpeg_op_##id = {                                            \
		.base.name = ("jpeg_" #id),                                                        \
		.base.fourcc_src = (MPIX_FMT_##fmt_src),                                           \
		.base.fourcc_dst = (MPIX_FMT_##fmt_dst),                                           \
		.base.window_size = 8,                                                             \
		.base.run = (op),                                                                  \
	}

#endif /** @} */
