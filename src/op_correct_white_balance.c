/* SPDX-License-Identifier: Apache-2.0 */

#include <mpix/low_level.h>
#include <mpix/operation.h>

MPIX_REGISTER_OP(correct_white_balance);

struct mpix_operation {
	struct mpix_base_op base;
	/* Controls */
	int32_t blue_balance_q10;
	int32_t red_balance_q10;
};

int mpix_add_correct_white_balance(struct mpix_image *img, const int32_t *params)
{
	struct mpix_operation *op;
	size_t pitch = mpix_format_pitch(&img->fmt);

	(void)params;

	/* Add an operation */
	op = mpix_op_append(img, MPIX_OP_CORRECT_WHITE_BALANCE, sizeof(*op), pitch);
	if (op == NULL) return -ENOMEM;

	/* Register controls */
	img->ctrls[MPIX_CID_BLUE_BALANCE] = &op->blue_balance_q10;
	img->ctrls[MPIX_CID_RED_BALANCE] = &op->red_balance_q10;

	return 0;
}

void mpix_correct_white_balance_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width,
				      int32_t red_level_q10, int32_t blue_level_q10)
{
	for (size_t w = 0; w < width; w++, src += 3, dst += 3) {
		dst[0] = MIN(src[0] * red_level_q10 >> 10, 0xff);
		dst[1] = src[1];
		dst[2] = MIN(src[2] * blue_level_q10 >> 10, 0xff);
	}
}

int mpix_run_correct_white_balance(struct mpix_base_op *base)
{
	struct mpix_operation *op = (void *)base;
	const uint8_t *src;
	uint8_t *dst;

	MPIX_OP_INPUT_LINES(base, &src, 1);
	MPIX_OP_OUTPUT_LINE(base, &dst);

	switch (base->fmt.fourcc) {
	case MPIX_FMT_RGB24:
		mpix_correct_white_balance_rgb24(src, dst, base->fmt.width, op->red_balance_q10,
						 op->blue_balance_q10);
		break;
	default:
		return -ENOTSUP;
	}

	MPIX_OP_OUTPUT_DONE(base);
	MPIX_OP_INPUT_DONE(base, 1);

	return 0;
}
