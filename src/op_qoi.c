/* SPDX-License-Identifier: Apache-2.0 */

#include <mpix/low_level.h>
#include <mpix/operation.h>

MPIX_REGISTER_OP(qoi_encode);

struct mpix_operation {
	struct mpix_base_op base;
	/** Array of previously seen pixels */
	uint8_t qoi_cache[64 * 3];
	/** The last seend pixel value just before the new pixel to encode */
	uint8_t qoi_prev[3];
	/** Size of the ongoing run */
	uint8_t qoi_run_length;
};

int mpix_add_qoi_encode(struct mpix_image *img, const int32_t *params)
{
	struct mpix_operation *op;
	size_t pitch = mpix_format_pitch(&img->fmt);

	(void)params;

	/* Add an operation */
	op = mpix_op_append(img, MPIX_OP_QOI_ENCODE, sizeof(*op), pitch);
	if (op == NULL) {
		return -ENOMEM;
	}

	/* Update the image format */
	img->fmt.fourcc = MPIX_FMT_QOI;

	return 0;
}

#define MPIX_QOI_OP_INDEX  0x00 /* 00xxxxxx */
#define MPIX_QOI_OP_DIFF   0x40 /* 01xxxxxx */
#define MPIX_QOI_OP_LUMA   0x80 /* 10xxxxxx */
#define MPIX_QOI_OP_RUN    0xc0 /* 11xxxxxx */
#define MPIX_QOI_OP_RGB    0xfe /* 11111110 */
#define MPIX_QOI_OP_RGBA   0xff /* 11111111 */

#define MPIX_QOI_PUT_U8(u) ({                                                                      \
	if (o + 1 >= dst_sz) {                                                                     \
		return o;                                                                          \
	}                                                                                          \
	dst[o++] = (u);                                                                            \
})

#define MPIX_QOI_PUT_U32(u) ({                                                                     \
	if (o + 4 >= dst_sz) {                                                                     \
		return o;                                                                          \
	}                                                                                          \
	dst[o++] = (u) >> 24;                                                                      \
	dst[o++] = (u) >> 16;                                                                      \
	dst[o++] = (u) >> 8;                                                                       \
	dst[o++] = (u) >> 0;                                                                       \
})

static inline size_t mpix_qoi_add_header(struct mpix_operation *op, uint8_t *dst, size_t dst_sz)
{
	size_t o = 0;

	/* magic */
	MPIX_QOI_PUT_U8('q');
	MPIX_QOI_PUT_U8('o');
	MPIX_QOI_PUT_U8('i');
	MPIX_QOI_PUT_U8('f');

	/* width */
	MPIX_QOI_PUT_U32(op->base.fmt.width);

	/* height */
	MPIX_QOI_PUT_U32(op->base.fmt.height);

	/* channels */
	MPIX_QOI_PUT_U8(3);

	/* colorspace */
	MPIX_QOI_PUT_U8(0);

	return o;
}

static inline size_t mpix_qoi_encode_rgb24(struct mpix_operation *op, const uint8_t *src,
					   uint8_t *dst, size_t dst_sz, bool is_last)
{
	const uint8_t r = src[0];
	const uint8_t g = src[1];
	const uint8_t b = src[2];
	const uint8_t a = 0xff;
	uint8_t cache_idx;
	size_t o = 0;

	/* Handle run-length operation */
	if (memcmp(op->qoi_prev, src, 3) == 0) {
		/* Increase run-length if same pixel */
		op->qoi_run_length++;

		/* Flush current run-length if reaching the maximum */
		if (op->qoi_run_length >= 62 || is_last) {
			MPIX_QOI_PUT_U8(MPIX_QOI_OP_RUN | (op->qoi_run_length - 1));
			op->qoi_run_length = 0;
		}
		return o;
	} else {
		/* Flush current run-length if different pixel */
		if (op->qoi_run_length > 0) {
			MPIX_QOI_PUT_U8(MPIX_QOI_OP_RUN | (op->qoi_run_length - 1));
			op->qoi_run_length = 0;
		}
	}

	/* Handle run-length operation */
	cache_idx = ((uint16_t)r * 3 + (uint16_t)g * 5 + (uint16_t)b * 7 + (uint16_t)a * 11) % 64;
	if (memcmp(&op->qoi_cache[3 * cache_idx], src, 3) == 0) {
		/* Use table encoding if previously encountered */
		MPIX_QOI_PUT_U8(MPIX_QOI_OP_INDEX | cache_idx);
		memcpy(op->qoi_prev, src, 3);
		return o;
	} else {
		/* Cache the pixel in the array since it is different */
		memcpy(&op->qoi_cache[3 * cache_idx], src, 3);
	}

	/* Relative difference with the previous pixels with wrap arround */
	int8_t dr = r - op->qoi_prev[0];
	int8_t dg = g - op->qoi_prev[1];
	int8_t db = b - op->qoi_prev[2];

	/* Difference between the green channel and the red/blue channel */
	int8_t dgr = dr - dg;
	int8_t dgb = db - dg;

	/* Handle difference operation */
	if (IN_RANGE(dr, -2, 1) && IN_RANGE(dg, -2, 1) && IN_RANGE(db, -2, 1)) {
		MPIX_QOI_PUT_U8(MPIX_QOI_OP_DIFF | (dr + 2) << 4 | (dg + 2) << 2 | (db + 2) << 0);
		memcpy(op->qoi_prev, src, 3);
		return o;
	}

	/* Handle luma operation */
	if (IN_RANGE(dgr, -8, 7) && IN_RANGE(dg, -32, 31) && IN_RANGE(dgb, -8, 7)) {
		MPIX_QOI_PUT_U8(MPIX_QOI_OP_LUMA | (dg + 32));
		MPIX_QOI_PUT_U8((dgr + 8) << 4 | (dgb + 8) << 0);
		memcpy(op->qoi_prev, src, 3);
		return o;
	}

	/* Handle full RGB operation as fallback */
	MPIX_QOI_PUT_U8(MPIX_QOI_OP_RGB);
	MPIX_QOI_PUT_U8(r);
	MPIX_QOI_PUT_U8(g);
	MPIX_QOI_PUT_U8(b);

	memcpy(op->qoi_prev, src, 3);
	return o;
}

int mpix_run_qoi_encode(struct mpix_base_op *base)
{
	struct mpix_operation *op = (void *)base;
	const uint8_t *src;
	uint8_t *dst;
	bool is_first = (base->line_offset == 0);
	bool is_last = (base->line_offset >= base->fmt.height);
	size_t dst_sz;
	size_t o = 0;

	MPIX_OP_INPUT_LINES(base, &src, 1);
	MPIX_OP_OUTPUT_PEEK(base, &dst, &dst_sz);

	if (is_first) {
		o += mpix_qoi_add_header(op, dst + o, dst_sz - o);
	}

	for (uint16_t w = 0; w < base->fmt.width - 1; w++, src += 3) {
		o += mpix_qoi_encode_rgb24(op, src, dst + o, dst_sz - o, false);
	}
	o += mpix_qoi_encode_rgb24(op, src, dst + o, dst_sz - o, is_last);

	if (is_last && o + 8 < dst_sz) {
		dst[o++] = 0x00, dst[o++] = 0x00, dst[o++] = 0x00, dst[o++] = 0x00;
		dst[o++] = 0x00, dst[o++] = 0x00, dst[o++] = 0x00, dst[o++] = 0x01;
	}

	MPIX_OP_OUTPUT_FLUSH(base, o);
	MPIX_OP_OUTPUT_DONE(base);
	MPIX_OP_INPUT_DONE(base, 1);

	return 0;
}
