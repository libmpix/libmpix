/* SPDX-License-Identifier: Apache-2.0 */

#include <assert.h>
#include <stdint.h>

#include <mpix/op_kernel.h>

#include "arm_mve.h"

/* Debug for 5x5 Gaussian was used during bring-up; code paths removed after stabilization. */

/*
 * ARM Helium MVE optimized 3x3 Gaussian blur for RGB24
 * Kernel:
 *   [1 2 1]
 *   [2 4 2]  -> shift right by 4 with rounding
 *   [1 2 1]
 * Implementation is separable: horizontal [1 2 1], then vertical [1 2 1].
 * We vectorize per 16 pixels with gather/scatter on interleaved RGB24.
 */

static inline uint8x16_t mpix_make_rgb_offsets(void)
{
	/* Offsets in bytes for 16 pixels: 0,3,6,...,45 */
	uint8x16_t inc = vidupq_n_u8(0, 1);
	return vmulq_n_u8(inc, 3);
}

static inline uint16x8_t mpix_horz_121_lo(uint8x16_t left, uint8x16_t center, uint8x16_t right)
{
	/* bottom 8 lanes */
	uint16x8_t l = vmovlbq_u8(left);
	uint16x8_t c = vmovlbq_u8(center);
	uint16x8_t r = vmovlbq_u8(right);
	uint16x8_t sum = vaddq_u16(l, r);
	sum = vaddq_u16(sum, vshlq_n_u16(c, 1)); /* + 2*center */
	return sum;
}

static inline uint16x8_t mpix_horz_121_hi(uint8x16_t left, uint8x16_t center, uint8x16_t right)
{
	/* top 8 lanes */
	uint16x8_t l = vmovltq_u8(left);
	uint16x8_t c = vmovltq_u8(center);
	uint16x8_t r = vmovltq_u8(right);
	uint16x8_t sum = vaddq_u16(l, r);
	sum = vaddq_u16(sum, vshlq_n_u16(c, 1)); /* + 2*center */
	return sum;
}

/* Note: mpix_store_rgb_block helper removed as unused. */

/* with-pre version: 复用每块已构造的offs与谓词，减少每通道重复开销 */
static inline void mpix_gauss3x3_channel_block_with_pre(
	const uint8_t *row0, const uint8_t *row1, const uint8_t *row2,
	uint8_t *out_base, uint16_t x_base, uint16_t block_len,
	uint8x16_t offs3, mve_pred16_t p0, int channel)
{
	(void)block_len;
	/* Base pointers for this block */
	const uint8_t *r0 = row0 + (uint32_t)x_base * 3u;
	const uint8_t *r1 = row1 + (uint32_t)x_base * 3u;
	const uint8_t *r2 = row2 + (uint32_t)x_base * 3u;

	/* Offsets per lane: center, left, right */
	uint8x16_t offC  = vaddq_n_u8(offs3, (uint8_t)channel);
	uint8x16_t offL  = vsubq_n_u8(offC, 3);
	uint8x16_t offR  = vaddq_n_u8(offC, 3);

	/* 针对首块（x_base==0）对lane0做左边界clamp，避免跨通道误读 */
	if (x_base == 0) {
		uint8x16_t idx_pix = vidupq_n_u8(0, 1);
	mve_pred16_t pfirst = vcmpeqq_n_u8(idx_pix, (uint8_t)0);
		offL = vpselq_u8(offC, offL, pfirst);
	}

	/* Gather left/center/right for 3 rows */
	uint8x16_t l0 = vldrbq_gather_offset_z_u8(r0, offL, p0);
	uint8x16_t c0 = vldrbq_gather_offset_z_u8(r0, offC, p0);
	uint8x16_t r0v= vldrbq_gather_offset_z_u8(r0, offR, p0);

	uint8x16_t l1 = vldrbq_gather_offset_z_u8(r1, offL, p0);
	uint8x16_t c1 = vldrbq_gather_offset_z_u8(r1, offC, p0);
	uint8x16_t r1v= vldrbq_gather_offset_z_u8(r1, offR, p0);

	uint8x16_t l2 = vldrbq_gather_offset_z_u8(r2, offL, p0);
	uint8x16_t c2 = vldrbq_gather_offset_z_u8(r2, offC, p0);
	uint8x16_t r2v= vldrbq_gather_offset_z_u8(r2, offR, p0);

	/* Horizontal 1-2-1 on each row */
	uint16x8_t h0_lo = mpix_horz_121_lo(l0, c0, r0v);
	uint16x8_t h0_hi = mpix_horz_121_hi(l0, c0, r0v);
	uint16x8_t h1_lo = mpix_horz_121_lo(l1, c1, r1v);
	uint16x8_t h1_hi = mpix_horz_121_hi(l1, c1, r1v);
	uint16x8_t h2_lo = mpix_horz_121_lo(l2, c2, r2v);
	uint16x8_t h2_hi = mpix_horz_121_hi(l2, c2, r2v);

	/* Vertical 1-2-1 combine, add rounding 8, shift >>4, pack */
	uint16x8_t sum_lo = vaddq_u16(h0_lo, h2_lo);
	sum_lo = vaddq_u16(sum_lo, vaddq_u16(h1_lo, h1_lo)); /* + 2*h1 */
	sum_lo = vaddq_u16(sum_lo, vdupq_n_u16(8));

	uint16x8_t sum_hi = vaddq_u16(h0_hi, h2_hi);
	sum_hi = vaddq_u16(sum_hi, vaddq_u16(h1_hi, h1_hi));
	sum_hi = vaddq_u16(sum_hi, vdupq_n_u16(8));

	uint8x16_t outv = vdupq_n_u8(0);
	outv = vshrnbq_n_u16(outv, sum_lo, 4);
	outv = vshrntq_n_u16(outv, sum_hi, 4);

	vstrbq_scatter_offset_p_s8((int8_t *)out_base, vreinterpretq_s8_u8(offC), vreinterpretq_s8_u8(outv), p0);
}

/* Identity kernels: output equals center row/channel */
void mpix_identity_rgb24_3x3(const uint8_t *in[3], uint8_t *out, uint16_t width)
{
	const uint8_t *row = in[1];
	uint16_t x = 0;
	for (; x + 16 <= width; x += 16) {
		uint8_t *out_base = out + (uint32_t)x * 3u;
		const uint8_t *row_base = row + (uint32_t)x * 3u;
		uint8x16_t offs3 = mpix_make_rgb_offsets();
		mve_pred16_t p0 = vctp8q(16);
		for (int ch = 0; ch < 3; ++ch) {
			uint8x16_t chOffs = vaddq_n_u8(offs3, (uint8_t)ch);
			uint8x16_t v = vldrbq_gather_offset_z_u8(row_base, chOffs, p0);
			vstrbq_scatter_offset_p_s8((int8_t *)out_base, vreinterpretq_s8_u8(chOffs), vreinterpretq_s8_u8(v), p0);
		}
	}
	uint16_t remain = width - x;
	if (remain) {
		uint8_t *out_base = out + (uint32_t)x * 3u;
		const uint8_t *row_base = row + (uint32_t)x * 3u;
		uint8x16_t offs3 = mpix_make_rgb_offsets();
		mve_pred16_t p0 = vctp8q(remain & 0xFF);
		for (int ch = 0; ch < 3; ++ch) {
			uint8x16_t chOffs = vaddq_n_u8(offs3, (uint8_t)ch);
			uint8x16_t v = vldrbq_gather_offset_z_u8(row_base, chOffs, p0);
			vstrbq_scatter_offset_p_s8((int8_t *)out_base, vreinterpretq_s8_u8(chOffs), vreinterpretq_s8_u8(v), p0);
		}
	}
}

