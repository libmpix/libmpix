/* SPDX-License-Identifier: Apache-2.0 */

#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#include <mpix/test.h>
#include <mpix/image.h>

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
		0xFF, 0x00, 0x00,  /* Red */
		0x00, 0xFF, 0x00,  /* Green */
		0x00, 0x00, 0xFF,  /* Blue */
		0xFF, 0xFF, 0xFF,  /* White */
		/* Row 1 */
		0x00, 0x00, 0x00,  /* Black */
		0xFF, 0x00, 0x00,  /* Red */
		0x00, 0xFF, 0x00,  /* Green */
		0x00, 0x00, 0xFF,  /* Blue */
		/* Row 2 */
		0xFF, 0xFF, 0xFF,  /* White */
		0x00, 0x00, 0x00,  /* Black */
		0xFF, 0x00, 0x00,  /* Red */
		0x00, 0xFF, 0x00,  /* Green */
		/* Row 3 */
		0x00, 0x00, 0xFF,  /* Blue */
		0xFF, 0xFF, 0xFF,  /* White */
		0x00, 0x00, 0x00,  /* Black */
		0xFF, 0x00, 0x00,  /* Red */
	};

	/* Expected result: crop region (1,1) with size 2x2 should give:
	 * 0xFF0000 0x00FF00
	 * 0x000000 0xFF0000
	 */
	uint8_t expected_data[2 * 2 * 3] = {
		0xFF, 0x00, 0x00,  /* Red */
		0x00, 0xFF, 0x00,  /* Green */
		0x00, 0x00, 0x00,  /* Black */
		0xFF, 0x00, 0x00,  /* Red */
	};

	uint8_t dst_data[2 * 2 * 3];
	struct mpix_image img;
	int ret;

	/* Initialize image */
	mpix_image_from_buf(&img, src_data, sizeof(src_data), 4, 4, MPIX_FMT_RGB24);

	/* Apply crop operation */
	ret = mpix_image_crop(&img, 1, 1, 2, 2);
	mpix_test_ok(ret);
	mpix_test_equal(img.width, 2);
	mpix_test_equal(img.height, 2);

	/* Convert to output buffer */
	ret = mpix_image_to_buf(&img, dst_data, sizeof(dst_data));
	mpix_test_ok(ret);

	/* Verify result */
	mpix_test(memcmp(dst_data, expected_data, sizeof(expected_data)) == 0);

	/* Cleanup */
	mpix_image_free(&img);
}

/**
 * Test crop validation - crop region exceeds image bounds
 */
static void test_crop_bounds_validation(void)
{
	uint8_t src_data[4 * 4 * 3];
	struct mpix_image img;
	int ret;

	/* Initialize image */
	mpix_image_from_buf(&img, src_data, sizeof(src_data), 4, 4, MPIX_FMT_RGB24);

	/* Test crop exceeding width */
	ret = mpix_image_crop(&img, 3, 0, 2, 2);  /* x=3, width=2 -> exceeds 4 */
	mpix_test(ret != 0);

	/* Reset image */
	mpix_image_from_buf(&img, src_data, sizeof(src_data), 4, 4, MPIX_FMT_RGB24);

	/* Test crop exceeding height */
	ret = mpix_image_crop(&img, 0, 3, 2, 2);  /* y=3, height=2 -> exceeds 4 */
	mpix_test(ret != 0);

	/* Reset image */
	mpix_image_from_buf(&img, src_data, sizeof(src_data), 4, 4, MPIX_FMT_RGB24);

	/* Test zero width */
	ret = mpix_image_crop(&img, 0, 0, 0, 2);
	mpix_test(ret != 0);

	/* Reset image */
	mpix_image_from_buf(&img, src_data, sizeof(src_data), 4, 4, MPIX_FMT_RGB24);

	/* Test zero height */
	ret = mpix_image_crop(&img, 0, 0, 2, 0);
	mpix_test(ret != 0);
}

/**
 * Test crop with different pixel formats
 */
static void test_crop_different_formats(void)
{
	/* Test with GREY format (8-bit) */
	uint8_t grey_src[4 * 4] = {
		0x00, 0x40, 0x80, 0xFF,
		0x20, 0x60, 0xA0, 0xE0,
		0x10, 0x50, 0x90, 0xD0,
		0x30, 0x70, 0xB0, 0xF0
	};

	uint8_t grey_expected[2 * 2] = {
		0x60, 0xA0,
		0x50, 0x90
	};

	uint8_t grey_dst[2 * 2];
	struct mpix_image img;
	int ret;

	/* Test GREY format */
	mpix_image_from_buf(&img, grey_src, sizeof(grey_src), 4, 4, MPIX_FMT_GREY);
	ret = mpix_image_crop(&img, 1, 1, 2, 2);
	mpix_test_ok(ret);
	ret = mpix_image_to_buf(&img, grey_dst, sizeof(grey_dst));
	mpix_test_ok(ret);
	mpix_test(memcmp(grey_dst, grey_expected, sizeof(grey_expected)) == 0);
	mpix_image_free(&img);
}

/**
 * Test edge crop (crop at image boundaries)
 */
static void test_crop_edge_cases(void)
{
	uint8_t src_data[3 * 3 * 3];
	uint8_t dst_data[1 * 1 * 3];
	struct mpix_image img;
	int ret;

	/* Initialize with simple pattern */
	for (int i = 0; i < 3 * 3; i++) {
		src_data[i * 3 + 0] = i;      /* R */
		src_data[i * 3 + 1] = i + 9;  /* G */
		src_data[i * 3 + 2] = i + 18; /* B */
	}

	/* Test crop to single pixel at top-left corner */
	mpix_image_from_buf(&img, src_data, sizeof(src_data), 3, 3, MPIX_FMT_RGB24);
	ret = mpix_image_crop(&img, 0, 0, 1, 1);
	mpix_test_ok(ret);
	mpix_test_equal(img.width, 1);
	mpix_test_equal(img.height, 1);
	ret = mpix_image_to_buf(&img, dst_data, sizeof(dst_data));
	mpix_test_ok(ret);
	mpix_test_equal(dst_data[0], 0);  /* First pixel R */
	mpix_test_equal(dst_data[1], 9);  /* First pixel G */
	mpix_test_equal(dst_data[2], 18); /* First pixel B */
	mpix_image_free(&img);

	/* Test crop to single pixel at bottom-right corner */
	mpix_image_from_buf(&img, src_data, sizeof(src_data), 3, 3, MPIX_FMT_RGB24);
	ret = mpix_image_crop(&img, 2, 2, 1, 1);
	mpix_test_ok(ret);
	ret = mpix_image_to_buf(&img, dst_data, sizeof(dst_data));
	mpix_test_ok(ret);
	mpix_test_equal(dst_data[0], 8);  /* Last pixel R */
	mpix_test_equal(dst_data[1], 17); /* Last pixel G */
	mpix_test_equal(dst_data[2], 26); /* Last pixel B */
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
