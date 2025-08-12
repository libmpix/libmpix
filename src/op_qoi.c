/* SPDX-License-Identifier: Apache-2.0 */

#include <assert.h>
#include <errno.h>

#include <mpix/formats.h>
#include <mpix/genlist.h>
#include <mpix/image.h>
#include <mpix/op_qoi.h>
#include <mpix/utils.h>

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

static inline size_t mpix_qoi_add_header(struct mpix_qoi_op *op, uint8_t *dst, size_t dst_sz)
{
	size_t o = 0;

	/* magic */
	MPIX_QOI_PUT_U8('q');
	MPIX_QOI_PUT_U8('o');
	MPIX_QOI_PUT_U8('i');
	MPIX_QOI_PUT_U8('f');

	/* width */
	MPIX_QOI_PUT_U32(op->base.width);

	/* height */
	MPIX_QOI_PUT_U32(op->base.height);

	/* channels */
	MPIX_QOI_PUT_U8(3);

	/* colorspace */
	MPIX_QOI_PUT_U8(0);

	return o;
}

static inline size_t mpix_qoi_encode_rgb24(struct mpix_qoi_op *op, const uint8_t *src,
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

void mpix_qoi_encode_rgb24_op(struct mpix_base_op *base)
{
	struct mpix_qoi_op *op = (void *)base;
	bool first = (base->line_offset == 0);
	const uint8_t *src = mpix_op_get_input_line(base);
	size_t dst_sz = 0;
	uint8_t *dst = mpix_op_peek_output(base, &dst_sz);
	bool is_last = (base->line_offset >= base->height);
	size_t o = 0;

	assert(base->width > 0);

	if (first) {
		o += mpix_qoi_add_header(op, dst + o, dst_sz - o);
	}

	for (uint16_t w = 0; w < base->width - 1; w++, src += 3) {
		o += mpix_qoi_encode_rgb24(op, src, dst + o, dst_sz - o, false);
	}
	o += mpix_qoi_encode_rgb24(op, src, dst + o, dst_sz - o, is_last);

	if (is_last && o + 8 < dst_sz) {
		dst[o++] = 0x00, dst[o++] = 0x00, dst[o++] = 0x00, dst[o++] = 0x00;
		dst[o++] = 0x00, dst[o++] = 0x00, dst[o++] = 0x00, dst[o++] = 0x01;
	}

	mpix_op_get_output_bytes(base, o);
	mpix_op_done(base);
}
MPIX_REGISTER_QOI_OP(encode_rgb24, mpix_qoi_encode_rgb24_op, RGB24, QOI);

static const struct mpix_qoi_op **mpix_qoi_op_list =
	(const struct mpix_qoi_op *[]){MPIX_LIST_QOI_OP};

int mpix_image_qoi_encode(struct mpix_image *img)
{
	struct mpix_qoi_op *op = NULL;

	op = mpix_op_by_format(mpix_qoi_op_list, img->fourcc, MPIX_FMT_QOI);
	if (op == NULL) {
		MPIX_ERR("Conversion operation from %s to %s not found",
			 MPIX_FOURCC_TO_STR(img->fourcc), MPIX_FOURCC_TO_STR(MPIX_FMT_QOI));
		return mpix_image_error(img, -ENOSYS);
	}

	return mpix_image_append_uncompressed_op(img, &op->base, sizeof(*op));
}
