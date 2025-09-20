/* SPDX-License-Identifier: Apache-2.0 */

#include <mpix/formats.h>
#include <mpix/low_level.h>
#include <mpix/operation.h>

MPIX_REGISTER_OP(convert, P_FOURCC);

int mpix_add_convert(struct mpix_image *img, const int32_t *params)
{
	struct mpix_base_op *op;
	size_t pitch = mpix_format_pitch(&img->fmt);

	/* Parameter validation */
	if (mpix_bits_per_pixel(params[P_FOURCC]) == 0) {
		return -EINVAL;
	}

	/* Add an operation */
	op = mpix_op_append(img, MPIX_OP_CONVERT, sizeof(*op), pitch);
	if (op == NULL) {
		return -ENOMEM;
	}

	/* Update the image format */
	img->fmt.fourcc = params[P_FOURCC];

	return 0;
}

void mpix_convert_rgb24_to_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width)
{
	memcpy(dst, src, width * 3);
}

void mpix_convert_grey_to_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width)
{
	for (int16_t w = 0; w < width; w++) {
		dst[w * 3 + 0] = dst[w * 3 + 1] = dst[w * 3 + 2] = src[w];
	}
}

void mpix_convert_rgb24_to_rgb332(const uint8_t *rgb24, uint8_t *rgb332, uint16_t width)
{
	for (size_t i = 0, o = 0, w = 0; w < width; w++, i += 3, o += 1) {
		rgb332[o] = 0;
		rgb332[o] |= (uint16_t)rgb24[i + 0] >> 5 << (0 + 3 + 2);
		rgb332[o] |= (uint16_t)rgb24[i + 1] >> 5 << (0 + 0 + 2);
		rgb332[o] |= (uint16_t)rgb24[i + 2] >> 6 << (0 + 0 + 0);
	}
}

void mpix_convert_rgb332_to_rgb24(const uint8_t *rgb332, uint8_t *rgb24, uint16_t width)
{
	for (size_t i = 0, o = 0, w = 0; w < width; w++, i += 1, o += 3) {
		rgb24[o + 0] = rgb332[i] >> (0 + 3 + 2) << 5;
		rgb24[o + 1] = rgb332[i] >> (0 + 0 + 2) << 5;
		rgb24[o + 2] = rgb332[i] >> (0 + 0 + 0) << 6;
	}
}

static inline uint16_t mpix_rgb24_to_rgb565(const uint8_t rgb24[3])
{
	uint16_t rgb565 = 0;

	rgb565 |= ((uint16_t)rgb24[0] >> 3 << (0 + 6 + 5));
	rgb565 |= ((uint16_t)rgb24[1] >> 2 << (0 + 0 + 5));
	rgb565 |= ((uint16_t)rgb24[2] >> 3 << (0 + 0 + 0));
	return rgb565;
}

static inline void mpix_rgb565_to_rgb24(uint16_t rgb565, uint8_t rgb24[3])
{
	rgb24[0] = rgb565 >> (0 + 6 + 5) << 3;
	rgb24[1] = rgb565 >> (0 + 0 + 5) << 2;
	rgb24[2] = rgb565 >> (0 + 0 + 0) << 3;
}

void mpix_convert_rgb24_to_rgb565be(const uint8_t *rgb24, uint8_t *rgb565be, uint16_t width)
{
	for (size_t i = 0, o = 0, w = 0; w < width; w++, i += 3, o += 2) {
		*(uint16_t *)&rgb565be[o] = mpix_htobe16(mpix_rgb24_to_rgb565(&rgb24[i]));
	}
}

void mpix_convert_rgb24_to_rgb565le(const uint8_t *rgb24, uint8_t *rgb565le, uint16_t width)
{
	for (size_t i = 0, o = 0, w = 0; w < width; w++, i += 3, o += 2) {
		*(uint16_t *)&rgb565le[o] = mpix_htole16(mpix_rgb24_to_rgb565(&rgb24[i]));
	}
}

void mpix_convert_rgb565be_to_rgb24(const uint8_t *rgb565be, uint8_t *rgb24, uint16_t width)
{
	for (size_t i = 0, o = 0, w = 0; w < width; w++, i += 2, o += 3) {
		mpix_rgb565_to_rgb24(mpix_be16toh(*(uint16_t *)&rgb565be[i]), &rgb24[o]);
	}
}

void mpix_convert_rgb565le_to_rgb24(const uint8_t *rgb565le, uint8_t *rgb24, uint16_t width)
{
	for (size_t i = 0, o = 0, w = 0; w < width; w++, i += 2, o += 3) {
		mpix_rgb565_to_rgb24(mpix_le16toh(*(uint16_t *)&rgb565le[i]), &rgb24[o]);
	}
}

#define Q(val) ((int32_t)((val) * (1 << 21)))

