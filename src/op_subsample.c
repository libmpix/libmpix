/* SPDX-License-Identifier: Apache-2.0 */

#include <mpix/low_level.h>
#include <mpix/operation.h>

MPIX_REGISTER_OP(subsample, P_WIDTH, P_HEIGHT);

int mpix_add_subsample(struct mpix_image *img, const int32_t *params)
{
	struct mpix_base_op *op;
	size_t pitch = mpix_format_pitch(&img->fmt);

	/* Parameter validation */
	if (params[P_WIDTH] < 1 || params[P_WIDTH] > UINT16_MAX ||
	    params[P_HEIGHT] < 1 || params[P_HEIGHT] > UINT16_MAX) {
		return -ERANGE;
	}

	/* Add an operation */
	op = mpix_op_append(img, MPIX_OP_SUBSAMPLE, sizeof(*op), pitch);
	if (op == NULL) {
		return -ENOMEM;
	}

	/* Update the image format */
	img->fmt.width = params[P_WIDTH];
	img->fmt.height = params[P_HEIGHT];

	return 0;
}

static inline void mpix_subsample_line(const uint8_t *src_buf, size_t src_width, uint8_t *dst_buf,
				       size_t dst_width, uint8_t bits_per_pixel)
{
	for (size_t dst_w = 0; dst_w < dst_width; dst_w++) {
		size_t src_w = dst_w * src_width / dst_width;
		size_t src_i = src_w * bits_per_pixel / BITS_PER_BYTE;
		size_t dst_i = dst_w * bits_per_pixel / BITS_PER_BYTE;

		memmove(&dst_buf[dst_i], &src_buf[src_i], bits_per_pixel / BITS_PER_BYTE);
	}
}

void mpix_subsample_frame(const uint8_t *src_buf, size_t src_width, size_t src_height,
			  uint8_t *dst_buf, size_t dst_width, size_t dst_height,
			  uint8_t bits_per_pixel)
{
	for (size_t dst_h = 0; dst_h < dst_height; dst_h++) {
		size_t src_h = dst_h * src_height / dst_height;
		size_t src_i = src_h * src_width * bits_per_pixel / BITS_PER_BYTE;
		size_t dst_i = dst_h * dst_width * bits_per_pixel / BITS_PER_BYTE;

		mpix_subsample_line(&src_buf[src_i], src_width, &dst_buf[dst_i], dst_width,
				    bits_per_pixel);
	}
}

int mpix_run_subsample(struct mpix_base_op *base)
{
	const uint8_t *src;
	uint8_t *dst;

	MPIX_OP_INPUT_LINES(base, &src, 1);

	struct mpix_base_op *next = base->next;
	uint8_t bits_per_pixel = mpix_bits_per_pixel(base->fmt.fourcc);
	uint16_t prev_offset = (base->line_offset + 0) * next->fmt.height / base->fmt.height;
	uint16_t next_offset = (base->line_offset + 1) * next->fmt.height / base->fmt.height;

	for (uint16_t i = 0; prev_offset + i < next_offset; i++) {
		MPIX_OP_OUTPUT_LINE(base, &dst);
		mpix_subsample_line(src, base->fmt.width, dst, next->fmt.width, bits_per_pixel);
		MPIX_OP_OUTPUT_DONE(base);
	}

	MPIX_OP_INPUT_DONE(base, 1);
	return 0;
}
