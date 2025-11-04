/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Helium-optimized JPEG op entry (strong symbol override)
 *
 * This file provides a strong-definition mpix_jpeg_encode_op to override the
 * weak one in op_jpeg.c when building on Cortex-M55 (Helium). The core
 * performance work happens inside JPEGENC_helium.c.
 */

#include <stdint.h>
#include <assert.h>
#include <mpix/formats.h>
#include <mpix/image.h>
#include <mpix/types.h>
#include <mpix/utils.h>

#include "JPEGENC.h"

// Helium wrapper API provided by JPEGENC_helium.c
int JPEGEncodeBegin_Helium(JPEGE_IMAGE *pJPEG, JPEGENCODE *pEncode, int iWidth, int iHeight,
			   uint8_t ucPixelType, uint8_t ucSubSample, uint8_t ucQFactor);
int JPEGEncodeEnd_Helium(JPEGE_IMAGE *pJPEG);
int JPEGAddMCU_Helium(JPEGE_IMAGE *pJPEG, JPEGENCODE *pEncode, uint8_t *pPixels, int iPitch);

#ifndef JPEGENC_HIGHWATER_MARGIN
#define JPEGENC_HIGHWATER_MARGIN 4096
#endif

#ifndef JPEGENC_SUBSAMPLE
#define JPEGENC_SUBSAMPLE JPEGE_SUBSAMPLE_444
#endif

#ifndef JPEGENC_Q
#define JPEGENC_Q JPEGE_Q_BEST
#endif

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

static int init_jpeg_helium(struct mpix_operation *op, uint8_t *buffer, size_t size)
{
	struct mpix_base_op *base = &op->base;
	int ret;

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
		return -1;
	}

	op->image.pOutput = buffer;
	op->image.iBufferSize = size;
	op->image.pHighWater = buffer + size - JPEGENC_HIGHWATER_MARGIN;

	ret = JPEGEncodeBegin_Helium(&op->image, &op->encoder, base->fmt.width, base->fmt.height,
				     op->image.ucPixelType, JPEGENC_SUBSAMPLE, JPEGENC_Q);
	if (ret != JPEGE_SUCCESS) {
		return -1;
	}
	return 0;
}

int mpix_jpeg_encode_op(struct mpix_base_op *base)
{
	// Strong symbol override to prefer Helium path when compiled in
	struct mpix_operation *op = (void *)base;
	size_t bytespp = mpix_bits_per_pixel(base->fmt.fourcc) / 8;
	size_t pitch = mpix_format_pitch(&base->fmt);
	size_t size;
	const uint8_t *src;
	uint8_t *dst;
	int ret;

	MPIX_OP_INPUT_BYTES(base, &src, pitch * 8);
	MPIX_OP_OUTPUT_PEEK(base, &dst, &size);

	if (base->line_offset == 0) {
		ret = init_jpeg_helium(op, dst, size);
		assert(ret == 0);
	}

	// 逐批获取8行输入，但只在最后一次 flush 计时，得到整帧 Helium JPEG 编码耗时
	for (int i = 0; i < base->fmt.width; i += op->encoder.cx) {
		ret = JPEGAddMCU_Helium(&op->image, &op->encoder, (uint8_t *)&(src[i * bytespp]),
					pitch);
		if (ret != JPEGE_SUCCESS) {
			MPIX_ERR("Failed to add an image block at column %u", i);
			return -1;
		}
		MPIX_OP_OUTPUT_DONE(base); /* 单次 flush，统计整帧 */
	}

	if (base->line_offset == base->fmt.height) {
		JPEGEncodeEnd_Helium(&op->image);
		MPIX_OP_OUTPUT_FLUSH(base, op->image.iDataSize);
		MPIX_OP_OUTPUT_DONE(base); /* 单次 flush，统计整帧 */
	}
	return 0;
}
