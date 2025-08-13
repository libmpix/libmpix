#include <mpix/op_correction.h>
#include <mpix/test.h>
#include <stdio.h>

#define WIDTH 16
#define HEIGHT 16

uint8_t src[WIDTH * HEIGHT * 3];
uint8_t dst[WIDTH * HEIGHT * 3];

void test_some_matrix()
{

	/* uses a 3x3 matrix we will ignore offsets and just use coefficients now */
	const struct  mpix_correction_color_matrix ccm = {
		.levels  = {2,1,1,0,2,0,1,1,1}
	};


	union mpix_correction_any correction = {
		.color_matrix = ccm
	};


	//call correction
	mpix_correction_color_rgb24(src, dst, WIDTH * HEIGHT, 0,  &correction);

	uint8_t *src_ptr = src;
	uint8_t *dst_ptr = dst;

	mpix_test_equal(dst_ptr[0],
			(src_ptr[0] * ccm.levels[0] +
			src_ptr[1] * ccm.levels[1] +
			src_ptr[2] * ccm.levels[2]));

	mpix_test_equal(dst_ptr[1],
			(src_ptr[0] * ccm.levels[3] +
			src_ptr[1] * ccm.levels[4] +
			src_ptr[2] * ccm.levels[5]));

	mpix_test_equal(dst_ptr[2],
			(src_ptr[0] * ccm.levels[6] +
			src_ptr[1] * ccm.levels[7] +
			src_ptr[2] * ccm.levels[8]));

}

void test_identity_matrix()
{
	const struct  mpix_correction_color_matrix ccm = {
		.levels  = {1.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 1.0}
	};

	union mpix_correction_any correction = {
		.color_matrix = ccm
	};

	mpix_correction_color_rgb24(src, dst, WIDTH * HEIGHT, 0,  &correction);

	uint8_t *src_ptr = src;
	uint8_t *dst_ptr = dst;

	mpix_test_equal(dst_ptr[0], src_ptr[0]);

	mpix_test_equal(dst_ptr[1], src_ptr[1]);

	mpix_test_equal(dst_ptr[2], src_ptr[2]);

}

void test_red_matrix()
{
	const struct  mpix_correction_color_matrix ccm = {
		.levels  = {1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0}
	};

	union mpix_correction_any correction = {
		.color_matrix = ccm
	};

	mpix_correction_color_rgb24(src, dst, WIDTH * HEIGHT, 0,  &correction);

	uint8_t *src_ptr = src;
	uint8_t *dst_ptr = dst;

	mpix_test_equal(dst_ptr[0], src_ptr[0]);

	mpix_test_equal(dst_ptr[1], 0);

	mpix_test_equal(dst_ptr[2], 0);

}

void test_green_matrix()
{
	const struct  mpix_correction_color_matrix ccm = {
		.levels  = {0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0}
	};

	union mpix_correction_any correction = {
		.color_matrix = ccm
	};

	mpix_correction_color_rgb24(src, dst, WIDTH * HEIGHT, 0,  &correction);

	uint8_t *src_ptr = src;
	uint8_t *dst_ptr = dst;

	mpix_test_equal(dst_ptr[0], 0);

	mpix_test_equal(dst_ptr[1], src_ptr[1]);

	mpix_test_equal(dst_ptr[2], 0);
}

void test_blue_matrix()
{
	const struct  mpix_correction_color_matrix ccm = {
		.levels  = {0.0, 0.0, 1.0, 0.0, 0.0, 1.0, 0.0, 0.0, 1.0}
	};

	union mpix_correction_any correction = {
		.color_matrix = ccm
	};

	mpix_correction_color_rgb24(src, dst, WIDTH * HEIGHT, 0,  &correction);

	uint8_t *src_ptr = src;
	uint8_t *dst_ptr = dst;

	mpix_test_equal(dst_ptr[0], 0);

	mpix_test_equal(dst_ptr[1], 0);

	mpix_test_equal(dst_ptr[2], src_ptr[2]);
}

void test_grayscale_matrix()
{
	const struct  mpix_correction_color_matrix ccm = {
		.levels  = {0.33, 0.33, 0.33, 0.33, 0.33, 0.33, 0.33, 0.33, 0.33}
	};

	union mpix_correction_any correction = {
		.color_matrix = ccm
	};

	mpix_correction_color_rgb24(src, dst, WIDTH * HEIGHT, 0,  &correction);

	uint8_t *src_ptr = src;
	uint8_t *dst_ptr = dst;

	mpix_test_equal(dst_ptr[0], dst_ptr[1]);
	mpix_test_equal(dst_ptr[1], dst_ptr[2]);
}

void test_extract_red_matrix()
{
	const struct  mpix_correction_color_matrix ccm = {
		.levels  = {1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0}
	};

	union mpix_correction_any correction = {
		.color_matrix = ccm
	};

	mpix_correction_color_rgb24(src, dst, WIDTH * HEIGHT, 0,  &correction);

	uint8_t *src_ptr = src;
	uint8_t *dst_ptr = dst;

	mpix_test_equal(dst_ptr[0], src_ptr[1]);
	mpix_test_equal(dst_ptr[1], 0);
	mpix_test_equal(dst_ptr[2], 0);


}

void test_extract_green_matrix()
{
	const struct  mpix_correction_color_matrix ccm = {
		.levels  = {0.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0}
	};

	union mpix_correction_any correction = {
		.color_matrix = ccm
	};

	mpix_correction_color_rgb24(src, dst, WIDTH * HEIGHT, 0,  &correction);

	uint8_t *src_ptr = src;
	uint8_t *dst_ptr = dst;

	mpix_test_equal(dst_ptr[0], 0);
	mpix_test_equal(dst_ptr[1], src_ptr[2]);
	mpix_test_equal(dst_ptr[2], 0);
}


void test_extract_blue_matrix()
{
	const struct  mpix_correction_color_matrix ccm = {
		.levels  = {0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 1.0}
	};

	union mpix_correction_any correction = {
		.color_matrix = ccm
	};

	mpix_correction_color_rgb24(src, dst, WIDTH * HEIGHT, 0,  &correction);

	uint8_t *src_ptr = src;
	uint8_t *dst_ptr = dst;

	mpix_test_equal(dst_ptr[0], 0);
	mpix_test_equal(dst_ptr[1], 0);
	mpix_test_equal(dst_ptr[2], src_ptr[2]);
}


int main(void)
{
	printf("In test \n");
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
	test_red_matrix();
	test_green_matrix();
	test_blue_matrix();
	test_grayscale_matrix();
	test_extract_red_matrix();
	test_extract_green_matrix();
	test_extract_blue_matrix();
	return 0;
}

