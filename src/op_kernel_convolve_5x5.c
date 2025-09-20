/* SPDX-License-Identifier: Apache-2.0 */

#include <mpix/low_level.h>
#include <mpix/operation.h>

MPIX_REGISTER_OP(kernel_convolve_5x5, P_TYPE);

struct mpix_operation {
	struct mpix_base_op base;
	/** Parameters */
	enum mpix_kernel_type type;
};

int mpix_add_kernel_convolve_5x5(struct mpix_image *img, const int32_t *params)
{
	struct mpix_operation *op;
	size_t pitch = mpix_format_pitch(&img->fmt);

	/* Parameter validation */
	if (params[P_TYPE] < 0 || params[P_TYPE] >= MPIX_NB_KERNEL) {
		return -ERANGE;
	}

	/* Add an operation */
	op = mpix_op_append(img, MPIX_OP_KERNEL_CONVOLVE_5X5, sizeof(*op), pitch * 5);
	if (op == NULL) {
		return -ENOMEM;
	}

	/* Store parameters */
	op->type = params[P_TYPE];

	return 0;
}

static void mpix_kernel_convolve_5x5(uint16_t base, const uint8_t *src[5], int i0, int i1, int i2,
				     int i3, int i4, uint8_t *dst, int o0, const int16_t *kernel)
{
	int32_t result = 0;
	int k = 0;

	/* Apply the coefficients on 5 rows */
	for (int h = 0; h < 5; h++) {
		/* Apply the coefficients on 5 columns */
		result += src[h][base + i0] * kernel[k++]; /* line h column 0 */
		result += src[h][base + i1] * kernel[k++]; /* line h column 1 */
		result += src[h][base + i2] * kernel[k++]; /* line h column 2 */
		result += src[h][base + i3] * kernel[k++]; /* line h column 3 */
		result += src[h][base + i4] * kernel[k++]; /* line h column 4 */
	}

	/* Store the scaled-down output */
	result >>= kernel[k];
	dst[base + o0] = CLAMP(result, 0x00, 0xff);
}

static const int16_t mpix_kernel_5x5[][5 * 5 + 1] = {
	[MPIX_KERNEL_EDGE_DETECT] = {
		-1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1,
		-1, -1, 24, -1, -1,
		-1, -1, -1, -1, -1,
		-1, -1, -1, -1, -1,
		0, /* Scale factor */
	},
	[MPIX_KERNEL_GAUSSIAN_BLUR] = {
		1,  4,  6,  4, 1,
		4, 16, 24, 16, 4,
		6, 24, 36, 24, 6,
		4, 16, 24, 16, 4,
		1,  4,  6,  4, 1,
		8, /* Scale factor */
	},
	[MPIX_KERNEL_IDENTITY] = {
		0, 0, 0, 0, 0,
		0, 0, 0, 0, 0,
		0, 0, 1, 0, 0,
		0, 0, 0, 0, 0,
		0, 0, 0, 0, 0,
		0, /* Scale factor */
	},
	[MPIX_KERNEL_SHARPEN] = {
		-1,  -4,  -6,  -4, -1,
		-4, -16, -24, -16, -4,
		-6, -24, 476, -24, -6,
		-4, -16, -24, -16, -4,
		-1,  -4,  -6,  -4, -1,
		8, /* Scale factor */
	},
};