void mpix_identity_rgb24_5x5(const uint8_t *in[5], uint8_t *out, uint16_t width)
{
	const uint8_t *row = in[2];
	uint16_t x = 0;
	for (; x + 16 <= width; x += 16) {
		uint8_t *out_base = out + (uint32_t)x * 3u;
		const uint8_t *row_base = row + (uint32_t)x * 3u;
		uint8x16_t offs3 = mpix_make_rgb_offsets();
		mve_pred16_t p0 = vctp8q(16);
		for (int ch = 0; ch < 3; ++ch) {
			uint8x16_t chOffs = vaddq_n_u8(offs3, (uint8_t)ch);
			uint8x16_t v = vldrbq_gather_offset_z_u8(row_base, chOffs, p0);
			vstrbq_scatter_offset_p_s8((int8_t *)out_base, vreinterpretq_s8_u8(chOffs), vreinterpretq_s8_u8(v), p0);
		}
	}
	uint16_t remain = width - x;
	if (remain) {
		uint8_t *out_base = out + (uint32_t)x * 3u;
		const uint8_t *row_base = row + (uint32_t)x * 3u;
		uint8x16_t offs3 = mpix_make_rgb_offsets();
		mve_pred16_t p0 = vctp8q(remain & 0xFF);
		for (int ch = 0; ch < 3; ++ch) {
			uint8x16_t chOffs = vaddq_n_u8(offs3, (uint8_t)ch);
			uint8x16_t v = vldrbq_gather_offset_z_u8(row_base, chOffs, p0);
			vstrbq_scatter_offset_p_s8((int8_t *)out_base, vreinterpretq_s8_u8(chOffs), vreinterpretq_s8_u8(v), p0);
		}
	}
}

/* Edge detect 3x3: -1 neighbors, +8 center (with-pre: reuse offsets/predicate) */
static inline void mpix_edge3x3_channel_block_with_pre(
	const uint8_t *row0, const uint8_t *row1, const uint8_t *row2,
	uint8_t *out_base, uint16_t x_base, uint16_t block_len,
	uint8x16_t offs3, mve_pred16_t p0, int channel)
{
	(void)block_len;
	const uint8_t *r0 = row0 + (uint32_t)x_base * 3u;
	const uint8_t *r1 = row1 + (uint32_t)x_base * 3u;
	const uint8_t *r2 = row2 + (uint32_t)x_base * 3u;

	uint8x16_t offC = vaddq_n_u8(offs3, (uint8_t)channel);
	uint8x16_t offL = vsubq_n_u8(offC, 3);
	uint8x16_t offR = vaddq_n_u8(offC, 3);
	if (x_base == 0) {
		uint8x16_t idx_pix = vidupq_n_u8(0, 1);
	mve_pred16_t pfirst = vcmpeqq_n_u8(idx_pix, (uint8_t)0);
		offL = vpselq_u8(offC, offL, pfirst);
	}

	uint8x16_t l0 = vldrbq_gather_offset_z_u8(r0, offL, p0);
	uint8x16_t c0 = vldrbq_gather_offset_z_u8(r0, offC, p0);
	uint8x16_t r0v = vldrbq_gather_offset_z_u8(r0, offR, p0);
	uint8x16_t l1 = vldrbq_gather_offset_z_u8(r1, offL, p0);
	uint8x16_t c1 = vldrbq_gather_offset_z_u8(r1, offC, p0);
	uint8x16_t r1v = vldrbq_gather_offset_z_u8(r1, offR, p0);
	uint8x16_t l2 = vldrbq_gather_offset_z_u8(r2, offL, p0);
	uint8x16_t c2 = vldrbq_gather_offset_z_u8(r2, offC, p0);
	uint8x16_t r2v = vldrbq_gather_offset_z_u8(r2, offR, p0);

	uint16x8_t neigh_lo = vaddq_u16(vaddq_u16(vmovlbq_u8(l0), vmovlbq_u8(c0)), vmovlbq_u8(r0v));
	neigh_lo = vaddq_u16(neigh_lo, vaddq_u16(vmovlbq_u8(l1), vmovlbq_u8(r1v)));
	neigh_lo = vaddq_u16(neigh_lo, vaddq_u16(vmovlbq_u8(l2), vmovlbq_u8(c2)));
	neigh_lo = vaddq_u16(neigh_lo, vmovlbq_u8(r2v));

	uint16x8_t neigh_hi = vaddq_u16(vaddq_u16(vmovltq_u8(l0), vmovltq_u8(c0)), vmovltq_u8(r0v));
	neigh_hi = vaddq_u16(neigh_hi, vaddq_u16(vmovltq_u8(l1), vmovltq_u8(r1v)));
	neigh_hi = vaddq_u16(neigh_hi, vaddq_u16(vmovltq_u8(l2), vmovltq_u8(c2)));
	neigh_hi = vaddq_u16(neigh_hi, vmovltq_u8(r2v));

	uint16x8_t cen_lo = vmovlbq_u8(c1);
	uint16x8_t cen_hi = vmovltq_u8(c1);
	cen_lo = vshlq_n_u16(cen_lo, 3);
	cen_hi = vshlq_n_u16(cen_hi, 3);

	uint16x8_t res_lo = vqsubq_u16(cen_lo, neigh_lo);
	uint16x8_t res_hi = vqsubq_u16(cen_hi, neigh_hi);

	uint8x16_t outv = vdupq_n_u8(0);
	outv = vqmovnbq_u16(outv, res_lo);
	outv = vqmovntq_u16(outv, res_hi);

	vstrbq_scatter_offset_p_s8((int8_t *)out_base, vreinterpretq_s8_u8(offC), vreinterpretq_s8_u8(outv), p0);
}

