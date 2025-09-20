/* Copyright (c) 2025 tinyVision.ai Inc. */
/* SPDX-License-Identifier: Apache-2.0 */

#include <assert.h>

#include <mpix/formats.h>
#include <mpix/image.h>
#include <mpix/low_level.h>
#include <mpix/print.h>
#include <mpix/utils.h>

/*
 * To get YUV BT.709 test data:
 *
 *	ffmpeg -y -f lavfi -colorspace bt709 -i color=#RRGGBB:2x2:d=3,format=rgb24 \
 *		-f rawvideo -pix_fmt yuyv422 - | hexdump -C
 *
 * To get RGB565 test data:
 *
 *	ffmpeg -y -f lavfi -i color=#RRGGBB:2x2:d=3,format=rgb24 \
 *		-f rawvideo -pix_fmt rgb565 - | hexdump -C
 */

const struct color_ref {
	uint8_t rgb24[3];
	uint8_t rgb565[2];
	uint8_t rgb332[1];
	uint8_t yuv24_bt709[3];
	uint8_t yuv24_bt601[3];
} reference_data[] = {

	/* Primary colors */
	{{0x00, 0x00, 0x00}, {0x00, 0x00}, {0x00}, {0x10, 0x80, 0x80}, {0x10, 0x80, 0x80}},
	{{0x00, 0x00, 0xff}, {0x00, 0x1f}, {0x03}, {0x20, 0xf0, 0x76}, {0x29, 0xf1, 0x6e}},
	{{0x00, 0xff, 0x00}, {0x07, 0xe0}, {0x1c}, {0xad, 0x2a, 0x1a}, {0x9a, 0x2a, 0x35}},
	{{0x00, 0xff, 0xff}, {0x07, 0xff}, {0x1f}, {0xbc, 0x9a, 0x10}, {0xb4, 0xa0, 0x23}},
	{{0xff, 0x00, 0x00}, {0xf8, 0x00}, {0xe0}, {0x3f, 0x66, 0xf0}, {0x50, 0x5b, 0xee}},
	{{0xff, 0x00, 0xff}, {0xf8, 0x1f}, {0xe3}, {0x4e, 0xd6, 0xe6}, {0x69, 0xcb, 0xdc}},
	{{0xff, 0xff, 0x00}, {0xff, 0xe0}, {0xfc}, {0xdb, 0x10, 0x8a}, {0xd0, 0x0a, 0x93}},
	{{0xff, 0xff, 0xff}, {0xff, 0xff}, {0xff}, {0xeb, 0x80, 0x80}, {0xeb, 0x80, 0x80}},

	/* Arbitrary colors */
	{{0x00, 0x70, 0xc5}, {0x03, 0x98}, {0x0f}, {0x61, 0xb1, 0x4b}, {0x5e, 0xb5, 0x4d}},
	{{0x33, 0x8d, 0xd1}, {0x3c, 0x7a}, {0x33}, {0x7d, 0xa7, 0x56}, {0x7b, 0xab, 0x57}},
	{{0x66, 0xa9, 0xdc}, {0x6d, 0x5b}, {0x77}, {0x98, 0x9d, 0x61}, {0x96, 0xa0, 0x61}},
	{{0x7d, 0xd2, 0xf7}, {0x86, 0x9e}, {0x7b}, {0xb7, 0x99, 0x59}, {0xb3, 0x9d, 0x5a}},
	{{0x97, 0xdb, 0xf9}, {0x9e, 0xde}, {0x9b}, {0xc2, 0x94, 0x61}, {0xbf, 0x97, 0x62}},
	{{0xb1, 0xe4, 0xfa}, {0xb7, 0x3f}, {0xbf}, {0xcc, 0x8f, 0x69}, {0xca, 0x91, 0x69}},
	{{0x79, 0x29, 0xd2}, {0x79, 0x5a}, {0x67}, {0x4c, 0xc2, 0x9c}, {0x57, 0xbf, 0x96}},
	{{0x94, 0x54, 0xdb}, {0x9a, 0xbb}, {0x8b}, {0x6c, 0xb5, 0x97}, {0x75, 0xb3, 0x92}},
	{{0xaf, 0x7f, 0xe4}, {0xb3, 0xfc}, {0xaf}, {0x8c, 0xa8, 0x91}, {0x93, 0xa6, 0x8d}},
};

void show_color_buf(const char *label, const uint8_t *buf, size_t size,
		    const struct mpix_format *fmt)
{
	printf("%s: %s [", label, MPIX_FOURCC_TO_STR(fmt->fourcc));
	mpix_print_2_rows(buf, buf, fmt->width, fmt->fourcc, true);
	printf("]");
	for (size_t i = 0; i < size; i++) {
		printf(" %02x", buf[i]);
	}
	printf("\n");
}

