/* SPDX-License-Identifier: Apache-2.0 */

#include <mpix/low_level.h>
#include <mpix/operation.h>

MPIX_REGISTER_OP(correct_color_matrix);

struct mpix_operation {
	struct mpix_base_op base;
	/* Controls */
	int32_t color_matrix_q10[9];
};

int mpix_add_correct_color_matrix(struct mpix_image *img, const int32_t *params)
{
	struct mpix_operation *op;
	size_t pitch = mpix_format_pitch(&img->fmt);

	(void)params;

	/* Add an operation */
	op = mpix_op_append(img, MPIX_OP_CORRECT_COLOR_MATRIX, sizeof(*op), pitch);
	if (op == NULL) return -ENOMEM;

	/* Register controls */
	img->ctrls[MPIX_CID_COLOR_MATRIX_0] = &op->color_matrix_q10[0];
	img->ctrls[MPIX_CID_COLOR_MATRIX_1] = &op->color_matrix_q10[1];
	img->ctrls[MPIX_CID_COLOR_MATRIX_2] = &op->color_matrix_q10[2];
	img->ctrls[MPIX_CID_COLOR_MATRIX_3] = &op->color_matrix_q10[3];
	img->ctrls[MPIX_CID_COLOR_MATRIX_4] = &op->color_matrix_q10[4];
	img->ctrls[MPIX_CID_COLOR_MATRIX_5] = &op->color_matrix_q10[5];
	img->ctrls[MPIX_CID_COLOR_MATRIX_6] = &op->color_matrix_q10[6];
	img->ctrls[MPIX_CID_COLOR_MATRIX_7] = &op->color_matrix_q10[7];
	img->ctrls[MPIX_CID_COLOR_MATRIX_8] = &op->color_matrix_q10[8];

	return 0;
}

void mpix_correct_color_matrix_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width,
				     int32_t matrix_q10[9])
{
	/* Run as many cycles of SIMD-accelerated code as possible */
#if CONFIG_MPIX_SIMD_NEON
	w += mpix_correct_color_matrix_raw8_neon(src, dst, width, matrix_q10);
#endif
#if CONFIG_MPIX_SIMD_HELIUM
	w += mpix_correct_color_matrix_raw8_helium(src, dst, width, matrix_q10);
#endif
#if CONFIG_MPIX_SIMD_RVV
	w += mpix_correct_color_matrix_raw8_rvv(src, dst, width, matrix_q10);
#endif

	/* Finish with the buffer with C implementation */
	for (size_t w = 0; w + 3 <= width; w++, dst += 3, src += 3) {
		int32_t r;
		int32_t g;
		int32_t b;

		r = (src[0] * matrix_q10[0]) >> 10;
		g = (src[1] * matrix_q10[1]) >> 10;
		b = (src[2] * matrix_q10[2]) >> 10;
		dst[0] = CLAMP(r + g + b, 0x00, 0xff);

		r = (src[0] * matrix_q10[3]) >> 10;
		g = (src[1] * matrix_q10[4]) >> 10;
		b = (src[2] * matrix_q10[5]) >> 10;
		dst[1] = CLAMP(r + g + b, 0x00, 0xff);

		r = (src[0] * matrix_q10[6]) >> 10;
		g = (src[1] * matrix_q10[7]) >> 10;
		b = (src[2] * matrix_q10[8]) >> 10;
		dst[2] = CLAMP(r + g + b, 0x00, 0xff);
	}
}

int mpix_run_correct_color_matrix(struct mpix_base_op *base)
{
	struct mpix_operation *op = (void *)base;
	const uint8_t *src;
	uint8_t *dst;

	MPIX_OP_INPUT_LINES(base, &src, 1);
	MPIX_OP_OUTPUT_LINE(base, &dst);

	switch (base->fmt.fourcc) {
	case MPIX_FMT_RGB24:
		mpix_correct_color_matrix_rgb24(src, dst, base->fmt.width, op->color_matrix_q10);
		break;
	default:
		return -ENOTSUP;
	}

	MPIX_OP_OUTPUT_DONE(base);
	MPIX_OP_INPUT_DONE(base, 1);

	return 0;
}
