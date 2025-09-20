/* SPDX-License-Identifier: Apache-2.0 */

#include <mpix/low_level.h>
#include <mpix/operation.h>

MPIX_REGISTER_OP(debayer_3x3);

int mpix_add_debayer_3x3(struct mpix_image *img, const int32_t *params)
{
	struct mpix_base_op *op;
	size_t pitch = mpix_format_pitch(&img->fmt);

	(void)params;

	/* Add an operation */
	op = mpix_op_append(img, MPIX_OP_DEBAYER_3X3, sizeof(*op), pitch * 3);
	if (op == NULL) {
		return -ENOMEM;
	}

	/* Update the image format */
	img->fmt.fourcc = MPIX_FMT_RGB24;

	return 0;
}

static inline void mpix_rggb8_to_rgb24_3x3(const uint8_t rgr0[3], const uint8_t gbg1[3],
					   const uint8_t rgr2[3], uint8_t rgb24[3])
{
	rgb24[0] = ((uint16_t)rgr0[0] + rgr0[2] + rgr2[0] + rgr2[2]) / 4;
	rgb24[1] = ((uint16_t)rgr0[1] + gbg1[2] + gbg1[0] + rgr2[1]) / 4;
	rgb24[2] = gbg1[1];
}

static inline void mpix_bggr8_to_rgb24_3x3(const uint8_t bgb0[3], const uint8_t grg1[3],
					   const uint8_t bgb2[3], uint8_t rgb24[3])
{
	rgb24[0] = grg1[1];
	rgb24[1] = ((uint16_t)bgb0[1] + grg1[2] + grg1[0] + bgb2[1]) / 4;
	rgb24[2] = ((uint16_t)bgb0[0] + bgb0[2] + bgb2[0] + bgb2[2]) / 4;
}

static inline void mpix_grbg8_to_rgb24_3x3(const uint8_t grg0[3], const uint8_t bgb1[3],
					   const uint8_t grg2[3], uint8_t rgb24[3])
{
	rgb24[0] = ((uint16_t)grg0[1] + grg2[1]) / 2;
	rgb24[1] = bgb1[1];
	rgb24[2] = ((uint16_t)bgb1[0] + bgb1[2]) / 2;
}

static inline void mpix_gbrg8_to_rgb24_3x3(const uint8_t gbg0[3], const uint8_t rgr1[3],
					   const uint8_t gbg2[3], uint8_t rgb24[3])
{
	rgb24[0] = ((uint16_t)rgr1[0] + rgr1[2]) / 2;
	rgb24[1] = rgr1[1];
	rgb24[2] = ((uint16_t)gbg0[1] + gbg2[1]) / 2;
}

