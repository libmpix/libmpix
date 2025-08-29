#include <mpix/op_correction.h>
#include <mpix/test.h>
#include <stdio.h>

#define WIDTH 16
#define HEIGHT 16

uint8_t src[WIDTH * HEIGHT * 3];
uint8_t dst[WIDTH * HEIGHT * 3];
static uint8_t tmp1[WIDTH * HEIGHT * 3];
static uint8_t tmp2[WIDTH * HEIGHT * 3];
static uint8_t tmp3[WIDTH * HEIGHT * 3];
static uint8_t dst_fused[WIDTH * HEIGHT * 3];

void test_some_matrix(void)
{
	union mpix_correction_any correction = {
		.color_matrix.levels  = {
			/* 1st matrix row */
			2.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			1.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			1.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			/* 2nd matrix row */
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			2.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			/* 3rd matrix row */
			1.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			1.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			1.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
		},
	};

	mpix_correction_color_matrix_rgb24(src, dst, WIDTH * HEIGHT, 0, &correction);

	mpix_test_equal(dst[0],
			(src[0] * correction.color_matrix.levels[0] +
			src[1] * correction.color_matrix.levels[1] +
			src[2] * correction.color_matrix.levels[2]));

	mpix_test_equal(dst[1],
			(src[0] * correction.color_matrix.levels[3] +
			src[1] * correction.color_matrix.levels[4] +
			src[2] * correction.color_matrix.levels[5]));

	mpix_test_equal(dst[2],
			(src[0] * correction.color_matrix.levels[6] +
			src[1] * correction.color_matrix.levels[7] +
			src[2] * correction.color_matrix.levels[8]));
}

void test_identity_matrix(void)
{
	union mpix_correction_any correction = {
		.color_matrix.levels  = {
			/* 1st matrix row */
			1.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			/* 2nd matrix row */
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			1.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			/* 3rd matrix row */
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			1.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
		},
	};

	mpix_correction_color_matrix_rgb24(src, dst, WIDTH * HEIGHT, 0, &correction);

	mpix_test_equal(dst[0], src[0]);
	mpix_test_equal(dst[1], src[1]);
	mpix_test_equal(dst[2], src[2]);

}

void test_red_to_gray_matrix(void)
{
	union mpix_correction_any correction = {
		.color_matrix.levels  = {
			/* 1st matrix row */
			1.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			/* 2nd matrix row */
			1.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			/* 3rd matrix row */
			1.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
		},
	};

	mpix_correction_color_matrix_rgb24(src, dst, WIDTH * HEIGHT, 0, &correction);

	mpix_test_equal(dst[0], src[0]);
	mpix_test_equal(dst[1], 0);
	mpix_test_equal(dst[2], 0);

}

void test_green_to_gray_matrix(void)
{
	union mpix_correction_any correction = {
		.color_matrix.levels  = {
			/* 1st matrix row */
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			1.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			/* 2nd matrix row */
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			1.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			/* 3rd matrix row */
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			1.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
		},
	};

	mpix_correction_color_matrix_rgb24(src, dst, WIDTH * HEIGHT, 0, &correction);

	mpix_test_equal(dst[0], 0);
	mpix_test_equal(dst[1], src[1]);
	mpix_test_equal(dst[2], 0);
}

void test_blue_to_gray_matrix(void)
{
	union mpix_correction_any correction = {
		.color_matrix.levels  = {
			/* 1st matrix row */
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			1.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			/* 2nd matrix row */
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			1.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			/* 3rd matrix row */
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			1.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
		},
	};

	mpix_correction_color_matrix_rgb24(src, dst, WIDTH * HEIGHT, 0, &correction);

	mpix_test_equal(dst[0], 0);
	mpix_test_equal(dst[1], 0);
	mpix_test_equal(dst[2], src[2]);
}

void test_grayscale_matrix(void)
{
	union mpix_correction_any correction = {
		.color_matrix.levels  = {
			/* 1st matrix row */
			0.33 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.33 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.33 * (1 << MPIX_CORRECTION_SCALE_BITS),
			/* 2nd matrix row */
			0.33 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.33 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.33 * (1 << MPIX_CORRECTION_SCALE_BITS),
			/* 3rd matrix row */
			0.33 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.33 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.33 * (1 << MPIX_CORRECTION_SCALE_BITS),
		},
	};

	mpix_correction_color_matrix_rgb24(src, dst, WIDTH * HEIGHT, 0, &correction);

	mpix_test_equal(dst[0], dst[1]);
	mpix_test_equal(dst[1], dst[2]);
}

