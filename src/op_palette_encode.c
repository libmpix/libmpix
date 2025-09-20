/* SPDX-License-Identifier: Apache-2.0 */

#include <mpix/low_level.h>
#include <mpix/operation.h>

MPIX_REGISTER_OP(palette_encode, P_FOURCC);

struct mpix_operation {
	struct mpix_base_op base;
	/* Color palette to use for encoding */
	const struct mpix_palette *palette;
};

int mpix_add_palette_encode(struct mpix_image *img, const int32_t *params)
{
	struct mpix_operation *op;
	size_t pitch = mpix_format_pitch(&img->fmt);

	/* Parameter validation */
	if (mpix_palette_bit_depth(params[P_FOURCC]) == 0) {
		MPIX_ERR("not a palette format: %s", MPIX_FOURCC_TO_STR(params[P_FOURCC]));
		return -ERANGE;
	}

	/* Add an operation */
	op = mpix_op_append(img, MPIX_OP_PALETTE_ENCODE, sizeof(*op), pitch);
	if (op == NULL) return -ENOMEM;

	/* Update the image format */
	img->fmt.fourcc = params[P_FOURCC];

	return 0;
}

int mpix_palette_encode_set_palette(struct mpix_base_op *base, const struct mpix_palette *palette)
{
	struct mpix_operation *op = (struct mpix_operation *)base;

	if (base->type != MPIX_OP_PALETTE_ENCODE ||
	    base->next == NULL || base->next->fmt.fourcc != palette->fourcc) {
		return -EINVAL;
	}

	op->palette = palette;

	return 0;
}

/*
 * The 3D (R, G, B) distance between two points is given by:
 *
 *     sqrt((r1 - r0)^2 + (g1 - g0)^2 + (b1 - b0)^2)
 *
 * But if A > B then sqrt(A) > sqrt(B), so no need to compute the square root.
 * The square of the distances are computed instead.
 */
static inline uint32_t mpix_rgb_square_distance(const uint8_t rgb0[3], const uint8_t rgb1[3])
{
	int16_t r_dist = (int16_t)rgb1[0] - (int16_t)rgb0[0];
	int16_t g_dist = (int16_t)rgb1[1] - (int16_t)rgb0[1];
	int16_t b_dist = (int16_t)rgb1[2] - (int16_t)rgb0[2];

	return r_dist * r_dist + g_dist * g_dist + b_dist + b_dist;
}

uint8_t mpix_palette_encode(const uint8_t rgb[3], const uint8_t colors_rgb24[], uint8_t bit_depth)
{
	size_t colors_nb = 1 << bit_depth;
	uint8_t best_color[3];
	uint32_t best_square_distance = UINT32_MAX;
	uint8_t idx = 0;

	for (size_t i = 0; i < colors_nb; i++) {
		const uint8_t *tmp = &colors_rgb24[i * 3];
		uint32_t square_distance = mpix_rgb_square_distance(tmp, rgb);

		if (square_distance < best_square_distance) {
			best_square_distance = square_distance;
			memcpy(best_color, tmp, sizeof(best_color));
			idx = i;
		}
	}

	return idx;
}

void mpix_convert_rgb24_to_palette1(const uint8_t *src, uint8_t *dst, uint16_t width,
				    const uint8_t colors_rgb24[3 << 1])
{
	assert(width % 8 == 0);

	for (uint16_t w = 0; w + 8 <= width; w += 8, src += 8 * 3, dst += 1) {
		*dst = mpix_palette_encode(&src[0], colors_rgb24, 1) << 7;
		*dst |= mpix_palette_encode(&src[3], colors_rgb24, 1) << 6;
		*dst |= mpix_palette_encode(&src[6], colors_rgb24, 1) << 5;
		*dst |= mpix_palette_encode(&src[9], colors_rgb24, 1) << 4;
		*dst |= mpix_palette_encode(&src[12], colors_rgb24, 1) << 3;
		*dst |= mpix_palette_encode(&src[15], colors_rgb24, 1) << 2;
		*dst |= mpix_palette_encode(&src[18], colors_rgb24, 1) << 1;
		*dst |= mpix_palette_encode(&src[21], colors_rgb24, 1) << 0;
	}
}

void mpix_convert_rgb24_to_palette2(const uint8_t *src, uint8_t *dst, uint16_t width,
				    const uint8_t colors_rgb24[3 << 2])
{
	assert(width % 4 == 0);

	for (uint16_t w = 0; w + 4 <= width; w += 4, src += 4 * 3, dst += 1) {
		*dst = mpix_palette_encode(&src[0], colors_rgb24, 2) << 6;
		*dst |= mpix_palette_encode(&src[3], colors_rgb24, 2) << 4;
		*dst |= mpix_palette_encode(&src[6], colors_rgb24, 2) << 2;
		*dst |= mpix_palette_encode(&src[9], colors_rgb24, 2) << 0;
	}
}

void mpix_convert_rgb24_to_palette4(const uint8_t *src, uint8_t *dst, uint16_t width,
				    const uint8_t colors_rgb24[3 << 4])
{
	assert(width % 2 == 0);

	for (uint16_t w = 0; w + 2 <= width; w += 2, src += 2 * 3, dst += 1) {
		*dst = mpix_palette_encode(&src[0], colors_rgb24, 4) << 4;
		*dst |= mpix_palette_encode(&src[3], colors_rgb24, 4) << 0;
	}
}

void mpix_convert_rgb24_to_palette8(const uint8_t *src, uint8_t *dst, uint16_t width,
				    const uint8_t colors_rgb24[3 << 8])
{
	for (uint16_t w = 0; w < width; w += 1, src += 3, dst += 1) {
		*dst = mpix_palette_encode(src, colors_rgb24, 8);
	}
}

int mpix_run_palette_encode(struct mpix_base_op *base)
{
	struct mpix_operation *op = (void *)base;
	uint16_t width = base->fmt.width;
	const uint8_t *src;
	uint8_t *dst;

	if (op->palette == NULL || base->fmt.fourcc != MPIX_FMT_RGB24 ||
	    base->next == NULL || base->next->fmt.fourcc != op->palette->fourcc) {
		return -EINVAL;
	}

	MPIX_OP_INPUT_LINES(base, &src, 1);
	MPIX_OP_OUTPUT_LINE(base, &dst);

	switch (base->next->fmt.fourcc) {
	case MPIX_FMT_PALETTE1:
		mpix_convert_rgb24_to_palette1(src, dst, width, op->palette->colors_rgb24);
		break;
	case MPIX_FMT_PALETTE2:
		mpix_convert_rgb24_to_palette2(src, dst, width, op->palette->colors_rgb24);
		break;
	case MPIX_FMT_PALETTE4:
		mpix_convert_rgb24_to_palette4(src, dst, width, op->palette->colors_rgb24);
		break;
	case MPIX_FMT_PALETTE8:
		mpix_convert_rgb24_to_palette8(src, dst, width, op->palette->colors_rgb24);
		break;
	default:
		return -ENOTSUP;
	}

	MPIX_OP_OUTPUT_DONE(base);
	MPIX_OP_INPUT_DONE(base, 1);

	return 0;
}
