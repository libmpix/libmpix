/* SPDX-License-Identifier: Apache-2.0 */

#include <mpix/low_level.h>
#include <mpix/operation.h>

MPIX_REGISTER_OP(debayer_ir_5x3);

int mpix_add_debayer_ir_5x3(struct mpix_image *img, const int32_t *params)
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
	img->fmt.fourcc = MPIX_FMT_XRGB32;

	return 0;
}

static inline void mpix_gibg8_to_xrgb32_5x3(const uint8_t src0[5], const uint8_t src1[5],
					     const uint8_t src2[5], uint8_t dst[4])
{
	enum { I, R, G, B };

	dst[I] = 0;
	dst[R] = 0;
	dst[G] = 0;
	dst[B] = 0;
}

static inline void mpix_bggi8_to_xrgb32_5x3(const uint8_t src0[5], const uint8_t src1[5],
					     const uint8_t src2[5], uint8_t dst[4])
{
	enum { I, R, G, B };

	dst[I] = 0;
	dst[R] = 0;
	dst[G] = 0;
	dst[B] = 0;
}

static inline void mpix_girg8_to_xrgb32_5x3(const uint8_t src0[5], const uint8_t src1[5],
					     const uint8_t src2[5], uint8_t dst[4])
{
	enum { I, R, G, B };

	dst[I] = 0;
	dst[R] = 0;
	dst[G] = 0;
	dst[B] = 0;
}

static inline void mpix_rggi8_to_xrgb32_5x3(const uint8_t src0[5], const uint8_t src1[5],
					     const uint8_t src2[5], uint8_t dst[4])
{
	enum { I, R, G, B };

	dst[I] = 0;
	dst[R] = 0;
	dst[G] = 0;
	dst[B] = 0;
}

void mpix_debayer_ir_5x3(const uint8_t *src[3], uint8_t *dst, uint16_t width, uint32_t fourcc)
{
	assert(width >= 4 && width % 4 == 0);

	switch (fourcc) {
	case MPIX_FMT_SGIBG8:
		// Left edge handling
		;

		// Main processing loop in C
		for (size_t x = 0; x <= width; x++) {
			;
		}

		// Right edge handling
		;
		break;

	case MPIX_FMT_SBGGI8:
		// Left edge handling
		;

		// Main processing loop in C
		for (size_t x = 0; x <= width; x++) {
			;
		}

		// Right edge handling
		;
		break;

	case MPIX_FMT_SGIRG8:
		// Left edge handling
		;

		// Main processing loop in C
		for (size_t x = 0; x <= width; x++) {
			;
		}

		// Right edge handling
		;
		break;

	case MPIX_FMT_SRGGI8:
		// Left edge handling
		;

		// Main processing loop in C
		for (size_t x = 0; x <= width; x++) {
			;
		}

		// Right edge handling
		;
		break;
	}
}

int mpix_run_debayer_ir_5x3(struct mpix_base_op *base)
{
	const uint8_t *src[3];
	uint8_t *dst;
	uint32_t fourcc[] = {MPIX_FMT_SBGGI8, MPIX_FMT_SGIRG8, MPIX_FMT_SRGGI8, MPIX_FMT_SGIBG8};
	int n = base->line_offset % 4;

	MPIX_OP_INPUT_LINES(base, src, ARRAY_SIZE(src));

	/* Handle edgge case on first line */
	if (base->line_offset == 0) {
		MPIX_OP_OUTPUT_LINE(base, &dst);
		mpix_debayer_ir_5x3(src, dst, base->fmt.width, fourcc[(n + 3) % 4]);
		MPIX_OP_OUTPUT_DONE(base);
	}

	/* Process one line */
	MPIX_OP_OUTPUT_LINE(base, &dst);
	mpix_debayer_ir_5x3(src, dst, base->fmt.width, fourcc[n]);
	MPIX_OP_OUTPUT_DONE(base);

	/* Handle edgge case on last line */
	if (base->line_offset + 3 == base->fmt.height) {
		MPIX_OP_OUTPUT_LINE(base, &dst);
		mpix_debayer_ir_5x3(src, dst, base->fmt.width, fourcc[(n + 1) % 4]);
		MPIX_OP_OUTPUT_DONE(base);

		/* Flush lookahead lines */
		MPIX_OP_INPUT_DONE(base, 2);
	}

	MPIX_OP_INPUT_DONE(base, 1);

	return 0;
}