void mpix_edgedetect_rgb24_3x3(const uint8_t *in[3], uint8_t *out, uint16_t width)
{
	const uint8_t *row0 = in[0];
	const uint8_t *row1 = in[1];
	const uint8_t *row2 = in[2];
	uint16_t x = 0;
	for (; (uint32_t)x + 16u <= (uint32_t)(width - 1); x += 16) {
		uint8_t *out_base = out + (uint32_t)x * 3u;
		uint8x16_t offs3 = mpix_make_rgb_offsets();
		mve_pred16_t p0 = vctp8q(16);
		for (int ch = 0; ch < 3; ++ch) {
			mpix_edge3x3_channel_block_with_pre(row0, row1, row2, out_base, x, 16, offs3, p0, ch);
		}
	}
	uint16_t remain = width - x;
	if (remain >= 2) {
		uint16_t blk = (uint16_t)(remain - 1);
		uint8_t *out_base = out + (uint32_t)x * 3u;
		uint8x16_t offs3 = mpix_make_rgb_offsets();
		mve_pred16_t p0 = vctp8q(blk & 0xFF);
		for (int ch = 0; ch < 3; ++ch) {
			mpix_edge3x3_channel_block_with_pre(row0, row1, row2, out_base, x, blk, offs3, p0, ch);
		}
		x += blk;
		remain = width - x;
	}
	if (remain == 1) {
		uint16_t xm1 = (x > 0) ? (uint16_t)(x - 1) : x;
		uint16_t xp1 = x;
		for (int ch = 0; ch < 3; ++ch) {
			int32_t s = 0;
			s -= (int32_t)in[0][xm1 * 3 + ch];
			s -= (int32_t)in[0][x * 3 + ch];
			s -= (int32_t)in[0][xp1 * 3 + ch];
			s -= (int32_t)in[1][xm1 * 3 + ch];
			s += (int32_t)in[1][x * 3 + ch] * 8;
			s -= (int32_t)in[1][xp1 * 3 + ch];
			s -= (int32_t)in[2][xm1 * 3 + ch];
			s -= (int32_t)in[2][x * 3 + ch];
			s -= (int32_t)in[2][xp1 * 3 + ch];
			if (s < 0) s = 0;
			if (s > 255) s = 255;
			out[x * 3 + ch] = (uint8_t)s;
		}
	}
}
void mpix_gaussianblur_rgb24_3x3(const uint8_t *in[3], uint8_t *out, uint16_t width)
{
	assert(width >= 3);

	const uint8_t *row0 = in[0];
	const uint8_t *row1 = in[1];
	const uint8_t *row2 = in[2];

	uint16_t x = 0;

	/* Fast path: full 16-pixel blocks with safe right neighbor (x+15+1 <= width-1) */
	for (; (uint32_t)x + 16u <= (uint32_t)(width - 1); x += 16) {
		uint8_t *out_base = out + (uint32_t)x * 3u;
		/* 预计算每块的偏移与谓词，3通道共享 */
		uint8x16_t offs3 = mpix_make_rgb_offsets();
		mve_pred16_t p0 = vctp8q(16);
		mpix_gauss3x3_channel_block_with_pre(row0, row1, row2, out_base, x, 16, offs3, p0, 0);
		mpix_gauss3x3_channel_block_with_pre(row0, row1, row2, out_base, x, 16, offs3, p0, 1);
		mpix_gauss3x3_channel_block_with_pre(row0, row1, row2, out_base, x, 16, offs3, p0, 2);
	}

	/* Tail: ensure right neighbor exists by processing (remain-1) lanes vectorized if possible */
	uint16_t remain = width - x;
	if (remain >= 2) {
		uint16_t blk = (uint16_t)(remain - 1); /* last pixel kept for scalar (needs clamping) */
		uint8_t *out_base = out + (uint32_t)x * 3u;
		uint8x16_t offs3 = mpix_make_rgb_offsets();
		mve_pred16_t p0 = vctp8q(blk & 0xFF);
		mpix_gauss3x3_channel_block_with_pre(row0, row1, row2, out_base, x, blk, offs3, p0, 0);
		mpix_gauss3x3_channel_block_with_pre(row0, row1, row2, out_base, x, blk, offs3, p0, 1);
		mpix_gauss3x3_channel_block_with_pre(row0, row1, row2, out_base, x, blk, offs3, p0, 2);
		x += blk;
		remain = width - x;
	}

	/* Last pixel (x == width-1): scalar fallback with edge clamp (only 1 pixel) */
	if (remain == 1) {
		uint16_t xm1 = (x > 0) ? (uint16_t)(x - 1) : x;
		uint16_t xp1 = x; /* clamp to edge */

		for (int ch = 0; ch < 3; ++ch) {
			uint32_t s = 0;
			/* row 0 */
			uint8_t a0 = row0[xm1 * 3 + ch];
			uint8_t b0 = row0[x * 3 + ch];
			uint8_t c0 = row0[xp1 * 3 + ch];
			s += (uint32_t)a0 + ((uint32_t)b0 << 1) + (uint32_t)c0;
			/* row 1 (weight x2) */
			uint8_t a1 = row1[xm1 * 3 + ch];
			uint8_t b1 = row1[x * 3 + ch];
			uint8_t c1 = row1[xp1 * 3 + ch];
			s += (((uint32_t)a1 + ((uint32_t)b1 << 1) + (uint32_t)c1) << 1);
			/* row 2 */
			uint8_t a2 = row2[xm1 * 3 + ch];
			uint8_t b2 = row2[x * 3 + ch];
			uint8_t c2 = row2[xp1 * 3 + ch];
			s += (uint32_t)a2 + ((uint32_t)b2 << 1) + (uint32_t)c2;

			s += 8u; /* rounding */
			s >>= 4; /* normalize */
			out[x * 3 + ch] = (uint8_t)(s > 255u ? 255u : s);
		}
	}
}

/* ========================== 5x5 Gaussian Blur (RGB24) ========================== */

