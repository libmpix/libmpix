/* SPDX-License-Identifier: Apache-2.0 */

#include <mpix/low_level.h>
#include <mpix/operation.h>

MPIX_REGISTER_OP(debayer_1x1);

int mpix_add_debayer_1x1(struct mpix_image *img, const int32_t *params)
{
	struct mpix_base_op *op;
	size_t pitch = mpix_format_pitch(&img->fmt);

	(void)params;

	/* Add an operation */
	op = mpix_op_append(img, MPIX_OP_DEBAYER_1X1, sizeof(*op), pitch);
	if (op == NULL) {
		return -ENOMEM;
	}

	/* Update the image format */
	img->fmt.fourcc = MPIX_FMT_RGB24;

	return 0;
}

int mpix_run_debayer_1x1(struct mpix_base_op *base)
{
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
		for (uint16_t w = 0; w < base->fmt.width; w++, src += 1, dst += 3) {
			dst[0] = dst[1] = dst[2] = src[0];
		}
		break;
	default:
		return -ENOTSUP;
	}

	MPIX_OP_OUTPUT_DONE(base);
	MPIX_OP_INPUT_DONE(base, 1);

	return 0;
}
