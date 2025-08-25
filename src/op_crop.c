/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <mpix/genlist.h>
#include <mpix/image.h>
#include <mpix/op_crop.h>
#include <mpix/utils.h>

static inline void mpix_crop_line(const uint8_t *src_buf, size_t src_width, uint8_t *dst_buf,
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

	for (size_t dst_h = 0; dst_h < crop_height; dst_h++) {
		size_t src_h = y_offset + dst_h;
		size_t src_line_offset = src_h * src_line_bytes;
		size_t dst_line_offset = dst_h * dst_line_bytes;

		mpix_crop_line(&src_buf[src_line_offset], src_width, &dst_buf[dst_line_offset],
			       x_offset, crop_width, bits_per_pixel);
	}
}

__attribute__((weak))
void mpix_crop_frame_raw24(const uint8_t *src_buf, size_t src_width, size_t src_height,
			   uint8_t *dst_buf, size_t x_offset, size_t y_offset,
			   size_t crop_width, size_t crop_height)
{
	mpix_crop_frame(src_buf, src_width, src_height, dst_buf, x_offset, y_offset,
			crop_width, crop_height, 24);
}

__attribute__((weak))
void mpix_crop_frame_raw16(const uint8_t *src_buf, size_t src_width, size_t src_height,
			   uint8_t *dst_buf, size_t x_offset, size_t y_offset,
			   size_t crop_width, size_t crop_height)
{
	mpix_crop_frame(src_buf, src_width, src_height, dst_buf, x_offset, y_offset,
			crop_width, crop_height, 16);
}

__attribute__((weak))
void mpix_crop_frame_raw8(const uint8_t *src_buf, size_t src_width, size_t src_height,
			  uint8_t *dst_buf, size_t x_offset, size_t y_offset,
			  size_t crop_width, size_t crop_height)
{
	mpix_crop_frame(src_buf, src_width, src_height, dst_buf, x_offset, y_offset,
			crop_width, crop_height, 8);
}

static inline void mpix_crop_op(struct mpix_base_op *base, uint8_t bits_per_pixel)
{
	struct mpix_crop_op *crop_op = (struct mpix_crop_op *)base;
	
	/* Check if we're past the crop region */
	if (base->line_offset >= crop_op->y_offset + crop_op->crop_height) {
		/* We're done cropping, consume remaining input without output */
		if (base->line_offset < base->height) {
			mpix_op_get_input_line(base);
		}
		return;
	}

	/* Skip lines until we reach the crop region */
	if (base->line_offset < crop_op->y_offset) {
		/* Skip lines before the crop region */
		mpix_op_get_input_line(base);
		return;
	}

	/* Process line within the crop region */
	const uint8_t *line_in = mpix_op_get_input_line(base);
	uint8_t *line_out = mpix_op_get_output_line(base);

	mpix_crop_line(line_in, base->width, line_out, crop_op->x_offset,
		       crop_op->crop_width, bits_per_pixel);

	mpix_op_done(base);
}

__attribute__((weak))
void mpix_crop_op_raw24(struct mpix_base_op *base)
{
	mpix_crop_op(base, 24);
}
MPIX_REGISTER_CROP_OP(rgb24, mpix_crop_op_raw24, RGB24);
MPIX_REGISTER_CROP_OP(yuv24, mpix_crop_op_raw24, YUV24);

__attribute__((weak))
void mpix_crop_op_raw16(struct mpix_base_op *base)
{
	mpix_crop_op(base, 16);
}
MPIX_REGISTER_CROP_OP(rgb565, mpix_crop_op_raw16, RGB565);
MPIX_REGISTER_CROP_OP(rgb565x, mpix_crop_op_raw16, RGB565X);

__attribute__((weak))
void mpix_crop_op_raw8(struct mpix_base_op *base)
{
	mpix_crop_op(base, 8);
}
MPIX_REGISTER_CROP_OP(grey, mpix_crop_op_raw8, GREY);
MPIX_REGISTER_CROP_OP(rgb332, mpix_crop_op_raw8, RGB332);

static const struct mpix_crop_op **mpix_crop_op_list =
	(const struct mpix_crop_op *[]){MPIX_LIST_CROP_OP};

int mpix_image_crop(struct mpix_image *img, uint16_t x_offset, uint16_t y_offset,
		    uint16_t crop_width, uint16_t crop_height)
{
	const struct mpix_crop_op *op = NULL;
	struct mpix_crop_op *crop_op = NULL;
	int ret;

	/* Validate crop parameters */
	if (x_offset + crop_width > img->width || y_offset + crop_height > img->height) {
		MPIX_ERR("Crop region (%d,%d) %dx%d exceeds image bounds %dx%d",
			 x_offset, y_offset, crop_width, crop_height, img->width, img->height);
		return mpix_image_error(img, -EINVAL);
	}

	if (crop_width == 0 || crop_height == 0) {
		MPIX_ERR("Crop dimensions must be greater than zero");
		return mpix_image_error(img, -EINVAL);
	}

	op = mpix_op_by_format(mpix_crop_op_list, img->fourcc, img->fourcc);
	if (op == NULL) {
		MPIX_ERR("Crop operation for %s not found", MPIX_FOURCC_TO_STR(img->fourcc));
		return mpix_image_error(img, -ENOSYS);
	}

	ret = mpix_image_append_uncompressed_op(img, &op->base, sizeof(*op));
	if (ret < 0) {
		return ret;
	}

	/* Get the allocated operation and configure it */
	crop_op = (struct mpix_crop_op *)img->ops.last;
	crop_op->x_offset = x_offset;
	crop_op->y_offset = y_offset;
	crop_op->crop_width = crop_width;
	crop_op->crop_height = crop_height;

	/* Update the image dimensions to reflect the crop */
	img->width = crop_width;
	img->height = crop_height;

	return ret;
}
