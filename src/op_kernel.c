/* SPDX-License-Identifier: Apache-2.0 */

#include <assert.h>
#include <errno.h>
#include <stdint.h>

#include <mpix/genlist.h>
#include <mpix/image.h>
#include <mpix/op_kernel.h>

/* Function that processes a 3x3 or 5x5 pixel block described by line buffers and column indexes */
typedef void kernel_3x3_t(const uint8_t *in[3], int i0, int i1, int i2,
			  uint8_t *out, int o0, uint16_t base, const int16_t *kernel);
typedef void kernel_5x5_t(const uint8_t *in[3], int i0, int i1, int i2, int i3, int i4,
			  uint8_t *out, int o0, uint16_t base, const int16_t *kernel);

/* Function that repeats a 3x3 or 5x5 block operation to each channel of a pixel format */
typedef void pixformat_3x3_t(const uint8_t *in[3], int i0, int i1, int i2,
			  uint8_t *out, int o0, uint16_t base, kernel_3x3_t *line_fn,
			  const int16_t *kernel);
typedef void pixformat_5x5_t(const uint8_t *in[5], int i0, int i1, int i2, int i3, int i4,
			  uint8_t *out, int o0, uint16_t base, kernel_5x5_t *line_fn,
			  const int16_t *kernel);

/* Function that repeats a 3x3 or 5x5 kernel operation over an entire line line */
typedef void line_3x3_t(const uint8_t *in[3], uint8_t *out, uint16_t width);
typedef void line_5x5_t(const uint8_t *in[5], uint8_t *out, uint16_t width);

/*
 * Convolution kernels: multiply a grid of coefficient with the input data and um them to produce
 * one output value.
 */

static void mpix_convolve_3x3(const uint8_t *in[3], int i0, int i1, int i2, uint8_t *out, int o0,
			      uint16_t base, const int16_t *kernel)
{
	int32_t result = 0;
	int k = 0;

	/* Apply the coefficients on 3 rows */
	for (int h = 0; h < 3; h++) {
		/* Apply the coefficients on 5 columns */
		result += in[h][base + i0] * kernel[k++]; /* line h column 0 */
		result += in[h][base + i1] * kernel[k++]; /* line h column 1 */
		result += in[h][base + i2] * kernel[k++]; /* line h column 2 */
	}

	/* Store the scaled-down output */
	result >>= kernel[k];
	out[base + o0] = CLAMP(result, 0x00, 0xff);
}

static void mpix_convolve_5x5(const uint8_t *in[5], int i0, int i1, int i2, int i3, int i4,
			      uint8_t *out, int o0, uint16_t base, const int16_t *kernel)
{
	int32_t result = 0;
	int k = 0;

	/* Apply the coefficients on 5 rows */
	for (int h = 0; h < 5; h++) {
		/* Apply the coefficients on 5 columns */
		result += in[h][base + i0] * kernel[k++]; /* line h column 0 */
		result += in[h][base + i1] * kernel[k++]; /* line h column 1 */
		result += in[h][base + i2] * kernel[k++]; /* line h column 2 */
		result += in[h][base + i3] * kernel[k++]; /* line h column 3 */
		result += in[h][base + i4] * kernel[k++]; /* line h column 4 */
	}

	/* Store the scaled-down output */
	result >>= kernel[k];
	out[base + o0] = CLAMP(result, 0x00, 0xff);
}

/*
 * Median kernels: find the median value of the input block and send it as output. The effect is to
 * denoise the input image while preserving sharpness of the large color regions.
 */

static inline uint8_t mpix_median(const uint8_t **in, int *idx, uint8_t size)
{
	uint8_t pivot_bot = 0x00;
	uint8_t pivot_top = 0xff;
	uint8_t num_higher;
	int16_t median;

	/* Binary-search of the appropriate median value, 8 steps for 8-bit depth */
	for (int i = 0; i < 8; i++) {
		num_higher = 0;
		median = (pivot_top + pivot_bot) / 2;

		for (uint16_t h = 0; h < size; h++) {
			for (uint16_t w = 0; w < size; w++) {
				num_higher += in[h][idx[w]] > median; /* line h column w */
			}
		}

		if (num_higher > size * size / 2) {
			pivot_bot = median;
		} else if (num_higher < size * size / 2) {
			pivot_top = median;
		}
	}

	/* Output the median value */
	return (pivot_top + pivot_bot) / 2;
}