static inline void mpix_gauss5x5_channel_block_with_pre(
	const uint8_t *r0, const uint8_t *r1, const uint8_t *r2, const uint8_t *r3, const uint8_t *r4,
	uint8_t *out_base, uint16_t x_base, uint16_t block_len,
	uint8x16_t offs3, mve_pred16_t p0, int channel)
{
	(void)block_len;
	const uint8_t *row0 = r0 + (uint32_t)x_base * 3u;
	const uint8_t *row1 = r1 + (uint32_t)x_base * 3u;
	const uint8_t *row2 = r2 + (uint32_t)x_base * 3u;
	const uint8_t *row3 = r3 + (uint32_t)x_base * 3u;
	const uint8_t *row4 = r4 + (uint32_t)x_base * 3u;

	uint8x16_t offC  = vaddq_n_u8(offs3, (uint8_t)channel);
	/* neighbors: -2,-1,+1,+2 pixels => -6,-3,+3,+6 bytes per lane */
	uint8x16_t offL1 = vsubq_n_u8(offC, 3);
	uint8x16_t offL2 = vsubq_n_u8(offC, 6);
	uint8x16_t offR1 = vaddq_n_u8(offC, 3);
	uint8x16_t offR2 = vaddq_n_u8(offC, 6);

	/* 对块首两列做lane级clamp，避免跨通道误读 */
	if (x_base == 0) {
		uint8x16_t idx_pix = vidupq_n_u8(0, 1);
	mve_pred16_t p0eq = vcmpeqq_n_u8(idx_pix, (uint8_t)0);
	mve_pred16_t p1eq = vcmpeqq_n_u8(idx_pix, (uint8_t)1);
		offL1 = vpselq_u8(offC, offL1, p0eq);
		offL2 = vpselq_u8(offC, offL2, p0eq);
		offL2 = vpselq_u8(offC, offL2, p1eq);
	} else if (x_base == 1) {
		uint8x16_t idx_pix = vidupq_n_u8(0, 1);
	mve_pred16_t p0eq = vcmpeqq_n_u8(idx_pix, (uint8_t)0);
		offL2 = vpselq_u8(offC, offL2, p0eq);
	}

	/* Gather for 5 rows */
	uint8x16_t a0 = vldrbq_gather_offset_z_u8(row0, offL2, p0);
	uint8x16_t b0 = vldrbq_gather_offset_z_u8(row0, offL1, p0);
	uint8x16_t c0 = vldrbq_gather_offset_z_u8(row0, offC,  p0);
	uint8x16_t d0 = vldrbq_gather_offset_z_u8(row0, offR1, p0);
	uint8x16_t e0 = vldrbq_gather_offset_z_u8(row0, offR2, p0);

	uint8x16_t a1 = vldrbq_gather_offset_z_u8(row1, offL2, p0);
	uint8x16_t b1 = vldrbq_gather_offset_z_u8(row1, offL1, p0);
	uint8x16_t c1 = vldrbq_gather_offset_z_u8(row1, offC,  p0);
	uint8x16_t d1 = vldrbq_gather_offset_z_u8(row1, offR1, p0);
	uint8x16_t e1 = vldrbq_gather_offset_z_u8(row1, offR2, p0);

	uint8x16_t a2 = vldrbq_gather_offset_z_u8(row2, offL2, p0);
	uint8x16_t b2 = vldrbq_gather_offset_z_u8(row2, offL1, p0);
	uint8x16_t c2 = vldrbq_gather_offset_z_u8(row2, offC,  p0);
	uint8x16_t d2 = vldrbq_gather_offset_z_u8(row2, offR1, p0);
	uint8x16_t e2 = vldrbq_gather_offset_z_u8(row2, offR2, p0);

	uint8x16_t a3 = vldrbq_gather_offset_z_u8(row3, offL2, p0);
	uint8x16_t b3 = vldrbq_gather_offset_z_u8(row3, offL1, p0);
	uint8x16_t c3 = vldrbq_gather_offset_z_u8(row3, offC,  p0);
	uint8x16_t d3 = vldrbq_gather_offset_z_u8(row3, offR1, p0);
	uint8x16_t e3 = vldrbq_gather_offset_z_u8(row3, offR2, p0);

	uint8x16_t a4 = vldrbq_gather_offset_z_u8(row4, offL2, p0);
	uint8x16_t b4 = vldrbq_gather_offset_z_u8(row4, offL1, p0);
	uint8x16_t c4 = vldrbq_gather_offset_z_u8(row4, offC,  p0);
	uint8x16_t d4 = vldrbq_gather_offset_z_u8(row4, offR1, p0);
	uint8x16_t e4 = vldrbq_gather_offset_z_u8(row4, offR2, p0);

	/* Horizontal 1-4-6-4-1 for each row, in 16-bit halves */
	/* helper macro to compute: (a+e) + 4*(b+d) + 6*c */
#define H121_ROW_LO(a,b,c,d,e) \
	({ \
		uint16x8_t _ae = vaddq_u16(vmovlbq_u8(a), vmovlbq_u8(e)); \
		uint16x8_t _bd = vaddq_u16(vmovlbq_u8(b), vmovlbq_u8(d)); \
		uint16x8_t _c  = vmovlbq_u8(c); \
		uint16x8_t _sum = vaddq_u16(_ae, vshlq_n_u16(_bd, 2)); \
		_sum = vaddq_u16(_sum, vaddq_u16(vshlq_n_u16(_c, 2), vshlq_n_u16(_c, 1))); \
		_sum; \
	})
#define H121_ROW_HI(a,b,c,d,e) \
	({ \
		uint16x8_t _ae = vaddq_u16(vmovltq_u8(a), vmovltq_u8(e)); \
		uint16x8_t _bd = vaddq_u16(vmovltq_u8(b), vmovltq_u8(d)); \
		uint16x8_t _c  = vmovltq_u8(c); \
		uint16x8_t _sum = vaddq_u16(_ae, vshlq_n_u16(_bd, 2)); \
		_sum = vaddq_u16(_sum, vaddq_u16(vshlq_n_u16(_c, 2), vshlq_n_u16(_c, 1))); \
		_sum; \
	})

	uint16x8_t h0_lo = H121_ROW_LO(a0,b0,c0,d0,e0);
	uint16x8_t h0_hi = H121_ROW_HI(a0,b0,c0,d0,e0);
	uint16x8_t h1_lo = H121_ROW_LO(a1,b1,c1,d1,e1);
	uint16x8_t h1_hi = H121_ROW_HI(a1,b1,c1,d1,e1);
	uint16x8_t h2_lo = H121_ROW_LO(a2,b2,c2,d2,e2);
	uint16x8_t h2_hi = H121_ROW_HI(a2,b2,c2,d2,e2);
	uint16x8_t h3_lo = H121_ROW_LO(a3,b3,c3,d3,e3);
	uint16x8_t h3_hi = H121_ROW_HI(a3,b3,c3,d3,e3);
	uint16x8_t h4_lo = H121_ROW_LO(a4,b4,c4,d4,e4);
	uint16x8_t h4_hi = H121_ROW_HI(a4,b4,c4,d4,e4);

	/* Vertical 1-4-6-4-1 on the horizontal sums */
	uint16x8_t v_lo = vaddq_u16(vaddq_u16(h0_lo, vshlq_n_u16(h1_lo, 2)), vaddq_u16(vshlq_n_u16(h2_lo, 2), vshlq_n_u16(h2_lo, 1))); /* +6*h2 */
	v_lo = vaddq_u16(v_lo, vshlq_n_u16(h3_lo, 2));
	v_lo = vaddq_u16(v_lo, h4_lo);
	v_lo = vaddq_u16(v_lo, vdupq_n_u16(128)); /* rounding */

	uint16x8_t v_hi = vaddq_u16(vaddq_u16(h0_hi, vshlq_n_u16(h1_hi, 2)), vaddq_u16(vshlq_n_u16(h2_hi, 2), vshlq_n_u16(h2_hi, 1)));
	v_hi = vaddq_u16(v_hi, vshlq_n_u16(h3_hi, 2));
	v_hi = vaddq_u16(v_hi, h4_hi);
	v_hi = vaddq_u16(v_hi, vdupq_n_u16(128));

	uint8x16_t outv = vdupq_n_u8(0);
	outv = vshrnbq_n_u16(outv, v_lo, 8);
	outv = vshrntq_n_u16(outv, v_hi, 8);

	vstrbq_scatter_offset_p_s8((int8_t *)out_base, vreinterpretq_s8_u8(offC), vreinterpretq_s8_u8(outv), p0);

#undef H121_ROW_LO
#undef H121_ROW_HI
}

