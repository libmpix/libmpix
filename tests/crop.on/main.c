/* SPDX-License-Identifier: Apache-2.0 */

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <mpix/image.h>
#include <mpix/print.h>

/**
 * Test basic crop functionality with a simple RGB24 image
 */
static void test_crop_rgb24_basic(void)
{
	/* Create a 4x4 RGB24 test image with pattern:
	 * 0xFF0000 0x00FF00 0x0000FF 0xFFFFFF
	 * 0x000000 0xFF0000 0x00FF00 0x0000FF
	 * 0xFFFFFF 0x000000 0xFF0000 0x00FF00
	 * 0x0000FF 0xFFFFFF 0x000000 0xFF0000
	 */
	uint8_t src_data[4 * 4 * 3] = {
		/* Row 0 */
		0x00, 0x00, 0x00, /* Col 0 */
		0x11, 0x11, 0x11, /* Col 1 */
		0x22, 0x22, 0x22, /* Col 2 */
		0x33, 0x33, 0x33, /* Col 3 */
		/* Row 1 */
		0x44, 0x44, 0x44, /* Col 0 */
		0x55, 0x55, 0x55, /* Col 1 */
		0x66, 0x66, 0x66, /* Col 2 */
		0x77, 0x77, 0x77, /* Col 3 */
		/* Row 2 */
		0x88, 0x88, 0x88, /* Col 0 */
		0x99, 0x99, 0x99, /* Col 1 */
		0xaa, 0xaa, 0xaa, /* Col 2 */
		0xbb, 0xbb, 0xbb, /* Col 3 */
		/* Row 3 */
		0xcc, 0xcc, 0xcc, /* Col 0 */
		0xdd, 0xdd, 0xdd, /* Col 1 */
		0xee, 0xee, 0xee, /* Col 2 */
		0xff, 0xff, 0xff, /* Col 3 */
	};

	/* Expected result: crop region (1,1) with size 2x2 should give:
	 * 0xFF0000 0x00FF00
	 * 0x000000 0xFF0000
	 */
	uint8_t expected_data[2 * 2 * 3] = {
		/* Row 1 */
		0x55, 0x55, 0x55, /* Col 1 */
		0x66, 0x66, 0x66, /* Col 2 */
		/* Row 2 */
		0x99, 0x99, 0x99, /* Col 1 */
		0xaa, 0xaa, 0xaa, /* Col 2 */
	};

	uint8_t dst_data[2 * 2 * 3] = {};
	struct mpix_format fmt = { .width = 4, .height = 4, .fourcc = MPIX_FMT_RGB24 };
	struct mpix_image img;

	/* Initialize image */
	mpix_image_from_buf(&img, src_data, sizeof(src_data), &fmt);

	/* Apply crop operation */
	assert(mpix_image_crop(&img, 1, 1, 2, 2) == 0);
	assert(img.fmt.width == 2);
	assert(img.fmt.height == 2);

	/* Convert to output buffer */
	assert(mpix_image_to_buf(&img, dst_data, sizeof(dst_data)) == 0);

	/* Verify result */
	assert(memcmp(dst_data, expected_data, sizeof(expected_data)) == 0);

	/* Cleanup */
	mpix_image_free(&img);
}

/**
 * Test crop validation - crop region exceeds image bounds
 */
static void test_crop_bounds_validation(void)
{
	uint8_t src_data[4 * 4 * 3];
	struct mpix_format fmt = { .width = 4, .height = 4, .fourcc = MPIX_FMT_RGB24 };
	struct mpix_image img;

	/* Initialize image */
	mpix_image_from_buf(&img, src_data, sizeof(src_data), &fmt);

	/* Test crop exceeding width */
	assert(mpix_image_crop(&img, 3, 0, 2, 2) == -ERANGE); /* x=3, width=2 -> exceeds 4 */

	/* Reset image */
	mpix_image_from_buf(&img, src_data, sizeof(src_data), &fmt);

	/* Test crop exceeding height */
	assert(mpix_image_crop(&img, 0, 3, 2, 2) == -ERANGE); /* y=3, height=2 -> exceeds 4 */

	/* Reset image */
	mpix_image_from_buf(&img, src_data, sizeof(src_data), &fmt);

	/* Test zero width */
	assert(mpix_image_crop(&img, 0, 0, 0, 2) == -ERANGE);

	/* Reset image */
	mpix_image_from_buf(&img, src_data, sizeof(src_data), &fmt);

	/* Test zero height */
	assert(mpix_image_crop(&img, 0, 0, 2, 0) == -ERANGE);
}

