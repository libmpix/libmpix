/* SPDX-License-Identifier: Apache-2.0 */

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "arm_mve.h"

#include <mpix/utils.h>

#define MPIX_CORRECTION_SCALE_BITS 10

/* -------------------- Common helpers -------------------- */

static inline uint8x16_t mpix_rgb_offsets3(void)
{
	/* byte offsets for 16 pixels in RGB24: 0,3,6,...,45 */
	return vmulq_n_u8(vidupq_n_u8(0, 1), 3);
}

static inline mve_pred16_t mpix_tail_pred_u8(unsigned lanes)
{
	return vctp8q((uint32_t)lanes & 0xFF);
}

/* -------------------- SOA helpers (deinterleave / reinterleave) -------------------- */

/* Process by blocks to amortize gathers and operate on contiguous planar memory. */
#define MPIX_SOA_BLK 128u /* pixels per block (multiple of 16 recommended) */

#if defined(__GNUC__)
#define MPIX_ALIGNED16 __attribute__((aligned(16)))
#else
#define MPIX_ALIGNED16
#endif

static inline void mpix_deint_rgb24_block(const uint8_t *inb, uint16_t blk, uint8_t *r, uint8_t *g,
					  uint8_t *b)
{
	/* Deinterleave AoS RGB24 into contiguous R,G,B buffers (length blk) */
	uint8x16_t offs3 = mpix_rgb_offsets3();
	uint16_t done = 0;
	/* Fast path: full vectors */
	while ((uint16_t)(blk - done) >= 16u) {
		const uint8_t *base = inb + (uint32_t)done * 3u;
		__builtin_prefetch(base + 96, 0, 1);
		uint8x16_t rv = vldrbq_gather_offset_u8(base, vaddq_n_u8(offs3, 0));
		uint8x16_t gv = vldrbq_gather_offset_u8(base, vaddq_n_u8(offs3, 1));
		uint8x16_t bv = vldrbq_gather_offset_u8(base, vaddq_n_u8(offs3, 2));
		vst1q_u8(r + done, rv);
		vst1q_u8(g + done, gv);
		vst1q_u8(b + done, bv);
		done = (uint16_t)(done + 16);
	}
	/* Tail */
	uint16_t rem = (uint16_t)(blk - done);
	if (rem) {
		mve_pred16_t p = vctp8q(rem);
		const uint8_t *base = inb + (uint32_t)done * 3u;
		uint8x16_t rv = vldrbq_gather_offset_z_u8(base, vaddq_n_u8(offs3, 0), p);
		uint8x16_t gv = vldrbq_gather_offset_z_u8(base, vaddq_n_u8(offs3, 1), p);
		uint8x16_t bv = vldrbq_gather_offset_z_u8(base, vaddq_n_u8(offs3, 2), p);
		vst1q_p_u8(r + done, rv, p);
		vst1q_p_u8(g + done, gv, p);
		vst1q_p_u8(b + done, bv, p);
	}
}

static inline void mpix_reint_rgb24_block(uint8_t *outb, uint16_t blk, const uint8_t *r,
					  const uint8_t *g, const uint8_t *b)
{
	/* Re-interleave contiguous R,G,B back to AoS RGB24 at outb */
	uint8x16_t offs3 = mpix_rgb_offsets3();
	uint16_t done = 0;
	/* Fast path: full vectors */
	while ((uint16_t)(blk - done) >= 16u) {
		uint8x16_t rv = vld1q_u8(r + done);
		uint8x16_t gv = vld1q_u8(g + done);
		uint8x16_t bv = vld1q_u8(b + done);
		uint8_t *dstb = outb + (uint32_t)done * 3u;
		vstrbq_scatter_offset_s8((int8_t *)dstb, vreinterpretq_s8_u8(vaddq_n_u8(offs3, 0)),
					 vreinterpretq_s8_u8(rv));
		vstrbq_scatter_offset_s8((int8_t *)dstb, vreinterpretq_s8_u8(vaddq_n_u8(offs3, 1)),
					 vreinterpretq_s8_u8(gv));
		vstrbq_scatter_offset_s8((int8_t *)dstb, vreinterpretq_s8_u8(vaddq_n_u8(offs3, 2)),
					 vreinterpretq_s8_u8(bv));
		done = (uint16_t)(done + 16);
	}
	/* Tail */
	uint16_t rem = (uint16_t)(blk - done);
	if (rem) {
		mve_pred16_t p = vctp8q(rem);
		uint8x16_t rv = vld1q_z_u8(r + done, p);
		uint8x16_t gv = vld1q_z_u8(g + done, p);
		uint8x16_t bv = vld1q_z_u8(b + done, p);
		uint8x16_t offR = vaddq_n_u8(offs3, 0);
		uint8x16_t offG = vaddq_n_u8(offs3, 1);
		uint8x16_t offB = vaddq_n_u8(offs3, 2);
		uint8_t *dstb = outb + (uint32_t)done * 3u;
		vstrbq_scatter_offset_p_s8((int8_t *)dstb, vreinterpretq_s8_u8(offR),
					   vreinterpretq_s8_u8(rv), p);
		vstrbq_scatter_offset_p_s8((int8_t *)dstb, vreinterpretq_s8_u8(offG),
					   vreinterpretq_s8_u8(gv), p);
		vstrbq_scatter_offset_p_s8((int8_t *)dstb, vreinterpretq_s8_u8(offB),
					   vreinterpretq_s8_u8(bv), p);
	}
}

