/* SPDX-License-Identifier: Apache-2.0 */

#include <assert.h>
#include <string.h>
#include <errno.h>

#include <mpix/genlist.h>
#include <mpix/image.h>
#include <mpix/low_level.h>
#include <mpix/port.h>
#include <mpix/sample.h>
#include <mpix/utils.h>

/*
 * This is an implementation of K-Mean algorithm to estimate the color palette that best matches
 * an image.
 */
int mpix_image_optimize_palette(struct mpix_image *img, struct mpix_palette *palette,
				uint16_t num_samples)
{
	size_t colors_nb = 1u << mpix_palette_bit_depth(palette->fourcc);
	uint32_t *sums;
	const size_t sums_sz = colors_nb * sizeof(*sums) * 3;
	uint16_t *nums;
	const size_t nums_sz = colors_nb * sizeof(*nums);
	uint8_t rgb[3];
	int err;

	sums = mpix_port_alloc(sums_sz);
	if (sums == NULL) {
		MPIX_ERR("Failed to allocate the sum array");
		return -ENOMEM;
	}
	memset(sums, 0x00, sums_sz);

	nums = mpix_port_alloc(nums_sz);
	if (nums == NULL) {
		MPIX_ERR("Failed to allocate the num array");
		mpix_port_free(sums);
		return -ENOMEM;
	}
	memset(nums, 0x00, nums_sz);

	/* Take samples from the input image, find which palette point they belong to, and
	 * accumulate the colors to get an average color.
	 */
	for (uint16_t i = 0; i < num_samples; i++) {
		uint8_t idx;

		err = mpix_sample_random_rgb(img->buffer, &img->first_op->fmt, rgb);
		if (err) return err;

		idx = mpix_palette_encode(rgb, palette->colors_rgb24,
					  mpix_palette_bit_depth(palette->fourcc));

		sums[idx * 3 + 0] += rgb[0];
		sums[idx * 3 + 1] += rgb[1];
		sums[idx * 3 + 2] += rgb[2];
		nums[idx]++;
	}

	/* Average each sum to generate the new palette, with now slightly adjusted colors to
	 * better fit the input image. Repeating the operation allows to improve the accuracy.
	 */
	for (size_t idx = 0; idx < colors_nb; idx++) {
		if (nums[idx] == 0) {
			/* If no value was detected, shift the colors a bit */
			palette->colors_rgb24[idx * 3 + 0] += 0x10;
			palette->colors_rgb24[idx * 3 + 1] += 0x10;
			palette->colors_rgb24[idx * 3 + 2] += 0x10;
		} else {
			/* If there are matches, re-compute a better value */
			palette->colors_rgb24[idx * 3 + 0] = sums[idx * 3 + 0] / nums[idx];
			palette->colors_rgb24[idx * 3 + 1] = sums[idx * 3 + 1] / nums[idx];
			palette->colors_rgb24[idx * 3 + 2] = sums[idx * 3 + 2] / nums[idx];
		}
	}

	mpix_port_free(sums);
	mpix_port_free(nums);

	return 0;
}
