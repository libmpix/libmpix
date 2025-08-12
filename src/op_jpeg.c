/*
 * Copyright (c) 2025 Brilliant Labs Ltd.
 * Copyright (c) 2025 tinyVision.ai Inc.
 * SPDX-License-Identifier: Apache-2.0
 */

#include <assert.h>
#include <errno.h>

#include <mpix/formats.h>
#include <mpix/genlist.h>
#include <mpix/image.h>
#include <mpix/op_jpeg.h>
#include <mpix/utils.h>

#define JPEGENC_HIGHWATER_MARGIN 4096

// 444 = 8x8 420 = 16x16, conditions minimnum buffer size for JPEGAddMCU
#define JPEGENC_SUBSAMPLE JPEGE_SUBSAMPLE_444

// TODO: Ctrl for this
#define JPEGENC_Q JPEGE_Q_MED

#include "JPEGENC.h"

int init_jpeg(struct mpix_jpeg_op *op, uint8_t *buffer, size_t size)
{
	struct mpix_base_op *base = &op->base;
	int ret;

	switch (op->base.fourcc_src) {
	case MPIX_FMT_RGB565:
		op->image.ucPixelType = JPEGE_PIXEL_RGB565;
		break;
	case MPIX_FMT_RGB24:
		op->image.ucPixelType = JPEGE_PIXEL_RGB24;
		break;
	case MPIX_FMT_YUYV:
		op->image.ucPixelType = JPEGE_PIXEL_YUYV;
		break;
	default:
		return -ENOTSUP;
	}

	op->image.pOutput = buffer;
	op->image.iBufferSize = size;
	op->image.pHighWater = buffer + size - JPEGENC_HIGHWATER_MARGIN;

	ret = JPEGEncodeBegin(&op->image, &op->encoder, base->width, base->height,
			      op->image.ucPixelType, JPEGENC_SUBSAMPLE, JPEGENC_Q);
	if (ret != JPEGE_SUCCESS) {
		return -1;
	}

	return 0;
}

void mpix_jpeg_encode_op(struct mpix_base_op *base)
{
	struct mpix_jpeg_op *op = (void *)base;
	size_t bytespp = mpix_bits_per_pixel(base->fourcc_src) / 8;
	size_t pitch = mpix_op_pitch(base);
	size_t size;
	const uint8_t *src;
	uint8_t *dst;
	int ret;

	if (base->line_offset == 0) {
		dst = mpix_op_peek_output(base, &size);
		ret = init_jpeg(op, dst, size);
		assert(ret == 0);
	}

	/* Process a line full of 8x8 blocks */
	src = mpix_op_get_input_lines(base, 8);
	for (int i = 0; i < base->width; i += op->encoder.cx) {
		/* Discards const, JPEGENC doesnt actually write to it */
		ret = JPEGAddMCU(&op->image, &op->encoder, (uint8_t*)&(src[i*bytespp]), pitch);
		if (ret != JPEGE_SUCCESS) {
			MPIX_ERR("Failed to add an image block at column %u", i);
			return;
		}
	}

	if (base->line_offset == base->height) {
		MPIX_INF("JPEG frame conversion complete");
		JPEGEncodeEnd(&op->image);

		/* Set the number of bytes read from the output buffer */
		mpix_op_get_output_bytes(base, op->image.iDataSize);
	}
}
MPIX_REGISTER_JPEG_OP(enc_rgb565, mpix_jpeg_encode_op, RGB565, JPEG);
MPIX_REGISTER_JPEG_OP(enc_rgb24, mpix_jpeg_encode_op, RGB24, JPEG);
MPIX_REGISTER_JPEG_OP(enc_yuyv, mpix_jpeg_encode_op, YUYV, JPEG);

static const struct mpix_jpeg_op **mpix_jpeg_op_list =
	(const struct mpix_jpeg_op *[]){MPIX_LIST_JPEG_OP};

int mpix_image_jpeg_encode(struct mpix_image *img, enum mpix_jpeg_quality quality)
{
	struct mpix_jpeg_op *op = NULL;

	op = mpix_op_by_format(mpix_jpeg_op_list, img->fourcc, MPIX_FMT_JPEG);
	if (op == NULL) {
		MPIX_ERR("Conversion operation from %s to %s not found",
			 MPIX_FOURCC_TO_STR(img->fourcc), MPIX_FOURCC_TO_STR(MPIX_FMT_JPEG));
		return mpix_image_error(img, -ENOSYS);
	}

	return mpix_image_append_uncompressed_op(img, &op->base, sizeof(*op));
}