/* Debug-only scalar reference for 5x5 Gaussian removed after validation. */

void mpix_gaussianblur_rgb24_5x5(const uint8_t *in[5], uint8_t *out, uint16_t width)
{
	assert(width >= 5);

	const uint8_t *r0 = in[0];
	const uint8_t *r1 = in[1];
	const uint8_t *r2 = in[2];
	const uint8_t *r3 = in[3];
	const uint8_t *r4 = in[4];

	uint16_t x = 0;
	/* Need two right neighbors, ensure x+15+2 <= width-1 => x+17 <= width */
	for (; (uint32_t)x + 16u <= (uint32_t)(width - 2); x += 16) {
		uint8_t *out_base = out + (uint32_t)x * 3u;
		uint8x16_t offs3 = mpix_make_rgb_offsets();
		mve_pred16_t p0 = vctp8q(16);
		mpix_gauss5x5_channel_block_with_pre(r0, r1, r2, r3, r4, out_base, x, 16, offs3, p0, 0);
		mpix_gauss5x5_channel_block_with_pre(r0, r1, r2, r3, r4, out_base, x, 16, offs3, p0, 1);
		mpix_gauss5x5_channel_block_with_pre(r0, r1, r2, r3, r4, out_base, x, 16, offs3, p0, 2);
	}

	uint16_t remain = width - x;
	if (remain >= 3) {
		uint16_t blk = (uint16_t)(remain - 2);
		uint8_t *out_base = out + (uint32_t)x * 3u;
		uint8x16_t offs3 = mpix_make_rgb_offsets();
		mve_pred16_t p0 = vctp8q(blk & 0xFF);
		mpix_gauss5x5_channel_block_with_pre(r0, r1, r2, r3, r4, out_base, x, blk, offs3, p0, 0);
		mpix_gauss5x5_channel_block_with_pre(r0, r1, r2, r3, r4, out_base, x, blk, offs3, p0, 1);
		mpix_gauss5x5_channel_block_with_pre(r0, r1, r2, r3, r4, out_base, x, blk, offs3, p0, 2);
		x += blk;
		remain = width - x;
	}

	/* Last two pixels: scalar fallback with edge clamp (apply full 2D 5x5 weights) */
	for (; remain > 0; --remain, ++x) {
		uint16_t xm2 = (x >= 2) ? (uint16_t)(x - 2) : 0;
		uint16_t xm1 = (x >= 1) ? (uint16_t)(x - 1) : 0;
		uint16_t xp1 = (x + 1 < width) ? (uint16_t)(x + 1) : (uint16_t)(width - 1);
		uint16_t xp2 = (x + 2 < width) ? (uint16_t)(x + 2) : (uint16_t)(width - 1);
		for (int ch = 0; ch < 3; ++ch) {
			/* Horizontal sums per row */
			uint32_t h0 = (uint32_t)in[0][xm2*3+ch] + (((uint32_t)in[0][xm1*3+ch])<<2)
			          + ((uint32_t)in[0][x*3+ch] * 6u) + (((uint32_t)in[0][xp1*3+ch])<<2)
			          + (uint32_t)in[0][xp2*3+ch];
			uint32_t h1 = (uint32_t)in[1][xm2*3+ch] + (((uint32_t)in[1][xm1*3+ch])<<2)
			          + ((uint32_t)in[1][x*3+ch] * 6u) + (((uint32_t)in[1][xp1*3+ch])<<2)
			          + (uint32_t)in[1][xp2*3+ch];
			uint32_t h2 = (uint32_t)in[2][xm2*3+ch] + (((uint32_t)in[2][xm1*3+ch])<<2)
			          + ((uint32_t)in[2][x*3+ch] * 6u) + (((uint32_t)in[2][xp1*3+ch])<<2)
			          + (uint32_t)in[2][xp2*3+ch];
			uint32_t h3 = (uint32_t)in[3][xm2*3+ch] + (((uint32_t)in[3][xm1*3+ch])<<2)
			          + ((uint32_t)in[3][x*3+ch] * 6u) + (((uint32_t)in[3][xp1*3+ch])<<2)
			          + (uint32_t)in[3][xp2*3+ch];
			uint32_t h4 = (uint32_t)in[4][xm2*3+ch] + (((uint32_t)in[4][xm1*3+ch])<<2)
			          + ((uint32_t)in[4][x*3+ch] * 6u) + (((uint32_t)in[4][xp1*3+ch])<<2)
			          + (uint32_t)in[4][xp2*3+ch];

			/* Vertical combine 1,4,6,4,1, add rounding and normalize */
			uint32_t s = h0 + (h1 << 2) + h4 + (h3 << 2) + (h2 << 2) + (h2 << 1);
			s += 128u;
			s >>= 8;
			out[x*3+ch] = (uint8_t)(s > 255u ? 255u : s);
		}
	}
}

/* ========================== 3x3 Sharpen (RGB24) ========================== */

static inline void mpix_sharpen3x3_channel_block_with_pre(
	const uint8_t *row0, const uint8_t *row1, const uint8_t *row2,
	uint8_t *out_base, uint16_t x_base, uint16_t block_len,
	uint8x16_t offs3, mve_pred16_t p0, int channel)
{
	const uint8_t *r0 = row0 + (uint32_t)x_base * 3u;
	const uint8_t *r1 = row1 + (uint32_t)x_base * 3u;
	const uint8_t *r2 = row2 + (uint32_t)x_base * 3u;

	(void)block_len;
	uint8x16_t offC = vaddq_n_u8(offs3, (uint8_t)channel);
	uint8x16_t offL = vsubq_n_u8(offC, 3);
	uint8x16_t offR = vaddq_n_u8(offC, 3);

	if (x_base == 0) {
		uint8x16_t idx_pix = vidupq_n_u8(0, 1);
	mve_pred16_t pfirst = vcmpeqq_n_u8(idx_pix, (uint8_t)0);
		offL = vpselq_u8(offC, offL, pfirst);
	}

	uint8x16_t u = vldrbq_gather_offset_z_u8(r0, offC, p0); /* up */
	uint8x16_t l = vldrbq_gather_offset_z_u8(r1, offL, p0); /* left */
	uint8x16_t c = vldrbq_gather_offset_z_u8(r1, offC, p0); /* center */
	uint8x16_t r = vldrbq_gather_offset_z_u8(r1, offR, p0); /* right */
	uint8x16_t d = vldrbq_gather_offset_z_u8(r2, offC, p0); /* down */

	uint16x8_t c_lo = vmovlbq_u8(c);
	uint16x8_t c_hi = vmovltq_u8(c);
	c_lo = vaddq_u16(vshlq_n_u16(c_lo, 2), c_lo); /* 5*c */
	c_hi = vaddq_u16(vshlq_n_u16(c_hi, 2), c_hi);

	uint16x8_t sumN_lo = vaddq_u16(vaddq_u16(vmovlbq_u8(l), vmovlbq_u8(r)), vaddq_u16(vmovlbq_u8(u), vmovlbq_u8(d)));
	uint16x8_t sumN_hi = vaddq_u16(vaddq_u16(vmovltq_u8(l), vmovltq_u8(r)), vaddq_u16(vmovltq_u8(u), vmovltq_u8(d)));

	uint16x8_t res_lo = vqsubq_u16(c_lo, sumN_lo);
	uint16x8_t res_hi = vqsubq_u16(c_hi, sumN_hi);

	uint8x16_t outv = vdupq_n_u8(0);
	outv = vqmovnbq_u16(outv, res_lo);
	outv = vqmovntq_u16(outv, res_hi);

	vstrbq_scatter_offset_p_s8((int8_t *)out_base, vreinterpretq_s8_u8(offC), vreinterpretq_s8_u8(outv), p0);
}

