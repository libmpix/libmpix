/*
 * Copyright (c) 2025 tinyVision.ai Inc.
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdint.h>

#include <mpix/image.h>
#include <mpix/test.h>

static void gradient(uint8_t *rgb24buf, size_t size, const uint8_t beg[3], const uint8_t end[3])
{
	for (int i = 0; i + 3 <= size; i += 3) {
		rgb24buf[i + 0] = (beg[0] * (size - i) + end[0] * i) / size;
		rgb24buf[i + 1] = (beg[1] * (size - i) + end[1] * i) / size;
		rgb24buf[i + 2] = (beg[2] * (size - i) + end[2] * i) / size;
	}
}

static uint8_t rgb24frame_in[32 * 8 * 3];
static uint8_t rgb24frame_out[120 * 40 * 3];

int main(void)
{
	const uint8_t beg[] = {0x00, 0x70, 0xc5};
	const uint8_t end[] = {0x79, 0x29, 0xd2};
	struct mpix_image img = {};

	/* Generate a smooth gradient for a small image */
	gradient(rgb24frame_in, sizeof(rgb24frame_in), beg, end);

	/* Open that buffer as an image type */
	mpix_image_from_buf(&img, rgb24frame_in, sizeof(rgb24frame_in), 32, 8, MPIX_FMT_RGB24);
	printf("input image, %ux%u, %zu bytes:\n", img.width, img.height, img.size);
	mpix_image_print_truecolor(&img);

	/* Turn it into a tall vertical image, now displeasant "banding" artifacts appear */
	mpix_image_resize(&img, 5, 40);

	/* Try to attenuate it with a blur effect (comment this line to see the difference) */
	mpix_image_kernel(&img, MPIX_KERNEL_GAUSSIAN_BLUR, 5);

	/* Stretch the gradient horizontally over the entire width of the output buffer */
	mpix_image_resize(&img, 120, 40);

	/* Save the image into the output buffer and check for errors */
	mpix_image_to_buf(&img, rgb24frame_out, sizeof(rgb24frame_out));
	mpix_test_ok(img.err);

	/* Now that the imagme is exported, we can print it */
	printf("output image, %ux%u, %zu bytes:\n", img.width, img.height, img.size);
	mpix_image_print_truecolor(&img);

	return 0;
}