/* -------------------- Black level -------------------- */

void mpix_correction_black_level_raw8(const uint8_t *src, uint8_t *dst, uint16_t width,
				      uint16_t line_offset, uint8_t level)
{
	(void)line_offset;
	uint16_t x = 0;
	uint8x16_t vlevel = vdupq_n_u8(level);
	for (; x + 16 <= width; x += 16) {
		uint8x16_t v = vld1q_u8(src + x);
		v = vqsubq_u8(v, vlevel);
		vst1q_u8(dst + x, v);
	}
	unsigned rem = (unsigned)width - x;
	if (rem) {
		mve_pred16_t p = mpix_tail_pred_u8(rem);
		uint8x16_t v = vld1q_z_u8(src + x, p);
		v = vqsubq_u8(v, vlevel);
		vst1q_p_u8(dst + x, v, p);
	}
}

void mpix_correction_black_level_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width,
				       uint16_t line_offset, uint8_t level)
{
	(void)line_offset;
	const uint8_t *inb_line = src;
	uint8_t *out_line = dst;
	uint16_t x = 0;
	uint8_t rbuf[MPIX_SOA_BLK] MPIX_ALIGNED16;
	uint8_t gbuf[MPIX_SOA_BLK] MPIX_ALIGNED16;
	uint8_t bbuf[MPIX_SOA_BLK] MPIX_ALIGNED16;

	while (x < width) {
		uint16_t blk = (uint16_t)MIN((uint16_t)MPIX_SOA_BLK, (uint16_t)(width - x));
		const uint8_t *inb = inb_line + (uint32_t)x * 3u;
		uint8_t *outb = out_line + (uint32_t)x * 3u;

		/* AoS -> SoA */
		mpix_deint_rgb24_block(inb, blk, rbuf, gbuf, bbuf);

		/* Vector black-level on contiguous buffers */
		uint16_t done = 0;
		uint8x16_t vlevel = vdupq_n_u8(level);
		while (done < blk) {
			uint16_t step = (uint16_t)MIN(16u, (uint16_t)(blk - done));
			mve_pred16_t p = vctp8q(step);
			uint8x16_t r = vld1q_z_u8(rbuf + done, p);
			uint8x16_t g = vld1q_z_u8(gbuf + done, p);
			uint8x16_t b = vld1q_z_u8(bbuf + done, p);
			r = vqsubq_u8(r, vlevel);
			g = vqsubq_u8(g, vlevel);
			b = vqsubq_u8(b, vlevel);
			vst1q_p_u8(rbuf + done, r, p);
			vst1q_p_u8(gbuf + done, g, p);
			vst1q_p_u8(bbuf + done, b, p);
			done += step;
		}

		/* SoA -> AoS */
		mpix_reint_rgb24_block(outb, blk, rbuf, gbuf, bbuf);

		x = (uint16_t)(x + blk);
	}
}

/* -------------------- White balance (RGB24, Q10) -------------------- */

static inline uint8x16_t mpix_wb_channel_u8(uint8x16_t vin, uint16_t gain_q10)
{
	/* u8 -> s16 and pre-shift by 5 so that qrdmulh with Q10 gain matches >>10 */
	int16x8_t lo = vreinterpretq_s16_u16(vmovlbq_u8(vin));
	int16x8_t hi = vreinterpretq_s16_u16(vmovltq_u8(vin));
	lo = vqshlq_n_s16(lo, 5);
	hi = vqshlq_n_s16(hi, 5);

	/* vqrdmulhq_n_s16 computes round((a*b)/2^15) with doubling, so (val<<5)*gain -> >>15 equals
	 * >>10 */
	int16x8_t lo_scaled = vqrdmulhq_n_s16(lo, (int16_t)gain_q10);
	int16x8_t hi_scaled = vqrdmulhq_n_s16(hi, (int16_t)gain_q10);

	/* clamp to [0,255] and pack */
	int16x8_t zero = vdupq_n_s16(0);
	lo_scaled = vmaxq_s16(lo_scaled, zero);
	hi_scaled = vmaxq_s16(hi_scaled, zero);
	const uint16x8_t max255 = vdupq_n_u16(255);
	uint16x8_t ulo = vminq_u16(vreinterpretq_u16_s16(lo_scaled), max255);
	uint16x8_t uhi = vminq_u16(vreinterpretq_u16_s16(hi_scaled), max255);
	uint8x16_t out8 = vdupq_n_u8(0);
	out8 = vqmovnbq_u16(out8, ulo);
	out8 = vqmovntq_u16(out8, uhi);
	return out8;
}

