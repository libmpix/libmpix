/* SPDX-License-Identifier: Apache-2.0 */

#include <mpix/low_level.h>
#include <mpix/operation.h>

MPIX_REGISTER_OP(debayer_2x2);

int mpix_add_debayer_2x2(struct mpix_image *img, const int32_t *params)
{
	struct mpix_base_op *op;
	size_t pitch = mpix_format_pitch(&img->fmt);

	(void)params;

	/* Add an operation */
	op = mpix_op_append(img, MPIX_OP_DEBAYER_2X2, sizeof(*op), pitch * 2);
	if (op == NULL) {
		return -ENOMEM;
	}

	/* Update the image format */
	img->fmt.fourcc = MPIX_FMT_RGB24;

	return 0;
}

static inline void mpix_rggb8_to_rgb24_2x2(uint8_t r0, uint8_t g0, uint8_t g1, uint8_t b0,
					   uint8_t dst[3])
{
	dst[0] = r0;
	dst[1] = ((uint16_t)g0 + g1) / 2;
	dst[2] = b0;
}

static inline void mpix_gbrg8_to_rgb24_2x2(uint8_t g0, uint8_t b0, uint8_t r0, uint8_t g1,
					   uint8_t dst[3])
{
	dst[0] = r0;
	dst[1] = ((uint16_t)g0 + g1) / 2;
	dst[2] = b0;
}

static inline void mpix_bggr8_to_rgb24_2x2(uint8_t b0, uint8_t g0, uint8_t g1, uint8_t r0,
					   uint8_t dst[3])
{
	dst[0] = r0;
	dst[1] = ((uint16_t)g0 + g1) / 2;
	dst[2] = b0;
}

static inline void mpix_grbg8_to_rgb24_2x2(uint8_t g0, uint8_t r0, uint8_t b0, uint8_t g1,
					   uint8_t dst[3])
{
	dst[0] = r0;
	dst[1] = ((uint16_t)g0 + g1) / 2;
	dst[2] = b0;
}

void mpix_debayer_2x2(const uint8_t *src[2], uint8_t *dst, uint16_t width, uint32_t fourcc)
{
	const uint8_t *src0 = src[0];
	const uint8_t *src1 = src[1];
	uint16_t w = 0;

	assert(width >= 2 && width % 2 == 0);

	switch (fourcc) {
	case MPIX_FMT_SRGGB8:
		// Main processing loop
		for (; w + 3 <= width; w += 2, src0 += 2, src1 += 2, dst += 6) {
			mpix_rggb8_to_rgb24_2x2(src0[0], src0[1], src1[0], src1[1], &dst[0]);
			mpix_grbg8_to_rgb24_2x2(src0[1], src0[2], src1[1], src1[2], &dst[3]);
		}

		// Right edge handling
		mpix_rggb8_to_rgb24_2x2(src0[0], src0[1], src1[0], src1[1], &dst[0]);
		mpix_grbg8_to_rgb24_2x2(src0[1], src0[-1], src1[1], src1[-1], &dst[3]);
		break;
	case MPIX_FMT_SBGGR8:
		// Main processing loop
		for (; w + 3 <= width; w += 2, src0 += 2, src1 += 2, dst += 6) {
			mpix_bggr8_to_rgb24_2x2(src0[0], src0[1], src1[0], src1[1], &dst[0]);
			mpix_gbrg8_to_rgb24_2x2(src0[1], src0[2], src1[1], src1[2], &dst[3]);
		}

		// Right edge handling
		mpix_bggr8_to_rgb24_2x2(src0[0], src0[1], src1[0], src1[1], &dst[0]);
		mpix_gbrg8_to_rgb24_2x2(src0[1], src0[-1], src1[1], src1[-1], &dst[3]);
		break;
	case MPIX_FMT_SGBRG8:
		// Main processing loop
		for (; w + 3 <= width; w += 2, src0 += 2, src1 += 2, dst += 6) {
			mpix_gbrg8_to_rgb24_2x2(src0[0], src0[1], src1[0], src1[1], &dst[0]);
			mpix_bggr8_to_rgb24_2x2(src0[1], src0[2], src1[1], src1[2], &dst[3]);
		}

		// Right edge handling
		mpix_gbrg8_to_rgb24_2x2(src0[0], src0[1], src1[0], src1[1], &dst[0]);
		mpix_bggr8_to_rgb24_2x2(src0[1], src0[-1], src1[1], src1[-1], &dst[3]);
		break;
	case MPIX_FMT_SGRBG8:
		// Main processing loop
		for (; w + 3 <= width; w += 2, src0 += 2, src1 += 2, dst += 6) {
			mpix_grbg8_to_rgb24_2x2(src0[0], src0[1], src1[0], src1[1], &dst[0]);
			mpix_rggb8_to_rgb24_2x2(src0[1], src0[2], src1[1], src1[2], &dst[3]);
		}

		// Right edge handling
		mpix_grbg8_to_rgb24_2x2(src0[0], src0[1], src1[0], src1[1], &dst[0]);
		mpix_rggb8_to_rgb24_2x2(src0[1], src0[-1], src1[1], src1[-1], &dst[3]);
		break;
	}
}

int mpix_run_debayer_2x2(struct mpix_base_op *base)
{
	const uint8_t *src[2];
	uint8_t *dst;

	MPIX_OP_INPUT_LINES(base, src, ARRAY_SIZE(src));

	uint32_t fourcc_even = base->fmt.fourcc;
	uint32_t fourcc_odd = mpix_format_line_down(base->fmt.fourcc);

	/* Process one line */
	if (base->line_offset % 2 == 0) {
		MPIX_OP_OUTPUT_LINE(base, &dst);
		mpix_debayer_2x2(src, dst, base->fmt.width, fourcc_even);
		MPIX_OP_OUTPUT_DONE(base);
	} else {
		MPIX_OP_OUTPUT_LINE(base, &dst);
		mpix_debayer_2x2(src, dst, base->fmt.width, fourcc_odd);
		MPIX_OP_OUTPUT_DONE(base);
	}

	/* Handle edgge case on last line */
	if (base->line_offset + 2 == base->fmt.height) {
		MPIX_OP_OUTPUT_LINE(base, &dst);
		mpix_debayer_2x2(src, dst, base->fmt.width, fourcc_even);
		MPIX_OP_OUTPUT_DONE(base);

		/* Flush lookahead lines */
		MPIX_OP_INPUT_DONE(base, 1);
	}

	MPIX_OP_INPUT_DONE(base, 1);

	return 0;
}
