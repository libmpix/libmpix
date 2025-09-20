/* SPDX-License-Identifier: Apache-2.0 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <mpix/formats.h>
#include <mpix/sample.h>
#include <mpix/utils.h>
#include <mpix/low_level.h>

static uint32_t mpix_lcg_rand_u32(void)
{
	static uint32_t lcg_state;

	/* Linear Congruent Generator (LCG) are low-quality but very fast, here considered enough
	 * as even a fixed offset would have been enough.The % phase is skipped as there is already
	 * "% vbuf->bytesused" downstream in the code.
	 *
	 * The constants are from https://en.wikipedia.org/wiki/Linear_congruential_generator
	 */
	lcg_state = lcg_state * 1103515245 + 12345;
	return lcg_state;
}

static inline void mpix_sample_random_raw24(const uint8_t *buf, uint16_t width, uint16_t height,
					    uint8_t rgb[3])
{
	uint32_t i = (mpix_lcg_rand_u32() % (width * height)) * 3;

	memcpy(rgb, &buf[i], 3);
}

static inline void mpix_sample_random_yuyv(const uint8_t *buf, uint16_t width, uint16_t height,
					    uint8_t rgb[3])
{
	uint32_t i = mpix_lcg_rand_u32() % (width * height) / 2 * 4;
	uint8_t rgb2[3 * 2];

	mpix_convert_yuyv_to_rgb24_bt709(&buf[i], rgb2, 2);
	memcpy(rgb, rgb2, 3);
}

static inline void mpix_sample_random_rgb565le(const uint8_t *buf, uint16_t width, uint16_t height,
					    uint8_t rgb[3])
{
	uint32_t i = mpix_lcg_rand_u32() % (width * height) * 2;

	mpix_convert_rgb565le_to_rgb24(&buf[i], rgb, 1);
}

static inline void mpix_sample_random_bayer(const uint8_t *buf, uint16_t width, uint16_t height,
					    uint8_t rgb[3], int i0, int i1, int i2, int i3)
{
	uint32_t w = (mpix_lcg_rand_u32() % width) & ~1U;
	uint32_t h = (mpix_lcg_rand_u32() % height) & ~1U;

	rgb[i0] = buf[(h + 0) * width + (w + 0)];
	rgb[i1] = buf[(h + 0) * width + (w + 1)];
	rgb[i2] = buf[(h + 1) * width + (w + 0)];
	rgb[i3] = buf[(h + 1) * width + (w + 1)];
}

int mpix_sample_random_rgb(const uint8_t *buf, const struct mpix_format *fmt, uint8_t *rgb)
{
	enum { R, G, B };

	switch (fmt->fourcc) {
	case MPIX_FMT_RGB24:
		mpix_sample_random_raw24(buf, fmt->width, fmt->height, rgb);
		break;
	case MPIX_FMT_RGB565:
		mpix_sample_random_rgb565le(buf, fmt->width, fmt->height, rgb);
		break;
	case MPIX_FMT_YUYV:
		mpix_sample_random_yuyv(buf, fmt->width, fmt->height, rgb);
		break;
	case MPIX_FMT_SRGGB8:
		mpix_sample_random_bayer(buf, fmt->width, fmt->height, rgb, R, G, G, B);
		break;
	case MPIX_FMT_SBGGR8:
		mpix_sample_random_bayer(buf, fmt->width, fmt->height, rgb, B, G, G, R);
		break;
	case MPIX_FMT_SGBRG8:
		mpix_sample_random_bayer(buf, fmt->width, fmt->height, rgb, G, B, R, G);
		break;
	case MPIX_FMT_SGRBG8:
		mpix_sample_random_bayer(buf, fmt->width, fmt->height, rgb, G, R, B, G);
		break;
	default:
		MPIX_ERR("Unsupported pixel format %s", MPIX_FOURCC_TO_STR(fmt->fourcc));
		return -ENOTSUP;
	}

	return 0;
}