/**
 * Test crop with different pixel formats
 */
static void test_crop_different_formats(void)
{
	/* Test with GREY format (8-bit) */
	uint8_t grey_src[4 * 4] = {
		0x00, 0x40, 0x80, 0xFF, /* Row 0 */
		0x20, 0x60, 0xA0, 0xE0, /* Row 1 */
		0x10, 0x50, 0x90, 0xD0, /* Row 2 */
		0x30, 0x70, 0xB0, 0xF0, /* Row 3 */
	};

	uint8_t grey_expected[2 * 2] = {
		0x60, 0xA0, /* Row 1, starting at col 1 */
		0x50, 0x90, /* Row 2, starting at col 1 */
	};

	uint8_t grey_dst[2 * 2];
	struct mpix_format fmt = { .width = 4, .height = 4, .fourcc = MPIX_FMT_GREY };
	struct mpix_image img;

	/* Test GREY format */
	mpix_image_from_buf(&img, grey_src, sizeof(grey_src), &fmt);
	assert(mpix_image_crop(&img, 1, 1, 2, 2) == 0);
	assert(mpix_image_to_buf(&img, grey_dst, sizeof(grey_dst)) == 0);
	assert(memcmp(grey_dst, grey_expected, sizeof(grey_expected)) == 0);
	mpix_image_free(&img);
}

/**
 * Test edge crop (crop at image boundaries)
 */
static void test_crop_edge_cases(void)
{
	uint8_t src_data[3 * 3 * 3] = {};
	uint8_t dst_data[1 * 1 * 3] = {};
	struct mpix_image img;
	struct mpix_format fmt = { .width = 3, .height = 3, .fourcc = MPIX_FMT_RGB24 };

	/* Initialize with simple pattern */
	for (int i = 0; i < 3 * 3; i++) {
		src_data[i * 3 + 0] = i;      /* R */
		src_data[i * 3 + 1] = i + 9;  /* G */
		src_data[i * 3 + 2] = i + 18; /* B */
	}

	/* Test crop to single pixel at top-left corner */
	mpix_image_from_buf(&img, src_data, sizeof(src_data), &fmt);
	assert(mpix_image_crop(&img, 0, 0, 1, 1) == 0);
	assert(img.fmt.width == 1);
	assert(img.fmt.height == 1);
	assert(mpix_image_to_buf(&img, dst_data, sizeof(dst_data)) == 0);
	assert(dst_data[0] == 0);  /* First pixel R */
	assert(dst_data[1] == 9);  /* First pixel G */
	assert(dst_data[2] == 18); /* First pixel B */
	mpix_image_free(&img);

	/* Test crop to single pixel at bottom-right corner */
	mpix_image_from_buf(&img, src_data, sizeof(src_data), &fmt);
	assert(mpix_image_crop(&img, 2, 2, 1, 1) == 0);
	assert(mpix_image_to_buf(&img, dst_data, sizeof(dst_data)) == 0);
	assert(dst_data[0] == 8);  /* Last pixel R */
	assert(dst_data[1] == 17); /* Last pixel G */
	assert(dst_data[2] == 26); /* Last pixel B */
	mpix_image_free(&img);
}

int main(void)
{
	printf("Testing basic RGB24 crop...\n");
	test_crop_rgb24_basic();
	printf("PASSED\n");

	printf("Testing crop bounds validation...\n");
	test_crop_bounds_validation();
	printf("PASSED\n");

	printf("Testing different pixel formats...\n");
	test_crop_different_formats();
	printf("PASSED\n");

	printf("Testing edge case crops...\n");
	test_crop_edge_cases();
	printf("PASSED\n");

	printf("All crop tests passed!\n");
	return 0;
}