void mpix_correction_white_balance_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width,
					 uint16_t line_offset, int32_t red_level_q10,
					 int32_t blue_level_q10)
{
	(void)line_offset;
	const uint16_t red_q10 = red_level_q10;
	const uint16_t blue_q10 = blue_level_q10;

	uint8_t rbuf[MPIX_SOA_BLK] MPIX_ALIGNED16;
	uint8_t gbuf[MPIX_SOA_BLK] MPIX_ALIGNED16;
	uint8_t bbuf[MPIX_SOA_BLK] MPIX_ALIGNED16;

	uint16_t x = 0;
	while (x < width) {
		uint16_t blk = (uint16_t)MIN((uint16_t)MPIX_SOA_BLK, (uint16_t)(width - x));
		const uint8_t *inb = src + (uint32_t)x * 3u;
		uint8_t *outb = dst + (uint32_t)x * 3u;

		/* AoS -> SoA */
		mpix_deint_rgb24_block(inb, blk, rbuf, gbuf, bbuf);

		/* Apply gains on contiguous R/B, G unchanged */
		uint16_t done = 0;
		while (done < blk) {
			uint16_t step = (uint16_t)MIN(16u, (uint16_t)(blk - done));
			mve_pred16_t p = vctp8q(step);
			uint8x16_t r = vld1q_z_u8(rbuf + done, p);
			uint8x16_t b = vld1q_z_u8(bbuf + done, p);
			r = mpix_wb_channel_u8(r, red_q10);
			b = mpix_wb_channel_u8(b, blue_q10);
			vst1q_p_u8(rbuf + done, r, p);
			vst1q_p_u8(bbuf + done, b, p);
			/* G remains */
			done += step;
		}

		/* SoA -> AoS */
		mpix_reint_rgb24_block(outb, blk, rbuf, gbuf, bbuf);
		x = (uint16_t)(x + blk);
	}
}

/* -------------------- Gamma (LUT approach) -------------------- */

#define MPIX_GAMMA_STEP 4
#define MPIX_GAMMA_MIN  1
#define MPIX_GAMMA_MAX  16

/* Copied from scalar implementation to keep identical behavior */
static const uint8_t mpix_gamma_y_tab[] = {
	181, 197, 215, 234, 128, 152, 181, 215, 90,  117, 152, 197, 64,  90, 128,
	181, 45,  69,  107, 165, 32,  53,  90,  152, 22,  41,  76,  139, 16, 32,
	64,  128, 11,  24,  53,  117, 8,   19,  45,  107, 5,   14,  38,  98, 4,
	11,  32,  90,  2,   8,   26,  82,  2,   6,   22,  76,  1,   5,   19, 69,
};
static const uint8_t mpix_gamma_x_tab[] = {1, 4, 16, 64};

static inline uint8_t mpix_gamma_interp_u8(uint8_t raw8, const uint8_t *gamma_y,
					   const uint8_t *gamma_x, size_t gamma_sz)
{
	uint8_t x0 = 0, x1 = 0, y0, y1;
	if (raw8 == 0) {
		return 0;
	}
	for (size_t i = 0;; i++) {
		if (i >= gamma_sz) {
			y0 = gamma_y[i - 1];
			x1 = y1 = 0xff;
			break;
		}
		x1 = gamma_x[i];
		if (raw8 < x1) {
			y0 = gamma_y[i - 1];
			y1 = gamma_y[i - 0];
			break;
		}
		x0 = x1;
	}
	return (uint8_t)(((uint32_t)(x1 - raw8) * y0 + (uint32_t)(raw8 - x0) * y1) /
			 (uint32_t)(x1 - x0));
}

static void mpix_build_gamma_lut(uint8_t lut[256], uint8_t level)
{
	const uint8_t *gy = &mpix_gamma_y_tab[(level - MPIX_GAMMA_MIN) * MPIX_GAMMA_STEP];
	for (int v = 0; v < 256; ++v) {
		lut[v] = mpix_gamma_interp_u8((uint8_t)v, gy, mpix_gamma_x_tab,
					      sizeof(mpix_gamma_x_tab));
	}
}

void mpix_correction_gamma_raw8(const uint8_t *src, uint8_t *dst, uint16_t width,
				uint16_t line_offset, uint16_t gamma_q10)
{
	(void)line_offset;
	/* level is stored on 8b with <<5 in scalar path; replicate that logic */
	const uint8_t level = gamma_q10 >> 5;
	static uint8_t s_lut[256];
	static uint8_t s_level;
	static uint8_t s_init;
	if (!s_init || s_level != level) {
		mpix_build_gamma_lut(s_lut, level);
		s_level = level;
		s_init = 1;
	}

	uint16_t x = 0;
	for (; x + 16 <= width; x += 16) {
		uint8x16_t pix = vld1q_u8(src + x);
		/* gather LUT by byte offsets == pixel value */
		uint8x16_t res = vldrbq_gather_offset_u8(s_lut, pix);
		vst1q_u8(dst + x, res);
	}
	unsigned rem = (unsigned)width - x;
	if (rem) {
		mve_pred16_t p = mpix_tail_pred_u8(rem);
		uint8x16_t pix = vld1q_z_u8(src + x, p);
		uint8x16_t res = vldrbq_gather_offset_u8(s_lut, pix);
		vst1q_p_u8(dst + x, res, p);
	}
}

