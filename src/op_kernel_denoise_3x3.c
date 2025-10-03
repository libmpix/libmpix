/* SPDX-License-Identifier: Apache-2.0 */

#include <mpix/low_level.h>
#include <mpix/operation.h>

MPIX_REGISTER_OP(kernel_denoise_3x3);

int mpix_add_kernel_denoise_3x3(struct mpix_image *img, const int32_t *params)
{
	struct mpix_base_op *op;
	size_t pitch = mpix_format_pitch(&img->fmt);

	(void)params;

	/* Add an operation */
	op = mpix_op_append(img, MPIX_OP_KERNEL_DENOISE_3X3, sizeof(*op), pitch * 3);
	if (op == NULL) {
		return -ENOMEM;
	}

	return 0;
}

static void mpix_kernel_denoise_3x3(uint16_t base, const uint8_t *src[3], int i0, int i1, int i2,
				    uint8_t *dst, int o0)
{
	uint8_t pivot_bot = 0x00;
	uint8_t pivot_top = 0xff;
	uint8_t num_higher;
	int16_t median;

	/* Binary-search of the appropriate median value, 8 steps for 8-bit depth */
	for (int i = 0; i < 8; i++) {
		num_higher = 0;
		median = (pivot_top + pivot_bot) / 2;

		for (uint16_t h = 0; h < 3; h++) {
			num_higher += src[h][base + i0] > median;
			num_higher += src[h][base + i1] > median;
			num_higher += src[h][base + i2] > median;
		}

		if (num_higher > 3 * 3 / 2) {
			pivot_bot = median;
		} else if (num_higher < 3 * 3 / 2) {
			pivot_top = median;
		}
	}

	dst[base + o0] = (pivot_top + pivot_bot) / 2;
}

static inline void mpix_kernel_denoise_3x3_rgb24(const uint8_t *src[3], uint8_t *dst,
						 uint16_t width)
{
	enum { R, G, B };
	uint16_t w = 0;

	/* Edge case on first two columns, repeat the left column to fill the blank */

	mpix_kernel_denoise_3x3(w * 3 + R, src, 0, 0, 3, dst, 0);
	mpix_kernel_denoise_3x3(w * 3 + G, src, 0, 0, 3, dst, 0);
	mpix_kernel_denoise_3x3(w * 3 + B, src, 0, 0, 3, dst, 0);

	/* process as much as possible with SIMD acceleration when available */

#ifdef CONFIG_MPIX_SIMD_HELIUM
	w += mpix_kernel_denoise_rgb24_3x3_helium(&src[w], &dst[w], width - w);
#endif

	/* Process the entire line except the first two and last two columns (edge cases) */

	for (; w + 5 <= width; w++) {
		mpix_kernel_denoise_3x3(w * 3 + R, src, 0, 3, 6, dst, 3);
		mpix_kernel_denoise_3x3(w * 3 + G, src, 0, 3, 6, dst, 3);
		mpix_kernel_denoise_3x3(w * 3 + B, src, 0, 3, 6, dst, 3);
	}

	/* Edge case on last two columns, repeat the right column to fill the blank */

	mpix_kernel_denoise_3x3(w * 3 + R, src, 3, 6, 6, dst, 3);
	mpix_kernel_denoise_3x3(w * 3 + G, src, 3, 6, 6, dst, 3);
	mpix_kernel_denoise_3x3(w * 3 + B, src, 3, 6, 6, dst, 3);
}

int mpix_run_kernel_denoise_3x3(struct mpix_base_op *base)
{
	const uint8_t *src[3];
	uint8_t *dst;

	MPIX_OP_INPUT_LINES(base, src, ARRAY_SIZE(src));

	uint16_t width = base->fmt.width;
	const uint8_t *lines[] = { src[0], src[0], src[1], src[2], src[2] };

	/* Handle edgge case on first line */
	if (base->line_offset == 0) {
		MPIX_OP_OUTPUT_LINE(base, &dst);
		mpix_kernel_denoise_3x3_rgb24(&lines[0], dst, width);
		MPIX_OP_OUTPUT_DONE(base);
	}

	/* Process one line */
	MPIX_OP_OUTPUT_LINE(base, &dst);
	mpix_kernel_denoise_3x3_rgb24(&lines[1], dst, width);
	MPIX_OP_OUTPUT_DONE(base);

	/* Handle edgge case on last line */
	if (base->line_offset + 3 >= base->fmt.height) {
		MPIX_OP_OUTPUT_LINE(base, &dst);
		mpix_kernel_denoise_3x3_rgb24(&lines[2], dst, width);
		MPIX_OP_OUTPUT_DONE(base);
	}

	MPIX_OP_INPUT_DONE(base, 1);

	return 0;
}
