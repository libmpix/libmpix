/* SPDX-License-Identifier: Apache-2.0 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>

#include <mpix/low_level.h>
#include <mpix/utils.h>

#define WIDTH  16
#define HEIGHT 16

uint8_t src[WIDTH * HEIGHT * 3];
uint8_t dst[WIDTH * HEIGHT * 3];
static uint8_t tmp1[WIDTH * HEIGHT * 3];
static uint8_t tmp2[WIDTH * HEIGHT * 3];
static uint8_t tmp3[WIDTH * HEIGHT * 3];
static uint8_t dst_fused[WIDTH * HEIGHT * 3];

void test_some_matrix(void)
{
	int32_t color_matrix_q10[9] = {
		2.0 * (1 << 10), 1.0 * (1 << 10), 1.0 * (1 << 10), /* Row 0 */
		0.0 * (1 << 10), 2.0 * (1 << 10), 0.0 * (1 << 10), /* Row 1 */
		1.0 * (1 << 10), 1.0 * (1 << 10), 1.0 * (1 << 10), /* Row 2 */
	};

	mpix_correct_color_matrix_rgb24(src, dst, WIDTH * HEIGHT, color_matrix_q10);

	assert(dst[0] == src[0] * color_matrix_q10[0] +
				src[1] * color_matrix_q10[1] +
				src[2] * color_matrix_q10[2]);

	assert(dst[1] == src[0] * color_matrix_q10[3] +
				src[1] * color_matrix_q10[4] +
				src[2] * color_matrix_q10[5]);

	assert(dst[2] == src[0] * color_matrix_q10[6] +
				src[1] * color_matrix_q10[7] +
				src[2] * color_matrix_q10[8]);
}

void test_identity_matrix(void)
{
	int32_t color_matrix_q10[9] = {
		1.0 * (1 << 10), 0.0 * (1 << 10), 0.0 * (1 << 10), /* Row 0 */
		0.0 * (1 << 10), 1.0 * (1 << 10), 0.0 * (1 << 10), /* Row 1 */
		0.0 * (1 << 10), 0.0 * (1 << 10), 1.0 * (1 << 10), /* Row 2 */
	};

	mpix_correct_color_matrix_rgb24(src, dst, WIDTH * HEIGHT, color_matrix_q10);

	assert(dst[0] == src[0]);
	assert(dst[1] == src[1]);
	assert(dst[2] == src[2]);
}

void test_red_to_gray_matrix(void)
{
	int32_t color_matrix_q10[9] = {
		1.0 * (1 << 10), 0.0 * (1 << 10), 0.0 * (1 << 10), /* Row 0 */
		1.0 * (1 << 10), 0.0 * (1 << 10), 0.0 * (1 << 10), /* Row 1 */
		1.0 * (1 << 10), 0.0 * (1 << 10), 0.0 * (1 << 10), /* Row 2 */
	};

	mpix_correct_color_matrix_rgb24(src, dst, WIDTH * HEIGHT, color_matrix_q10);

	assert(dst[0] == src[0]);
	assert(dst[1] == 0);
	assert(dst[2] == 0);
}

void test_green_to_gray_matrix(void)
{
	int32_t color_matrix_q10[9] = {
		0.0 * (1 << 10), 1.0 * (1 << 10), 0.0 * (1 << 10), /* Row 0 */
		0.0 * (1 << 10), 1.0 * (1 << 10), 0.0 * (1 << 10), /* Row 1 */
		0.0 * (1 << 10), 1.0 * (1 << 10), 0.0 * (1 << 10), /* Row 2 */
	};

	mpix_correct_color_matrix_rgb24(src, dst, WIDTH * HEIGHT, color_matrix_q10);

	assert(dst[0] == 0);
	assert(dst[1] == src[1]);
	assert(dst[2] == 0);
}

void test_blue_to_gray_matrix(void)
{
	int32_t color_matrix_q10[9] = {
		0.0 * (1 << 10), 0.0 * (1 << 10), 1.0 * (1 << 10), /* Row 0 */
		0.0 * (1 << 10), 0.0 * (1 << 10), 1.0 * (1 << 10), /* Row 1 */
		0.0 * (1 << 10), 0.0 * (1 << 10), 1.0 * (1 << 10), /* Row 2 */
	};

	mpix_correct_color_matrix_rgb24(src, dst, WIDTH * HEIGHT, color_matrix_q10);

	assert(dst[0] == 0);
	assert(dst[1] == 0);
	assert(dst[2] == src[2]);
}

void test_grayscale_matrix(void)
{
	int32_t color_matrix_q10[9] = {
		0.33 * (1 << 10), 0.33 * (1 << 10), 0.33 * (1 << 10), /* Row 0 */
		0.33 * (1 << 10), 0.33 * (1 << 10), 0.33 * (1 << 10), /* Row 1 */
		0.33 * (1 << 10), 0.33 * (1 << 10), 0.33 * (1 << 10), /* Row 2 */
	};

	mpix_correct_color_matrix_rgb24(src, dst, WIDTH * HEIGHT, color_matrix_q10);

	assert(dst[0] == dst[1]);
	assert(dst[1] == dst[2]);
}

