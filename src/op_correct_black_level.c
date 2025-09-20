/* SPDX-License-Identifier: Apache-2.0 */

#include <mpix/formats.h>
#include <mpix/low_level.h>
#include <mpix/operation.h>

MPIX_REGISTER_OP(correct_black_level);

struct mpix_operation {
	struct mpix_base_op base;
	/* Controls */
	int32_t black_level;
};

int mpix_add_correct_black_level(struct mpix_image *img, const int32_t *params)
{
	struct mpix_operation *op;
	size_t pitch = mpix_format_pitch(&img->fmt);

	(void)params;

	/* Add an operation */
	op = mpix_op_append(img, MPIX_OP_CORRECT_BLACK_LEVEL, sizeof(*op), pitch);
	if (op == NULL) return -ENOMEM;

	/* Register controls */
	img->ctrls[MPIX_CID_BLACK_LEVEL] = &op->black_level;

	return 0;
}

void mpix_correct_black_level_raw8(const uint8_t *src, uint8_t *dst, uint16_t width, uint8_t level)
{
	for (size_t w = 0; w < width; w++, src++, dst++) {
		*dst = MAX(0, *src - level);
	}
}

int mpix_run_correct_black_level(struct mpix_base_op *base)
{
	struct mpix_operation *op = (struct mpix_operation *)base;
	const uint8_t *src;
	uint8_t *dst;

	MPIX_OP_INPUT_LINES(base, &src, 1);
	MPIX_OP_OUTPUT_LINE(base, &dst);

	switch (base->fmt.fourcc) {
	case MPIX_FMT_SBGGR8:
	case MPIX_FMT_SRGGB8:
	case MPIX_FMT_SGRBG8:
	case MPIX_FMT_SGBRG8:
	case MPIX_FMT_GREY:
		mpix_correct_black_level_raw8(src, dst, base->fmt.width, op->black_level);
		break;
	case MPIX_FMT_RGB24:
		mpix_correct_black_level_raw8(src, dst, base->fmt.width * 3, op->black_level);
		break;
	default:
		return -ENOTSUP;
	}

	MPIX_OP_OUTPUT_DONE(base);
	MPIX_OP_INPUT_DONE(base, 1);

	return 0;
}