void mpix_sharpen_rgb24_3x3(const uint8_t *in[3], uint8_t *out, uint16_t width)
{
	assert(width >= 3);
	const uint8_t *row0 = in[0];
	const uint8_t *row1 = in[1];
	const uint8_t *row2 = in[2];

	uint16_t x = 0;
	for (; (uint32_t)x + 16u <= (uint32_t)(width - 1); x += 16) {
		uint8_t *out_base = out + (uint32_t)x * 3u;
		uint8x16_t offs3 = mpix_make_rgb_offsets();
		mve_pred16_t p0 = vctp8q(16);
		for (int ch = 0; ch < 3; ++ch) {
			mpix_sharpen3x3_channel_block_with_pre(row0, row1, row2, out_base, x, 16, offs3, p0, ch);
		}
	}
	uint16_t remain = width - x;
	if (remain >= 2) {
		uint16_t blk = (uint16_t)(remain - 1);
		uint8_t *out_base = out + (uint32_t)x * 3u;
		uint8x16_t offs3 = mpix_make_rgb_offsets();
		mve_pred16_t p0 = vctp8q(blk & 0xFF);
		for (int ch = 0; ch < 3; ++ch) {
			mpix_sharpen3x3_channel_block_with_pre(row0, row1, row2, out_base, x, blk, offs3, p0, ch);
		}
		x += blk;
		remain = width - x;
	}
	if (remain == 1) {
		uint16_t xm1 = (x > 0) ? (uint16_t)(x - 1) : x;
		uint16_t xp1 = x;
		for (int ch = 0; ch < 3; ++ch) {
			int32_t s = 0;
			s += 5 * (int32_t)in[1][x * 3 + ch];
			s -= (int32_t)in[1][xm1 * 3 + ch];
			s -= (int32_t)in[1][xp1 * 3 + ch];
			s -= (int32_t)in[0][x * 3 + ch];
			s -= (int32_t)in[2][x * 3 + ch];
			if (s < 0) s = 0;
			if (s > 255) s = 255;
			out[x * 3 + ch] = (uint8_t)s;
		}
	}
}


/* ========================== 3x3 Median (RGB24) ========================== */

static inline void mpix_swap_u8x16(uint8x16_t *a, uint8x16_t *b)
{
	uint8x16_t ta = *a;
	*a = vminq_u8(ta, *b);
	*b = vmaxq_u8(ta, *b);
}

static inline void mpix_median3x3_channel_block_with_pre(
	const uint8_t *row0, const uint8_t *row1, const uint8_t *row2,
	uint8_t *out_base, uint16_t x_base, uint16_t block_len,
	uint8x16_t offs3, mve_pred16_t p0, int channel)
{
	const uint8_t *r0 = row0 + (uint32_t)x_base * 3u;
	const uint8_t *r1 = row1 + (uint32_t)x_base * 3u;
	const uint8_t *r2 = row2 + (uint32_t)x_base * 3u;

	(void)block_len;
	uint8x16_t offC  = vaddq_n_u8(offs3, (uint8_t)channel);
	uint8x16_t offL_raw = vsubq_n_u8(offC, 3);
	uint8x16_t offL = offL_raw;
	if (x_base == 0) {
		/* Only the very first pixel of the line lacks a left neighbor */
		uint8x16_t idx_pix = vidupq_n_u8(0, 1);
	mve_pred16_t pfirst = vcmpeqq_n_u8(idx_pix, (uint8_t)0);
		offL = vpselq_u8(offC, offL_raw, pfirst);
	}
	uint8x16_t offR  = vaddq_n_u8(offC, 3);


	/* Load 9 neighbors: row0/1/2 x left/center/right */
	uint8x16_t v0 = vldrbq_gather_offset_z_u8(r0, offL, p0);
	uint8x16_t v1 = vldrbq_gather_offset_z_u8(r0, offC, p0);
	uint8x16_t v2 = vldrbq_gather_offset_z_u8(r0, offR, p0);
	uint8x16_t v3 = vldrbq_gather_offset_z_u8(r1, offL, p0);
	uint8x16_t v4 = vldrbq_gather_offset_z_u8(r1, offC, p0);
	uint8x16_t v5 = vldrbq_gather_offset_z_u8(r1, offR, p0);
	uint8x16_t v6 = vldrbq_gather_offset_z_u8(r2, offL, p0);
	uint8x16_t v7 = vldrbq_gather_offset_z_u8(r2, offC, p0);
	uint8x16_t v8 = vldrbq_gather_offset_z_u8(r2, offR, p0);

	/*
	 * Robust median-of-9 algorithm:
	 * 1) Sort each row triple so that v0<=v1<=v2, v3<=v4<=v5, v6<=v7<=v8
	 * 2) a = max(v0,v3,v6)   (max of row minima)
	 *    b = median(v1,v4,v7) (median of row middles)
	 *    c = min(v2,v5,v8)   (min of row maxima)
	 * 3) median = median(a,b,c)
	 */
	/* step 1: row-wise sort3 */
	mpix_swap_u8x16(&v0, &v1); mpix_swap_u8x16(&v1, &v2); mpix_swap_u8x16(&v0, &v1);
	mpix_swap_u8x16(&v3, &v4); mpix_swap_u8x16(&v4, &v5); mpix_swap_u8x16(&v3, &v4);
	mpix_swap_u8x16(&v6, &v7); mpix_swap_u8x16(&v7, &v8); mpix_swap_u8x16(&v6, &v7);

	/* step 2: candidates a,b,c */
	uint8x16_t a = vmaxq_u8(v0, vmaxq_u8(v3, v6));
	/* b = median(v1,v4,v7) via sort3 */
	uint8x16_t b0 = v1, b1 = v4, b2 = v7;
	mpix_swap_u8x16(&b0, &b1); mpix_swap_u8x16(&b1, &b2); mpix_swap_u8x16(&b0, &b1);
	uint8x16_t b = b1;
	uint8x16_t c = vminq_u8(v2, vminq_u8(v5, v8));

	/* step 3: median of (a,b,c) */
	mpix_swap_u8x16(&a, &b); mpix_swap_u8x16(&b, &c); mpix_swap_u8x16(&a, &b);
	uint8x16_t med = b;

	/* Store median to center position */
	vstrbq_scatter_offset_p_s8((int8_t *)out_base, vreinterpretq_s8_u8(offC), vreinterpretq_s8_u8(med), p0);
}