void mpix_debayer_3x3(const uint8_t *src[3], uint8_t *dst, uint16_t width, uint32_t fourcc)
{
	const uint8_t *src0 = src[0];
	const uint8_t *src1 = src[1];
	const uint8_t *src2 = src[2];
	const uint8_t src_l[3][4] =  {
		{src0[1], src0[0], src0[1]},
		{src1[1], src1[0], src1[1]},
		{src2[1], src2[0], src2[1]},
	};
	const uint8_t src_r[3][3] = {
		{src0[width - 2], src0[width - 1], src0[width - 2]},
		{src1[width - 2], src1[width - 1], src1[width - 2]},
		{src2[width - 2], src2[width - 1], src2[width - 2]},
	};

	assert(width >= 4 && width % 2 == 0);

	switch (fourcc) {
	case MPIX_FMT_SRGGB8:
		// Left edge handling
		mpix_grbg8_to_rgb24_3x3(src_l[0], src_l[1], src_l[2], &dst[0]);

		// Main processing loop in C
		for (size_t i = 0, o = 3; i + 4 <= width; i += 2, o += 6) {
			mpix_rggb8_to_rgb24_3x3(&src0[i + 0], &src1[i + 0], &src2[i + 0], &dst[o + 0]);
			mpix_grbg8_to_rgb24_3x3(&src0[i + 1], &src1[i + 1], &src2[i + 1], &dst[o + 3]);
		}

		// Right edge handling
		mpix_rggb8_to_rgb24_3x3(src_r[0], src_r[1], src_r[2], &dst[width * 3 - 3]);
		break;

	case MPIX_FMT_SGRBG8:
		// Left edge handling
		mpix_rggb8_to_rgb24_3x3(src_l[0], src_l[1], src_l[2], &dst[0]);

		// Main processing loop in C
		for (size_t i = 0, o = 3; i + 4 <= width; i += 2, o += 6) {
			mpix_grbg8_to_rgb24_3x3(&src0[i + 0], &src1[i + 0], &src2[i + 0], &dst[o + 0]);
			mpix_rggb8_to_rgb24_3x3(&src0[i + 1], &src1[i + 1], &src2[i + 1], &dst[o + 3]);
		}

		// Right edge handling
		mpix_grbg8_to_rgb24_3x3(src_r[0], src_r[1], src_r[2], &dst[width * 3 - 3]);
		break;

	case MPIX_FMT_SBGGR8:
		// Left edge handling
		mpix_gbrg8_to_rgb24_3x3(src_l[0], src_l[1], src_l[2], &dst[0]);

		// Main processing loop in C
		for (size_t i = 0, o = 3; i + 4 <= width; i += 2, o += 6) {
			mpix_bggr8_to_rgb24_3x3(&src0[i + 0], &src1[i + 0], &src2[i + 0], &dst[o + 0]);
			mpix_gbrg8_to_rgb24_3x3(&src0[i + 1], &src1[i + 1], &src2[i + 1], &dst[o + 3]);
		}

		// Right edge handling
		mpix_bggr8_to_rgb24_3x3(src_r[0], src_r[1], src_r[2], &dst[width * 3 - 3]);
		break;

	case MPIX_FMT_SGBRG8:
		// Left edge handling
		mpix_bggr8_to_rgb24_3x3(src_l[0], src_l[1], src_l[2], &dst[0]);

		// Main processing loop in C
		for (size_t i = 0, o = 3; i + 4 <= width; i += 2, o += 6) {
			mpix_gbrg8_to_rgb24_3x3(&src0[i + 0], &src1[i + 0], &src2[i + 0], &dst[o + 0]);
			mpix_bggr8_to_rgb24_3x3(&src0[i + 1], &src1[i + 1], &src2[i + 1], &dst[o + 3]);
		}

		// Right edge handling
		mpix_gbrg8_to_rgb24_3x3(src_r[0], src_r[1], src_r[2], &dst[width * 3 - 3]);
		break;
	}
}

int mpix_run_debayer_3x3(struct mpix_base_op *base)
{
	const uint8_t *src[3];
	uint8_t *dst;

	MPIX_OP_INPUT_LINES(base, src, ARRAY_SIZE(src));

	uint32_t fourcc_even = base->fmt.fourcc;
	uint32_t fourcc_odd = mpix_format_line_down(base->fmt.fourcc);

	/* Handle edgge case on first line */
	if (base->line_offset == 0) {
		MPIX_OP_OUTPUT_LINE(base, &dst);
		mpix_debayer_3x3(src, dst, base->fmt.width, fourcc_even);
		MPIX_OP_OUTPUT_DONE(base);
	}

	/* Process one line */
	if (base->line_offset % 2 == 0) {
		MPIX_OP_OUTPUT_LINE(base, &dst);
		mpix_debayer_3x3(src, dst, base->fmt.width, fourcc_even);
		MPIX_OP_OUTPUT_DONE(base);
	} else {
		MPIX_OP_OUTPUT_LINE(base, &dst);
		mpix_debayer_3x3(src, dst, base->fmt.width, fourcc_odd);
		MPIX_OP_OUTPUT_DONE(base);
	}

	/* Handle edgge case on last line */
	if (base->line_offset + 3 == base->fmt.height) {
		MPIX_OP_OUTPUT_LINE(base, &dst);
		mpix_debayer_3x3(src, dst, base->fmt.width, fourcc_odd);
		MPIX_OP_OUTPUT_DONE(base);

		/* Flush lookahead lines */
		MPIX_OP_INPUT_DONE(base, 2);
	}

	MPIX_OP_INPUT_DONE(base, 1);

	return 0;
}
