/* SPDX-License-Identifier: Apache-2.0 */

#include <mpix/low_level.h>
#include <mpix/operation.h>

MPIX_REGISTER_OP(crop, P_X_OFFSET, P_Y_OFFSET, P_WIDTH, P_HEIGHT);

struct mpix_operation {
	struct mpix_base_op base;
	/* Parameters */
	uint16_t x_offset;
	uint16_t y_offset;
	uint16_t width;
	uint16_t height;
};

int mpix_add_crop(struct mpix_image *img, const int32_t *params)
{
	struct mpix_operation *op;
	size_t pitch = mpix_format_pitch(&img->fmt);

	/* Parameter validation */
	if (params[P_X_OFFSET] < 0 || params[P_X_OFFSET] > UINT16_MAX ||
	    params[P_Y_OFFSET] < 0 || params[P_Y_OFFSET] > UINT16_MAX ||
	    params[P_WIDTH] < 1 || params[P_WIDTH] > UINT16_MAX ||
	    params[P_HEIGHT] < 1 || params[P_HEIGHT] > UINT16_MAX) {
		return -ERANGE;
	}
	if (params[P_X_OFFSET] + params[P_WIDTH] > img->fmt.width ||
	    params[P_Y_OFFSET] + params[P_HEIGHT] > img->fmt.height) {
		return -ERANGE;
	}

	/* Add an operation */
	op = mpix_op_append(img, MPIX_OP_CROP, sizeof(*op), pitch);
	if (op == NULL) return -ENOMEM;

	/* Store parameters */
	op->x_offset = params[P_X_OFFSET];
	op->y_offset = params[P_Y_OFFSET];
	op->width = params[P_WIDTH];
	op->height = params[P_HEIGHT];

	/* Update the image format */
	img->fmt.width = params[P_WIDTH];
	img->fmt.height = params[P_HEIGHT];

	return 0;
}

static inline void mpix_crop_line(const uint8_t *src_buf, uint8_t *dst_buf,
				  size_t x_offset, size_t crop_width, uint8_t bits_per_pixel)
{
	size_t src_offset_bytes = x_offset * bits_per_pixel / BITS_PER_BYTE;
	size_t crop_width_bytes = crop_width * bits_per_pixel / BITS_PER_BYTE;

	memmove(dst_buf, &src_buf[src_offset_bytes], crop_width_bytes);
}

static inline void mpix_crop_frame(const uint8_t *src_buf, size_t src_width, size_t src_height,
				   uint8_t *dst_buf, size_t x_offset, size_t y_offset,
				   size_t crop_width, size_t crop_height, uint8_t bits_per_pixel)
{
	size_t src_line_bytes = src_width * bits_per_pixel / BITS_PER_BYTE;
	size_t dst_line_bytes = crop_width * bits_per_pixel / BITS_PER_BYTE;

	for (size_t dst_h = 0; dst_h < crop_height && dst_h < src_height; dst_h++) {
		size_t src_h = y_offset + dst_h;
		size_t src_line_offset = src_h * src_line_bytes;
		size_t dst_line_offset = dst_h * dst_line_bytes;

		mpix_crop_line(&src_buf[src_line_offset], &dst_buf[dst_line_offset],
			       x_offset, crop_width, bits_per_pixel);
	}
}

int mpix_run_crop(struct mpix_base_op *base)
{
	struct mpix_operation *op = (void *)base;
	const uint8_t *src;
	uint8_t *dst;

	MPIX_OP_INPUT_LINES(base, &src, 1);

	/* Skip lines until we reach the crop region */
	if (base->line_offset < op->y_offset) {
		MPIX_OP_INPUT_DONE(base, 1);
		return 0;
	}

	/* Check if we're past the crop region */
	if (base->line_offset >= op->y_offset + op->height) {
		MPIX_OP_INPUT_DONE(base, 1);
		return 0;
	}

	MPIX_OP_OUTPUT_LINE(base, &dst);
	mpix_crop_line(src, dst, op->x_offset, op->width, mpix_bits_per_pixel(base->fmt.fourcc));
	MPIX_OP_OUTPUT_DONE(base);

	MPIX_OP_INPUT_DONE(base, 1);

	return 0;
}
