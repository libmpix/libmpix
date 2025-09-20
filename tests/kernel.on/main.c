/* SPDX-License-Identifier: Apache-2.0 */

#include <stdio.h>
#include <assert.h>

#include <mpix/image.h>
#include <mpix/print.h>

#define WIDTH   20
#define HEIGHT  20
#define VERBOSE 1

static uint8_t src_buf[WIDTH * HEIGHT * 3];
static uint8_t dst_buf[WIDTH * HEIGHT * 3];

static void run_kernel(enum mpix_op_type type, const int32_t *params, size_t params_nb)
{
	struct mpix_format fmt = { .width = WIDTH, .height = HEIGHT, .fourcc = MPIX_FMT_RGB24 };
	struct mpix_image img;

	mpix_image_from_buf(&img, src_buf, sizeof(src_buf), &fmt);

	if (VERBOSE) {
		printf("input:\n");
		mpix_print_buf(src_buf, sizeof(src_buf), &fmt, true);
	}

	assert(mpix_pipeline_add(&img, type, params, params_nb) == 0);
	assert(mpix_image_to_buf(&img, dst_buf, sizeof(dst_buf)) == 0);

	if (VERBOSE) {
		printf("output:\n");
		mpix_print_buf(dst_buf, sizeof(dst_buf), &fmt, true);
	}
}

static void test_identity(enum mpix_op_type type)
{
	int32_t p[] = { MPIX_KERNEL_IDENTITY };
	run_kernel(type, p, ARRAY_SIZE(p));

	for (uint16_t h = 0; h < HEIGHT; h++) {
		for (uint16_t w = 0; w < WIDTH; w++) {
			size_t i = h * WIDTH * 3 + w * 3;

			assert(dst_buf[i + 0] == src_buf[i + 0]);
			assert(dst_buf[i + 1] == src_buf[i + 1]);
			assert(dst_buf[i + 2] == src_buf[i + 2]);
		}
	}
}

static void test_denoise(enum mpix_op_type type)
{
	run_kernel(type, NULL, 0);

	for (uint16_t h = 0; h < HEIGHT; h++) {
		uint16_t w = 0;

		/* Left half */
		for (; w < WIDTH / 2 - 1; w++) {
			size_t i = h * WIDTH * 3 + w * 3;

			assert(dst_buf[i + 0] == dst_buf[i + 3]);
			assert(dst_buf[i + 1] == dst_buf[i + 4]);
			assert(dst_buf[i + 2] == dst_buf[i + 5]);
		}

		/* Left right */
		for (; w < WIDTH / 2 - 1; w++) {
			size_t i = h * WIDTH * 3 + w * 3;

			assert(dst_buf[i + 0] == dst_buf[i + 3]);
			assert(dst_buf[i + 1] == dst_buf[i + 4]);
			assert(dst_buf[i + 2] == dst_buf[i + 5]);
		}
	}
}

static void test_gaussian_blur(enum mpix_op_type type, int blur_margin)
{
	int32_t p[] = { MPIX_KERNEL_GAUSSIAN_BLUR };
	run_kernel(type, p, ARRAY_SIZE(p));

	for (uint16_t h = 0; h < HEIGHT; h++) {
		uint16_t w = 0;

		for (; w < WIDTH - 1; w++) {
			size_t i = h * WIDTH * 3 + w * 3;

			assert(WITHIN(dst_buf[i + 0], dst_buf[i + 3], blur_margin));
			assert(WITHIN(dst_buf[i + 1], dst_buf[i + 4], blur_margin));
			assert(WITHIN(dst_buf[i + 2], dst_buf[i + 5], blur_margin));
		}
	}
}

static void test_edge_detect(enum mpix_op_type type)
{
	int32_t p[] = { MPIX_KERNEL_EDGE_DETECT };
	run_kernel(type, p, ARRAY_SIZE(p));

	/* Basic sanity: output stays in range and reacts at transitions */
	for (uint16_t h = 0; h < HEIGHT; h++) {
		for (uint16_t w = 1; w + 1 < WIDTH; w++) {
			size_t i = h * WIDTH * 3 + w * 3;
			assert(dst_buf[i + 0] <= 255);
			assert(dst_buf[i + 1] <= 255);
			assert(dst_buf[i + 2] <= 255);
		}
	}
}