static void mpix_median_3x3(const uint8_t *in[3], int i0, int i1, int i2, uint8_t *out, int o0,
			    uint16_t base, const int16_t *unused)
{
	int idx[] = {base + i0, base + i1, base + i2};

	out[base + o0] = mpix_median(in, idx, 3);
}

static void mpix_median_5x5(const uint8_t *in[5], int i0, int i1, int i2, int i3, int i4,
			    uint8_t *out, int o0, uint16_t base, const int16_t *unused)
{
	int idx[] = {base + i0, base + i1, base + i2, base + i3, base + i4};

	out[base + o0] = mpix_median(in, idx, 5);
}

/*
 * Convert pixel offsets into byte offset, and repeat a kernel function for every channel of a
 * pixel format.
 */

static void mpix_kernel_rgb24_3x3(const uint8_t *in[3], int i0, int i1, int i2, uint8_t *out,
				  int o0, uint16_t base, kernel_3x3_t *line_fn,
				  const int16_t *kernel)
{
	i0 *= 3, i1 *= 3, i2 *= 3, o0 *= 3, base *= 3;
	line_fn(in, i0, i1, i2, out, o0, base + 0, kernel); /* R */
	line_fn(in, i0, i1, i2, out, o0, base + 1, kernel); /* G */
	line_fn(in, i0, i1, i2, out, o0, base + 2, kernel); /* B */
}

static void mpix_kernel_rgb24_5x5(const uint8_t *in[5], int i0, int i1, int i2, int i3, int i4,
				  uint8_t *out, int o0, uint16_t base, kernel_5x5_t *line_fn,
				  const int16_t *kernel)
{
	i0 *= 3, i1 *= 3, i2 *= 3, i3 *= 3, i4 *= 3, o0 *= 3, base *= 3;
	line_fn(in, i0, i1, i2, i3, i4, out, o0, base + 0, kernel); /* R */
	line_fn(in, i0, i1, i2, i3, i4, out, o0, base + 1, kernel); /* G */
	line_fn(in, i0, i1, i2, i3, i4, out, o0, base + 2, kernel); /* B */
}

/*
 * Portable/default C implementation of line processing functions. They are inlined into
 * line-conversion functions at the bottom of this file declared as weak aliases.
 */

static inline void mpix_kernel_3x3(const uint8_t *in[3], uint8_t *out, uint16_t width,
				   pixformat_3x3_t *pixformat_fn, kernel_3x3_t *line_fn,
				   const int16_t *kernel)
{
	uint16_t w = 0;

	/* Edge case on first two columns */
	pixformat_fn(in, 0, 0, 1, out, 0, w + 0, line_fn, kernel);

	/* process the entire line except the first two and last two columns (edge cases) */
	for (w = 0; w + 3 <= width; w++) {
		pixformat_fn(in, 0, 1, 2, out, 1, w, line_fn, kernel);
	}

	/* Edge case on last two columns */
	pixformat_fn(in, 0, 1, 1, out, 1, w, line_fn, kernel);
}

static inline void mpix_kernel_5x5(const uint8_t *in[5], uint8_t *out, uint16_t width,
				   pixformat_5x5_t *pixformat_fn, kernel_5x5_t *line_fn,
				   const int16_t *kernel)
{
	uint16_t w = 0;

	/* Edge case on first two columns, repeat the left column to fill the blank */
	pixformat_fn(in, 0, 0, 0, 1, 2, out, 0, w, line_fn, kernel);
	pixformat_fn(in, 0, 0, 1, 2, 3, out, 1, w, line_fn, kernel);

	/* process the entire line except the first two and last two columns (edge cases) */
	for (w = 0; w + 5 <= width; w++) {
		pixformat_fn(in, 0, 1, 2, 3, 4, out, 2, w, line_fn, kernel);
	}

	/* Edge case on last two columns, repeat the right column to fill the blank */
	pixformat_fn(in, 0, 1, 2, 3, 3, out, 2, w, line_fn, kernel);
	pixformat_fn(in, 1, 2, 3, 3, 3, out, 3, w, line_fn, kernel);
}

