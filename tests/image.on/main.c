/*
 * Copyright (c) 2025 tinyVision.ai Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>
#include <assert.h>
#include <stdio.h>

#include <mpix/image.h>
#include <mpix/print.h>

static void gradient(uint8_t *dst, size_t size, const uint8_t beg[3], const uint8_t end[3])
{
	for (size_t i = 0; i + 3 <= size; i += 3) {
		dst[i + 0] = (beg[0] * (size - i) + end[0] * i) / size;
		dst[i + 1] = (beg[1] * (size - i) + end[1] * i) / size;
		dst[i + 2] = (beg[2] * (size - i) + end[2] * i) / size;
	}
}

static uint8_t src_buf[32 * 8 * 3];
static uint8_t dst_buf[120 * 40 * 3];

int main(void)
{
	const uint8_t beg[] = {0x00, 0x70, 0xc5};
	const uint8_t end[] = {0x79, 0x29, 0xd2};
	struct mpix_image img = {};
	struct mpix_format src_fmt = {.width = 32, .height = 8, .fourcc = MPIX_FMT_RGB24};

	/* Generate a smooth gradient for a small image */
	gradient(src_buf, sizeof(src_buf), beg, end);

	/* Open that buffer as an image type */
	mpix_image_from_buf(&img, src_buf, sizeof(src_buf), &src_fmt);
	MPIX_INF("input image, %ux%u, %zu bytes:", img.fmt.width, img.fmt.height, img.size);
	mpix_print_buf(src_buf, sizeof(src_buf), &src_fmt, true);

	/* Turn it into a tall vertical image, now displeasant "banding" artifacts appear */
	assert(mpix_image_subsample(&img, 5, 40) == 0);

	/* Try to attenuate it with a blur effect (comment this line to see the difference) */
	assert(mpix_image_gaussian_blur(&img, 3) == 0);

	/* Stretch the gradient horizontally over the entire size of the output buffer */
	//assert(mpix_image_subsample(&img, 120, img.fmt.height) == 0);

	/* Save the image into the output buffer and check for errors */
	assert(mpix_image_to_buf(&img, dst_buf, sizeof(dst_buf)) == 0);

	/* Now that the imagme is exported, we can print it */
	printf("output image, %ux%u, %zu bytes:\n", img.fmt.width, img.fmt.height, img.size);
	mpix_print_buf(dst_buf, sizeof(dst_buf), &img.fmt, true);

	mpix_print_pipeline(img.first_op);

	mpix_image_free(&img);
	return 0;
}