bool test_conversion(const uint8_t *src_buf, const uint8_t *dst_buf, const uint8_t *ref_buf,
		     struct mpix_format *src_fmt, struct mpix_format *dst_fmt, uint8_t err_margin)
{
	size_t src_size = mpix_format_pitch(src_fmt);
	size_t dst_size = mpix_format_pitch(dst_fmt);

	printf("\n");
	show_color_buf("src", src_buf, src_size, src_fmt);
	show_color_buf("dst", dst_buf, dst_size, dst_fmt);
	show_color_buf("ref", ref_buf, dst_size, dst_fmt);

	/* Scan the result against the reference output pixel to make sure it worked */
	for (size_t i = 0; i < dst_size; i++) {
		if (!WITHIN(dst_buf[i], ref_buf[i], err_margin)) {
			return false;
		}
	}

	return true;
}

void test_low_level(void)
{
	enum { ERR = 9 };
	static uint8_t dst_buf[100] = {};

	for (size_t i = 0; i < ARRAY_SIZE(reference_data); i++) {
		/* The current color we are testing */
		const struct color_ref *ref = &reference_data[i];

		/* Generate very small buffers out of the reference tables */
		const uint8_t rgb24[] = {
			ref->rgb24[0], ref->rgb24[1], ref->rgb24[2], /* Left */
			ref->rgb24[0], ref->rgb24[1], ref->rgb24[2], /* Right */
		};
		const uint8_t rgb565be[] = {
			ref->rgb565[0], ref->rgb565[1], /* Left */
			ref->rgb565[0], ref->rgb565[1], /* Right */
		};
		const uint8_t rgb565le[] = {
			ref->rgb565[1], ref->rgb565[0], /* Left */
			ref->rgb565[1], ref->rgb565[0], /* Right */
		};
		const uint8_t rgb332[] = {
			ref->rgb332[0], /* Left */
			ref->rgb332[0], /* Right */
		};
		const uint8_t yuv24_bt709[] = {
			ref->yuv24_bt709[0], ref->yuv24_bt709[1], ref->yuv24_bt709[2], /* Left */
			ref->yuv24_bt709[0], ref->yuv24_bt709[1], ref->yuv24_bt709[2], /* Right */
		};
		const uint8_t yuyv_bt709[] = {
			ref->yuv24_bt709[0], ref->yuv24_bt709[1], /* Left */
			ref->yuv24_bt709[0], ref->yuv24_bt709[2], /* Right */
		};

		/* Set the width to 2 for YUYV being patterns in blocks of 2 pixels */
		struct mpix_format src_fmt = {.width = 2, .height = 1 };
		struct mpix_format dst_fmt = {.width = 2, .height = 1 };

		printf("\nColor #%02x%02x%02x\n", ref->rgb24[0], ref->rgb24[1],
				 ref->rgb24[2]);

		src_fmt.fourcc = MPIX_FMT_RGB24;
		dst_fmt.fourcc = MPIX_FMT_RGB565X;
		mpix_convert_rgb24_to_rgb565be(rgb24, dst_buf, dst_fmt.width);
		assert(test_conversion(rgb24, dst_buf, rgb565be, &src_fmt, &dst_fmt, ERR));

		src_fmt.fourcc = MPIX_FMT_RGB24;
		dst_fmt.fourcc = MPIX_FMT_RGB565;
		mpix_convert_rgb24_to_rgb565le(rgb24, dst_buf, dst_fmt.width);
		assert(test_conversion(rgb24, dst_buf, rgb565le, &src_fmt, &dst_fmt, ERR));

		src_fmt.fourcc = MPIX_FMT_RGB24;
		dst_fmt.fourcc = MPIX_FMT_RGB332;
		mpix_convert_rgb24_to_rgb332(rgb24, dst_buf, dst_fmt.width);
		assert(test_conversion(rgb24, dst_buf, rgb332, &src_fmt, &dst_fmt, ERR));

		src_fmt.fourcc = MPIX_FMT_RGB565X;
		dst_fmt.fourcc = MPIX_FMT_RGB24;
		mpix_convert_rgb565be_to_rgb24(rgb565be, dst_buf, dst_fmt.width);
		assert(test_conversion(rgb565be, dst_buf, rgb24, &src_fmt, &dst_fmt, ERR));

		src_fmt.fourcc = MPIX_FMT_RGB565;
		dst_fmt.fourcc = MPIX_FMT_RGB24;
		mpix_convert_rgb565le_to_rgb24(rgb565le, dst_buf, dst_fmt.width);
		assert(test_conversion(rgb565le, dst_buf, rgb24, &src_fmt, &dst_fmt, ERR));

		src_fmt.fourcc = MPIX_FMT_RGB24;
		dst_fmt.fourcc = MPIX_FMT_YUYV;
		mpix_convert_rgb24_to_yuyv_bt709(rgb24, dst_buf, dst_fmt.width);
		assert(test_conversion(rgb24, dst_buf, yuyv_bt709, &src_fmt, &dst_fmt, ERR));

		src_fmt.fourcc = MPIX_FMT_YUYV;
		dst_fmt.fourcc = MPIX_FMT_RGB24;
		mpix_convert_yuyv_to_rgb24_bt709(yuyv_bt709, dst_buf, dst_fmt.width);
		assert(test_conversion(yuyv_bt709, dst_buf, rgb24, &src_fmt, &dst_fmt, ERR));

		src_fmt.fourcc = MPIX_FMT_RGB24;
		dst_fmt.fourcc = MPIX_FMT_YUV24;
		mpix_convert_rgb24_to_yuv24_bt709(rgb24, dst_buf, dst_fmt.width);
		assert(test_conversion(rgb24, dst_buf, yuv24_bt709, &src_fmt, &dst_fmt, ERR));

		src_fmt.fourcc = MPIX_FMT_YUV24;
		dst_fmt.fourcc = MPIX_FMT_RGB24;
		mpix_convert_yuv24_to_rgb24_bt709(yuv24_bt709, dst_buf, dst_fmt.width);
		assert(test_conversion(yuv24_bt709, dst_buf, rgb24, &src_fmt, &dst_fmt, ERR));

		src_fmt.fourcc = MPIX_FMT_YUV24;
		dst_fmt.fourcc = MPIX_FMT_YUYV;
		mpix_convert_yuv24_to_yuyv(yuv24_bt709, dst_buf, dst_fmt.width);
		assert(test_conversion(yuv24_bt709, dst_buf, yuyv_bt709, &src_fmt, &dst_fmt, ERR));

		src_fmt.fourcc = MPIX_FMT_YUYV;
		dst_fmt.fourcc = MPIX_FMT_YUV24;
		mpix_convert_yuyv_to_yuv24(yuyv_bt709, dst_buf, dst_fmt.width);
		assert(test_conversion(yuyv_bt709, dst_buf, yuv24_bt709, &src_fmt, &dst_fmt, ERR));
	}
}

