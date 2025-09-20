/* SPDX-License-Identifier: Apache-2.0 */
/* Copyright (c) 2025 Brilliant Labs Ltd. */
/* Copyright (c) 2025 tinyVision.ai Inc. */

#include <mpix/low_level.h>
#include <mpix/operation.h>

MPIX_REGISTER_OP(jpeg_encode);

#include "JPEGENC.h"

struct mpix_operation {
	/** Fields common to all operations. */
	struct mpix_base_op base;
	/** Controls */
	int32_t quality;
	/** Image context from the JPEGENC library */
	JPEGE_IMAGE image;
	/** Image encoder context from the JPEGENC library */
	JPEGENCODE encoder;
};

int mpix_add_jpeg_encode(struct mpix_image *img, const int32_t *params)
{
	struct mpix_operation *op;
	size_t pitch = mpix_format_pitch(&img->fmt);

	(void)params;

	/* Add an operation */
	op = mpix_op_append(img, MPIX_OP_JPEG_ENCODE, sizeof(*op), pitch * 8);
	if (op == NULL) {
		return -ENOMEM;
	}

	/* Register controls */
	img->ctrls[MPIX_CID_JPEG_QUALITY] = &op->quality;

	/* Update the image format */
	img->fmt.fourcc = MPIX_FMT_JPEG;
	img->fmt.width -= img->fmt.width % 8;
	img->fmt.height -= img->fmt.height % 8;

	return 0;
}

// 444 = 8x8 420 = 16x16, conditions minimnum buffer size for JPEGAddMCU
#define JPEGENC_SUBSAMPLE JPEGE_SUBSAMPLE_444

#define JPEGENC_HIGHWATER_MARGIN 4096

int mpix_jpeg_encode_init(struct mpix_operation *op, uint8_t *dst, size_t size)
{
	switch (op->base.fmt.fourcc) {
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

	struct mpix_format *fmt = &op->base.next->fmt;
	op->image.pOutput = dst;
	op->image.iBufferSize = size;
	op->image.pHighWater = &dst[size] - JPEGENC_HIGHWATER_MARGIN;

	return JPEGEncodeBegin(&op->image, &op->encoder, fmt->width, fmt->height,
			       op->image.ucPixelType, JPEGENC_SUBSAMPLE, op->quality);
}

__attribute__((weak)) int mpix_run_jpeg_encode(struct mpix_base_op *base)
{
	struct mpix_operation *op = (void *)base;
	const uint8_t *src;
	uint8_t *dst;
	size_t bytespp = mpix_bits_per_pixel(base->fmt.fourcc) / BITS_PER_BYTE;
	size_t pitch = mpix_format_pitch(&base->fmt);
	size_t dst_size;
	int err;

	MPIX_OP_INPUT_BYTES(base, &src, pitch * 8);
	MPIX_OP_OUTPUT_PEEK(base, &dst, &dst_size);

	if (base->line_offset == 0) {
		err = mpix_jpeg_encode_init(op, dst, dst_size);
		if (err != JPEGE_SUCCESS) {
			return -EBADMSG;
		}
	}

	/* Process a line full of 8x8 blocks */
	for (int i = 0; i + op->encoder.cx <= base->fmt.width; i += op->encoder.cx) {
		/* Discards const, JPEGENC doesnt actually write to it */
		err = JPEGAddMCU(&op->image, &op->encoder, (uint8_t *)&(src[i * bytespp]), pitch);
		if (err != JPEGE_SUCCESS) {
			MPIX_ERR("Failed to add an image block at column %u", i);
			return err;
		}
	}

	if (base->line_offset + 8 >= base->fmt.height) {
		JPEGEncodeEnd(&op->image);

		MPIX_OP_OUTPUT_FLUSH(base, op->image.iDataSize);
		MPIX_OP_OUTPUT_DONE(base);
	}

	MPIX_OP_INPUT_DONE(base, 8);

	return 0;
}
