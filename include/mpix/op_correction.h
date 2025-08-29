/**
 * SPDX-License-Identifier: Apache-2.0
 * @defgroup mpix_op_correction_h mpix/op_correction.h
 * @brief Low-level image correction operations
 * @{
 */
#ifndef MPIX_OP_CORRECTION_H
#define MPIX_OP_CORRECTION_H

#include <stdint.h>
#include <stdlib.h>

#include <mpix/op.h>

/**
 * @brief Amount of scaling applied to the red_level and blue_level values.
 */
#define MPIX_CORRECTION_SCALE_BITS 10

/**
 * @brief Balance between the red and green value, or blue and green value
 */
struct mpix_correction_white_balance {
	/** Red value correction level multiplied by 1024: 2048 applies 2x to red channe */
	uint16_t red_level;
	/** Blue value correction level multiplied by 1024: 2048 applies 2x to blue channel */
	uint16_t blue_level;
};

/**
 * @brief Color calibration obtained from a photo of a Color Checker pattern or equivalent
 */
struct mpix_correction_color_matrix {
	/** 3x3 array with values obtained by calibration */
	int16_t levels[9];
};

/**
 * @brief Gamma value affecting the strength of the gamma level
 */
struct mpix_correction_gamma {
	/** Min value 1 for gamma=1/16. Max value 15  for gamma=15/16. */
	uint8_t level;
};

/**
 * @brief Offset removed to every pixel
 */
struct mpix_correction_black_level {
	/** 0 for no correction, 255 for dark image. */
	uint8_t level;
};

/**
 * @brief Aggregation of all possible correction types
 */
struct mpix_correction_all {
	/** Storage for the white balance controls */
	struct mpix_correction_white_balance white_balance;
	/** Storage for the color correction matrix controls */
	struct mpix_correction_color_matrix color_matrix;
	/** Storage for the gamma correction controls */
	struct mpix_correction_gamma gamma;
	/** Storage for the black level correction controls */
	struct mpix_correction_black_level black_level;
};

/**
 * @brief Selection of any possible correction types
 */
union mpix_correction_any {
	/** Option for the white balance controls */
	struct mpix_correction_white_balance white_balance;
	/** Option for the color correction matrix controls */
	struct mpix_correction_color_matrix color_matrix;
	/** Option for the gamma correction controls */
	struct mpix_correction_gamma gamma;
	/** Option for the black level correction controls */
	struct mpix_correction_black_level black_level;
};

/**
 * @brief Correction types that can be applied to the image
 */
enum mpix_correction_type {
	/** Correct the black level applied to every pixel */
	MPIX_CORRECTION_BLACK_LEVEL,
	/** Apply white balance to control the red and blue channels gain (green unchanged) */
	MPIX_CORRECTION_WHITE_BALANCE,
	/** Apply gamma correction to non-linearly increase the brightness of the image */
	MPIX_CORRECTION_GAMMA,
	/** Apply color correction to every pixel*/
	MPIX_CORRECTION_COLOR_MATRIX,
};

/**
 * @brief Image correction operation
 * @internal
 */
struct mpix_correction_op {
	/** Fields common to all operations. */
	struct mpix_base_op base;
	/** Type of correction operation */
	enum mpix_correction_type type;
	/** Function to convert one line of pixels */
	void (*correction_fn)(const uint8_t *src, uint8_t *dst, uint16_t width,
			      uint16_t line_offset, union mpix_correction_any *corr);
	/** Storage for the control level, when when a runtime control needed */
	union mpix_correction_any correction;
};

/**
 * @brief Define a new Correction operation.
 *
 * @param id Short identifier to differentiate operations of the same type.
 * @param fn Function converting 3 input lines into 1 output line.
 * @param t Correction operation type from @ref mpix_correction_type
 * @param fmt The input format for that operation.
 */
#define MPIX_REGISTER_CORRECTION_OP(id, fn, t, fmt)                                                \
	const struct mpix_correction_op mpix_correction_op_##id = {                                \
		.base.name = ("correction_" #id),                                                  \
		.base.fourcc_src = (MPIX_FMT_##fmt),                                               \
		.base.fourcc_dst = (MPIX_FMT_##fmt),                                               \
		.base.window_size = 1,                                                             \
		.base.run = mpix_correction_op,                                                    \
		.correction_fn = (fn),                                                             \
		.type = (MPIX_CORRECTION_##t),                                                     \
	}

/**
 * @brief Correct the black level of an input line in any 8-bit RGB or grayscale pixel format.
 *
 * @param src Input buffers to convert.
 * @param dst Output line buffer receiving the conversion result.
 * @param width Width of the input and output lines in pixels.
 * @param line_offset Number of the line being processed within the frame.
 * @param corr Correction levels to apply to the image.
 */
void mpix_correction_black_level_raw8(const uint8_t *src, uint8_t *dst, uint16_t width,
				      uint16_t line_offset, union mpix_correction_any *corr);
/**
 * @brief Correct the black level of an input line in RGB24 pixel format.
 * @copydetails mpix_correction_black_level_raw8
 */
void mpix_correction_black_level_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width,
				       uint16_t line_offset, union mpix_correction_any *corr);
/**
 * @brief Correct the white balance of an input line in RGB24 pixel format.
 * @copydetails mpix_correction_black_level_raw8
 */
void mpix_correction_white_balance_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width,
					 uint16_t line_offset, union mpix_correction_any *corr);

/**
 * @brief Perform color correction of an input line in RGB24 pixel format.
 * @copydetails mpix_correction_black_level_raw8
 */
void mpix_correction_color_matrix_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width,
					uint16_t line_offset, union mpix_correction_any *corr);

/**
 * @brief Fused one-pass correction on RGB24: black-level -> white-balance -> 3x3 matrix -> gamma.
 *        Uses SoA-in-registers with MVE gather/scatter for maximal throughput on Cortex-M55.
 *        This is a convenience API; it doesn't participate in the op registration pipeline.
 */
void mpix_correction_fused_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width,
								 uint16_t line_offset, const struct mpix_correction_all *corr);
/**
 * @brief Helper to simplify the implementation of a image correction operation.
 *
 * @internal
 *
 * @param base Base operation type, casted to @ref mpix_correction_op.
 */
void mpix_correction_op(struct mpix_base_op *base);

#endif /** @} */
