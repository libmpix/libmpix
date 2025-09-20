/* Copyright (c) 2025 tinyVision.ai Inc. */
/* SPDX-License-Identifier: Apache-2.0 */

#include <stdio.h>
#include <assert.h>

#include <mpix/image.h>
#include <mpix/print.h>

#define SRC_WIDTH  6
#define SRC_HEIGHT 10
#define PITCH_IN  (SRC_WIDTH * 3)

#define DST_WIDTH  40
#define DST_HEIGHT 22
#define PITCH_OUT  (DST_WIDTH * 3)

#define ERROR_MARGIN 9

/* Input/output buffers */
static uint8_t src_buf[SRC_WIDTH * SRC_HEIGHT * 3];
static uint8_t dst_buf[DST_WIDTH * DST_HEIGHT * 3];

const struct mpix_format src_fmt = {
	.width = SRC_WIDTH,
	.height = SRC_HEIGHT,
	.fourcc = MPIX_FMT_RGB24,
};

static void test_subsample(uint32_t fourcc)
{
	struct mpix_image img;
	const size_t w = DST_WIDTH;
	const size_t h = DST_HEIGHT;
	const size_t p = PITCH_OUT;
	struct mpix_format dst_fmt = { .width = w, .height = h, .fourcc = fourcc };

	mpix_image_from_buf(&img, src_buf, sizeof(src_buf), &src_fmt);

	MPIX_INF("%s:", MPIX_FOURCC_TO_STR(fourcc));
	MPIX_INF("input:");
	mpix_print_buf(src_buf, sizeof(src_buf), &src_fmt, true);

	assert(mpix_image_convert(&img, fourcc) == 0);
	assert(mpix_image_subsample(&img, DST_WIDTH, DST_HEIGHT) == 0);
	assert(mpix_image_convert(&img, MPIX_FMT_RGB24) == 0);
	assert(mpix_image_to_buf(&img, dst_buf, sizeof(dst_buf)) == 0);
	MPIX_INF("output:");
	mpix_print_buf(dst_buf, sizeof(dst_buf), &dst_fmt, true);

	mpix_image_free(&img);

	/* Test top left quadramt */
	assert(WITHIN(dst_buf[(0) * p + (0) * 3 + 0], 0x00, ERROR_MARGIN));
	assert(WITHIN(dst_buf[(0) * p + (0) * 3 + 1], 0x00, ERROR_MARGIN));
	assert(WITHIN(dst_buf[(0) * p + (0) * 3 + 2], 0x7f, ERROR_MARGIN));
	assert(WITHIN(dst_buf[(h / 2 - 1) * p + (w / 2 - 1) * 3 + 0], 0x00, ERROR_MARGIN));
	assert(WITHIN(dst_buf[(h / 2 - 1) * p + (w / 2 - 1) * 3 + 1], 0x00, ERROR_MARGIN));
	assert(WITHIN(dst_buf[(h / 2 - 1) * p + (w / 2 - 1) * 3 + 2], 0x7f, ERROR_MARGIN));

	/* Test bottom left quadrant */
	assert(WITHIN(dst_buf[(h - 1) * p + (0) * 3 + 0], 0x00, ERROR_MARGIN));
	assert(WITHIN(dst_buf[(h - 1) * p + (0) * 3 + 1], 0xff, ERROR_MARGIN));
	assert(WITHIN(dst_buf[(h - 1) * p + (0) * 3 + 2], 0x7f, ERROR_MARGIN));
	assert(WITHIN(dst_buf[(h / 2 + 1) * p + (w / 2 - 1) * 3 + 0], 0x00, ERROR_MARGIN));
	assert(WITHIN(dst_buf[(h / 2 + 1) * p + (w / 2 - 1) * 3 + 1], 0xff, ERROR_MARGIN));
	assert(WITHIN(dst_buf[(h / 2 + 1) * p + (w / 2 - 1) * 3 + 2], 0x7f, ERROR_MARGIN));

	/* Test top right quadrant */
	assert(WITHIN(dst_buf[(0) * p + (w - 1) * 3 + 0], 0xff, ERROR_MARGIN));
	assert(WITHIN(dst_buf[(0) * p + (w - 1) * 3 + 1], 0x00, ERROR_MARGIN));
	assert(WITHIN(dst_buf[(0) * p + (w - 1) * 3 + 2], 0x7f, ERROR_MARGIN));
	assert(WITHIN(dst_buf[(h / 2 - 1) * p + (w / 2 + 1) * 3 + 0], 0xff, ERROR_MARGIN));
	assert(WITHIN(dst_buf[(h / 2 - 1) * p + (w / 2 + 1) * 3 + 1], 0x00, ERROR_MARGIN));
	assert(WITHIN(dst_buf[(h / 2 - 1) * p + (w / 2 + 1) * 3 + 2], 0x7f, ERROR_MARGIN));

	/* Test bottom right quadrant */
	assert(WITHIN(dst_buf[(h - 1) * p + (w - 1) * 3 + 0], 0xff, ERROR_MARGIN));
	assert(WITHIN(dst_buf[(h - 1) * p + (w - 1) * 3 + 1], 0xff, ERROR_MARGIN));
	assert(WITHIN(dst_buf[(h - 1) * p + (w - 1) * 3 + 2], 0x7f, ERROR_MARGIN));
	assert(WITHIN(dst_buf[(h / 2 + 1) * p + (w / 2 + 1) * 3 + 0], 0xff, ERROR_MARGIN));
	assert(WITHIN(dst_buf[(h / 2 + 1) * p + (w / 2 + 1) * 3 + 1], 0xff, ERROR_MARGIN));
	assert(WITHIN(dst_buf[(h / 2 + 1) * p + (w / 2 + 1) * 3 + 2], 0x7f, ERROR_MARGIN));
}

int main(void)
{
	/* Generate test input data */
	for (uint16_t h = 0; h < SRC_HEIGHT; h++) {
		for (uint16_t w = 0; w < SRC_WIDTH; w++) {
			src_buf[h * PITCH_IN + w * 3 + 0] = w < SRC_WIDTH / 2 ? 0x00 : 0xff;
			src_buf[h * PITCH_IN + w * 3 + 1] = h < SRC_HEIGHT / 2 ? 0x00 : 0xff;
			src_buf[h * PITCH_IN + w * 3 + 2] = 0x7f;
		}
	}

	test_subsample(MPIX_FMT_RGB24);
	test_subsample(MPIX_FMT_RGB565);
	test_subsample(MPIX_FMT_RGB565X);

	return 0;
}
