#include <mpix/op_correction.h>
#include <mpix/test.h>

#define WIDTH 16
#define HEIGHT 16

uint8_t src[WIDTH * HEIGHT * 3];
uint8_t dst[WIDTH * HEIGHT * 3];

/* uses a 3x3 matrix we will ignore offsets and just use coefficients now */
const struct  mpix_correction_color_matrix ccm = {
	.levels  = {2,1,1,0,2,0,1,1,1}
};

union mpix_correction_any correction = {
	.color_matrix = ccm
};

void test_correction()
{
	/* Generate test input data */
	for (uint16_t h = 0; h < HEIGHT; h++) {
		for (uint16_t w = 0; w < WIDTH; w++) {
			src[h * WIDTH * 3 + w * 3 + 0] = w < WIDTH / 2 ? 0x00 : 0xff;
			src[h * WIDTH * 3 + w * 3 + 1] = (h % 3 + w % 3) / 4 * 0xff;
			src[h * WIDTH * 3 + w * 3 + 2] = h * 0xff / HEIGHT;
		}
	}

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

int main(void)
{
	test_correction();
	return 0;
}

