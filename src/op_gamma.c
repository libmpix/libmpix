/* SPDX-License-Identifier: Apache-2.0 */

#define MPIX_GAMMA_STEP 4
#define MPIX_GAMMA_MIN 1
#define MPIX_GAMMA_MAX 16

/* Generated by gengamma.py */
static const uint8_t mpix_gamma_log4_y[] = {
	181, 197, 215, 234,			/* y for gamma = 1 / 16 */
	128, 152, 181, 215,			/* y for gamma = 2 / 16 */
	90, 117, 152, 197,			/* y for gamma = 3 / 16 */
	64, 90, 128, 181,			/* y for gamma = 4 / 16 */
	45, 69, 107, 165,			/* y for gamma = 5 / 16 */
	32, 53, 90, 152,			/* y for gamma = 6 / 16 */
	22, 41, 76, 139,			/* y for gamma = 7 / 16 */
	16, 32, 64, 128,			/* y for gamma = 8 / 16 */
	11, 24, 53, 117,			/* y for gamma = 9 / 16 */
	8, 19, 45, 107,				/* y for gamma = 10 / 16 */
	5, 14, 38, 98,				/* y for gamma = 11 / 16 */
	4, 11, 32, 90,				/* y for gamma = 12 / 16 */
	2, 8, 26, 82,				/* y for gamma = 13 / 16 */
	2, 6, 22, 76,				/* y for gamma = 14 / 16 */
	1, 5, 19, 69,				/* y for gamma = 15 / 16 */
};
static const uint8_t mpix_gamma_log4_x[] = {
	1, 4, 16, 64,				/* x scale */
};

static inline uint8_t mpix_gamma_raw8(uint8_t raw8, const uint8_t *table, const uint8_t *scale,
				      size_t size)
{
	uint8_t x0 = 0;
	uint8_t x1 = 0;
	uint8_t y0;
	uint8_t y1;
	int i;

	if (raw8 == 0) {
		return 0;
	}

	/* Lookup the values through the scale of values. This is expected to be optimized
	 * into  afixed set of values by the compiler as the gamma scale is very small.
	 */
	for (i = 0;; i++) {
		if (i >= size) {
			y0 = table[i - 1];
			x1 = y1 = 0xff;
			break;
		}

		x1 = scale[i];

		if (raw8 < x1) {
			y0 = table[i - 1];
			y1 = table[i - 0];
			break;
		}

		x0 = x1;
	}

	return ((x1 - raw8) * y0 + (raw8 - x0) * y1) / (x1 - x0);
}

void mpix_op_gamma_raw8(struct mpix_base_op *base)
{
	struct mpix_isp_op *op = (void *)base;
	const uint8_t *src = mpix_op_get_input_line(base);
	uint8_t *dst = mpix_op_get_output_line(base);
	const uint8_t *gamma_y;

	if (!IN_RANGE(op->gamma_level, MPIX_GAMMA_MIN, MPIX_GAMMA_MAX)) {
		MPIX_ERR("Gamma level %u not in range [%u, %u]",
			op->gamma_level, MPIX_GAMMA_MIN, MPIX_GAMMA_MAX);
		mpix_op_done(base);
		return;
	}

	/* Select the range of values from the lookup table that matches the desired gamma */
	gamma_y = &mpix_gamma_log4_y[(op->gamma_level - MPIX_GAMMA_MIN) * MPIX_GAMMA_STEP];

	/* Apply the gamma correction to every value by interpolation from the lookup table */
	for (int w = 0; w < base->width; w++, dst++, src++) {
		*dst = mpix_gamma_raw8(*src, level, gamma_y, mpix_gamma_log4_x,
				       sizeof(mpix_gamma_log4_x));
	}

	mpix_op_done(base);
}