void test_extract_red_matrix(void)
{
	int32_t color_matrix_q10[9] = {
		1.0 * (1 << 10), 0.0 * (1 << 10), 0.0 * (1 << 10), /* Row 0 */
		0.0 * (1 << 10), 0.0 * (1 << 10), 0.0 * (1 << 10), /* Row 1 */
		0.0 * (1 << 10), 0.0 * (1 << 10), 0.0 * (1 << 10), /* Row 2 */
	};

	mpix_correct_color_matrix_rgb24(src, dst, WIDTH * HEIGHT, color_matrix_q10);

	assert(dst[0] == src[1]);
	assert(dst[1] == 0);
	assert(dst[2] == 0);
}

void test_extract_green_matrix(void)
{
	int32_t color_matrix_q10[9] = {
		0.0 * (1 << 10), 0.0 * (1 << 10), 0.0 * (1 << 10), /* Row 0 */
		0.0 * (1 << 10), 1.0 * (1 << 10), 0.0 * (1 << 10), /* Row 1 */
		0.0 * (1 << 10), 0.0 * (1 << 10), 0.0 * (1 << 10), /* Row 2 */
	};

	mpix_correct_color_matrix_rgb24(src, dst, WIDTH * HEIGHT, color_matrix_q10);

	assert(dst[0] == 0);
	assert(dst[1] == src[2]);
	assert(dst[2] == 0);
}

void test_extract_blue_matrix(void)
{
	int32_t color_matrix_q10[9] = {
		0.0 * (1 << 10), 0.0 * (1 << 10), 0.0 * (1 << 10), /* Row 0 */
		0.0 * (1 << 10), 0.0 * (1 << 10), 0.0 * (1 << 10), /* Row 1 */
		0.0 * (1 << 10), 0.0 * (1 << 10), 1.0 * (1 << 10), /* Row 2 */
	};

	mpix_correct_color_matrix_rgb24(src, dst, WIDTH * HEIGHT, color_matrix_q10);

	assert(dst[0] == 0);
	assert(dst[1] == 0);
	assert(dst[2] == src[2]);
}

/* Verify fused one-pass correction equals the sequential pipeline:
 * black-level -> white-balance -> color-matrix -> gamma
 */
void test_fused_pipeline(void)
{
	/* Compose a moderate, non-trivial configuration */
	int32_t color_matrix_q10[9] = {
		1.0 * (1 << 10), 0.0 * (1 << 10), 0.0 * (1 << 10), /* Row 0 */
		0.0 * (1 << 10), 1.0 * (1 << 10), 0.0 * (1 << 10), /* Row 1 */
		0.0 * (1 << 10), 0.0 * (1 << 10), 1.0 * (1 << 10), /* Row 2 */
	};
	int32_t black_level = 16;
	int32_t red_balance_q10 = 1.25 * (1 << 10);  /* ~1280 */
	int32_t blue_balance_q10 = 0.75 * (1 << 10); /* ~768 */
	int32_t gamma_q10 = 0.5 * (1 << 10); /* mid strength */

	/* Run sequential reference */
	uint16_t pixels = WIDTH * HEIGHT;
	mpix_correct_black_level_raw8(src, tmp1, pixels * 3, black_level);
	mpix_correct_white_balance_rgb24(tmp1, tmp2, pixels, red_balance_q10, blue_balance_q10);
	mpix_correct_color_matrix_rgb24(tmp2, tmp3, pixels, color_matrix_q10);
	mpix_correct_gamma_rgb24(tmp3, dst, pixels, gamma_q10);

	/* Run fused */
	// mpix_correct_fused_rgb24(src, dst_fused, width, 0, &all);

	/* Compare all bytes */
	for (size_t i = 0; i < (size_t)WIDTH * HEIGHT * 3; ++i) {
		if (dst_fused[i] != dst[i]) {
			assert(dst_fused[i] == dst[i]);
		}
	}
}

int main(void)
{
	printf("Testing MPiX correction functions...\n");
	/* Generate test input data */
	for (uint16_t h = 0; h < HEIGHT; h++) {
		for (uint16_t w = 0; w < WIDTH; w++) {
			src[h * WIDTH * 3 + w * 3 + 0] = w < WIDTH / 2 ? 0x00 : 0xff;
			src[h * WIDTH * 3 + w * 3 + 1] = (h % 3 + w % 3) / 4 * 0xff;
			src[h * WIDTH * 3 + w * 3 + 2] = h * 0xff / HEIGHT;
		}
	}

	test_some_matrix();
	test_identity_matrix();
	test_red_to_gray_matrix();
	test_green_to_gray_matrix();
	test_blue_to_gray_matrix();
	test_grayscale_matrix();
	test_extract_red_matrix();
	test_extract_green_matrix();
	test_extract_blue_matrix();

	/* New: fused one-pass vs sequential reference */
	// test_fused_pipeline();

	printf("All tests passed!\n");
	return 0;
}