void mpix_median_rgb24_3x3(const uint8_t *in[3], uint8_t *out, uint16_t width)
{
	assert(width >= 3);

	const uint8_t *row0 = in[0];
	const uint8_t *row1 = in[1];
	const uint8_t *row2 = in[2];

	uint16_t x = 0;
	/* Full blocks where right neighbor exists for all 16 lanes */
	for (; (uint32_t)x + 16u <= (uint32_t)(width - 1); x += 16) {
		uint8_t *out_base = out + (uint32_t)x * 3u;
		uint8x16_t offs3 = mpix_make_rgb_offsets();
		mve_pred16_t p0 = vctp8q(16);
		for (int ch = 0; ch < 3; ++ch) {
			mpix_median3x3_channel_block_with_pre(row0, row1, row2, out_base, x, 16, offs3, p0, ch);
		}
	}

	/* Tail: keep last pixel for scalar so that right neighbor exists */
	uint16_t remain = width - x;
	if (remain >= 2) {
		uint16_t blk = (uint16_t)(remain - 1);
		uint8_t *out_base = out + (uint32_t)x * 3u;
		uint8x16_t offs3 = mpix_make_rgb_offsets();
		mve_pred16_t p0 = vctp8q(blk & 0xFF);
		for (int ch = 0; ch < 3; ++ch) {
			mpix_median3x3_channel_block_with_pre(row0, row1, row2, out_base, x, blk, offs3, p0, ch);
		}
		x += blk;
		remain = width - x;
	}

	/* Last pixel: scalar median-of-9 with horizontal clamp; vertical handled by caller */
	if (remain == 1) {
		uint16_t xm1 = (x > 0) ? (uint16_t)(x - 1) : x;
		uint16_t xp1 = (x + 1 < width) ? (uint16_t)(x + 1) : x; /* caller guarantees width>=3, so at last pixel xp1==x */
		for (int ch = 0; ch < 3; ++ch) {
			uint8_t vals[9] = {
				row0[xm1*3+ch], row0[x*3+ch], row0[xp1*3+ch],
				row1[xm1*3+ch], row1[x*3+ch], row1[xp1*3+ch],
				row2[xm1*3+ch], row2[x*3+ch], row2[xp1*3+ch]
			};
			/* simple insertion sort for 9 elements */
			for (int i = 1; i < 9; ++i) {
				uint8_t key = vals[i];
				int j = i - 1;
				while (j >= 0 && vals[j] > key) { vals[j+1] = vals[j]; --j; }
				vals[j+1] = key;
			}
			out[x*3+ch] = vals[4];
		}
	}
}

/* ========================== 5x5 Median (RGB24) ========================== */

static inline uint8x16_t mpix_avg_u8(uint8x16_t a, uint8x16_t b)
{
	/* (a + b) / 2 without overflow by widening to u16 */
	uint16x8_t alo = vmovlbq_u8(a), ahi = vmovltq_u8(a);
	uint16x8_t blo = vmovlbq_u8(b), bhi = vmovltq_u8(b);
	uint16x8_t slo = vaddq_u16(alo, blo);
	uint16x8_t shi = vaddq_u16(ahi, bhi);
	slo = vshrq_n_u16(slo, 1);
	shi = vshrq_n_u16(shi, 1);
	uint8x16_t out = vdupq_n_u8(0);
	out = vqmovnbq_u16(out, slo);
	out = vqmovntq_u16(out, shi);
	return out;
}