void test_high_level(void)
{
	enum { WIDTH = 8, HEIGHT = 8, };
	uint8_t src_buf[WIDTH * HEIGHT * 3] = {};
	uint8_t dst_buf[WIDTH * HEIGHT * 3] = {};
	struct mpix_image img = {
		.buffer = src_buf,
		.size = sizeof(src_buf),
		.fmt = {.width = WIDTH, .height = HEIGHT, .fourcc = MPIX_FMT_RGB24},
	};

	/* Generate test input data */
	for (size_t i = 0; i < sizeof(src_buf); i++) {
		src_buf[i] = i / 3;
	}

	printf("input:\n");
	mpix_print_buf(src_buf, sizeof(src_buf), &img.fmt, true);

	assert(mpix_image_convert(&img, MPIX_FMT_RGB24) == 0);

	/* Test the RGB24 <-> RGB565 conversion */
	assert(mpix_image_convert(&img, MPIX_FMT_RGB565) == 0);
	assert(mpix_image_convert(&img, MPIX_FMT_RGB24) == 0);

	/* Test the RGB24 <-> RGB565X conversion */
	assert(mpix_image_convert(&img, MPIX_FMT_RGB565X) == 0);
	assert(mpix_image_convert(&img, MPIX_FMT_RGB24) == 0);

	/* Test the RGB24 <-> YUV24 conversion */
	assert(mpix_image_convert(&img, MPIX_FMT_YUV24) == 0);
	assert(mpix_image_convert(&img, MPIX_FMT_RGB24) == 0);

	/* Test the YUYV <-> YUV24 conversion */
	assert(mpix_image_convert(&img, MPIX_FMT_YUYV) == 0);
	assert(mpix_image_convert(&img, MPIX_FMT_YUV24) == 0);
	assert(mpix_image_convert(&img, MPIX_FMT_YUYV) == 0);
	assert(mpix_image_convert(&img, MPIX_FMT_RGB24) == 0);

	mpix_image_to_buf(&img, dst_buf, sizeof(dst_buf));

	printf("output:\n");
	mpix_print_buf(dst_buf, sizeof(dst_buf), &img.fmt, true);

	for (size_t i = 0; i < sizeof(dst_buf); i++) {
		/* Precision is not 100% as some conversions steps are lossy */
		assert(WITHIN(src_buf[i], dst_buf[i], 13));
	}
}

int main(void)
{
	test_low_level();
	test_high_level();

	return 0;
}
