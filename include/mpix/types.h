/**
 * SPDX-License-Identifier: Apache-2.0
 * @defgroup mpix_types_h mpix/types.h
 * @brief All of libmpix type definitions.
 * @{
 */

#ifndef MPIX_TYPES_H
#define MPIX_TYPES_H

#include <stdint.h>
#include <stddef.h>

#include <mpix/genlist.h>

/**
 * @brief MPIX operation type identifying an operation family.
 *
 * Each type can further be using its own methods for identifying the particular operation.
 */
enum mpix_op_type {
#define MPIX_OP_ENUM(X, x) \
	MPIX_OP_##X,
MPIX_FOR_EACH_OP(MPIX_OP_ENUM)
	MPIX_OP_END,
	MPIX_NB_OP,
};

/** JPEG image quality */
enum mpix_jpeg_quality {
	/** Default JPEG quality level of the library */
	MPIX_JPEG_QUALITY_DEFAULT,
	/** Number of possible JPEG qualities */
	MPIX_NB_JPEG_QUALITY,
};

/** Type of convolution kernel */
enum mpix_kernel_type {
	/** Turn the high contrast region in white, and low-contrast in black */
	MPIX_KERNEL_EDGE_DETECT,
	/** Apply a blur to the image, the intensity depends on the kernel size */
	MPIX_KERNEL_GAUSSIAN_BLUR,
	/** No modification to the image at all, useful for testing purpose */
	MPIX_KERNEL_IDENTITY,
	/** Accentuate the contrasted region of the image */
	MPIX_KERNEL_SHARPEN,
	/* Total number of kernels */
	MPIX_NB_KERNEL,
};

/** Control identifiers to select which parameter of the ISP to tune */
enum mpix_control_id {
	/** Value in [0..255] range to subtract to every pixel to compensate the sensor offset */
	MPIX_CID_BLACK_LEVEL,
	/** Gamma value (in Q.10) to use to gamma-encode the image */
	MPIX_CID_GAMMA_LEVEL,
	/** Correction level (in Q.10) to apply to the red pixels as opposed to green pixels */
	MPIX_CID_RED_BALANCE,
	/** Correction level (in Q.10) to apply to the blue pixels as opposed to green pixels */
	MPIX_CID_BLUE_BALANCE,
	/** JPEG quality level (enum) to use while encoding/decoding images */
	MPIX_CID_JPEG_QUALITY,
	/** Color correction matrix coefficient (in Q.10): red effect on red */
	MPIX_CID_COLOR_MATRIX_0,
	/** Color correction matrix coefficient (in Q.10): green effect on red */
	MPIX_CID_COLOR_MATRIX_1,
	/** Color correction matrix coefficient (in Q.10): blue effect on red */
	MPIX_CID_COLOR_MATRIX_2,
	/** Color correction matrix coefficient (in Q.10): red effect on green */
	MPIX_CID_COLOR_MATRIX_3,
	/** Color correction matrix coefficient (in Q.10): green effect on green */
	MPIX_CID_COLOR_MATRIX_4,
	/** Color correction matrix coefficient (in Q.10): blue effect on green */
	MPIX_CID_COLOR_MATRIX_5,
	/** Color correction matrix coefficient (in Q.10): red effect on blue */
	MPIX_CID_COLOR_MATRIX_6,
	/** Color correction matrix coefficient (in Q.10): green effect on blue */
	MPIX_CID_COLOR_MATRIX_7,
	/** Color correction matrix coefficient (in Q.10): blue effect on blue */
	MPIX_CID_COLOR_MATRIX_8,
	/** Total number of control IDs */
	MPIX_NB_CID,
};

/**
 * @brief Flag to specify which memory pool to use
 */
enum mpix_mem_source {
	/** Default/internal heap */
	MPIX_MEM_SOURCE_DEFAULT,
	/** External user-provided memory (not freed by libmpix) */
	MPIX_MEM_SOURCE_USER,
	/** Custom memory types for user code definitions */
	MPIX_MEM_SOURCE_CUSTOM0,
};

/**
 * @brief Image format description.
 */
struct mpix_format {
	/** Four Character Code, describing the pixel format */
	uint32_t fourcc;
	/** Frame width, in number of pixel */
	uint16_t width;
	/** Frame height, in number of pixel */
	uint16_t height;
};