void mpix_correction_gamma_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width,
				 uint16_t line_offset, uint16_t gamma_q10)
{
	(void)line_offset;
	const uint8_t level = gamma_q10 >> 5;
	static uint8_t s_lut[256];
	static uint8_t s_level;
	static uint8_t s_init;
	if (!s_init || s_level != level) {
		mpix_build_gamma_lut(s_lut, level);
		s_level = level;
		s_init = 1;
	}

	uint8_t rbuf[MPIX_SOA_BLK] MPIX_ALIGNED16;
	uint8_t gbuf[MPIX_SOA_BLK] MPIX_ALIGNED16;
	uint8_t bbuf[MPIX_SOA_BLK] MPIX_ALIGNED16;

	uint16_t x = 0;
	while (x < width) {
		uint16_t blk = (uint16_t)MIN((uint16_t)MPIX_SOA_BLK, (uint16_t)(width - x));
		const uint8_t *inb = src + (uint32_t)x * 3u;
		uint8_t *outb = dst + (uint32_t)x * 3u;

		/* AoS -> SoA */
		mpix_deint_rgb24_block(inb, blk, rbuf, gbuf, bbuf);

		/* LUT on contiguous channels */
		uint16_t done = 0;
		while (done < blk) {
			uint16_t step = (uint16_t)MIN(16u, (uint16_t)(blk - done));
			mve_pred16_t p = vctp8q(step);
			uint8x16_t r = vld1q_z_u8(rbuf + done, p);
			uint8x16_t g = vld1q_z_u8(gbuf + done, p);
			uint8x16_t b = vld1q_z_u8(bbuf + done, p);
			r = vldrbq_gather_offset_u8(s_lut, r);
			g = vldrbq_gather_offset_u8(s_lut, g);
			b = vldrbq_gather_offset_u8(s_lut, b);
			vst1q_p_u8(rbuf + done, r, p);
			vst1q_p_u8(gbuf + done, g, p);
			vst1q_p_u8(bbuf + done, b, p);
			done += step;
		}

		/* SoA -> AoS */
		mpix_reint_rgb24_block(outb, blk, rbuf, gbuf, bbuf);
		x = (uint16_t)(x + blk);
	}
}

/* -------------------- Color matrix (RGB24, 3x3, Q10) -------------------- */

static inline void mpix_ccm_pack_and_store(uint8_t *outb, uint8x16_t offs3, int channel,
					   int32x4_t lo_a, int32x4_t lo_b, int32x4_t hi_a,
					   int32x4_t hi_b, mve_pred16_t p)
{
	/* rounding shift and narrow s32 -> s16 in one step */
	/* rounding shift and narrow s32 -> s16 */
	int16x8_t v16lo = vqrshrnbq_n_s32(vdupq_n_s16(0), lo_a, 10);
	v16lo = vqrshrntq_n_s32(v16lo, lo_b, 10);
	int16x8_t v16hi = vqrshrnbq_n_s32(vdupq_n_s16(0), hi_a, 10);
	v16hi = vqrshrntq_n_s32(v16hi, hi_b, 10);

	/* clamp to [0,255] */
	int16x8_t zero = vdupq_n_s16(0);
	v16lo = vmaxq_s16(v16lo, zero);
	v16hi = vmaxq_s16(v16hi, zero);
	uint16x8_t u16lo = vreinterpretq_u16_s16(v16lo);
	uint16x8_t u16hi = vreinterpretq_u16_s16(v16hi);
	const uint16x8_t max255 = vdupq_n_u16(255);
	u16lo = vminq_u16(u16lo, max255);
	u16hi = vminq_u16(u16hi, max255);

	uint8x16_t out8 = vdupq_n_u8(0);
	out8 = vqmovnbq_u16(out8, u16lo);
	out8 = vqmovntq_u16(out8, u16hi);

	uint8x16_t offC = vaddq_n_u8(offs3, (uint8_t)channel);
	vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offC),
				   vreinterpretq_s8_u8(out8), p);
}

