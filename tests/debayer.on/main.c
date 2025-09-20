/* SPDX-License-Identifier: Apache-2.0 */

#include <assert.h>
#include <stdio.h>

#include <mpix/print.h>
#include <mpix/image.h>

#define WIDTH  16
#define HEIGHT 16

#define ERROR_MARGIN 13

static uint8_t src_buf[WIDTH * HEIGHT * 1];
static uint8_t dst_buf[WIDTH * HEIGHT * 3];

void test_bayer(uint32_t fourcc, uint32_t window_size, uint32_t expected_color)
{
	uint8_t r = expected_color >> 16;
	uint8_t g = expected_color >> 8;
	uint8_t b = expected_color >> 0;
	struct mpix_image img;
	struct mpix_format fmt = {.width = WIDTH, .height = HEIGHT, .fourcc = fourcc};

	mpix_image_from_buf(&img, src_buf, sizeof(src_buf), &fmt);

	MPIX_INF("input: %s, bayer %ux%u", MPIX_FOURCC_TO_STR(fourcc), window_size, window_size);
	mpix_print_buf(src_buf, sizeof(src_buf), &img.fmt, true);

	assert(mpix_image_debayer(&img, window_size) == 0);

	mpix_image_to_buf(&img, dst_buf, sizeof(dst_buf));

	MPIX_INF("output: (expecting #%06x, R:%02x G:%02x B:%02x)", expected_color, r, g, b);
	mpix_print_buf(dst_buf, sizeof(dst_buf), &img.fmt, true);

	for (int i = 0; i < sizeof(dst_buf) / 3; i++) {
		assert(r == dst_buf[i * 3 + 0]);
		assert(g == dst_buf[i * 3 + 1]);
		assert(b == dst_buf[i * 3 + 2]);
	}
}

int main(void)
{
	/* Generate test input data for 2x2 debayer */
	for (size_t h = 0; h < HEIGHT; h++) {
		memset(src_buf + h * WIDTH, h % 2 ? 0xff : 0x00, WIDTH * 1);
	}

	test_bayer(MPIX_FMT_SRGGB8, 2, 0x007fff);
	test_bayer(MPIX_FMT_SGRBG8, 2, 0x007fff);
	test_bayer(MPIX_FMT_SBGGR8, 2, 0xff7f00);
	test_bayer(MPIX_FMT_SGBRG8, 2, 0xff7f00);

	/* Generate test input data for 3x3 debayer */
	for (size_t h = 0; h < HEIGHT; h++) {
		for (size_t w = 0; w < WIDTH; w++) {
			src_buf[h * WIDTH + w] = (h + w) % 2 ? 0xff : 0x00;
		}
	}

	test_bayer(MPIX_FMT_SRGGB8, 3, 0x00ff00);
	test_bayer(MPIX_FMT_SGBRG8, 3, 0xff00ff);
	test_bayer(MPIX_FMT_SBGGR8, 3, 0x00ff00);
	test_bayer(MPIX_FMT_SGRBG8, 3, 0xff00ff);

	return 0;
}