static inline void mpix_median5x5_channel_block_with_pre(
	const uint8_t *r0, const uint8_t *r1, const uint8_t *r2, const uint8_t *r3, const uint8_t *r4,
	uint8_t *out_base, uint16_t x_base, uint16_t block_len,
	uint8x16_t offs3, mve_pred16_t p0, int channel)
{
	const uint8_t *row0 = r0 + (uint32_t)x_base * 3u;
	const uint8_t *row1 = r1 + (uint32_t)x_base * 3u;
	const uint8_t *row2 = r2 + (uint32_t)x_base * 3u;
	const uint8_t *row3 = r3 + (uint32_t)x_base * 3u;
	const uint8_t *row4 = r4 + (uint32_t)x_base * 3u;

	(void)block_len;
	uint8x16_t offC  = vaddq_n_u8(offs3, (uint8_t)channel);
	/* raw neighbors: -2,-1,+1,+2 pixels => -6,-3,+3,+6 bytes */
	uint8x16_t offL1_raw = vsubq_n_u8(offC, 3);
	uint8x16_t offL2_raw = vsubq_n_u8(offC, 6);
	uint8x16_t offR1 = vaddq_n_u8(offC, 3);
	uint8x16_t offR2 = vaddq_n_u8(offC, 6);

	/* Fix left-edge clamping per-lane to keep the same channel: clamp to offC on underflow lanes */
	uint8x16_t idx_pix = vidupq_n_u8(0, 1);
	mve_pred16_t p_idx0 = vcmpeqq_n_u8(idx_pix, (uint8_t)0);
	mve_pred16_t p_idx1 = vcmpeqq_n_u8(idx_pix, (uint8_t)1);
	uint8x16_t offL1 = offL1_raw;
	uint8x16_t offL2 = offL2_raw;
	if (x_base == 0) {
		/* x==0: L1 underflow at lane0; L2 underflow at lanes 0,1 */
		offL1 = vpselq_u8(offC, offL1_raw, p_idx0);
		offL2 = vpselq_u8(offC, offL2_raw, p_idx0);
		offL2 = vpselq_u8(offC, offL2,      p_idx1);
	} else if (x_base == 1) {
		/* x==1: L2 underflow at lane0 */
		offL2 = vpselq_u8(offC, offL2_raw, p_idx0);
	}
	/* Gather 25 values: rows 0..4, cols L2 L1 C R1 R2 */
	uint8x16_t a0 = vldrbq_gather_offset_z_u8(row0, offL2, p0);
	uint8x16_t b0 = vldrbq_gather_offset_z_u8(row0, offL1, p0);
	uint8x16_t c0 = vldrbq_gather_offset_z_u8(row0, offC,  p0);
	uint8x16_t d0 = vldrbq_gather_offset_z_u8(row0, offR1, p0);
	uint8x16_t e0 = vldrbq_gather_offset_z_u8(row0, offR2, p0);

	uint8x16_t a1 = vldrbq_gather_offset_z_u8(row1, offL2, p0);
	uint8x16_t b1 = vldrbq_gather_offset_z_u8(row1, offL1, p0);
	uint8x16_t c1 = vldrbq_gather_offset_z_u8(row1, offC,  p0);
	uint8x16_t d1 = vldrbq_gather_offset_z_u8(row1, offR1, p0);
	uint8x16_t e1 = vldrbq_gather_offset_z_u8(row1, offR2, p0);

	uint8x16_t a2 = vldrbq_gather_offset_z_u8(row2, offL2, p0);
	uint8x16_t b2 = vldrbq_gather_offset_z_u8(row2, offL1, p0);
	uint8x16_t c2 = vldrbq_gather_offset_z_u8(row2, offC,  p0);
	uint8x16_t d2 = vldrbq_gather_offset_z_u8(row2, offR1, p0);
	uint8x16_t e2 = vldrbq_gather_offset_z_u8(row2, offR2, p0);

	uint8x16_t a3 = vldrbq_gather_offset_z_u8(row3, offL2, p0);
	uint8x16_t b3 = vldrbq_gather_offset_z_u8(row3, offL1, p0);
	uint8x16_t c3 = vldrbq_gather_offset_z_u8(row3, offC,  p0);
	uint8x16_t d3 = vldrbq_gather_offset_z_u8(row3, offR1, p0);
	uint8x16_t e3 = vldrbq_gather_offset_z_u8(row3, offR2, p0);

	uint8x16_t a4 = vldrbq_gather_offset_z_u8(row4, offL2, p0);
	uint8x16_t b4 = vldrbq_gather_offset_z_u8(row4, offL1, p0);
	uint8x16_t c4 = vldrbq_gather_offset_z_u8(row4, offC,  p0);
	uint8x16_t d4 = vldrbq_gather_offset_z_u8(row4, offR1, p0);
	uint8x16_t e4 = vldrbq_gather_offset_z_u8(row4, offR2, p0);

	/* Vectorized binary-search median per lane over 25 values */
	uint8x16_t bot = vdupq_n_u8(0);
	uint8x16_t top = vdupq_n_u8(255);
	uint8x16_t one = vdupq_n_u8(1);
	uint8x16_t half = vdupq_n_u8(12); /* 25/2 */
	for (int i = 0; i < 8; ++i) {
		uint8x16_t mid = mpix_avg_u8(top, bot);
		uint8x16_t cnt = vdupq_n_u8(0);
		mve_pred16_t p;
	/* accumulate counts where v > mid: explicit unsigned compare variant */
	p = vcmphiq_u8(a0, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
	p = vcmphiq_u8(b0, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
	p = vcmphiq_u8(c0, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
	p = vcmphiq_u8(d0, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
	p = vcmphiq_u8(e0, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);

	p = vcmphiq_u8(a1, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
	p = vcmphiq_u8(b1, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
	p = vcmphiq_u8(c1, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
	p = vcmphiq_u8(d1, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
	p = vcmphiq_u8(e1, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);

	p = vcmphiq_u8(a2, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
	p = vcmphiq_u8(b2, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
	p = vcmphiq_u8(c2, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
	p = vcmphiq_u8(d2, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
	p = vcmphiq_u8(e2, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);

	p = vcmphiq_u8(a3, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
	p = vcmphiq_u8(b3, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
	p = vcmphiq_u8(c3, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
	p = vcmphiq_u8(d3, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
	p = vcmphiq_u8(e3, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);

	p = vcmphiq_u8(a4, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
	p = vcmphiq_u8(b4, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
	p = vcmphiq_u8(c4, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
	p = vcmphiq_u8(d4, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
	p = vcmphiq_u8(e4, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);

    /* update pivots */
	mve_pred16_t p_gt = vcmphiq_u8(cnt, half); /* cnt > half */
	mve_pred16_t p_lt = vcmphiq_u8(half, cnt); /* cnt < half */
		top = vpselq_u8(mid, top, p_lt);
		bot = vpselq_u8(mid, bot, p_gt);
	}

	uint8x16_t med = mpix_avg_u8(top, bot);
	/* Store */
	vstrbq_scatter_offset_p_s8((int8_t *)out_base, vreinterpretq_s8_u8(offC), vreinterpretq_s8_u8(med), p0);
}

void mpix_median_rgb24_5x5(const uint8_t *in[5], uint8_t *out, uint16_t width)
{
	assert(width >= 5);

	const uint8_t *r0 = in[0];
	const uint8_t *r1 = in[1];
	const uint8_t *r2 = in[2];
	const uint8_t *r3 = in[3];
	const uint8_t *r4 = in[4];

	uint16_t x = 0;
	/* Need two right neighbors: ensure x+15+2 <= width-1 */
	for (; (uint32_t)x + 16u <= (uint32_t)(width - 2); x += 16) {
		uint8_t *out_base = out + (uint32_t)x * 3u;
		uint8x16_t offs3 = mpix_make_rgb_offsets();
		mve_pred16_t p0 = vctp8q(16);
		for (int ch = 0; ch < 3; ++ch) {
			mpix_median5x5_channel_block_with_pre(r0, r1, r2, r3, r4, out_base, x, 16, offs3, p0, ch);
		}
	}

	uint16_t remain = width - x;
	if (remain >= 3) {
		uint16_t blk = (uint16_t)(remain - 2);
		uint8_t *out_base = out + (uint32_t)x * 3u;
		uint8x16_t offs3 = mpix_make_rgb_offsets();
		mve_pred16_t p0 = vctp8q(blk & 0xFF);
		for (int ch = 0; ch < 3; ++ch) {
			mpix_median5x5_channel_block_with_pre(r0, r1, r2, r3, r4, out_base, x, blk, offs3, p0, ch);
		}
		x += blk;
		remain = width - x;
	}

	/* Last up to two pixels: scalar median-of-25 with clamp-to-edge horizontally */
	for (; remain > 0; --remain, ++x) {
		uint16_t xm2 = (x >= 2) ? (uint16_t)(x - 2) : 0;
		uint16_t xm1 = (x >= 1) ? (uint16_t)(x - 1) : 0;
		uint16_t xp1 = (x + 1 < width) ? (uint16_t)(x + 1) : (uint16_t)(width - 1);
		uint16_t xp2 = (x + 2 < width) ? (uint16_t)(x + 2) : (uint16_t)(width - 1);
		for (int ch = 0; ch < 3; ++ch) {
			uint8_t vals[25];
			int idx = 0;
			const uint8_t *rows[5] = { r0, r1, r2, r3, r4 };
			uint16_t xs[5] = { xm2, xm1, x, xp1, xp2 };
			for (int rr = 0; rr < 5; ++rr) {
				uint16_t xx = xs[rr];
				vals[idx++] = rows[0][xx*3+ch];
				vals[idx++] = rows[1][xx*3+ch];
				vals[idx++] = rows[2][xx*3+ch];
				vals[idx++] = rows[3][xx*3+ch];
				vals[idx++] = rows[4][xx*3+ch];
			}
			/* insertion sort */
			for (int i = 1; i < 25; ++i) {
				uint8_t key = vals[i];
				int j = i - 1;
				while (j >= 0 && vals[j] > key) { vals[j+1] = vals[j]; --j; }
				vals[j+1] = key;
			}
			out[x*3+ch] = vals[12];
		}
	}
}