void mpix_correction_color_matrix_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width,
					uint16_t line_offset, int16_t matrix_q10[9])
{
	(void)line_offset;
	const int16_t *L = matrix_q10; /* row-major 3x3 */

	uint8_t rbuf[MPIX_SOA_BLK] MPIX_ALIGNED16;
	uint8_t gbuf[MPIX_SOA_BLK] MPIX_ALIGNED16;
	uint8_t bbuf[MPIX_SOA_BLK] MPIX_ALIGNED16;

	uint16_t x = 0;
	while (x < width) {
		uint16_t blk = (uint16_t)MIN((uint16_t)MPIX_SOA_BLK, (uint16_t)(width - x));
		const uint8_t *inb = src + (uint32_t)x * 3u;
		uint8_t *outb = dst + (uint32_t)x * 3u;

		/* AoS -> SoA */
		mpix_deint_rgb24_block(inb, blk, rbuf, gbuf, bbuf);

		/* Process contiguous channels in blocks of 16 */
		uint16_t done = 0;
		while (done < blk) {
			uint16_t step = (uint16_t)MIN(16u, (uint16_t)(blk - done));
			mve_pred16_t p = vctp8q(step);

			uint8x16_t r8 = vld1q_z_u8(rbuf + done, p);
			uint8x16_t g8 = vld1q_z_u8(gbuf + done, p);
			uint8x16_t b8 = vld1q_z_u8(bbuf + done, p);

			/* widen to s16 */
			int16x8_t r16_lo = vreinterpretq_s16_u16(vmovlbq_u8(r8));
			int16x8_t r16_hi = vreinterpretq_s16_u16(vmovltq_u8(r8));
			int16x8_t g16_lo = vreinterpretq_s16_u16(vmovlbq_u8(g8));
			int16x8_t g16_hi = vreinterpretq_s16_u16(vmovltq_u8(g8));
			int16x8_t b16_lo = vreinterpretq_s16_u16(vmovlbq_u8(b8));
			int16x8_t b16_hi = vreinterpretq_s16_u16(vmovltq_u8(b8));

			/* R' */
			int16x8_t L0v = vdupq_n_s16(L[0]);
			int16x8_t L1v = vdupq_n_s16(L[1]);
			int16x8_t L2v = vdupq_n_s16(L[2]);
			int32x4_t rr_ll = vaddq_s32(vmullbq_int_s16(r16_lo, L0v),
						    vaddq_s32(vmullbq_int_s16(g16_lo, L1v),
							      vmullbq_int_s16(b16_lo, L2v)));
			int32x4_t rr_lh = vaddq_s32(vmulltq_int_s16(r16_lo, L0v),
						    vaddq_s32(vmulltq_int_s16(g16_lo, L1v),
							      vmulltq_int_s16(b16_lo, L2v)));
			int32x4_t rr_hl = vaddq_s32(vmullbq_int_s16(r16_hi, L0v),
						    vaddq_s32(vmullbq_int_s16(g16_hi, L1v),
							      vmullbq_int_s16(b16_hi, L2v)));
			int32x4_t rr_hh = vaddq_s32(vmulltq_int_s16(r16_hi, L0v),
						    vaddq_s32(vmulltq_int_s16(g16_hi, L1v),
							      vmulltq_int_s16(b16_hi, L2v)));

			/* pack & clamp */
			int16x8_t zero = vdupq_n_s16(0);
			int16x8_t v16lo = vqrshrnbq_n_s32(vdupq_n_s16(0), rr_ll, MPIX_CORRECTION_SCALE_BITS);
			v16lo = vqrshrntq_n_s32(v16lo, rr_lh, MPIX_CORRECTION_SCALE_BITS);
			int16x8_t v16hi = vqrshrnbq_n_s32(vdupq_n_s16(0), rr_hl, MPIX_CORRECTION_SCALE_BITS);
			v16hi = vqrshrntq_n_s32(v16hi, rr_hh, MPIX_CORRECTION_SCALE_BITS);
			v16lo = vmaxq_s16(v16lo, zero);
			v16hi = vmaxq_s16(v16hi, zero);
			uint16x8_t u16lo = vreinterpretq_u16_s16(v16lo);
			uint16x8_t u16hi = vreinterpretq_u16_s16(v16hi);
			const uint16x8_t max255 = vdupq_n_u16(255);
			u16lo = vminq_u16(u16lo, max255);
			u16hi = vminq_u16(u16hi, max255);
			uint8x16_t rout = vdupq_n_u8(0);
			rout = vqmovnbq_u16(rout, u16lo);
			rout = vqmovntq_u16(rout, u16hi);
			vst1q_p_u8(rbuf + done, rout, p);

			/* G' */
			int16x8_t L3v = vdupq_n_s16(L[3]);
			int16x8_t L4v = vdupq_n_s16(L[4]);
			int16x8_t L5v = vdupq_n_s16(L[5]);
			int32x4_t gg_ll = vaddq_s32(vmullbq_int_s16(r16_lo, L3v),
						    vaddq_s32(vmullbq_int_s16(g16_lo, L4v),
							      vmullbq_int_s16(b16_lo, L5v)));
			int32x4_t gg_lh = vaddq_s32(vmulltq_int_s16(r16_lo, L3v),
						    vaddq_s32(vmulltq_int_s16(g16_lo, L4v),
							      vmulltq_int_s16(b16_lo, L5v)));
			int32x4_t gg_hl = vaddq_s32(vmullbq_int_s16(r16_hi, L3v),
						    vaddq_s32(vmullbq_int_s16(g16_hi, L4v),
							      vmullbq_int_s16(b16_hi, L5v)));
			int32x4_t gg_hh = vaddq_s32(vmulltq_int_s16(r16_hi, L3v),
						    vaddq_s32(vmulltq_int_s16(g16_hi, L4v),
							      vmulltq_int_s16(b16_hi, L5v)));
			int16x8_t g16o = vqrshrnbq_n_s32(vdupq_n_s16(0), gg_ll, (MPIX_CORRECTION_SCALE_BITS));
			g16o = vqrshrntq_n_s32(g16o, gg_lh, MPIX_CORRECTION_SCALE_BITS);
			int16x8_t g16o_hi = vqrshrnbq_n_s32(vdupq_n_s16(0), gg_hl, MPIX_CORRECTION_SCALE_BITS);
			g16o_hi = vqrshrntq_n_s32(g16o_hi, gg_hh, MPIX_CORRECTION_SCALE_BITS);
			g16o = vmaxq_s16(g16o, zero);
			g16o_hi = vmaxq_s16(g16o_hi, zero);
			uint16x8_t g16u = vminq_u16(vreinterpretq_u16_s16(g16o), max255);
			uint16x8_t g16u_hi = vminq_u16(vreinterpretq_u16_s16(g16o_hi), max255);
			uint8x16_t gout = vdupq_n_u8(0);
			gout = vqmovnbq_u16(gout, g16u);
			gout = vqmovntq_u16(gout, g16u_hi);
			vst1q_p_u8(gbuf + done, gout, p);

			/* B' */
			int16x8_t L6v = vdupq_n_s16(L[6]);
			int16x8_t L7v = vdupq_n_s16(L[7]);
			int16x8_t L8v = vdupq_n_s16(L[8]);
			int32x4_t bb_ll = vaddq_s32(vmullbq_int_s16(r16_lo, L6v),
						    vaddq_s32(vmullbq_int_s16(g16_lo, L7v),
							      vmullbq_int_s16(b16_lo, L8v)));
			int32x4_t bb_lh = vaddq_s32(vmulltq_int_s16(r16_lo, L6v),
						    vaddq_s32(vmulltq_int_s16(g16_lo, L7v),
							      vmulltq_int_s16(b16_lo, L8v)));
			int32x4_t bb_hl = vaddq_s32(vmullbq_int_s16(r16_hi, L6v),
						    vaddq_s32(vmullbq_int_s16(g16_hi, L7v),
							      vmullbq_int_s16(b16_hi, L8v)));
			int32x4_t bb_hh = vaddq_s32(vmulltq_int_s16(r16_hi, L6v),
						    vaddq_s32(vmulltq_int_s16(g16_hi, L7v),
							      vmulltq_int_s16(b16_hi, L8v)));
			int16x8_t b16o = vqrshrnbq_n_s32(vdupq_n_s16(0), bb_ll, MPIX_CORRECTION_SCALE_BITS);
			b16o = vqrshrntq_n_s32(b16o, bb_lh, MPIX_CORRECTION_SCALE_BITS);
			int16x8_t b16o_hi = vqrshrnbq_n_s32(vdupq_n_s16(0), bb_hl, MPIX_CORRECTION_SCALE_BITS);
			b16o_hi = vqrshrntq_n_s32(b16o_hi, bb_hh, MPIX_CORRECTION_SCALE_BITS);
			b16o = vmaxq_s16(b16o, zero);
			b16o_hi = vmaxq_s16(b16o_hi, zero);
			uint16x8_t b16u = vminq_u16(vreinterpretq_u16_s16(b16o), max255);
			uint16x8_t b16u_hi = vminq_u16(vreinterpretq_u16_s16(b16o_hi), max255);
			uint8x16_t bout = vdupq_n_u8(0);
			bout = vqmovnbq_u16(bout, b16u);
			bout = vqmovntq_u16(bout, b16u_hi);
			vst1q_p_u8(bbuf + done, bout, p);

			done += step;
		}

		/* SoA -> AoS */
		mpix_reint_rgb24_block(outb, blk, rbuf, gbuf, bbuf);
		x = (uint16_t)(x + blk);
	}
}