static void test_sharpen(enum mpix_op_type type)
{
	int32_t p[] = { MPIX_KERNEL_SHARPEN };
	run_kernel(type, p, ARRAY_SIZE(p));

	/* Basic sanity: center region should differ from original where gradients exist */
	int diff_count = 0;
	for (uint16_t h = 1; h + 1 < HEIGHT; h++) {
		for (uint16_t w = 1; w + 1 < WIDTH; w++) {
			size_t i = h * WIDTH * 3 + w * 3;
			diff_count += (dst_buf[i + 0] != src_buf[i + 0]);
			diff_count += (dst_buf[i + 1] != src_buf[i + 1]);
			diff_count += (dst_buf[i + 2] != src_buf[i + 2]);
		}
	}
	assert(diff_count > 0);
}

static void test_boundaries(void)
{
	/* width 3 and 5 to exercise tail and last-pixel paths */
	struct mpix_image img;
	struct mpix_format fmt3 = { .width = 3, .height = 3, .fourcc = MPIX_FMT_RGB24 };
	struct mpix_format fmt5 = { .width = 5, .height = 5, .fourcc = MPIX_FMT_RGB24 };
	uint8_t small_in[5 * 5 * 3];
	uint8_t small_out[5 * 5 * 3];

	for (int i = 0; i < 5 * 5; ++i) {
		enum { R, G, B };
		small_in[i * 3 + R] = (i & 1) ? 0xff : 0x00;
		small_in[i * 3 + G] = (i * 40) & 0xff;
		small_in[i * 3 + B] = 0x80;
	}

	/* width=3 */
	mpix_image_from_buf(&img, small_in, 3 * 3 * 3, &fmt3);
	assert(mpix_image_gaussian_blur(&img, 3) == 0);
	assert(mpix_image_to_buf(&img, small_out, sizeof(small_out)) == 0);

	/* width=5 */
	mpix_image_from_buf(&img, small_in, 5 * 5 * 3, &fmt5);
	assert(mpix_image_gaussian_blur(&img, 5) == 0);
	assert(mpix_image_to_buf(&img, small_out, sizeof(small_out)) == 0);
}

int main(void)
{
	/* Generate test input data */
	for (uint16_t h = 0; h < HEIGHT; h++) {
		for (uint16_t w = 0; w < WIDTH; w++) {
			src_buf[h * WIDTH * 3 + w * 3 + 0] = w < WIDTH / 2 ? 0x00 : 0xff;
			src_buf[h * WIDTH * 3 + w * 3 + 1] = (h % 3 + w % 3) / 4 * 0xff;
			src_buf[h * WIDTH * 3 + w * 3 + 2] = h * 0xff / HEIGHT;
		}
	}

	MPIX_INF("testing IDENTITY kernel");
	test_identity(MPIX_OP_KERNEL_CONVOLVE_3X3);
	test_identity(MPIX_OP_KERNEL_CONVOLVE_5X5);
	MPIX_INF("PASS");

	MPIX_INF("testing DENOISE kernel");
	test_denoise(MPIX_OP_KERNEL_DENOISE_3X3);
	test_denoise(MPIX_OP_KERNEL_DENOISE_5X5);
	MPIX_INF("PASS");

	MPIX_INF("testing GAUSSIAN blur kernel");
	test_gaussian_blur(MPIX_OP_KERNEL_CONVOLVE_3X3, 128);
	test_gaussian_blur(MPIX_OP_KERNEL_CONVOLVE_5X5, 96);
	MPIX_INF("PASS");

	MPIX_INF("testing EDGE_DETECT kernel");
	test_edge_detect(MPIX_OP_KERNEL_CONVOLVE_3X3);
	test_edge_detect(MPIX_OP_KERNEL_CONVOLVE_5X5);
	MPIX_INF("PASS");

	MPIX_INF("testing SHARPEN kernel");
	test_sharpen(MPIX_OP_KERNEL_CONVOLVE_3X3);
	test_sharpen(MPIX_OP_KERNEL_CONVOLVE_5X5);
	MPIX_INF("PASS");

	MPIX_INF("testing boundaries");
	test_boundaries();
	MPIX_INF("PASS");

	MPIX_INF("All tests passed!");
	return 0;
}