/*
 * Call a line-processing function on every line, handling the edge-cases on first line and last
 * line by repeating the lines at the edge to fill the gaps.
 */

void mpix_kernel_3x3_op(struct mpix_base_op *base)
{
	struct mpix_kernel_op *op = (void *)base;
	uint16_t prev_line_offset = base->line_offset;
	const uint8_t *src[] = {
		mpix_op_get_input_line(base),
		mpix_op_peek_input_line(base),
		mpix_op_peek_input_line(base),
	};

	assert(base->width >= 3);
	assert(base->height >= 3);

	/* Allow overflowing before the top by repeating the first line */
	if (prev_line_offset == 0) {
		const uint8_t *top[] = {src[0], src[0], src[1]};

		op->kernel_fn(top, mpix_op_get_output_line(base), base->width);
		mpix_op_done(base);
	}

	/* Process one more line */
	op->kernel_fn(src, mpix_op_get_output_line(base), base->width);
	mpix_op_done(base);

	/* Allow overflowing after the bottom by repeating the last line */
	if (prev_line_offset + 3 >= base->height) {
		const uint8_t *bot[] = {src[1], src[2], src[2]};

		op->kernel_fn(bot, mpix_op_get_output_line(base), base->width);
		mpix_op_done(base);

		/* Flush the remaining lines that were used for lookahead context */
		mpix_op_get_input_line(base);
		mpix_op_get_input_line(base);
	}
}

void mpix_kernel_5x5_op(struct mpix_base_op *base)
{
	struct mpix_kernel_op *op = (void *)base;
	uint16_t prev_line_offset = base->line_offset;
	const uint8_t *src[] = {
		mpix_op_get_input_line(base),
		mpix_op_peek_input_line(base),
		mpix_op_peek_input_line(base),
		mpix_op_peek_input_line(base),
		mpix_op_peek_input_line(base),
	};

	assert(base->width >= 5);
	assert(base->height >= 5);

	/* Allow overflowing before the top by repeating the first line */
	if (prev_line_offset == 0) {
		const uint8_t *top[] = {src[0], src[0], src[0], src[1], src[2], src[3]};

		op->kernel_fn(&top[0], mpix_op_get_output_line(base), base->width);
		mpix_op_done(base);

		op->kernel_fn(&top[1], mpix_op_get_output_line(base), base->width);
		mpix_op_done(base);
	}

	/* Process one more line */
	op->kernel_fn(src, mpix_op_get_output_line(base), base->width);
	mpix_op_done(base);

	/* Allow overflowing after the bottom by repeating the last line */
	if (prev_line_offset + 5 >= base->height) {
		const uint8_t *bot[] = {src[1], src[2], src[3], src[4], src[4], src[4]};

		op->kernel_fn(&bot[0], mpix_op_get_output_line(base), base->width);
		mpix_op_done(base);

		op->kernel_fn(&bot[1], mpix_op_get_output_line(base), base->width);
		mpix_op_done(base);

		/* Flush the remaining lines that were used for lookahead context */
		mpix_op_get_input_line(base);
		mpix_op_get_input_line(base);
		mpix_op_get_input_line(base);
		mpix_op_get_input_line(base);
	}
}

/*
 * Declaration of convolution kernels, with the line-processing functions declared as weak aliases
 * to allow them to be replaced with optimized versions
 */

static const int16_t mpix_identity_3x3[] = {
	0, 0, 0,
	0, 1, 0,
	0, 0, 0, 0
};