static inline uint8_t mpix_rgb24_to_y8_bt709(const uint8_t rgb24[3])
{
	int16_t r = rgb24[0], g = rgb24[1], b = rgb24[2];

	return CLAMP(((Q(+0.1826) * r + Q(+0.6142) * g + Q(+0.0620) * b) >> 21) + 16, 0x00, 0xff);
}

static inline uint8_t mpix_rgb24_to_u8_bt709(const uint8_t rgb24[3])
{
	int16_t r = rgb24[0], g = rgb24[1], b = rgb24[2];

	return CLAMP(((Q(-0.1006) * r + Q(-0.3386) * g + Q(+0.4392) * b) >> 21) + 128, 0x00, 0xff);
}

static inline uint8_t mpix_rgb24_to_v8_bt709(const uint8_t rgb24[3])
{
	int16_t r = rgb24[0], g = rgb24[1], b = rgb24[2];

	return CLAMP(((Q(+0.4392) * r + Q(-0.3989) * g + Q(-0.0403) * b) >> 21) + 128, 0x00, 0xff);
}

static inline void mpix_yuv24_to_rgb24_bt709(const uint8_t y, uint8_t u, uint8_t v,
					      uint8_t rgb24[3])
{
	int32_t yy = (int32_t)y - 16, uu = (int32_t)u - 128, vv = (int32_t)v - 128;

	/* Y range [16:235], U/V range [16:240], RGB range[0:255] (full range) */
	rgb24[0] = CLAMP((Q(+1.1644) * yy + Q(+0.0000) * uu + Q(+1.7928) * vv) >> 21, 0x00, 0xff);
	rgb24[1] = CLAMP((Q(+1.1644) * yy + Q(-0.2133) * uu + Q(-0.5330) * vv) >> 21, 0x00, 0xff);
	rgb24[2] = CLAMP((Q(+1.1644) * yy + Q(+2.1124) * uu + Q(+0.0000) * vv) >> 21, 0x00, 0xff);
}

#undef Q

uint8_t mpix_rgb24_get_luma_bt709(const uint8_t rgb24[3])
{
	return mpix_rgb24_to_y8_bt709(rgb24);
}

void mpix_convert_yuv24_to_rgb24_bt709(const uint8_t *yuv24, uint8_t *rgb24, uint16_t width)
{
	for (size_t i = 0, o = 0, w = 0; w < width; w++, i += 3, o += 3) {
		mpix_yuv24_to_rgb24_bt709(yuv24[i + 0], yuv24[i + 1], yuv24[i + 2], &rgb24[o]);
	}
}

void mpix_convert_rgb24_to_yuv24_bt709(const uint8_t *rgb24, uint8_t *yuv24, uint16_t width)
{
	for (size_t i = 0, o = 0, w = 0; w < width; w++, i += 3, o += 3) {
		yuv24[o + 0] = mpix_rgb24_to_y8_bt709(&rgb24[i]);
		yuv24[o + 1] = mpix_rgb24_to_u8_bt709(&rgb24[i]);
		yuv24[o + 2] = mpix_rgb24_to_v8_bt709(&rgb24[i]);
	}
}

void mpix_convert_yuv24_to_yuyv(const uint8_t *yuv24, uint8_t *yuyv, uint16_t width)
{
	for (size_t i = 0, o = 0, w = 0; w + 2 <= width; w += 2, i += 6, o += 4) {
		/* Pixel 0 */
		yuyv[o + 0] = yuv24[i + 0];
		yuyv[o + 1] = yuv24[i + 1];
		/* Pixel 1 */
		yuyv[o + 2] = yuv24[i + 3];
		yuyv[o + 3] = yuv24[i + 5];
	}
}

void mpix_convert_yuyv_to_yuv24(const uint8_t *yuyv, uint8_t *yuv24, uint16_t width)
{
	for (size_t i = 0, o = 0, w = 0; w + 2 <= width; w += 2, i += 4, o += 6) {
		/* Pixel 0 */
		yuv24[o + 0] = yuyv[i + 0];
		yuv24[o + 1] = yuyv[i + 1];
		yuv24[o + 2] = yuyv[i + 3];
		/* Pixel 1 */
		yuv24[o + 3] = yuyv[i + 2];
		yuv24[o + 4] = yuyv[i + 1];
		yuv24[o + 5] = yuyv[i + 3];
	}
}

void mpix_convert_rgb24_to_yuyv_bt709(const uint8_t *rgb24, uint8_t *yuyv, uint16_t width)
{
	for (size_t i = 0, o = 0, w = 0; w + 2 <= width; w += 2, i += 6, o += 4) {
		/* Pixel 0 */
		yuyv[o + 0] = mpix_rgb24_to_y8_bt709(&rgb24[i + 0]);
		yuyv[o + 1] = mpix_rgb24_to_u8_bt709(&rgb24[i + 0]);
		/* Pixel 1 */
		yuyv[o + 2] = mpix_rgb24_to_y8_bt709(&rgb24[i + 3]);
		yuyv[o + 3] = mpix_rgb24_to_v8_bt709(&rgb24[i + 3]);
	}
}