/* -------------------- Fused one-pass: BLC -> WB -> CCM -> Gamma -------------------- */

void mpix_correction_fused_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width,
				 uint16_t line_offset, uint8_t black_level, uint16_t red_level,
				 uint16_t blue_level, uint16_t gamma_level_q10,
				 int16_t color_matrix_q10[9])
{
	(void)line_offset;
	const uint8_t blc = black_level;
	const uint16_t wr_q10 = red_level;
	const uint16_t wb_q10 = blue_level;
	const int16_t *M = color_matrix_q10; /* Q10 */

	/* Build gamma LUT once per call (level in corr->gamma.level uses same scheme as others) */
	const uint8_t level = gamma_level_q10 >> 5;
	static uint8_t s_lut[256];
	static uint8_t s_level;
	static uint8_t s_init;
	if (!s_init || s_level != level) {
		mpix_build_gamma_lut(s_lut, level);
		s_level = level;
		s_init = 1;
	}

	/* Pre-fold WB into CCM columns to save一阶段 */
	int16_t Mc[9];
	/* 乘以 Q10，保持 Q10 精度：后续仍按 >>10 收敛 */
	for (int r = 0; r < 3; ++r) {
		int32_t c0 = (int32_t)M[r * 3 + 0] * wr_q10;    /* R 列 */
		int32_t c1 = (int32_t)M[r * 3 + 1] * (1 << 10); /* G 列 */
		int32_t c2 = (int32_t)M[r * 3 + 2] * wb_q10;    /* B 列 */
		/* 现在三列都是 Q20，需要在使用前统一右移 10 到 Q10。
		 * 为避免精度抖动，这里先四舍五入到 Q10，再截断到 int16 */
		c0 = (c0 + (1 << (10 - 1))) >> 10;
		c1 = (c1 + (1 << (10 - 1))) >> 10;
		c2 = (c2 + (1 << (10 - 1))) >> 10;
		if (c0 > 32767) {
			c0 = 32767;
		}
		if (c0 < -32768) {
			c0 = -32768;
		}
		if (c1 > 32767) {
			c1 = 32767;
		}
		if (c1 < -32768) {
			c1 = -32768;
		}
		if (c2 > 32767) {
			c2 = 32767;
		}
		if (c2 < -32768) {
			c2 = -32768;
		}
		Mc[r * 3 + 0] = (int16_t)c0;
		Mc[r * 3 + 1] = (int16_t)c1;
		Mc[r * 3 + 2] = (int16_t)c2;
	}

	uint8_t rbuf[MPIX_SOA_BLK] MPIX_ALIGNED16;
	uint8_t gbuf[MPIX_SOA_BLK] MPIX_ALIGNED16;
	uint8_t bbuf[MPIX_SOA_BLK] MPIX_ALIGNED16;

	uint16_t x = 0;
	while (x < width) {
		uint16_t blk = (uint16_t)MIN((uint16_t)MPIX_SOA_BLK, (uint16_t)(width - x));
		const uint8_t *inb = src + (uint32_t)x * 3u;
		uint8_t *outb = dst + (uint32_t)x * 3u;

		/* AoS -> SoA */
		mpix_deint_rgb24_block(inb, blk, rbuf, gbuf, bbuf);

		/* 在 SoA 上进行：BLC -> CCM(Mc) -> Gamma；WB 已折叠入矩阵，G 通道等效 1x  */
		uint16_t done = 0;
		uint8x16_t vbl = vdupq_n_u8(blc);
		const uint16x8_t max255 = vdupq_n_u16(255);
		while (done < blk) {
			uint16_t vec = (uint16_t)MIN(16u, (uint16_t)(blk - done));
			mve_pred16_t p = vctp8q(vec);
			/* Load & black level */
			uint8x16_t r8 = vld1q_z_u8(rbuf + done, p);
			uint8x16_t g8 = vld1q_z_u8(gbuf + done, p);
			uint8x16_t b8 = vld1q_z_u8(bbuf + done, p);
			r8 = vqsubq_u8(r8, vbl);
			g8 = vqsubq_u8(g8, vbl);
			b8 = vqsubq_u8(b8, vbl);

			/* widen to s16 */
			int16x8_t r16_lo = vreinterpretq_s16_u16(vmovlbq_u8(r8));
			int16x8_t r16_hi = vreinterpretq_s16_u16(vmovltq_u8(r8));
			int16x8_t g16_lo = vreinterpretq_s16_u16(vmovlbq_u8(g8));
			int16x8_t g16_hi = vreinterpretq_s16_u16(vmovltq_u8(g8));
			int16x8_t b16_lo = vreinterpretq_s16_u16(vmovlbq_u8(b8));
			int16x8_t b16_hi = vreinterpretq_s16_u16(vmovltq_u8(b8));

			/* CCM with Mc (Q10) */
			int16x8_t L0v = vdupq_n_s16(Mc[0]);
			int16x8_t L1v = vdupq_n_s16(Mc[1]);
			int16x8_t L2v = vdupq_n_s16(Mc[2]);
			int32x4_t rr_ll = vaddq_s32(vmullbq_int_s16(r16_lo, L0v),
						    vaddq_s32(vmullbq_int_s16(g16_lo, L1v),
							      vmullbq_int_s16(b16_lo, L2v)));
			int32x4_t rr_lh = vaddq_s32(vmulltq_int_s16(r16_lo, L0v),
						    vaddq_s32(vmulltq_int_s16(g16_lo, L1v),
							      vmulltq_int_s16(b16_lo, L2v)));
			int32x4_t rr_hl = vaddq_s32(vmullbq_int_s16(r16_hi, L0v),
						    vaddq_s32(vmullbq_int_s16(g16_hi, L1v),
							      vmullbq_int_s16(b16_hi, L2v)));
			int32x4_t rr_hh = vaddq_s32(vmulltq_int_s16(r16_hi, L0v),
						    vaddq_s32(vmulltq_int_s16(g16_hi, L1v),
							      vmulltq_int_s16(b16_hi, L2v)));

			int16x8_t v16lo = vqrshrnbq_n_s32(vdupq_n_s16(0), rr_ll, MPIX_CORRECTION_SCALE_BITS);
			v16lo = vqrshrntq_n_s32(v16lo, rr_lh, MPIX_CORRECTION_SCALE_BITS);
			int16x8_t v16hi = vqrshrnbq_n_s32(vdupq_n_s16(0), rr_hl, MPIX_CORRECTION_SCALE_BITS);
			v16hi = vqrshrntq_n_s32(v16hi, rr_hh, MPIX_CORRECTION_SCALE_BITS);
			int16x8_t zero = vdupq_n_s16(0);
			v16lo = vmaxq_s16(v16lo, zero);
			v16hi = vmaxq_s16(v16hi, zero);
			uint16x8_t u16lo = vminq_u16(vreinterpretq_u16_s16(v16lo), max255);
			uint16x8_t u16hi = vminq_u16(vreinterpretq_u16_s16(v16hi), max255);
			uint8x16_t rout = vdupq_n_u8(0);
			rout = vqmovnbq_u16(rout, u16lo);
			rout = vqmovntq_u16(rout, u16hi);

			/* G' */
			int16x8_t L3v = vdupq_n_s16(Mc[3]);
			int16x8_t L4v = vdupq_n_s16(Mc[4]);
			int16x8_t L5v = vdupq_n_s16(Mc[5]);
			int32x4_t gg_ll = vaddq_s32(vmullbq_int_s16(r16_lo, L3v),
						    vaddq_s32(vmullbq_int_s16(g16_lo, L4v),
							      vmullbq_int_s16(b16_lo, L5v)));
			int32x4_t gg_lh = vaddq_s32(vmulltq_int_s16(r16_lo, L3v),
						    vaddq_s32(vmulltq_int_s16(g16_lo, L4v),
							      vmulltq_int_s16(b16_lo, L5v)));
			int32x4_t gg_hl = vaddq_s32(vmullbq_int_s16(r16_hi, L3v),
						    vaddq_s32(vmullbq_int_s16(g16_hi, L4v),
							      vmullbq_int_s16(b16_hi, L5v)));
			int32x4_t gg_hh = vaddq_s32(vmulltq_int_s16(r16_hi, L3v),
						    vaddq_s32(vmulltq_int_s16(g16_hi, L4v),
							      vmulltq_int_s16(b16_hi, L5v)));
			int16x8_t g16o = vqrshrnbq_n_s32(vdupq_n_s16(0), gg_ll, MPIX_CORRECTION_SCALE_BITS);
			g16o = vqrshrntq_n_s32(g16o, gg_lh, MPIX_CORRECTION_SCALE_BITS);
			int16x8_t g16o_hi = vqrshrnbq_n_s32(vdupq_n_s16(0), gg_hl, MPIX_CORRECTION_SCALE_BITS);
			g16o_hi = vqrshrntq_n_s32(g16o_hi, gg_hh, MPIX_CORRECTION_SCALE_BITS);
			g16o = vmaxq_s16(g16o, zero);
			g16o_hi = vmaxq_s16(g16o_hi, zero);
			uint16x8_t g16u = vminq_u16(vreinterpretq_u16_s16(g16o), max255);
			uint16x8_t g16u_hi = vminq_u16(vreinterpretq_u16_s16(g16o_hi), max255);
			uint8x16_t gout = vdupq_n_u8(0);
			gout = vqmovnbq_u16(gout, g16u);
			gout = vqmovntq_u16(gout, g16u_hi);

			/* B' */
			int16x8_t L6v = vdupq_n_s16(Mc[6]);
			int16x8_t L7v = vdupq_n_s16(Mc[7]);
			int16x8_t L8v = vdupq_n_s16(Mc[8]);
			int32x4_t bb_ll = vaddq_s32(vmullbq_int_s16(r16_lo, L6v),
						    vaddq_s32(vmullbq_int_s16(g16_lo, L7v),
							      vmullbq_int_s16(b16_lo, L8v)));
			int32x4_t bb_lh = vaddq_s32(vmulltq_int_s16(r16_lo, L6v),
						    vaddq_s32(vmulltq_int_s16(g16_lo, L7v),
							      vmulltq_int_s16(b16_lo, L8v)));
			int32x4_t bb_hl = vaddq_s32(vmullbq_int_s16(r16_hi, L6v),
						    vaddq_s32(vmullbq_int_s16(g16_hi, L7v),
							      vmullbq_int_s16(b16_hi, L8v)));
			int32x4_t bb_hh = vaddq_s32(vmulltq_int_s16(r16_hi, L6v),
						    vaddq_s32(vmulltq_int_s16(g16_hi, L7v),
							      vmulltq_int_s16(b16_hi, L8v)));
			int16x8_t b16o = vqrshrnbq_n_s32(vdupq_n_s16(0), bb_ll, MPIX_CORRECTION_SCALE_BITS);
			b16o = vqrshrntq_n_s32(b16o, bb_lh, MPIX_CORRECTION_SCALE_BITS);
			int16x8_t b16o_hi = vqrshrnbq_n_s32(vdupq_n_s16(0), bb_hl, MPIX_CORRECTION_SCALE_BITS);
			b16o_hi = vqrshrntq_n_s32(b16o_hi, bb_hh, MPIX_CORRECTION_SCALE_BITS);
			b16o = vmaxq_s16(b16o, zero);
			b16o_hi = vmaxq_s16(b16o_hi, zero);
			uint16x8_t b16u = vminq_u16(vreinterpretq_u16_s16(b16o), max255);
			uint16x8_t b16u_hi = vminq_u16(vreinterpretq_u16_s16(b16o_hi), max255);
			uint8x16_t bout = vdupq_n_u8(0);
			bout = vqmovnbq_u16(bout, b16u);
			bout = vqmovntq_u16(bout, b16u_hi);

			/* Gamma LUT （u8->u8）*/
			rout = vldrbq_gather_offset_u8(s_lut, rout);
			gout = vldrbq_gather_offset_u8(s_lut, gout);
			bout = vldrbq_gather_offset_u8(s_lut, bout);

			/* store back to SoA buffers */
			vst1q_p_u8(rbuf + done, rout, p);
			vst1q_p_u8(gbuf + done, gout, p);
			vst1q_p_u8(bbuf + done, bout, p);
			done = (uint16_t)(done + vec);
		}

		/* SoA -> AoS */
		mpix_reint_rgb24_block(outb, blk, rbuf, gbuf, bbuf);
		x = (uint16_t)(x + blk);
	}
}