__attribute__((weak))
void mpix_identity_rgb24_3x3(const uint8_t *in[3], uint8_t *out, uint16_t width)
{
	mpix_kernel_3x3(in, out, width, mpix_kernel_rgb24_3x3, mpix_convolve_3x3,
			 mpix_identity_3x3);
}
MPIX_REGISTER_KERNEL_3X3_OP(identity_rgb24, mpix_identity_rgb24_3x3, IDENTITY, RGB24);

static const int16_t mpix_identity_5x5[] = {
	0, 0, 0, 0, 0,
	0, 0, 0, 0, 0,
	0, 0, 1, 0, 0,
	0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0
};

__attribute__((weak))
void mpix_identity_rgb24_5x5(const uint8_t *in[5], uint8_t *out, uint16_t width)
{
	mpix_kernel_5x5(in, out, width, mpix_kernel_rgb24_5x5, mpix_convolve_5x5,
			mpix_identity_5x5);
}
MPIX_REGISTER_KERNEL_5X5_OP(identity_rgb24, mpix_identity_rgb24_5x5, IDENTITY, RGB24);

static const int16_t mpix_edgedetect_3x3[] = {
	-1, -1, -1,
	-1,  8, -1,
	-1, -1, -1, 0
};

__attribute__((weak))
void mpix_edgedetect_rgb24_3x3(const uint8_t *in[3], uint8_t *out, uint16_t width)
{
	mpix_kernel_3x3(in, out, width, mpix_kernel_rgb24_3x3, mpix_convolve_3x3,
			mpix_edgedetect_3x3);
}
MPIX_REGISTER_KERNEL_3X3_OP(edge_detect_rgb24, mpix_edgedetect_rgb24_3x3, EDGE_DETECT, RGB24);

/* 5x5 edge detect (Laplacian-style): center +24, all 24 neighbors -1, no shift */
static const int16_t mpix_edgedetect_5x5[] = {
	-1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1,
	-1, -1, 24, -1, -1,
	-1, -1, -1, -1, -1,
	-1, -1, -1, -1, -1, 0
};

__attribute__((weak))
void mpix_edgedetect_rgb24_5x5(const uint8_t *in[5], uint8_t *out, uint16_t width)
{
	mpix_kernel_5x5(in, out, width, mpix_kernel_rgb24_5x5, mpix_convolve_5x5,
			mpix_edgedetect_5x5);
}
MPIX_REGISTER_KERNEL_5X5_OP(edge_detect_rgb24, mpix_edgedetect_rgb24_5x5, EDGE_DETECT, RGB24);

static const int16_t mpix_gaussianblur_3x3[] = {
	1, 2, 1,
	2, 4, 2,
	1, 2, 1, 4
};

__attribute__((weak))
void mpix_gaussianblur_rgb24_3x3(const uint8_t *in[3], uint8_t *out, uint16_t width)
{
	mpix_kernel_3x3(in, out, width, mpix_kernel_rgb24_3x3, mpix_convolve_3x3,
			mpix_gaussianblur_3x3);
}
MPIX_REGISTER_KERNEL_3X3_OP(gaussian_blur_rgb24, mpix_gaussianblur_rgb24_3x3, GAUSSIAN_BLUR, RGB24);

static const int16_t mpix_gaussianblur_5x5[] = {
	1,  4,  6,  4, 1,
	4, 16, 24, 16, 4,
	6, 24, 36, 24, 6,
	4, 16, 24, 16, 4,
	1,  4,  6,  4, 1, 8
};

__attribute__((weak))
void mpix_gaussianblur_rgb24_5x5(const uint8_t *in[5], uint8_t *out, uint16_t width)
{
	mpix_kernel_5x5(in, out, width, mpix_kernel_rgb24_5x5, mpix_convolve_5x5,
			mpix_gaussianblur_5x5);
}
MPIX_REGISTER_KERNEL_5X5_OP(gaussian_blur_rgb24, mpix_gaussianblur_rgb24_5x5, GAUSSIAN_BLUR, RGB24);