void mpix_convert_yuyv_to_rgb24_bt709(const uint8_t *yuyv, uint8_t *rgb24, uint16_t width)
{
	for (size_t i = 0, o = 0, w = 0; w + 2 <= width; w += 2, i += 4, o += 6) {
		/* Pixel 0 */
		mpix_yuv24_to_rgb24_bt709(yuyv[i + 0], yuyv[i + 1], yuyv[i + 3], &rgb24[o + 0]);
		/* Pixel 1 */
		mpix_yuv24_to_rgb24_bt709(yuyv[i + 2], yuyv[i + 1], yuyv[i + 3], &rgb24[o + 3]);
	}
}

void mpix_convert_y8_to_rgb24_bt709(const uint8_t *y8, uint8_t *rgb24, uint16_t width)
{
	for (size_t i = 0, o = 0, w = 0; w < width; w++, i += 1, o += 3) {
		mpix_yuv24_to_rgb24_bt709(y8[i], UINT8_MAX / 2, UINT8_MAX / 2, &rgb24[o]);
	}
}

void mpix_convert_rgb24_to_y8_bt709(const uint8_t *rgb24, uint8_t *y8, uint16_t width)
{
	for (size_t i = 0, o = 0, w = 0; w < width; w++, i += 3, o += 1) {
		y8[o] = mpix_rgb24_to_y8_bt709(&rgb24[i]);
	}
}

int mpix_run_convert(struct mpix_base_op *base)
{
	const uint8_t *src;
	uint8_t *dst;
	uint32_t src_fmt = base->fmt.fourcc;
	uint32_t dst_fmt = base->next->fmt.fourcc;
	uint16_t width = base->fmt.width;

	MPIX_OP_INPUT_LINES(base, &src, 1);
	MPIX_OP_OUTPUT_LINE(base, &dst);

	if (src_fmt == MPIX_FMT_RGB24 && dst_fmt == MPIX_FMT_RGB24) {
		mpix_convert_rgb24_to_rgb24(src, dst, width);
	} else if (src_fmt == MPIX_FMT_RGB24 && dst_fmt == MPIX_FMT_RGB332) {
		mpix_convert_rgb24_to_rgb332(src, dst, width);
	} else if (src_fmt == MPIX_FMT_RGB332 && dst_fmt == MPIX_FMT_RGB24) {
		mpix_convert_rgb332_to_rgb24(src, dst, width);
	} else if (src_fmt == MPIX_FMT_RGB24 && dst_fmt == MPIX_FMT_RGB565X) {
		mpix_convert_rgb24_to_rgb565be(src, dst, width);
	} else if (src_fmt == MPIX_FMT_RGB24 && dst_fmt == MPIX_FMT_RGB565) {
		mpix_convert_rgb24_to_rgb565le(src, dst, width);
	} else if (src_fmt == MPIX_FMT_RGB565X && dst_fmt == MPIX_FMT_RGB24) {
		mpix_convert_rgb565be_to_rgb24(src, dst, width);
	} else if (src_fmt == MPIX_FMT_RGB565 && dst_fmt == MPIX_FMT_RGB24) {
		mpix_convert_rgb565le_to_rgb24(src, dst, width);
	} else if (src_fmt == MPIX_FMT_YUV24 && dst_fmt == MPIX_FMT_RGB24) {
		mpix_convert_yuv24_to_rgb24_bt709(src, dst, width);
	} else if (src_fmt == MPIX_FMT_RGB24 && dst_fmt == MPIX_FMT_YUV24) {
		mpix_convert_rgb24_to_yuv24_bt709(src, dst, width);
	} else if (src_fmt == MPIX_FMT_YUV24 && dst_fmt == MPIX_FMT_YUYV) {
		mpix_convert_yuv24_to_yuyv(src, dst, width);
	} else if (src_fmt == MPIX_FMT_YUYV && dst_fmt == MPIX_FMT_YUV24) {
		mpix_convert_yuyv_to_yuv24(src, dst, width);
	} else if (src_fmt == MPIX_FMT_RGB24 && dst_fmt == MPIX_FMT_YUYV) {
		mpix_convert_rgb24_to_yuyv_bt709(src, dst, width);
	} else if (src_fmt == MPIX_FMT_YUYV && dst_fmt == MPIX_FMT_RGB24) {
		mpix_convert_yuyv_to_rgb24_bt709(src, dst, width);
	} else {
		return -ENOTSUP;
	}

	MPIX_OP_OUTPUT_DONE(base);
	MPIX_OP_INPUT_DONE(base, 1);

	return 0;
}
