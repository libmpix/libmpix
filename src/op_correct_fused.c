/* SPDX-License-Identifier: Apache-2.0 */

#include <mpix/low_level.h>
#include <mpix/operation.h>

MPIX_REGISTER_OP(correct_fused);

struct mpix_operation {
	struct mpix_base_op base;
	/* Controls */
	int32_t black_level;
	int32_t gamma_level_q10;
	int32_t color_matrix_q10[9];
};

int mpix_add_correct_fused(struct mpix_image *img, const int32_t *params)
{
	struct mpix_operation *op;
	size_t pitch = mpix_format_pitch(&img->fmt);

	(void)params;

	/* Add an operation */
	op = mpix_op_append(img, MPIX_OP_CORRECT_FUSED, sizeof(*op), pitch);
	if (op == NULL) return -ENOMEM;

	/* Register controls */
	img->ctrls[MPIX_CID_BLACK_LEVEL] = &op->black_level;
	img->ctrls[MPIX_CID_GAMMA_LEVEL] = &op->gamma_level_q10;
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

void mpix_correct_fused_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width, uint8_t black_level,
			      uint16_t gamma_level_q10, int32_t color_matrix_q10[9])
{
	/* TODO: implementation */
	(void)src;
	(void)dst;
	(void)width;
	(void)black_level;
	(void)gamma_level_q10;
	(void)color_matrix_q10;
}

int mpix_run_correct_fused(struct mpix_base_op *base)
{
	struct mpix_operation *op = (void *)base;
	const uint8_t *src;
	uint8_t *dst;

	MPIX_OP_INPUT_LINES(base, &src, 1);
	MPIX_OP_OUTPUT_LINE(base, &dst);

	switch (base->fmt.fourcc) {
	case MPIX_FMT_RGB24:
		mpix_correct_fused_rgb24(src, dst, op->base.fmt.width, op->black_level,
					 op->gamma_level_q10, op->color_matrix_q10);
		break;
	default:
		return -ENOTSUP;
	}

	MPIX_OP_OUTPUT_DONE(base);
	MPIX_OP_INPUT_DONE(base, 1);

	return 0;
}