void test_extract_red_matrix(void)
{
	union mpix_correction_any correction = {
		.color_matrix.levels  = {
			/* 1st matrix row */
			1.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			/* 2nd matrix row */
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			/* 3rd matrix row */
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
		},
	};

	mpix_correction_color_matrix_rgb24(src, dst, WIDTH * HEIGHT, 0, &correction);

	mpix_test_equal(dst[0], src[1]);
	mpix_test_equal(dst[1], 0);
	mpix_test_equal(dst[2], 0);
}

void test_extract_green_matrix(void)
{
	union mpix_correction_any correction = {
		.color_matrix.levels  = {
			/* 1st matrix row */
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			/* 2nd matrix row */
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			1.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			/* 3rd matrix row */
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
		},
	};

	mpix_correction_color_matrix_rgb24(src, dst, WIDTH * HEIGHT, 0, &correction);

	mpix_test_equal(dst[0], 0);
	mpix_test_equal(dst[1], src[2]);
	mpix_test_equal(dst[2], 0);
}

void test_extract_blue_matrix(void)
{
	union mpix_correction_any correction = {
		.color_matrix.levels  = {
			/* 1st matrix row */
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			/* 2nd matrix row */
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			/* 3rd matrix row */
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			0.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			1.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
		},
	};

	mpix_correction_color_matrix_rgb24(src, dst, WIDTH * HEIGHT, 0, &correction);

	mpix_test_equal(dst[0], 0);
	mpix_test_equal(dst[1], 0);
	mpix_test_equal(dst[2], src[2]);
}

/* Verify fused one-pass correction equals the sequential pipeline:
 * black-level -> white-balance -> color-matrix -> gamma
 */
void test_fused_pipeline(void)
{
	/* Compose a moderate, non-trivial configuration */
	struct mpix_correction_all all = {
		.white_balance = {
			.red_level = (uint16_t)(1.25 * (1 << MPIX_CORRECTION_SCALE_BITS)),   /* ~1280 */
			.blue_level = (uint16_t)(0.75 * (1 << MPIX_CORRECTION_SCALE_BITS)),  /* ~768 */
		},
		.color_matrix = {
			/* Identity matrix in Q10 */
			.levels = {
				1.0 * (1 << MPIX_CORRECTION_SCALE_BITS), 0.0, 0.0,
				0.0, 1.0 * (1 << MPIX_CORRECTION_SCALE_BITS), 0.0,
				0.0, 0.0, 1.0 * (1 << MPIX_CORRECTION_SCALE_BITS),
			},
		},
		.gamma = {
			.level = 8, /* mid strength */
		},
		.black_level = {
			.level = 16,
		},
	};

	/* Run sequential reference */
	union mpix_correction_any bl = { .black_level.level = all.black_level.level };
	union mpix_correction_any wb; wb.white_balance.red_level = all.white_balance.red_level; wb.white_balance.blue_level = all.white_balance.blue_level;
	union mpix_correction_any cm; for (int i = 0; i < 9; ++i) cm.color_matrix.levels[i] = all.color_matrix.levels[i];
	union mpix_correction_any gm = { .gamma.level = all.gamma.level };

	uint16_t width = WIDTH * HEIGHT;
	mpix_correction_black_level_rgb24(src, tmp1, width, 0, &bl);
	mpix_correction_white_balance_rgb24(tmp1, tmp2, width, 0, &wb);
	mpix_correction_color_matrix_rgb24(tmp2, tmp3, width, 0, &cm);
	mpix_correction_gamma_rgb24(tmp3, dst, width, 0, &gm);

	/* Run fused */
	mpix_correction_fused_rgb24(src, dst_fused, width, 0, &all);

	/* Compare all bytes */
	for (size_t i = 0; i < (size_t)WIDTH * HEIGHT * 3; ++i) {
		if (dst_fused[i] != dst[i]) {
			mpix_test_equal(dst_fused[i], dst[i]);
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
	test_fused_pipeline();

	printf("All tests passed!\n");
	return 0;
}
