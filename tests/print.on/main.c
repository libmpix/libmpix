/*
 * Copyright (c) 2025 tinyVision.ai Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include <mpix/print.h>
#include <mpix/utils.h>
#include <mpix/formats.h>

static const uint16_t rgb_hist[] = {
	9, 4, 7, 1, 0, 5, 1, 0, 0, 2, 2, 3, 0, 1, 3, 0, /* Red histogram values */
	7, 6, 5, 1, 1, 4, 2, 0, 1, 2, 3, 4, 1, 1, 2, 2, /* Green histogram values */
	8, 4, 7, 4, 2, 3, 1, 2, 2, 2, 2, 2, 0, 0, 1, 1, /* Blue histogram values */
};

static const uint16_t y_hist[] = {
	8, 5, 6, 2, 1, 4, 1, 1, 1, 2, 3, 3, 1, 1, 2, 1, /* Luma histogram values */
};

const struct mpix_format src_fmt = { .width = 16, .height = 32, .fourcc = MPIX_FMT_RGB24 };
static uint8_t src_buf[16 * 32 * 3];

int main(void)
{
	/* Print a complete image */
	{
		const uint8_t beg[] = {0x00, 0x70, 0xc5};
		const uint8_t end[] = {0x79, 0x29, 0xd2};

		/* Generate an image with a gradient of the two colors above */
		for (size_t i = 0, size = sizeof(src_buf); i + 3 <= size; i += 3) {
			src_buf[i + 0] = (beg[0] * (size - i) + end[0] * i) / size;
			src_buf[i + 1] = (beg[1] * (size - i) + end[1] * i) / size;
			src_buf[i + 2] = (beg[2] * (size - i) + end[2] * i) / size;
		}

		MPIX_INF("Printing the gradient #%02x%02x%02x -> #%02x%02x%02x",
		       beg[0], beg[1], beg[2], end[0], end[1], end[2]);

		MPIX_INF("hexdump:");
		mpix_hexdump_buf(src_buf, sizeof(src_buf), &src_fmt);

		MPIX_INF("truecolor:");
		mpix_print_buf(src_buf, sizeof(src_buf), &src_fmt, true);

		MPIX_INF("256color:");
		mpix_print_buf(src_buf, sizeof(src_buf), &src_fmt, false);
	}

	/* Print histograms */
	{
		const uint16_t *r_hist = &rgb_hist[ARRAY_SIZE(rgb_hist) * 0 / 3];
		const uint16_t *g_hist = &rgb_hist[ARRAY_SIZE(rgb_hist) * 1 / 3];
		const uint16_t *b_hist = &rgb_hist[ARRAY_SIZE(rgb_hist) * 2 / 3];

		MPIX_INF("Printing a hist of %zu RGB buckets", ARRAY_SIZE(rgb_hist) / 3);
		mpix_print_rgb_hist(r_hist, g_hist, b_hist, ARRAY_SIZE(rgb_hist), 8);

		MPIX_INF("Printing a hist of %zu Y (luma) buckets", ARRAY_SIZE(y_hist));
		mpix_print_y_hist(y_hist, ARRAY_SIZE(y_hist), 8);
	}

	return 0;
}