static inline void mpix_kernel_convolve_5x5_rgb24(const uint8_t *src[5], uint8_t *dst,
						  uint16_t width, enum mpix_kernel_type type)
{
	enum { R, G, B };
	const int16_t *kernel = mpix_kernel_5x5[type];
	uint16_t w = 0;

	/* Edge case on first two columns */

	mpix_kernel_convolve_5x5(w * 3 + R, src, 0, 0, 0, 3, 6, dst, 0, kernel);
	mpix_kernel_convolve_5x5(w * 3 + G, src, 0, 0, 0, 3, 6, dst, 0, kernel);
	mpix_kernel_convolve_5x5(w * 3 + B, src, 0, 0, 0, 3, 6, dst, 0, kernel);

	mpix_kernel_convolve_5x5(w * 3 + R, src, 0, 0, 3, 6, 9, dst, 3, kernel);
	mpix_kernel_convolve_5x5(w * 3 + G, src, 0, 0, 3, 6, 9, dst, 3, kernel);
	mpix_kernel_convolve_5x5(w * 3 + B, src, 0, 0, 3, 6, 9, dst, 3, kernel);

	/* process as much as possible with SIMD acceleration when available */

#ifdef CONFIG_MPIX_SIMD_HELIUM
	switch (type) {
	case MPIX_KERNEL_SHARPEN:
		w += mpix_sharpen_rgb24_5x5_helium(&src[w], &dst[w], width - w);
		break;
	case MPIX_KERNEL_GAUSSIAN_BLUR:
		w += mpix_gaussianblur_rgb24_5x5_helium(&src[w], &dst[w], width - w);
		break;
	case MPIX_KERNEL_EDGE_DETECT:
		w += mpix_edgedetect_rgb24_5x5_helium(&src[w], &dst[w], width - w);
		break;
	case MPIX_KERNEL_IDENTITY:
		w += mpix_identity_rgb24_5x5_helium(&src[w], &dst[w], width - w);
		break;
	}
#endif

	/* Process the entire line except the first two and last two columns (edge cases) */

	for (; w + 5 <= width; w++) {
		mpix_kernel_convolve_5x5(w * 3 + R, src, 0, 3, 6, 9, 12, dst, 6, kernel);
		mpix_kernel_convolve_5x5(w * 3 + G, src, 0, 3, 6, 9, 12, dst, 6, kernel);
		mpix_kernel_convolve_5x5(w * 3 + B, src, 0, 3, 6, 9, 12, dst, 6, kernel);
	}

	/* Edge case on last two columns, repeat the right column to fill the blank */

	mpix_kernel_convolve_5x5(w * 3 + R, src, 0, 3, 6, 9, 9, dst, 6, kernel);
	mpix_kernel_convolve_5x5(w * 3 + G, src, 0, 3, 6, 9, 9, dst, 6, kernel);
	mpix_kernel_convolve_5x5(w * 3 + B, src, 0, 3, 6, 9, 9, dst, 6, kernel);

	mpix_kernel_convolve_5x5(w * 3 + R, src, 3, 6, 9, 9, 9, dst, 9, kernel);
	mpix_kernel_convolve_5x5(w * 3 + G, src, 3, 6, 9, 9, 9, dst, 9, kernel);
	mpix_kernel_convolve_5x5(w * 3 + B, src, 3, 6, 9, 9, 9, dst, 9, kernel);
}

int mpix_run_kernel_convolve_5x5(struct mpix_base_op *base)
{
	struct mpix_operation *op = (struct mpix_operation *)base;
	const uint8_t *src[5];
	uint8_t *dst;

	MPIX_OP_INPUT_LINES(base, src, ARRAY_SIZE(src));

	uint16_t width = base->fmt.width;
	const uint8_t *lines[] = { src[0], src[0], src[0], src[1], src[2], src[3],
				   src[4], src[4], src[4] };

	/* Handle edgge case on first line */
	if (base->line_offset == 0) {
		MPIX_OP_OUTPUT_LINE(base, &dst);
		mpix_kernel_convolve_5x5_rgb24(&lines[0], dst, width, op->type);
		MPIX_OP_OUTPUT_DONE(base);

		MPIX_OP_OUTPUT_LINE(base, &dst);
		mpix_kernel_convolve_5x5_rgb24(&lines[1], dst, width, op->type);
		MPIX_OP_OUTPUT_DONE(base);
	}

	/* Process one line */
	MPIX_OP_OUTPUT_LINE(base, &dst);
	mpix_kernel_convolve_5x5_rgb24(&lines[2], dst, width, op->type);
	MPIX_OP_OUTPUT_DONE(base);

	/* Handle edgge case on last line */
	if (base->line_offset + 5 >= base->fmt.height) {
		MPIX_OP_OUTPUT_LINE(base, &dst);
		mpix_kernel_convolve_5x5_rgb24(&lines[3], dst, width, op->type);
		MPIX_OP_OUTPUT_DONE(base);

		MPIX_OP_OUTPUT_LINE(base, &dst);
		mpix_kernel_convolve_5x5_rgb24(&lines[4], dst, width, op->type);
		MPIX_OP_OUTPUT_DONE(base);

		/* Flush the lookahead lines */
	}

	MPIX_OP_INPUT_DONE(base, 1);

	return 0;
}
