/* SPDX-License-Identifier: Apache-2.0 */

#include <mpix/low_level.h>
#include <mpix/operation.h>

MPIX_REGISTER_OP(palette_decode);

struct mpix_operation {
	struct mpix_base_op base;
	/* Color palette to use for decodeing */
	const struct mpix_palette *palette;
};

int mpix_add_palette_decode(struct mpix_image *img, const int32_t *params)
{
	struct mpix_operation *op;
	size_t pitch = mpix_format_pitch(&img->fmt);

	(void)params;

	/* Parameter validation */
	if (mpix_palette_bit_depth(img->fmt.fourcc) == 0) {
		MPIX_ERR("not a palette format: %s", MPIX_FOURCC_TO_STR(img->fmt.fourcc));
		return -ERANGE;
	}

	/* Add an operation */
	op = mpix_op_append(img, MPIX_OP_PALETTE_DECODE, sizeof(*op), pitch);
	if (op == NULL) return -ENOMEM;

	/* Update the image format */
	img->fmt.fourcc = MPIX_FMT_RGB24;

	return 0;
}

int mpix_palette_decode_set_palette(struct mpix_base_op *base, const struct mpix_palette *palette)
{
	struct mpix_operation *op = (struct mpix_operation *)base;

	if (base->type != MPIX_OP_PALETTE_DECODE || base->fmt.fourcc != palette->fourcc) {
		return -EINVAL;
	}

	op->palette = palette;

	return 0;
}

void mpix_convert_palette1_to_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width,
				    const uint8_t colors_rgb24[3 << 1])
{
	assert(width % 8 == 0);

	for (uint16_t w = 0; w + 8 <= width; w += 8, src += 1, dst += 8 * 3) {
		memcpy(&dst[0], &colors_rgb24[((*src >> 7) & 0x1) * 3], 3);
		memcpy(&dst[3], &colors_rgb24[((*src >> 6) & 0x1) * 3], 3);
		memcpy(&dst[6], &colors_rgb24[((*src >> 5) & 0x1) * 3], 3);
		memcpy(&dst[9], &colors_rgb24[((*src >> 4) & 0x1) * 3], 3);
		memcpy(&dst[12], &colors_rgb24[((*src >> 3) & 0x1) * 3], 3);
		memcpy(&dst[15], &colors_rgb24[((*src >> 2) & 0x1) * 3], 3);
		memcpy(&dst[18], &colors_rgb24[((*src >> 1) & 0x1) * 3], 3);
		memcpy(&dst[21], &colors_rgb24[((*src >> 0) & 0x1) * 3], 3);
	}
}

void mpix_convert_palette2_to_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width,
				    const uint8_t colors_rgb24[3 << 2])
{
	assert(width % 4 == 0);

	for (uint16_t w = 0; w + 4 <= width; w += 4, src += 1, dst += 4 * 3) {
		memcpy(&dst[0], &colors_rgb24[((*src >> 6) & 0x3) * 3], 3);
		memcpy(&dst[3], &colors_rgb24[((*src >> 4) & 0x3) * 3], 3);
		memcpy(&dst[6], &colors_rgb24[((*src >> 2) & 0x3) * 3], 3);
		memcpy(&dst[9], &colors_rgb24[((*src >> 0) & 0x3) * 3], 3);
	}
}

void mpix_convert_palette4_to_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width,
				    const uint8_t colors_rgb24[3 << 4])
{
	assert(width % 2 == 0);

	for (uint16_t w = 0; w + 2 <= width; w += 2, src += 1, dst += 2 * 3) {
		memcpy(&dst[0], &colors_rgb24[((*src >> 4) & 0xf) * 3], 3);
		memcpy(&dst[3], &colors_rgb24[((*src >> 0) & 0xf) * 3], 3);
	}
}

void mpix_convert_palette8_to_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width,
				    const uint8_t colors_rgb24[3 << 8])
{
	for (uint16_t w = 0; w < width; w += 1, src += 1, dst += 3) {
		memcpy(dst, &colors_rgb24[*src * 3], 3);
	}
}

int mpix_run_palette_decode(struct mpix_base_op *base)
{
	struct mpix_operation *op = (void *)base;
	uint16_t width = base->fmt.width;
	const uint8_t *src;
	uint8_t *dst;

	if (op->palette == NULL || base->fmt.fourcc != op->palette->fourcc ||
	    base->next == NULL || base->next->fmt.fourcc != MPIX_FMT_RGB24) {
		return -EINVAL;
	}

	MPIX_OP_INPUT_LINES(base, &src, 1);
	MPIX_OP_OUTPUT_LINE(base, &dst);

	switch (base->fmt.fourcc) {
	case MPIX_FMT_PALETTE1:
		mpix_convert_palette1_to_rgb24(src, dst, width, op->palette->colors_rgb24);
		break;
	case MPIX_FMT_PALETTE2:
		mpix_convert_palette2_to_rgb24(src, dst, width, op->palette->colors_rgb24);
		break;
	case MPIX_FMT_PALETTE4:
		mpix_convert_palette4_to_rgb24(src, dst, width, op->palette->colors_rgb24);
		break;
	case MPIX_FMT_PALETTE8:
		mpix_convert_palette8_to_rgb24(src, dst, width, op->palette->colors_rgb24);
		break;
	default:
		return -ENOTSUP;
	}

	MPIX_OP_OUTPUT_DONE(base);
	MPIX_OP_INPUT_DONE(base, 1);

	return 0;
}
