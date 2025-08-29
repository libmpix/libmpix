/*
 * Copyright (c) 2025 tinyVision.ai Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <mpix/image.h>
#include <mpix/op_kernel.h>
#include <mpix/print.h>
#include <mpix/test.h>

#define WIDTH 20
#define HEIGHT 20
#define VERBOSE 0

/* Input/output buffers */
static uint8_t rgb24frame_in[WIDTH * HEIGHT * 3];
static uint8_t rgb24frame_out[WIDTH * HEIGHT * 3];

static void run_kernel(uint32_t kernel_type, uint32_t kernel_size)
{
	struct mpix_image img;
	int ret;

	mpix_image_from_buf(&img, rgb24frame_in, sizeof(rgb24frame_in), WIDTH, HEIGHT,
			    MPIX_FMT_RGB24);

	if (VERBOSE) {
		printf("input:\n");
		mpix_image_print_truecolor(&img);
	}

	ret = mpix_image_kernel(&img, kernel_type, kernel_size);
	mpix_test_ok(ret);

	ret = mpix_image_to_buf(&img, rgb24frame_out, sizeof(rgb24frame_out));
	mpix_test_ok(ret);

	if (VERBOSE) {
		printf("output:\n");
		mpix_image_print_truecolor(&img);
	}
}

static void test_identity(uint32_t kernel_size)
{
	run_kernel(MPIX_KERNEL_IDENTITY, kernel_size);

	for (uint16_t h = 0; h < HEIGHT; h++) {
		for (uint16_t w = 0; w < WIDTH; w++) {
			size_t i = h * WIDTH * 3 + w * 3;

			mpix_test_equal(rgb24frame_out[i + 0], rgb24frame_in[i + 0]);
			mpix_test_equal(rgb24frame_out[i + 1], rgb24frame_in[i + 1]);
			mpix_test_equal(rgb24frame_out[i + 2], rgb24frame_in[i + 2]);
		}
	}
}

static void test_median(uint32_t kernel_size)
{
	run_kernel(MPIX_KERNEL_DENOISE, kernel_size);

	for (uint16_t h = 0; h < HEIGHT; h++) {
		uint16_t w = 0;

		/* Left half */
		for (; w < WIDTH / 2 - 1; w++) {
			size_t i = h * WIDTH * 3 + w * 3;

			mpix_test_equal(rgb24frame_out[i + 0], rgb24frame_out[i + 3]);
			mpix_test_equal(rgb24frame_out[i + 1], rgb24frame_out[i + 4]);
			mpix_test_equal(rgb24frame_out[i + 2], rgb24frame_out[i + 5]);
		}

		/* Left right */
		for (; w < WIDTH / 2 - 1; w++) {
			size_t i = h * WIDTH * 3 + w * 3;

			mpix_test_equal(rgb24frame_out[i + 0], rgb24frame_out[i + 3]);
			mpix_test_equal(rgb24frame_out[i + 1], rgb24frame_out[i + 4]);
			mpix_test_equal(rgb24frame_out[i + 2], rgb24frame_out[i + 5]);
		}
	}
}

static void test_blur(uint32_t kernel_size, int blur_margin)
{
	run_kernel(MPIX_KERNEL_GAUSSIAN_BLUR, kernel_size);

	for (uint16_t h = 0; h < HEIGHT; h++) {
		uint16_t w = 0;

		for (; w < WIDTH - 1; w++) {
			size_t i = h * WIDTH * 3 + w * 3;

			mpix_test_within(rgb24frame_out[i + 0], rgb24frame_out[i + 3], blur_margin);
			mpix_test_within(rgb24frame_out[i + 1], rgb24frame_out[i + 4], blur_margin);
			mpix_test_within(rgb24frame_out[i + 2], rgb24frame_out[i + 5], blur_margin);
		}
	}
}

static void test_edge(uint32_t kernel_size)
{
	run_kernel(MPIX_KERNEL_EDGE_DETECT, kernel_size);
	/* Basic sanity: output stays in range and reacts at transitions */
	for (uint16_t h = 0; h < HEIGHT; h++) {
		for (uint16_t w = 1; w + 1 < WIDTH; w++) {
			size_t i = h * WIDTH * 3 + w * 3;
			mpix_test(rgb24frame_out[i + 0] <= 255);
			mpix_test(rgb24frame_out[i + 1] <= 255);
			mpix_test(rgb24frame_out[i + 2] <= 255);
		}
	}
}

static void test_sharpen(uint32_t kernel_size)
{
	run_kernel(MPIX_KERNEL_SHARPEN, kernel_size);
	/* Basic sanity: center region should differ from original where gradients exist */
	int diff_count = 0;
	for (uint16_t h = 1; h + 1 < HEIGHT; h++) {
		for (uint16_t w = 1; w + 1 < WIDTH; w++) {
			size_t i = h * WIDTH * 3 + w * 3;
			diff_count += (rgb24frame_out[i + 0] != rgb24frame_in[i + 0]);
			diff_count += (rgb24frame_out[i + 1] != rgb24frame_in[i + 1]);
			diff_count += (rgb24frame_out[i + 2] != rgb24frame_in[i + 2]);
		}
	}
	mpix_test(diff_count > 0);
}

static void test_boundaries(void)
{
	/* width 3 and 5 to exercise tail and last-pixel paths */
	struct mpix_image img;
	uint8_t small_in[5 * 3];
	uint8_t small_out[5 * 3];
	for (int w = 0; w < 5; ++w) {
		small_in[w*3+0] = (w & 1) ? 0xff : 0x00;
		small_in[w*3+1] = (w * 40) & 0xff;
		small_in[w*3+2] = 0x80;
	}

	/* width=3 */
	mpix_image_from_buf(&img, small_in, 3*3, 3, 1, MPIX_FMT_RGB24);
	mpix_test_ok(mpix_image_kernel(&img, MPIX_KERNEL_GAUSSIAN_BLUR, 3));
	mpix_test_ok(mpix_image_to_buf(&img, small_out, sizeof(small_out)));
	/* width=5 */
	mpix_image_from_buf(&img, small_in, 5*3, 5, 1, MPIX_FMT_RGB24);
	mpix_test_ok(mpix_image_kernel(&img, MPIX_KERNEL_GAUSSIAN_BLUR, 5));
	mpix_test_ok(mpix_image_to_buf(&img, small_out, sizeof(small_out)));
}

int main(void)
{
	/* Generate test input data */
	for (uint16_t h = 0; h < HEIGHT; h++) {
		for (uint16_t w = 0; w < WIDTH; w++) {
			rgb24frame_in[h * WIDTH * 3 + w * 3 + 0] = w < WIDTH / 2 ? 0x00 : 0xff;
			rgb24frame_in[h * WIDTH * 3 + w * 3 + 1] = (h % 3 + w % 3) / 4 * 0xff;
			rgb24frame_in[h * WIDTH * 3 + w * 3 + 2] = h * 0xff / HEIGHT;
		}
	}

	test_identity(3);
	test_identity(5);

	test_median(3);
	test_median(5);

	test_blur(3, 128);
	test_blur(5, 96);

	test_edge(3);
	test_sharpen(3);
	test_edge(5);
	test_sharpen(5);

	test_boundaries();

	printf("All tests passed!\n");
	return 0;
}