/**
 * @brief Ring buffer of pixels
 *
 * Store the data betwen a previous operation and the next operation.
 */
struct mpix_ring {
	/** Pointer to the buffer that stores the data */
	uint8_t *buffer;
	/** Total size of the buffer */
	size_t size;
	/** Position of the writing head where data is inserted */
	size_t head;
	/** Position of the reading tail where data is read and removed */
	size_t tail;
	/** Position of the peeking tail where data is read ahead of the tail */
	size_t peek;
	/** Memory source of allocated buffer */
	enum mpix_mem_source mem_source;
	/** Flag to tell apart between full and empty when head == tail */
	uint8_t full : 1;
};

/**
 * @brief One step of a line operation pipeline
 *
 * @c mpix_base_op structs are chained together into a linked list.
 * Each step of the linked list contain a ring buffer for the input data, and a pointer to a
 * conversion function processing it.
 * Along with extra metadata, this is used to process data as a operation of lines.
 */
struct mpix_base_op {
	/** Linked-list entry */
	struct mpix_base_op *next;
	/** Type of the operation for selecting what function to run */
	enum mpix_op_type type;
	/** Input format of the operation: fourcc and resolution */
	struct mpix_format fmt;
	/** Current position within the frame */
	uint16_t line_offset;
	/** Ring buffer with the input data to process */
	struct mpix_ring ring;
	/** Timestamp since the op started working in CPU cycles */
	uint32_t start_time_us;
	/** Total time spent working in this op through the operation in CPU cycles */
	uint32_t total_time_us;
};

/**
 * @brief Represent the image currently being processed
 *
 * When adding operations to an image, the buffer is not converted yet.
 *
 * The struct fields are meant to reflect the buffer after it got converted, so after adding
 * operations, there might be a mismatch between the data format of the buffer and the .
 */
struct mpix_image {
	/** First element of the linked list of operations */
	struct mpix_base_op *first_op;
	/** Last element of of the linked list of operations */
	struct mpix_base_op *last_op;
	/** Input buffer used as source for the conversion */
	const uint8_t *buffer;
	/** Size of the input buffer */
	size_t size;
	/** Image format of the latest operation or input buffer: fourcc and resolution */
	struct mpix_format fmt;
	/** Array of controls provided by the operations, or NULL if none is defined */
	int32_t *ctrls[MPIX_NB_CID];
	/** Allocation source of memory */
	enum mpix_mem_source mem_source;
};

/** Entry of a table mapping strings to integer values, useful for macros and enums */
struct mpix_str {
	/** The string identifier */
	const char *name;
	/** The value identifier for this name */
	uint32_t value;
};

/** Color palette for encoding/decoding an RGB24 image from/to an indexed color image format */
struct mpix_palette {
	/** Color value where the position in the array (in pixel number) is the color inxdex. */
	uint8_t colors_rgb24[3 << 8];
	/** Format of the indexed colors, defining the palette size. */
	uint32_t fourcc;
};

/** Statistics generated by libmpix */
struct mpix_stats {
	/** Storage for the values. One buffer per channel. Maximum number of channels is 3. */
	uint16_t y_histogram[64];
	/** Average value for each histogram bin. */
	uint8_t y_histogram_vals[64];
	/** Total value for each histogram bin. */
	uint16_t y_histogram_total;
	/** Average pixel value */
	uint8_t rgb_average[3];
	/** Minimum of each channel */
	uint8_t rgb_min[3];
	/** Maximum of each channel */
	uint8_t rgb_max[3];
	/** Number of values collected for building these statistics. */
	uint16_t nvals;
};

/** Parameters controlling/controleld by auto-correction */
struct mpix_auto_ctrls {
	/** (input) target luma (0-255) for AE */
	uint8_t ae_target;
	/** Maximum sensor exposure value */
	int32_t exposure_max;
	/** (Output) Current sensor exposure value */
	int32_t exposure_level;
	/** (Output) Black Level Correction (BLC) */
	int32_t black_level;
	/** (Output) red balance component of AWB */
	int32_t red_balance_q10;
	/** (Output) blue balance component of AWB */
	int32_t blue_balance_q10;
};

#endif /** @} */