static const int16_t mpix_sharpen_3x3[] = {
	 0, -1,  0,
	-1,  5, -1,
	 0, -1,  0, 0
};

__attribute__((weak))
void mpix_sharpen_rgb24_3x3(const uint8_t *in[3], uint8_t *out, uint16_t width)
{
	mpix_kernel_3x3(in, out, width, mpix_kernel_rgb24_3x3, mpix_convolve_3x3,
			mpix_sharpen_3x3);
}
MPIX_REGISTER_KERNEL_3X3_OP(sharpen_rgb24, mpix_sharpen_rgb24_3x3, SHARPEN, RGB24);

static const int16_t mpix_unsharp_5x5[] = {
	-1,  -4,  -6,  -4, -1,
	-4, -16, -24, -16, -4,
	-6, -24, 476, -24, -6,
	-4, -16, -24, -16, -4,
	-1,  -4,  -6,  -4, -1, 8
};

__attribute__((weak))
void mpix_sharpen_rgb24_5x5(const uint8_t *in[5], uint8_t *out, uint16_t width)
{
	mpix_kernel_5x5(in, out, width, mpix_kernel_rgb24_5x5, mpix_convolve_5x5, mpix_unsharp_5x5);
}
MPIX_REGISTER_KERNEL_5X5_OP(sharpen_rgb24, mpix_sharpen_rgb24_5x5, SHARPEN, RGB24);

/*
 * Declaration of median kernels, with the line-processing functions declared as weak aliases to
 * allow them to be replaced with optimized versions
 */

__attribute__((weak))
void mpix_median_rgb24_5x5(const uint8_t *in[5], uint8_t *out, uint16_t width)
{
	mpix_kernel_5x5(in, out, width, mpix_kernel_rgb24_5x5, mpix_median_5x5, NULL);
}
MPIX_REGISTER_KERNEL_5X5_OP(denoise_rgb24, mpix_median_rgb24_5x5, DENOISE, RGB24);

__attribute__((weak))
void mpix_median_rgb24_3x3(const uint8_t *in[3], uint8_t *out, uint16_t width)
{
	mpix_kernel_3x3(in, out, width, mpix_kernel_rgb24_3x3, mpix_median_3x3, NULL);
}
MPIX_REGISTER_KERNEL_3X3_OP(denoise_rgb24, mpix_median_rgb24_3x3, DENOISE, RGB24);

static const struct mpix_kernel_op **mpix_kernel_5x5_op_list =
	(const struct mpix_kernel_op *[]){MPIX_LIST_KERNEL_5X5_OP};

static const struct mpix_kernel_op **mpix_kernel_3x3_op_list =
	(const struct mpix_kernel_op *[]){MPIX_LIST_KERNEL_3X3_OP};

int mpix_image_kernel(struct mpix_image *img, uint32_t kernel_type, int kernel_size)
{
	const struct mpix_kernel_op *op = NULL;
	const struct mpix_kernel_op **kernel_op_list;

	switch (kernel_size) {
	case 3:
		kernel_op_list = mpix_kernel_3x3_op_list;
		break;
	case 5:
		kernel_op_list = mpix_kernel_5x5_op_list;
		break;
	default:
		MPIX_ERR("Unsupported kernel size %u, only supporting 3 or 5", kernel_size);
		return mpix_image_error(img, -ENOTSUP);
	}

	for (size_t i = 0; kernel_op_list[i] != NULL; i++) {
		const struct mpix_kernel_op *tmp = kernel_op_list[i];

		if (tmp->base.fourcc_src == img->fourcc &&
		    tmp->base.window_size == kernel_size &&
		    tmp->type == kernel_type) {
			op = tmp;
			break;
		}
	}

	if (op == NULL) {
		MPIX_ERR("Kernel operation %u of size %ux%u on %s data not found",
			 kernel_type, kernel_size, kernel_size, MPIX_FOURCC_TO_STR(img->fourcc));
		return mpix_image_error(img, -ENOSYS);
	}

	return mpix_image_append_uncompressed_op(img, &op->base, sizeof(*op));
}
