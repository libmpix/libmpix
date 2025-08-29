/* SPDX-License-Identifier: Apache-2.0 */

#include <assert.h>
#include <stdint.h>

#include <mpix/op_kernel.h>

#include "arm_mve.h"

/* Debug for 5x5 Gaussian was used during bring-up; code paths removed after stabilization. */

/*
 * SOA (planar) helpers
 * We convert an AoS RGB24 line block into three planar buffers (R,G,B) with guard padding,
 * so we can perform horizontal neighbor accesses with contiguous loads instead of repeated gathers.
 * This reduces gather count and improves memory throughput on MVE.
 */

/* Forward decl for existing helper defined below */
static inline uint8x16_t mpix_make_rgb_offsets(void);

static inline uint8x16_t mpix_make_inc_offsets(void)
{
	/* 0..15 */
	return vidupq_n_u8(0, 1);
}

/* Deinterleave a block of width blk from an AoS row (starting at pixel x_base)
 * into R/G/B planar buffers with 1-pixel guard on both sides (for 3x3 kernels).
 * pr/pg/pb must point to the first valid pixel (index 0), and we will also write
 * pr[-1], pr[blk] as clamped guards. Same for pg/pb. */
static inline void mpix_deint_row_soa_guard1(const uint8_t *row_base, uint16_t blk,
											 uint8_t *pr, uint8_t *pg, uint8_t *pb)
{
	uint8x16_t offs3 = mpix_make_rgb_offsets();
	uint8x16_t inc   = mpix_make_inc_offsets();
	mve_pred16_t p0 = vctp8q(blk & 0xFF);

	/* Gather center values for this block */
	uint8x16_t r = vldrbq_gather_offset_z_u8(row_base, offs3, p0);
	uint8x16_t g = vldrbq_gather_offset_z_u8(row_base, vaddq_n_u8(offs3, 1), p0);
	uint8x16_t b = vldrbq_gather_offset_z_u8(row_base, vaddq_n_u8(offs3, 2), p0);

	/* Store contiguously into planar buffers */
	vstrbq_scatter_offset_p_s8((int8_t *)pr, vreinterpretq_s8_u8(inc), vreinterpretq_s8_u8(r), p0);
	vstrbq_scatter_offset_p_s8((int8_t *)pg, vreinterpretq_s8_u8(inc), vreinterpretq_s8_u8(g), p0);
	vstrbq_scatter_offset_p_s8((int8_t *)pb, vreinterpretq_s8_u8(inc), vreinterpretq_s8_u8(b), p0);

	/* Scalar guard replication (first/last) */
	uint8_t first_r = pr[0];
	uint8_t first_g = pg[0];
	uint8_t first_b = pb[0];
	pr[-1] = first_r; pg[-1] = first_g; pb[-1] = first_b;
	uint8_t last_idx = (blk == 0) ? 0 : (uint8_t)(blk - 1);
	uint8_t last_r = pr[last_idx];
	uint8_t last_g = pg[last_idx];
	uint8_t last_b = pb[last_idx];
	pr[blk] = last_r; pg[blk] = last_g; pb[blk] = last_b;
}

/* Same as above but with 2-pixel guards (for 5x5 kernels). We write pr[-2], pr[-1], pr[blk], pr[blk+1]. */
static inline void mpix_deint_row_soa_guard2(const uint8_t *row_base, uint16_t blk,
											 uint8_t *pr, uint8_t *pg, uint8_t *pb)
{
	uint8x16_t offs3 = mpix_make_rgb_offsets();
	uint8x16_t inc   = mpix_make_inc_offsets();
	mve_pred16_t p0 = vctp8q(blk & 0xFF);

	uint8x16_t r = vldrbq_gather_offset_z_u8(row_base, offs3, p0);
	uint8x16_t g = vldrbq_gather_offset_z_u8(row_base, vaddq_n_u8(offs3, 1), p0);
	uint8x16_t b = vldrbq_gather_offset_z_u8(row_base, vaddq_n_u8(offs3, 2), p0);

	vstrbq_scatter_offset_p_s8((int8_t *)pr, vreinterpretq_s8_u8(inc), vreinterpretq_s8_u8(r), p0);
	vstrbq_scatter_offset_p_s8((int8_t *)pg, vreinterpretq_s8_u8(inc), vreinterpretq_s8_u8(g), p0);
	vstrbq_scatter_offset_p_s8((int8_t *)pb, vreinterpretq_s8_u8(inc), vreinterpretq_s8_u8(b), p0);

	/* Guards: replicate two left and two right pixels */
	uint8_t r0 = pr[0], r1 = pr[(blk>1)?1:0];
	uint8_t g0 = pg[0], g1 = pg[(blk>1)?1:0];
	uint8_t b0 = pb[0], b1 = pb[(blk>1)?1:0];
	pr[-2] = r0; pr[-1] = r0; pg[-2] = g0; pg[-1] = g0; pb[-2] = b0; pb[-1] = b0;
	uint8_t last = (blk == 0) ? 0 : (uint8_t)(blk - 1);
	uint8_t lastm1 = (blk >= 2) ? (uint8_t)(blk - 2) : last;
	pr[blk] = pr[last]; pr[blk+1] = pr[last];
	pg[blk] = pg[last]; pg[blk+1] = pg[last];
	pb[blk] = pb[last]; pb[blk+1] = pb[last];
	(void)r1; (void)g1; (void)b1; /* silence unused (kept for clarity) */
}

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
		const uint16_t blk = 16;
		uint8_t *out_base = out + (uint32_t)x * 3u;
		mve_pred16_t p0 = vctp8q(blk & 0xFF);
		uint8x16_t inc = mpix_make_inc_offsets();
		uint8x16_t offs3 = mpix_make_rgb_offsets();

		uint8_t r0p[16 + 2], g0p[16 + 2], b0p[16 + 2];
		uint8_t r1p[16 + 2], g1p[16 + 2], b1p[16 + 2];
		uint8_t r2p[16 + 2], g2p[16 + 2], b2p[16 + 2];
		mpix_deint_row_soa_guard1(row0 + (uint32_t)x * 3u, blk, &r0p[1], &g0p[1], &b0p[1]);
		mpix_deint_row_soa_guard1(row1 + (uint32_t)x * 3u, blk, &r1p[1], &g1p[1], &b1p[1]);
		mpix_deint_row_soa_guard1(row2 + (uint32_t)x * 3u, blk, &r2p[1], &g2p[1], &b2p[1]);

		for (int ch = 0; ch < 3; ++ch) {
			const uint8_t *u = (ch==0)? &r0p[1] : (ch==1)? &g0p[1] : &b0p[1];
			const uint8_t *c = (ch==0)? &r1p[1] : (ch==1)? &g1p[1] : &b1p[1];
			const uint8_t *d = (ch==0)? &r2p[1] : (ch==1)? &g2p[1] : &b2p[1];
			uint8x16_t left  = vldrbq_gather_offset_z_u8(c - 1, inc, p0);
			uint8x16_t cen   = vldrbq_gather_offset_z_u8(c + 0, inc, p0);
			uint8x16_t right = vldrbq_gather_offset_z_u8(c + 1, inc, p0);
			uint8x16_t up    = vldrbq_gather_offset_z_u8(u + 0, inc, p0);
			uint8x16_t down  = vldrbq_gather_offset_z_u8(d + 0, inc, p0);

			uint16x8_t cen_lo = vshlq_n_u16(vmovlbq_u8(cen), 3);
			uint16x8_t cen_hi = vshlq_n_u16(vmovltq_u8(cen), 3);
			uint16x8_t neigh_lo = vaddq_u16(vaddq_u16(vmovlbq_u8(left), vmovlbq_u8(right)), vaddq_u16(vmovlbq_u8(up), vmovlbq_u8(down)));
			uint16x8_t neigh_hi = vaddq_u16(vaddq_u16(vmovltq_u8(left), vmovltq_u8(right)), vaddq_u16(vmovltq_u8(up), vmovltq_u8(down)));
			uint16x8_t res_lo = vqsubq_u16(cen_lo, neigh_lo);
			uint16x8_t res_hi = vqsubq_u16(cen_hi, neigh_hi);
			uint8x16_t outv = vdupq_n_u8(0);
			outv = vqmovnbq_u16(outv, res_lo);
			outv = vqmovntq_u16(outv, res_hi);
			uint8x16_t chOffs = vaddq_n_u8(offs3, (uint8_t)ch);
			vstrbq_scatter_offset_p_s8((int8_t *)out_base, vreinterpretq_s8_u8(chOffs), vreinterpretq_s8_u8(outv), p0);
		}
	}
	uint16_t remain = width - x;
	if (remain >= 2) {
		uint16_t blk = (uint16_t)(remain - 1);
		uint8_t *out_base = out + (uint32_t)x * 3u;
		mve_pred16_t p0 = vctp8q(blk & 0xFF);
		uint8x16_t inc = mpix_make_inc_offsets();
		uint8x16_t offs3 = mpix_make_rgb_offsets();
		uint8_t r0p[16 + 2], g0p[16 + 2], b0p[16 + 2];
		uint8_t r1p[16 + 2], g1p[16 + 2], b1p[16 + 2];
		uint8_t r2p[16 + 2], g2p[16 + 2], b2p[16 + 2];
		mpix_deint_row_soa_guard1(row0 + (uint32_t)x * 3u, blk, &r0p[1], &g0p[1], &b0p[1]);
		mpix_deint_row_soa_guard1(row1 + (uint32_t)x * 3u, blk, &r1p[1], &g1p[1], &b1p[1]);
		mpix_deint_row_soa_guard1(row2 + (uint32_t)x * 3u, blk, &r2p[1], &g2p[1], &b2p[1]);
		for (int ch = 0; ch < 3; ++ch) {
			const uint8_t *u = (ch==0)? &r0p[1] : (ch==1)? &g0p[1] : &b0p[1];
			const uint8_t *c = (ch==0)? &r1p[1] : (ch==1)? &g1p[1] : &b1p[1];
			const uint8_t *d = (ch==0)? &r2p[1] : (ch==1)? &g2p[1] : &b2p[1];
			uint8x16_t left  = vldrbq_gather_offset_z_u8(c - 1, inc, p0);
			uint8x16_t cen   = vldrbq_gather_offset_z_u8(c + 0, inc, p0);
			uint8x16_t right = vldrbq_gather_offset_z_u8(c + 1, inc, p0);
			uint8x16_t up    = vldrbq_gather_offset_z_u8(u + 0, inc, p0);
			uint8x16_t down  = vldrbq_gather_offset_z_u8(d + 0, inc, p0);
			uint16x8_t cen_lo = vshlq_n_u16(vmovlbq_u8(cen), 3);
			uint16x8_t cen_hi = vshlq_n_u16(vmovltq_u8(cen), 3);
			uint16x8_t neigh_lo = vaddq_u16(vaddq_u16(vmovlbq_u8(left), vmovlbq_u8(right)), vaddq_u16(vmovlbq_u8(up), vmovlbq_u8(down)));
			uint16x8_t neigh_hi = vaddq_u16(vaddq_u16(vmovltq_u8(left), vmovltq_u8(right)), vaddq_u16(vmovltq_u8(up), vmovltq_u8(down)));
			uint16x8_t res_lo = vqsubq_u16(cen_lo, neigh_lo);
			uint16x8_t res_hi = vqsubq_u16(cen_hi, neigh_hi);
			uint8x16_t outv = vdupq_n_u8(0);
			outv = vqmovnbq_u16(outv, res_lo);
			outv = vqmovntq_u16(outv, res_hi);
			uint8x16_t chOffs = vaddq_n_u8(offs3, (uint8_t)ch);
			vstrbq_scatter_offset_p_s8((int8_t *)out_base, vreinterpretq_s8_u8(chOffs), vreinterpretq_s8_u8(outv), p0);
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
	/* Process vector blocks; we emit last pixel scalar as before for simplicity */
	for (; (uint32_t)x + 16u <= (uint32_t)(width - 1); x += 16) {
		const uint16_t blk = 16;
		uint8_t *out_base = out + (uint32_t)x * 3u;
		mve_pred16_t p0 = vctp8q(blk & 0xFF);
		uint8x16_t inc = mpix_make_inc_offsets();
		uint8x16_t offs3 = mpix_make_rgb_offsets();

		/* Deinterleave three rows to SOA with guard1 */
		uint8_t r0[16 + 2], g0[16 + 2], b0[16 + 2];
		uint8_t r1p[16 + 2], g1p[16 + 2], b1p[16 + 2];
		uint8_t r2p[16 + 2], g2p[16 + 2], b2p[16 + 2];
		mpix_deint_row_soa_guard1(row0 + (uint32_t)x * 3u, blk, &r0[1], &g0[1], &b0[1]);
		mpix_deint_row_soa_guard1(row1 + (uint32_t)x * 3u, blk, &r1p[1], &g1p[1], &b1p[1]);
		mpix_deint_row_soa_guard1(row2 + (uint32_t)x * 3u, blk, &r2p[1], &g2p[1], &b2p[1]);

		for (int ch = 0; ch < 3; ++ch) {
			const uint8_t *p0c = (ch==0)? &r0[1] : (ch==1)? &g0[1] : &b0[1];
			const uint8_t *p1c = (ch==0)? &r1p[1] : (ch==1)? &g1p[1] : &b1p[1];
			const uint8_t *p2c = (ch==0)? &r2p[1] : (ch==1)? &g2p[1] : &b2p[1];

			/* contiguous loads via inc offsets */
			uint8x16_t l0 = vldrbq_gather_offset_z_u8(p0c - 1, inc, p0);
			uint8x16_t c0 = vldrbq_gather_offset_z_u8(p0c + 0, inc, p0);
			uint8x16_t r0v= vldrbq_gather_offset_z_u8(p0c + 1, inc, p0);
			uint8x16_t l1 = vldrbq_gather_offset_z_u8(p1c - 1, inc, p0);
			uint8x16_t c1 = vldrbq_gather_offset_z_u8(p1c + 0, inc, p0);
			uint8x16_t r1v= vldrbq_gather_offset_z_u8(p1c + 1, inc, p0);
			uint8x16_t l2 = vldrbq_gather_offset_z_u8(p2c - 1, inc, p0);
			uint8x16_t c2 = vldrbq_gather_offset_z_u8(p2c + 0, inc, p0);
			uint8x16_t r2v= vldrbq_gather_offset_z_u8(p2c + 1, inc, p0);

			uint16x8_t h0_lo = mpix_horz_121_lo(l0, c0, r0v);
			uint16x8_t h0_hi = mpix_horz_121_hi(l0, c0, r0v);
			uint16x8_t h1_lo = mpix_horz_121_lo(l1, c1, r1v);
			uint16x8_t h1_hi = mpix_horz_121_hi(l1, c1, r1v);
			uint16x8_t h2_lo = mpix_horz_121_lo(l2, c2, r2v);
			uint16x8_t h2_hi = mpix_horz_121_hi(l2, c2, r2v);

			uint16x8_t sum_lo = vaddq_u16(h0_lo, h2_lo);
			sum_lo = vaddq_u16(sum_lo, vaddq_u16(h1_lo, h1_lo));
			sum_lo = vaddq_u16(sum_lo, vdupq_n_u16(8));
			uint16x8_t sum_hi = vaddq_u16(h0_hi, h2_hi);
			sum_hi = vaddq_u16(sum_hi, vaddq_u16(h1_hi, h1_hi));
			sum_hi = vaddq_u16(sum_hi, vdupq_n_u16(8));

			uint8x16_t outv = vdupq_n_u8(0);
			outv = vshrnbq_n_u16(outv, sum_lo, 4);
			outv = vshrntq_n_u16(outv, sum_hi, 4);

			uint8x16_t chOffs = vaddq_n_u8(offs3, (uint8_t)ch);
			vstrbq_scatter_offset_p_s8((int8_t *)out_base, vreinterpretq_s8_u8(chOffs), vreinterpretq_s8_u8(outv), p0);
		}
	}

	/* Tail handling: vectorize remain-1, then 1 scalar pixel */
	uint16_t remain = width - x;
	if (remain >= 2) {
		uint16_t blk = (uint16_t)(remain - 1);
		uint8_t *out_base = out + (uint32_t)x * 3u;
		mve_pred16_t p0 = vctp8q(blk & 0xFF);
		uint8x16_t inc = mpix_make_inc_offsets();
		uint8x16_t offs3 = mpix_make_rgb_offsets();

		uint8_t r0[16 + 2], g0[16 + 2], b0[16 + 2];
		uint8_t r1p[16 + 2], g1p[16 + 2], b1p[16 + 2];
		uint8_t r2p[16 + 2], g2p[16 + 2], b2p[16 + 2];
		mpix_deint_row_soa_guard1(row0 + (uint32_t)x * 3u, blk, &r0[1], &g0[1], &b0[1]);
		mpix_deint_row_soa_guard1(row1 + (uint32_t)x * 3u, blk, &r1p[1], &g1p[1], &b1p[1]);
		mpix_deint_row_soa_guard1(row2 + (uint32_t)x * 3u, blk, &r2p[1], &g2p[1], &b2p[1]);

		for (int ch = 0; ch < 3; ++ch) {
			const uint8_t *p0c = (ch==0)? &r0[1] : (ch==1)? &g0[1] : &b0[1];
			const uint8_t *p1c = (ch==0)? &r1p[1] : (ch==1)? &g1p[1] : &b1p[1];
			const uint8_t *p2c = (ch==0)? &r2p[1] : (ch==1)? &g2p[1] : &b2p[1];

			uint8x16_t l0 = vldrbq_gather_offset_z_u8(p0c - 1, inc, p0);
			uint8x16_t c0 = vldrbq_gather_offset_z_u8(p0c + 0, inc, p0);
			uint8x16_t r0v= vldrbq_gather_offset_z_u8(p0c + 1, inc, p0);
			uint8x16_t l1 = vldrbq_gather_offset_z_u8(p1c - 1, inc, p0);
			uint8x16_t c1 = vldrbq_gather_offset_z_u8(p1c + 0, inc, p0);
			uint8x16_t r1v= vldrbq_gather_offset_z_u8(p1c + 1, inc, p0);
			uint8x16_t l2 = vldrbq_gather_offset_z_u8(p2c - 1, inc, p0);
			uint8x16_t c2 = vldrbq_gather_offset_z_u8(p2c + 0, inc, p0);
			uint8x16_t r2v= vldrbq_gather_offset_z_u8(p2c + 1, inc, p0);

			uint16x8_t h0_lo = mpix_horz_121_lo(l0, c0, r0v);
			uint16x8_t h0_hi = mpix_horz_121_hi(l0, c0, r0v);
			uint16x8_t h1_lo = mpix_horz_121_lo(l1, c1, r1v);
			uint16x8_t h1_hi = mpix_horz_121_hi(l1, c1, r1v);
			uint16x8_t h2_lo = mpix_horz_121_lo(l2, c2, r2v);
			uint16x8_t h2_hi = mpix_horz_121_hi(l2, c2, r2v);

			uint16x8_t sum_lo = vaddq_u16(h0_lo, h2_lo);
			sum_lo = vaddq_u16(sum_lo, vaddq_u16(h1_lo, h1_lo));
			sum_lo = vaddq_u16(sum_lo, vdupq_n_u16(8));
			uint16x8_t sum_hi = vaddq_u16(h0_hi, h2_hi);
			sum_hi = vaddq_u16(sum_hi, vaddq_u16(h1_hi, h1_hi));
			sum_hi = vaddq_u16(sum_hi, vdupq_n_u16(8));

			uint8x16_t outv = vdupq_n_u8(0);
			outv = vshrnbq_n_u16(outv, sum_lo, 4);
			outv = vshrntq_n_u16(outv, sum_hi, 4);

			uint8x16_t chOffs = vaddq_n_u8(offs3, (uint8_t)ch);
			vstrbq_scatter_offset_p_s8((int8_t *)out_base, vreinterpretq_s8_u8(chOffs), vreinterpretq_s8_u8(outv), p0);
		}

		x += blk;
		remain = width - x;
	}

	if (remain == 1) {
		uint16_t xm1 = (x > 0) ? (uint16_t)(x - 1) : x;
		uint16_t xp1 = x;
		for (int ch = 0; ch < 3; ++ch) {
			uint32_t s = 0;
			uint8_t a0 = row0[xm1 * 3 + ch];
			uint8_t b0 = row0[x * 3 + ch];
			uint8_t c0 = row0[xp1 * 3 + ch];
			s += (uint32_t)a0 + ((uint32_t)b0 << 1) + (uint32_t)c0;
			uint8_t a1 = row1[xm1 * 3 + ch];
			uint8_t b1 = row1[x * 3 + ch];
			uint8_t c1 = row1[xp1 * 3 + ch];
			s += (((uint32_t)a1 + ((uint32_t)b1 << 1) + (uint32_t)c1) << 1);
			uint8_t a2 = row2[xm1 * 3 + ch];
			uint8_t b2 = row2[x * 3 + ch];
			uint8_t c2 = row2[xp1 * 3 + ch];
			s += (uint32_t)a2 + ((uint32_t)b2 << 1) + (uint32_t)c2;
			s += 8u; s >>= 4;
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
		const uint16_t blk = 16;
		uint8_t *out_base = out + (uint32_t)x * 3u;
		mve_pred16_t p0 = vctp8q(blk & 0xFF);
		uint8x16_t inc = mpix_make_inc_offsets();
		uint8x16_t offs3 = mpix_make_rgb_offsets();

		/* Deinterleave 5 rows with guard2 */
		uint8_t r0p[16 + 4], g0p[16 + 4], b0p[16 + 4];
		uint8_t r1p[16 + 4], g1p[16 + 4], b1p[16 + 4];
		uint8_t r2p[16 + 4], g2p[16 + 4], b2p[16 + 4];
		uint8_t r3p[16 + 4], g3p[16 + 4], b3p[16 + 4];
		uint8_t r4p[16 + 4], g4p[16 + 4], b4p[16 + 4];
		mpix_deint_row_soa_guard2(r0 + (uint32_t)x * 3u, blk, &r0p[2], &g0p[2], &b0p[2]);
		mpix_deint_row_soa_guard2(r1 + (uint32_t)x * 3u, blk, &r1p[2], &g1p[2], &b1p[2]);
		mpix_deint_row_soa_guard2(r2 + (uint32_t)x * 3u, blk, &r2p[2], &g2p[2], &b2p[2]);
		mpix_deint_row_soa_guard2(r3 + (uint32_t)x * 3u, blk, &r3p[2], &g3p[2], &b3p[2]);
		mpix_deint_row_soa_guard2(r4 + (uint32_t)x * 3u, blk, &r4p[2], &g4p[2], &b4p[2]);

		for (int ch = 0; ch < 3; ++ch) {
			const uint8_t *a = (ch==0)? &r0p[2] : (ch==1)? &g0p[2] : &b0p[2];
			const uint8_t *b = (ch==0)? &r1p[2] : (ch==1)? &g1p[2] : &b1p[2];
			const uint8_t *c = (ch==0)? &r2p[2] : (ch==1)? &g2p[2] : &b2p[2];
			const uint8_t *d = (ch==0)? &r3p[2] : (ch==1)? &g3p[2] : &b3p[2];
			const uint8_t *e = (ch==0)? &r4p[2] : (ch==1)? &g4p[2] : &b4p[2];

			/* Horizontal 1-4-6-4-1 per row using 5 overlapping contiguous loads */
			uint8x16_t a0 = vldrbq_gather_offset_z_u8(a - 2, inc, p0);
			uint8x16_t b0 = vldrbq_gather_offset_z_u8(a - 1, inc, p0);
			uint8x16_t c0 = vldrbq_gather_offset_z_u8(a + 0, inc, p0);
			uint8x16_t d0 = vldrbq_gather_offset_z_u8(a + 1, inc, p0);
			uint8x16_t e0 = vldrbq_gather_offset_z_u8(a + 2, inc, p0);

			uint8x16_t a1 = vldrbq_gather_offset_z_u8(b - 2, inc, p0);
			uint8x16_t b1 = vldrbq_gather_offset_z_u8(b - 1, inc, p0);
			uint8x16_t c1 = vldrbq_gather_offset_z_u8(b + 0, inc, p0);
			uint8x16_t d1 = vldrbq_gather_offset_z_u8(b + 1, inc, p0);
			uint8x16_t e1 = vldrbq_gather_offset_z_u8(b + 2, inc, p0);

			uint8x16_t a2 = vldrbq_gather_offset_z_u8(c - 2, inc, p0);
			uint8x16_t b2 = vldrbq_gather_offset_z_u8(c - 1, inc, p0);
			uint8x16_t c2 = vldrbq_gather_offset_z_u8(c + 0, inc, p0);
			uint8x16_t d2 = vldrbq_gather_offset_z_u8(c + 1, inc, p0);
			uint8x16_t e2 = vldrbq_gather_offset_z_u8(c + 2, inc, p0);

			uint8x16_t a3 = vldrbq_gather_offset_z_u8(d - 2, inc, p0);
			uint8x16_t b3 = vldrbq_gather_offset_z_u8(d - 1, inc, p0);
			uint8x16_t c3 = vldrbq_gather_offset_z_u8(d + 0, inc, p0);
			uint8x16_t d3 = vldrbq_gather_offset_z_u8(d + 1, inc, p0);
			uint8x16_t e3 = vldrbq_gather_offset_z_u8(d + 2, inc, p0);

			uint8x16_t a4 = vldrbq_gather_offset_z_u8(e - 2, inc, p0);
			uint8x16_t b4 = vldrbq_gather_offset_z_u8(e - 1, inc, p0);
			uint8x16_t c4 = vldrbq_gather_offset_z_u8(e + 0, inc, p0);
			uint8x16_t d4 = vldrbq_gather_offset_z_u8(e + 1, inc, p0);
			uint8x16_t e4 = vldrbq_gather_offset_z_u8(e + 2, inc, p0);

			/* Use existing H121 macros logic by reusing helpers inline */
#define H121_ROW_LOA(a,b,c,d,e) ({ \
	uint16x8_t _ae = vaddq_u16(vmovlbq_u8(a), vmovlbq_u8(e)); \
	uint16x8_t _bd = vaddq_u16(vmovlbq_u8(b), vmovlbq_u8(d)); \
	uint16x8_t _c  = vmovlbq_u8(c); \
	uint16x8_t _sum = vaddq_u16(_ae, vshlq_n_u16(_bd, 2)); \
	_sum = vaddq_u16(_sum, vaddq_u16(vshlq_n_u16(_c, 2), vshlq_n_u16(_c, 1))); \
	_sum; })
#define H121_ROW_HIA(a,b,c,d,e) ({ \
	uint16x8_t _ae = vaddq_u16(vmovltq_u8(a), vmovltq_u8(e)); \
	uint16x8_t _bd = vaddq_u16(vmovltq_u8(b), vmovltq_u8(d)); \
	uint16x8_t _c  = vmovltq_u8(c); \
	uint16x8_t _sum = vaddq_u16(_ae, vshlq_n_u16(_bd, 2)); \
	_sum = vaddq_u16(_sum, vaddq_u16(vshlq_n_u16(_c, 2), vshlq_n_u16(_c, 1))); \
	_sum; })

			uint16x8_t h0_lo = H121_ROW_LOA(a0,b0,c0,d0,e0);
			uint16x8_t h0_hi = H121_ROW_HIA(a0,b0,c0,d0,e0);
			uint16x8_t h1_lo = H121_ROW_LOA(a1,b1,c1,d1,e1);
			uint16x8_t h1_hi = H121_ROW_HIA(a1,b1,c1,d1,e1);
			uint16x8_t h2_lo = H121_ROW_LOA(a2,b2,c2,d2,e2);
			uint16x8_t h2_hi = H121_ROW_HIA(a2,b2,c2,d2,e2);
			uint16x8_t h3_lo = H121_ROW_LOA(a3,b3,c3,d3,e3);
			uint16x8_t h3_hi = H121_ROW_HIA(a3,b3,c3,d3,e3);
			uint16x8_t h4_lo = H121_ROW_LOA(a4,b4,c4,d4,e4);
			uint16x8_t h4_hi = H121_ROW_HIA(a4,b4,c4,d4,e4);

			uint16x8_t v_lo = vaddq_u16(vaddq_u16(h0_lo, vshlq_n_u16(h1_lo, 2)), vaddq_u16(vshlq_n_u16(h2_lo, 2), vshlq_n_u16(h2_lo, 1)));
			v_lo = vaddq_u16(v_lo, vshlq_n_u16(h3_lo, 2));
			v_lo = vaddq_u16(v_lo, h4_lo);
			v_lo = vaddq_u16(v_lo, vdupq_n_u16(128));

			uint16x8_t v_hi = vaddq_u16(vaddq_u16(h0_hi, vshlq_n_u16(h1_hi, 2)), vaddq_u16(vshlq_n_u16(h2_hi, 2), vshlq_n_u16(h2_hi, 1)));
			v_hi = vaddq_u16(v_hi, vshlq_n_u16(h3_hi, 2));
			v_hi = vaddq_u16(v_hi, h4_hi);
			v_hi = vaddq_u16(v_hi, vdupq_n_u16(128));

			uint8x16_t outv = vdupq_n_u8(0);
			outv = vshrnbq_n_u16(outv, v_lo, 8);
			outv = vshrntq_n_u16(outv, v_hi, 8);

			uint8x16_t chOffs = vaddq_n_u8(offs3, (uint8_t)ch);
			vstrbq_scatter_offset_p_s8((int8_t *)out_base, vreinterpretq_s8_u8(chOffs), vreinterpretq_s8_u8(outv), p0);

#undef H121_ROW_LOA
#undef H121_ROW_HIA
		}
	}

	uint16_t remain = width - x;
	if (remain >= 3) {
		uint16_t blk = (uint16_t)(remain - 2);
		uint8_t *out_base = out + (uint32_t)x * 3u;
		mve_pred16_t p0 = vctp8q(blk & 0xFF);
		uint8x16_t inc = mpix_make_inc_offsets();
		uint8x16_t offs3 = mpix_make_rgb_offsets();

		uint8_t r0p[16 + 4], g0p[16 + 4], b0p[16 + 4];
		uint8_t r1p[16 + 4], g1p[16 + 4], b1p[16 + 4];
		uint8_t r2p[16 + 4], g2p[16 + 4], b2p[16 + 4];
		uint8_t r3p[16 + 4], g3p[16 + 4], b3p[16 + 4];
		uint8_t r4p[16 + 4], g4p[16 + 4], b4p[16 + 4];
		mpix_deint_row_soa_guard2(r0 + (uint32_t)x * 3u, blk, &r0p[2], &g0p[2], &b0p[2]);
		mpix_deint_row_soa_guard2(r1 + (uint32_t)x * 3u, blk, &r1p[2], &g1p[2], &b1p[2]);
		mpix_deint_row_soa_guard2(r2 + (uint32_t)x * 3u, blk, &r2p[2], &g2p[2], &b2p[2]);
		mpix_deint_row_soa_guard2(r3 + (uint32_t)x * 3u, blk, &r3p[2], &g3p[2], &b3p[2]);
		mpix_deint_row_soa_guard2(r4 + (uint32_t)x * 3u, blk, &r4p[2], &g4p[2], &b4p[2]);

		for (int ch = 0; ch < 3; ++ch) {
			const uint8_t *a = (ch==0)? &r0p[2] : (ch==1)? &g0p[2] : &b0p[2];
			const uint8_t *b = (ch==0)? &r1p[2] : (ch==1)? &g1p[2] : &b1p[2];
			const uint8_t *c = (ch==0)? &r2p[2] : (ch==1)? &g2p[2] : &b2p[2];
			const uint8_t *d = (ch==0)? &r3p[2] : (ch==1)? &g3p[2] : &b3p[2];
			const uint8_t *e = (ch==0)? &r4p[2] : (ch==1)? &g4p[2] : &b4p[2];

			uint8x16_t a0 = vldrbq_gather_offset_z_u8(a - 2, inc, p0);
			uint8x16_t b0 = vldrbq_gather_offset_z_u8(a - 1, inc, p0);
			uint8x16_t c0 = vldrbq_gather_offset_z_u8(a + 0, inc, p0);
			uint8x16_t d0 = vldrbq_gather_offset_z_u8(a + 1, inc, p0);
			uint8x16_t e0 = vldrbq_gather_offset_z_u8(a + 2, inc, p0);

			uint8x16_t a1 = vldrbq_gather_offset_z_u8(b - 2, inc, p0);
			uint8x16_t b1 = vldrbq_gather_offset_z_u8(b - 1, inc, p0);
			uint8x16_t c1 = vldrbq_gather_offset_z_u8(b + 0, inc, p0);
			uint8x16_t d1 = vldrbq_gather_offset_z_u8(b + 1, inc, p0);
			uint8x16_t e1 = vldrbq_gather_offset_z_u8(b + 2, inc, p0);

			uint8x16_t a2 = vldrbq_gather_offset_z_u8(c - 2, inc, p0);
			uint8x16_t b2 = vldrbq_gather_offset_z_u8(c - 1, inc, p0);
			uint8x16_t c2 = vldrbq_gather_offset_z_u8(c + 0, inc, p0);
			uint8x16_t d2 = vldrbq_gather_offset_z_u8(c + 1, inc, p0);
			uint8x16_t e2 = vldrbq_gather_offset_z_u8(c + 2, inc, p0);

			uint8x16_t a3 = vldrbq_gather_offset_z_u8(d - 2, inc, p0);
			uint8x16_t b3 = vldrbq_gather_offset_z_u8(d - 1, inc, p0);
			uint8x16_t c3 = vldrbq_gather_offset_z_u8(d + 0, inc, p0);
			uint8x16_t d3 = vldrbq_gather_offset_z_u8(d + 1, inc, p0);
			uint8x16_t e3 = vldrbq_gather_offset_z_u8(d + 2, inc, p0);

			uint8x16_t a4 = vldrbq_gather_offset_z_u8(e - 2, inc, p0);
			uint8x16_t b4 = vldrbq_gather_offset_z_u8(e - 1, inc, p0);
			uint8x16_t c4 = vldrbq_gather_offset_z_u8(e + 0, inc, p0);
			uint8x16_t d4 = vldrbq_gather_offset_z_u8(e + 1, inc, p0);
			uint8x16_t e4 = vldrbq_gather_offset_z_u8(e + 2, inc, p0);

			uint16x8_t h0_lo = vaddq_u16(vaddq_u16(vmovlbq_u8(a0), vmovlbq_u8(e0)), vshlq_n_u16(vaddq_u16(vmovlbq_u8(b0), vmovlbq_u8(d0)), 2));
			h0_lo = vaddq_u16(h0_lo, vaddq_u16(vshlq_n_u16(vmovlbq_u8(c0), 2), vshlq_n_u16(vmovlbq_u8(c0), 1)));
			uint16x8_t h0_hi = vaddq_u16(vaddq_u16(vmovltq_u8(a0), vmovltq_u8(e0)), vshlq_n_u16(vaddq_u16(vmovltq_u8(b0), vmovltq_u8(d0)), 2));
			h0_hi = vaddq_u16(h0_hi, vaddq_u16(vshlq_n_u16(vmovltq_u8(c0), 2), vshlq_n_u16(vmovltq_u8(c0), 1)));

			uint16x8_t h1_lo = vaddq_u16(vaddq_u16(vmovlbq_u8(a1), vmovlbq_u8(e1)), vshlq_n_u16(vaddq_u16(vmovlbq_u8(b1), vmovlbq_u8(d1)), 2));
			h1_lo = vaddq_u16(h1_lo, vaddq_u16(vshlq_n_u16(vmovlbq_u8(c1), 2), vshlq_n_u16(vmovlbq_u8(c1), 1)));
			uint16x8_t h1_hi = vaddq_u16(vaddq_u16(vmovltq_u8(a1), vmovltq_u8(e1)), vshlq_n_u16(vaddq_u16(vmovltq_u8(b1), vmovltq_u8(d1)), 2));
			h1_hi = vaddq_u16(h1_hi, vaddq_u16(vshlq_n_u16(vmovltq_u8(c1), 2), vshlq_n_u16(vmovltq_u8(c1), 1)));

			uint16x8_t h2_lo = vaddq_u16(vaddq_u16(vmovlbq_u8(a2), vmovlbq_u8(e2)), vshlq_n_u16(vaddq_u16(vmovlbq_u8(b2), vmovlbq_u8(d2)), 2));
			h2_lo = vaddq_u16(h2_lo, vaddq_u16(vshlq_n_u16(vmovlbq_u8(c2), 2), vshlq_n_u16(vmovlbq_u8(c2), 1)));
			uint16x8_t h2_hi = vaddq_u16(vaddq_u16(vmovltq_u8(a2), vmovltq_u8(e2)), vshlq_n_u16(vaddq_u16(vmovltq_u8(b2), vmovltq_u8(d2)), 2));
			h2_hi = vaddq_u16(h2_hi, vaddq_u16(vshlq_n_u16(vmovltq_u8(c2), 2), vshlq_n_u16(vmovltq_u8(c2), 1)));

			uint16x8_t h3_lo = vaddq_u16(vaddq_u16(vmovlbq_u8(a3), vmovlbq_u8(e3)), vshlq_n_u16(vaddq_u16(vmovlbq_u8(b3), vmovlbq_u8(d3)), 2));
			h3_lo = vaddq_u16(h3_lo, vaddq_u16(vshlq_n_u16(vmovlbq_u8(c3), 2), vshlq_n_u16(vmovlbq_u8(c3), 1)));
			uint16x8_t h3_hi = vaddq_u16(vaddq_u16(vmovltq_u8(a3), vmovltq_u8(e3)), vshlq_n_u16(vaddq_u16(vmovltq_u8(b3), vmovltq_u8(d3)), 2));
			h3_hi = vaddq_u16(h3_hi, vaddq_u16(vshlq_n_u16(vmovltq_u8(c3), 2), vshlq_n_u16(vmovltq_u8(c3), 1)));

			uint16x8_t h4_lo = vaddq_u16(vaddq_u16(vmovlbq_u8(a4), vmovlbq_u8(e4)), vshlq_n_u16(vaddq_u16(vmovlbq_u8(b4), vmovlbq_u8(d4)), 2));
			h4_lo = vaddq_u16(h4_lo, vaddq_u16(vshlq_n_u16(vmovlbq_u8(c4), 2), vshlq_n_u16(vmovlbq_u8(c4), 1)));
			uint16x8_t h4_hi = vaddq_u16(vaddq_u16(vmovltq_u8(a4), vmovltq_u8(e4)), vshlq_n_u16(vaddq_u16(vmovltq_u8(b4), vmovltq_u8(d4)), 2));
			h4_hi = vaddq_u16(h4_hi, vaddq_u16(vshlq_n_u16(vmovltq_u8(c4), 2), vshlq_n_u16(vmovltq_u8(c4), 1)));

			uint16x8_t v_lo = vaddq_u16(vaddq_u16(h0_lo, vshlq_n_u16(h1_lo, 2)), vaddq_u16(vshlq_n_u16(h2_lo, 2), vshlq_n_u16(h2_lo, 1)));
			v_lo = vaddq_u16(v_lo, vshlq_n_u16(h3_lo, 2));
			v_lo = vaddq_u16(v_lo, h4_lo);
			v_lo = vaddq_u16(v_lo, vdupq_n_u16(128));
			uint16x8_t v_hi = vaddq_u16(vaddq_u16(h0_hi, vshlq_n_u16(h1_hi, 2)), vaddq_u16(vshlq_n_u16(h2_hi, 2), vshlq_n_u16(h2_hi, 1)));
			v_hi = vaddq_u16(v_hi, vshlq_n_u16(h3_hi, 2));
			v_hi = vaddq_u16(v_hi, h4_hi);
			v_hi = vaddq_u16(v_hi, vdupq_n_u16(128));

			uint8x16_t outv = vdupq_n_u8(0);
			outv = vshrnbq_n_u16(outv, v_lo, 8);
			outv = vshrntq_n_u16(outv, v_hi, 8);

			uint8x16_t chOffs = vaddq_n_u8(offs3, (uint8_t)ch);
			vstrbq_scatter_offset_p_s8((int8_t *)out_base, vreinterpretq_s8_u8(chOffs), vreinterpretq_s8_u8(outv), p0);
		}

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
		const uint16_t blk = 16;
		uint8_t *out_base = out + (uint32_t)x * 3u;
		mve_pred16_t p0 = vctp8q(blk & 0xFF);
		uint8x16_t inc = mpix_make_inc_offsets();
		uint8x16_t offs3 = mpix_make_rgb_offsets();
		uint8_t r0p[16 + 2], g0p[16 + 2], b0p[16 + 2];
		uint8_t r1p[16 + 2], g1p[16 + 2], b1p[16 + 2];
		uint8_t r2p[16 + 2], g2p[16 + 2], b2p[16 + 2];
		mpix_deint_row_soa_guard1(row0 + (uint32_t)x * 3u, blk, &r0p[1], &g0p[1], &b0p[1]);
		mpix_deint_row_soa_guard1(row1 + (uint32_t)x * 3u, blk, &r1p[1], &g1p[1], &b1p[1]);
		mpix_deint_row_soa_guard1(row2 + (uint32_t)x * 3u, blk, &r2p[1], &g2p[1], &b2p[1]);

		for (int ch = 0; ch < 3; ++ch) {
			const uint8_t *u = (ch==0)? &r0p[1] : (ch==1)? &g0p[1] : &b0p[1];
			const uint8_t *c = (ch==0)? &r1p[1] : (ch==1)? &g1p[1] : &b1p[1];
			const uint8_t *d = (ch==0)? &r2p[1] : (ch==1)? &g2p[1] : &b2p[1];
			uint8x16_t left  = vldrbq_gather_offset_z_u8(c - 1, inc, p0);
			uint8x16_t cen   = vldrbq_gather_offset_z_u8(c + 0, inc, p0);
			uint8x16_t right = vldrbq_gather_offset_z_u8(c + 1, inc, p0);
			uint8x16_t up    = vldrbq_gather_offset_z_u8(u + 0, inc, p0);
			uint8x16_t down  = vldrbq_gather_offset_z_u8(d + 0, inc, p0);

			uint16x8_t c_lo = vaddq_u16(vshlq_n_u16(vmovlbq_u8(cen), 2), vmovlbq_u8(cen));
			uint16x8_t c_hi = vaddq_u16(vshlq_n_u16(vmovltq_u8(cen), 2), vmovltq_u8(cen));
			uint16x8_t sumN_lo = vaddq_u16(vaddq_u16(vmovlbq_u8(left), vmovlbq_u8(right)), vaddq_u16(vmovlbq_u8(up), vmovlbq_u8(down)));
			uint16x8_t sumN_hi = vaddq_u16(vaddq_u16(vmovltq_u8(left), vmovltq_u8(right)), vaddq_u16(vmovltq_u8(up), vmovltq_u8(down)));
			uint16x8_t res_lo = vqsubq_u16(c_lo, sumN_lo);
			uint16x8_t res_hi = vqsubq_u16(c_hi, sumN_hi);
			uint8x16_t outv = vdupq_n_u8(0);
			outv = vqmovnbq_u16(outv, res_lo);
			outv = vqmovntq_u16(outv, res_hi);
			uint8x16_t chOffs = vaddq_n_u8(offs3, (uint8_t)ch);
			vstrbq_scatter_offset_p_s8((int8_t *)out_base, vreinterpretq_s8_u8(chOffs), vreinterpretq_s8_u8(outv), p0);
		}
	}
	uint16_t remain = width - x;
	if (remain >= 2) {
		uint16_t blk = (uint16_t)(remain - 1);
		uint8_t *out_base = out + (uint32_t)x * 3u;
		mve_pred16_t p0 = vctp8q(blk & 0xFF);
		uint8x16_t inc = mpix_make_inc_offsets();
		uint8x16_t offs3 = mpix_make_rgb_offsets();
		uint8_t r0p[16 + 2], g0p[16 + 2], b0p[16 + 2];
		uint8_t r1p[16 + 2], g1p[16 + 2], b1p[16 + 2];
		uint8_t r2p[16 + 2], g2p[16 + 2], b2p[16 + 2];
		mpix_deint_row_soa_guard1(row0 + (uint32_t)x * 3u, blk, &r0p[1], &g0p[1], &b0p[1]);
		mpix_deint_row_soa_guard1(row1 + (uint32_t)x * 3u, blk, &r1p[1], &g1p[1], &b1p[1]);
		mpix_deint_row_soa_guard1(row2 + (uint32_t)x * 3u, blk, &r2p[1], &g2p[1], &b2p[1]);
		for (int ch = 0; ch < 3; ++ch) {
			const uint8_t *u = (ch==0)? &r0p[1] : (ch==1)? &g0p[1] : &b0p[1];
			const uint8_t *c = (ch==0)? &r1p[1] : (ch==1)? &g1p[1] : &b1p[1];
			const uint8_t *d = (ch==0)? &r2p[1] : (ch==1)? &g2p[1] : &b2p[1];
			uint8x16_t left  = vldrbq_gather_offset_z_u8(c - 1, inc, p0);
			uint8x16_t cen   = vldrbq_gather_offset_z_u8(c + 0, inc, p0);
			uint8x16_t right = vldrbq_gather_offset_z_u8(c + 1, inc, p0);
			uint8x16_t up    = vldrbq_gather_offset_z_u8(u + 0, inc, p0);
			uint8x16_t down  = vldrbq_gather_offset_z_u8(d + 0, inc, p0);
			uint16x8_t c_lo = vaddq_u16(vshlq_n_u16(vmovlbq_u8(cen), 2), vmovlbq_u8(cen));
			uint16x8_t c_hi = vaddq_u16(vshlq_n_u16(vmovltq_u8(cen), 2), vmovltq_u8(cen));
			uint16x8_t sumN_lo = vaddq_u16(vaddq_u16(vmovlbq_u8(left), vmovlbq_u8(right)), vaddq_u16(vmovlbq_u8(up), vmovlbq_u8(down)));
			uint16x8_t sumN_hi = vaddq_u16(vaddq_u16(vmovltq_u8(left), vmovltq_u8(right)), vaddq_u16(vmovltq_u8(up), vmovltq_u8(down)));
			uint16x8_t res_lo = vqsubq_u16(c_lo, sumN_lo);
			uint16x8_t res_hi = vqsubq_u16(c_hi, sumN_hi);
			uint8x16_t outv = vdupq_n_u8(0);
			outv = vqmovnbq_u16(outv, res_lo);
			outv = vqmovntq_u16(outv, res_hi);
			uint8x16_t chOffs = vaddq_n_u8(offs3, (uint8_t)ch);
			vstrbq_scatter_offset_p_s8((int8_t *)out_base, vreinterpretq_s8_u8(chOffs), vreinterpretq_s8_u8(outv), p0);
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

static inline void mpix_median3x3_channel_block_soa(
	const uint8_t *p0c, const uint8_t *p1c, const uint8_t *p2c,
	uint8_t *out_base, uint8x16_t offs3, mve_pred16_t p0, int channel,
	uint8x16_t inc)
{
	uint8x16_t left  = vldrbq_gather_offset_z_u8(p1c - 1, inc, p0);
	uint8x16_t cen   = vldrbq_gather_offset_z_u8(p1c + 0, inc, p0);
	uint8x16_t right = vldrbq_gather_offset_z_u8(p1c + 1, inc, p0);
	uint8x16_t upL   = vldrbq_gather_offset_z_u8(p0c - 1, inc, p0);
	uint8x16_t upC   = vldrbq_gather_offset_z_u8(p0c + 0, inc, p0);
	uint8x16_t upR   = vldrbq_gather_offset_z_u8(p0c + 1, inc, p0);
	uint8x16_t dnL   = vldrbq_gather_offset_z_u8(p2c - 1, inc, p0);
	uint8x16_t dnC   = vldrbq_gather_offset_z_u8(p2c + 0, inc, p0);
	uint8x16_t dnR   = vldrbq_gather_offset_z_u8(p2c + 1, inc, p0);

	/* row-wise sort3 */
	mpix_swap_u8x16(&upL, &upC); mpix_swap_u8x16(&upC, &upR); mpix_swap_u8x16(&upL, &upC);
	mpix_swap_u8x16(&left, &cen); mpix_swap_u8x16(&cen, &right); mpix_swap_u8x16(&left, &cen);
	mpix_swap_u8x16(&dnL, &dnC); mpix_swap_u8x16(&dnC, &dnR); mpix_swap_u8x16(&dnL, &dnC);

	uint8x16_t a = vmaxq_u8(upL, vmaxq_u8(left, dnL));
	uint8x16_t b0 = upC, b1 = cen, b2 = dnC;
	mpix_swap_u8x16(&b0, &b1); mpix_swap_u8x16(&b1, &b2); mpix_swap_u8x16(&b0, &b1);
	uint8x16_t b = b1;
	uint8x16_t c = vminq_u8(upR, vminq_u8(right, dnR));

	mpix_swap_u8x16(&a, &b); mpix_swap_u8x16(&b, &c); mpix_swap_u8x16(&a, &b);
	uint8x16_t med = b;

	uint8x16_t chOffs = vaddq_n_u8(offs3, (uint8_t)channel);
	vstrbq_scatter_offset_p_s8((int8_t *)out_base, vreinterpretq_s8_u8(chOffs), vreinterpretq_s8_u8(med), p0);
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
		const uint16_t blk = 16;
		uint8_t *out_base = out + (uint32_t)x * 3u;
		uint8x16_t offs3 = mpix_make_rgb_offsets();
		mve_pred16_t p0 = vctp8q(blk & 0xFF);
		uint8x16_t inc = mpix_make_inc_offsets();

		uint8_t r0p[16 + 2], g0p[16 + 2], b0p[16 + 2];
		uint8_t r1p[16 + 2], g1p[16 + 2], b1p[16 + 2];
		uint8_t r2p[16 + 2], g2p[16 + 2], b2p[16 + 2];
		mpix_deint_row_soa_guard1(row0 + (uint32_t)x * 3u, blk, &r0p[1], &g0p[1], &b0p[1]);
		mpix_deint_row_soa_guard1(row1 + (uint32_t)x * 3u, blk, &r1p[1], &g1p[1], &b1p[1]);
		mpix_deint_row_soa_guard1(row2 + (uint32_t)x * 3u, blk, &r2p[1], &g2p[1], &b2p[1]);

		for (int ch = 0; ch < 3; ++ch) {
			const uint8_t *p0c = (ch==0)? &r0p[1] : (ch==1)? &g0p[1] : &b0p[1];
			const uint8_t *p1c = (ch==0)? &r1p[1] : (ch==1)? &g1p[1] : &b1p[1];
			const uint8_t *p2c = (ch==0)? &r2p[1] : (ch==1)? &g2p[1] : &b2p[1];
			mpix_median3x3_channel_block_soa(p0c, p1c, p2c, out_base, offs3, p0, ch, inc);
		}
	}

	/* Tail: keep last pixel for scalar so that right neighbor exists */
	uint16_t remain = width - x;
	if (remain >= 2) {
		uint16_t blk = (uint16_t)(remain - 1);
		uint8_t *out_base = out + (uint32_t)x * 3u;
		uint8x16_t offs3 = mpix_make_rgb_offsets();
		mve_pred16_t p0 = vctp8q(blk & 0xFF);
		uint8x16_t inc = mpix_make_inc_offsets();
		uint8_t r0p[16 + 2], g0p[16 + 2], b0p[16 + 2];
		uint8_t r1p[16 + 2], g1p[16 + 2], b1p[16 + 2];
		uint8_t r2p[16 + 2], g2p[16 + 2], b2p[16 + 2];
		mpix_deint_row_soa_guard1(row0 + (uint32_t)x * 3u, blk, &r0p[1], &g0p[1], &b0p[1]);
		mpix_deint_row_soa_guard1(row1 + (uint32_t)x * 3u, blk, &r1p[1], &g1p[1], &b1p[1]);
		mpix_deint_row_soa_guard1(row2 + (uint32_t)x * 3u, blk, &r2p[1], &g2p[1], &b2p[1]);
		for (int ch = 0; ch < 3; ++ch) {
			const uint8_t *p0c = (ch==0)? &r0p[1] : (ch==1)? &g0p[1] : &b0p[1];
			const uint8_t *p1c = (ch==0)? &r1p[1] : (ch==1)? &g1p[1] : &b1p[1];
			const uint8_t *p2c = (ch==0)? &r2p[1] : (ch==1)? &g2p[1] : &b2p[1];
			mpix_median3x3_channel_block_soa(p0c, p1c, p2c, out_base, offs3, p0, ch, inc);
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
	const uint16_t blk = block_len;
	(void)blk;
	uint8x16_t inc = mpix_make_inc_offsets();
	/* We'll load 5 contiguous windows from SOA input prepared by caller */
	const uint8_t *a = r0; const uint8_t *b = r1; const uint8_t *c = r2; const uint8_t *d = r3; const uint8_t *e = r4;
	if (channel == 1) { a = r0 + 0; b = r1 + 0; c = r2 + 0; d = r3 + 0; e = r4 + 0; }
	(void)channel; /* channel is applied by out scatter offsets below */
	/* gather from SOA rows already at channel-specific buffers */
	uint8x16_t a2 = vldrbq_gather_offset_z_u8(a - 2, inc, p0);
	uint8x16_t a1 = vldrbq_gather_offset_z_u8(a - 1, inc, p0);
	uint8x16_t a0 = vldrbq_gather_offset_z_u8(a + 0, inc, p0);
	uint8x16_t a_1= vldrbq_gather_offset_z_u8(a + 1, inc, p0);
	uint8x16_t a_2= vldrbq_gather_offset_z_u8(a + 2, inc, p0);

	uint8x16_t b2 = vldrbq_gather_offset_z_u8(b - 2, inc, p0);
	uint8x16_t b1 = vldrbq_gather_offset_z_u8(b - 1, inc, p0);
	uint8x16_t b0 = vldrbq_gather_offset_z_u8(b + 0, inc, p0);
	uint8x16_t b_1= vldrbq_gather_offset_z_u8(b + 1, inc, p0);
	uint8x16_t b_2= vldrbq_gather_offset_z_u8(b + 2, inc, p0);

	uint8x16_t c2 = vldrbq_gather_offset_z_u8(c - 2, inc, p0);
	uint8x16_t c1 = vldrbq_gather_offset_z_u8(c - 1, inc, p0);
	uint8x16_t c0 = vldrbq_gather_offset_z_u8(c + 0, inc, p0);
	uint8x16_t c_1= vldrbq_gather_offset_z_u8(c + 1, inc, p0);
	uint8x16_t c_2= vldrbq_gather_offset_z_u8(c + 2, inc, p0);

	uint8x16_t d2 = vldrbq_gather_offset_z_u8(d - 2, inc, p0);
	uint8x16_t d1 = vldrbq_gather_offset_z_u8(d - 1, inc, p0);
	uint8x16_t d0 = vldrbq_gather_offset_z_u8(d + 0, inc, p0);
	uint8x16_t d_1= vldrbq_gather_offset_z_u8(d + 1, inc, p0);
	uint8x16_t d_2= vldrbq_gather_offset_z_u8(d + 2, inc, p0);

	uint8x16_t e2 = vldrbq_gather_offset_z_u8(e - 2, inc, p0);
	uint8x16_t e1 = vldrbq_gather_offset_z_u8(e - 1, inc, p0);
	uint8x16_t e0 = vldrbq_gather_offset_z_u8(e + 0, inc, p0);
	uint8x16_t e_1= vldrbq_gather_offset_z_u8(e + 1, inc, p0);
	uint8x16_t e_2= vldrbq_gather_offset_z_u8(e + 2, inc, p0);

	/* Vectorized binary-search median per lane over 25 values using counts > mid */
	uint8x16_t bot = vdupq_n_u8(0);
	uint8x16_t top = vdupq_n_u8(255);
	uint8x16_t one = vdupq_n_u8(1);
	uint8x16_t half = vdupq_n_u8(12); /* 25/2 */
	for (int i = 0; i < 8; ++i) {
		uint8x16_t mid = mpix_avg_u8(top, bot);
		uint8x16_t cnt = vdupq_n_u8(0);
		mve_pred16_t p;
		/* accumulate counts where v > mid */
		p = vcmphiq_u8(a2, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
		p = vcmphiq_u8(a1, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
		p = vcmphiq_u8(a0, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
		p = vcmphiq_u8(a_1, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
		p = vcmphiq_u8(a_2, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);

		p = vcmphiq_u8(b2, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
		p = vcmphiq_u8(b1, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
		p = vcmphiq_u8(b0, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
		p = vcmphiq_u8(b_1, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
		p = vcmphiq_u8(b_2, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);

		p = vcmphiq_u8(c2, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
		p = vcmphiq_u8(c1, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
		p = vcmphiq_u8(c0, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
		p = vcmphiq_u8(c_1, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
		p = vcmphiq_u8(c_2, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);

		p = vcmphiq_u8(d2, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
		p = vcmphiq_u8(d1, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
		p = vcmphiq_u8(d0, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
		p = vcmphiq_u8(d_1, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
		p = vcmphiq_u8(d_2, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);

		p = vcmphiq_u8(e2, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
		p = vcmphiq_u8(e1, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
		p = vcmphiq_u8(e0, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
		p = vcmphiq_u8(e_1, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);
		p = vcmphiq_u8(e_2, mid); cnt = vpselq_u8(vaddq_u8(cnt, one), cnt, p);

    /* update pivots */
	mve_pred16_t p_gt = vcmphiq_u8(cnt, half); /* cnt > half */
	mve_pred16_t p_lt = vcmphiq_u8(half, cnt); /* cnt < half */
		top = vpselq_u8(mid, top, p_lt);
		bot = vpselq_u8(mid, bot, p_gt);
	}

	uint8x16_t med = mpix_avg_u8(top, bot);
	uint8x16_t offC  = vaddq_n_u8(offs3, (uint8_t)channel);
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
		const uint16_t blk = 16;
		uint8_t *out_base = out + (uint32_t)x * 3u;
		uint8x16_t offs3 = mpix_make_rgb_offsets();
		mve_pred16_t p0 = vctp8q(blk & 0xFF);
		/* Deinterleave 5 rows with guard2 */
		uint8_t r0p[16 + 4], g0p[16 + 4], b0p[16 + 4];
		uint8_t r1p[16 + 4], g1p[16 + 4], b1p[16 + 4];
		uint8_t r2p[16 + 4], g2p[16 + 4], b2p[16 + 4];
		uint8_t r3p[16 + 4], g3p[16 + 4], b3p[16 + 4];
		uint8_t r4p[16 + 4], g4p[16 + 4], b4p[16 + 4];
		mpix_deint_row_soa_guard2(r0 + (uint32_t)x * 3u, blk, &r0p[2], &g0p[2], &b0p[2]);
		mpix_deint_row_soa_guard2(r1 + (uint32_t)x * 3u, blk, &r1p[2], &g1p[2], &b1p[2]);
		mpix_deint_row_soa_guard2(r2 + (uint32_t)x * 3u, blk, &r2p[2], &g2p[2], &b2p[2]);
		mpix_deint_row_soa_guard2(r3 + (uint32_t)x * 3u, blk, &r3p[2], &g3p[2], &b3p[2]);
		mpix_deint_row_soa_guard2(r4 + (uint32_t)x * 3u, blk, &r4p[2], &g4p[2], &b4p[2]);
		for (int ch = 0; ch < 3; ++ch) {
			const uint8_t *a = (ch==0)? &r0p[2] : (ch==1)? &g0p[2] : &b0p[2];
			const uint8_t *b = (ch==0)? &r1p[2] : (ch==1)? &g1p[2] : &b1p[2];
			const uint8_t *c = (ch==0)? &r2p[2] : (ch==1)? &g2p[2] : &b2p[2];
			const uint8_t *d = (ch==0)? &r3p[2] : (ch==1)? &g3p[2] : &b3p[2];
			const uint8_t *e = (ch==0)? &r4p[2] : (ch==1)? &g4p[2] : &b4p[2];
			mpix_median5x5_channel_block_with_pre(a, b, c, d, e, out_base, x, blk, offs3, p0, ch);
		}
	}

	uint16_t remain = width - x;
	if (remain >= 3) {
		uint16_t blk = (uint16_t)(remain - 2);
		uint8_t *out_base = out + (uint32_t)x * 3u;
		uint8x16_t offs3 = mpix_make_rgb_offsets();
		mve_pred16_t p0 = vctp8q(blk & 0xFF);
		uint8_t r0p[16 + 4], g0p[16 + 4], b0p[16 + 4];
		uint8_t r1p[16 + 4], g1p[16 + 4], b1p[16 + 4];
		uint8_t r2p[16 + 4], g2p[16 + 4], b2p[16 + 4];
		uint8_t r3p[16 + 4], g3p[16 + 4], b3p[16 + 4];
		uint8_t r4p[16 + 4], g4p[16 + 4], b4p[16 + 4];
		mpix_deint_row_soa_guard2(r0 + (uint32_t)x * 3u, blk, &r0p[2], &g0p[2], &b0p[2]);
		mpix_deint_row_soa_guard2(r1 + (uint32_t)x * 3u, blk, &r1p[2], &g1p[2], &b1p[2]);
		mpix_deint_row_soa_guard2(r2 + (uint32_t)x * 3u, blk, &r2p[2], &g2p[2], &b2p[2]);
		mpix_deint_row_soa_guard2(r3 + (uint32_t)x * 3u, blk, &r3p[2], &g3p[2], &b3p[2]);
		mpix_deint_row_soa_guard2(r4 + (uint32_t)x * 3u, blk, &r4p[2], &g4p[2], &b4p[2]);
		for (int ch = 0; ch < 3; ++ch) {
			const uint8_t *a = (ch==0)? &r0p[2] : (ch==1)? &g0p[2] : &b0p[2];
			const uint8_t *b = (ch==0)? &r1p[2] : (ch==1)? &g1p[2] : &b1p[2];
			const uint8_t *c = (ch==0)? &r2p[2] : (ch==1)? &g2p[2] : &b2p[2];
			const uint8_t *d = (ch==0)? &r3p[2] : (ch==1)? &g3p[2] : &b3p[2];
			const uint8_t *e = (ch==0)? &r4p[2] : (ch==1)? &g4p[2] : &b4p[2];
			mpix_median5x5_channel_block_with_pre(a, b, c, d, e, out_base, x, blk, offs3, p0, ch);
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



/* ========================== 5x5 Edge Detect (RGB24) ========================== */

static inline void mpix_edge5x5_channel_block_with_pre(
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
	uint8x16_t offL1 = vsubq_n_u8(offC, 3);
	uint8x16_t offL2 = vsubq_n_u8(offC, 6);
	uint8x16_t offR1 = vaddq_n_u8(offC, 3);
	uint8x16_t offR2 = vaddq_n_u8(offC, 6);

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

	/* Gather neighbors for 5 rows */
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

	/* sum neighbors (all except center) */
	uint16x8_t sum0_lo = vaddq_u16(vaddq_u16(vmovlbq_u8(a0), vmovlbq_u8(b0)), vaddq_u16(vmovlbq_u8(d0), vmovlbq_u8(e0)));
	sum0_lo = vaddq_u16(sum0_lo, vmovlbq_u8(c0));
	uint16x8_t sum0_hi = vaddq_u16(vaddq_u16(vmovltq_u8(a0), vmovltq_u8(b0)), vaddq_u16(vmovltq_u8(d0), vmovltq_u8(e0)));
	sum0_hi = vaddq_u16(sum0_hi, vmovltq_u8(c0));

	uint16x8_t sum1_lo = vaddq_u16(vaddq_u16(vmovlbq_u8(a1), vmovlbq_u8(b1)), vaddq_u16(vmovlbq_u8(d1), vmovlbq_u8(e1)));
	sum1_lo = vaddq_u16(sum1_lo, vmovlbq_u8(c1));
	uint16x8_t sum1_hi = vaddq_u16(vaddq_u16(vmovltq_u8(a1), vmovltq_u8(b1)), vaddq_u16(vmovltq_u8(d1), vmovltq_u8(e1)));
	sum1_hi = vaddq_u16(sum1_hi, vmovltq_u8(c1));

	uint16x8_t sum2_lo = vaddq_u16(vaddq_u16(vmovlbq_u8(a2), vmovlbq_u8(b2)), vaddq_u16(vmovlbq_u8(d2), vmovlbq_u8(e2)));
	sum2_lo = vaddq_u16(sum2_lo, vaddq_u16(vaddq_u16(vmovlbq_u8(a2), vmovlbq_u8(b2)), vaddq_u16(vmovlbq_u8(d2), vmovlbq_u8(e2)))); /* we'll subtract later */
	uint16x8_t sum2_hi = vaddq_u16(vaddq_u16(vmovltq_u8(a2), vmovltq_u8(b2)), vaddq_u16(vmovltq_u8(d2), vmovltq_u8(e2)));
	sum2_hi = vaddq_u16(sum2_hi, vaddq_u16(vaddq_u16(vmovltq_u8(a2), vmovltq_u8(b2)), vaddq_u16(vmovltq_u8(d2), vmovltq_u8(e2))));

	uint16x8_t sum3_lo = vaddq_u16(vaddq_u16(vmovlbq_u8(a3), vmovlbq_u8(b3)), vaddq_u16(vmovlbq_u8(d3), vmovlbq_u8(e3)));
	sum3_lo = vaddq_u16(sum3_lo, vmovlbq_u8(c3));
	uint16x8_t sum3_hi = vaddq_u16(vaddq_u16(vmovltq_u8(a3), vmovltq_u8(b3)), vaddq_u16(vmovltq_u8(d3), vmovltq_u8(e3)));
	sum3_hi = vaddq_u16(sum3_hi, vmovltq_u8(c3));

	uint16x8_t sum4_lo = vaddq_u16(vaddq_u16(vmovlbq_u8(a4), vmovlbq_u8(b4)), vaddq_u16(vmovlbq_u8(d4), vmovlbq_u8(e4)));
	sum4_lo = vaddq_u16(sum4_lo, vmovlbq_u8(c4));
	uint16x8_t sum4_hi = vaddq_u16(vaddq_u16(vmovltq_u8(a4), vmovltq_u8(b4)), vaddq_u16(vmovltq_u8(d4), vmovltq_u8(e4)));
	sum4_hi = vaddq_u16(sum4_hi, vmovltq_u8(c4));

	/* total neighbors sum across 5x5 excluding center (we added c2 multiple times above to balance) */
	uint16x8_t neigh_lo = vaddq_u16(vaddq_u16(sum0_lo, sum1_lo), vaddq_u16(sum3_lo, sum4_lo));
	neigh_lo = vaddq_u16(neigh_lo, vaddq_u16(vmovlbq_u8(a2), vmovlbq_u8(b2)));
	neigh_lo = vaddq_u16(neigh_lo, vaddq_u16(vmovlbq_u8(d2), vmovlbq_u8(e2)));
	neigh_lo = vaddq_u16(neigh_lo, vaddq_u16(vmovlbq_u8(c0), vaddq_u16(vmovlbq_u8(c1), vaddq_u16(vmovlbq_u8(c3), vmovlbq_u8(c4)))));

	uint16x8_t neigh_hi = vaddq_u16(vaddq_u16(sum0_hi, sum1_hi), vaddq_u16(sum3_hi, sum4_hi));
	neigh_hi = vaddq_u16(neigh_hi, vaddq_u16(vmovltq_u8(a2), vmovltq_u8(b2)));
	neigh_hi = vaddq_u16(neigh_hi, vaddq_u16(vmovltq_u8(d2), vmovltq_u8(e2)));
	neigh_hi = vaddq_u16(neigh_hi, vaddq_u16(vmovltq_u8(c0), vaddq_u16(vmovltq_u8(c1), vaddq_u16(vmovltq_u8(c3), vmovltq_u8(c4)))));

	/* center * 24 - neighbors */
	uint16x8_t c_lo = vmovlbq_u8(c2);
	uint16x8_t c_hi = vmovltq_u8(c2);
	/* 24 = 16 + 8 */
	c_lo = vaddq_u16(vshlq_n_u16(c_lo, 4), vshlq_n_u16(c_lo, 3));
	c_hi = vaddq_u16(vshlq_n_u16(c_hi, 4), vshlq_n_u16(c_hi, 3));

	uint16x8_t res_lo = vqsubq_u16(c_lo, neigh_lo);
	uint16x8_t res_hi = vqsubq_u16(c_hi, neigh_hi);

	uint8x16_t outv = vdupq_n_u8(0);
	outv = vqmovnbq_u16(outv, res_lo);
	outv = vqmovntq_u16(outv, res_hi);

	vstrbq_scatter_offset_p_s8((int8_t *)out_base, vreinterpretq_s8_u8(offC), vreinterpretq_s8_u8(outv), p0);
}

void mpix_edgedetect_rgb24_5x5(const uint8_t *in[5], uint8_t *out, uint16_t width)
{
	assert(width >= 5);
	const uint8_t *r0 = in[0];
	const uint8_t *r1 = in[1];
	const uint8_t *r2 = in[2];
	const uint8_t *r3 = in[3];
	const uint8_t *r4 = in[4];

	uint16_t x = 0;
	for (; (uint32_t)x + 16u <= (uint32_t)(width - 2); x += 16) {
		const uint16_t blk = 16;
		uint8_t *out_base = out + (uint32_t)x * 3u;
		mve_pred16_t p0 = vctp8q(blk & 0xFF);
		uint8x16_t inc = mpix_make_inc_offsets();
		uint8x16_t offs3 = mpix_make_rgb_offsets();

		uint8_t r0p[16 + 4], g0p[16 + 4], b0p[16 + 4];
		uint8_t r1p[16 + 4], g1p[16 + 4], b1p[16 + 4];
		uint8_t r2p[16 + 4], g2p[16 + 4], b2p[16 + 4];
		uint8_t r3p[16 + 4], g3p[16 + 4], b3p[16 + 4];
		uint8_t r4p[16 + 4], g4p[16 + 4], b4p[16 + 4];
		mpix_deint_row_soa_guard2(r0 + (uint32_t)x * 3u, blk, &r0p[2], &g0p[2], &b0p[2]);
		mpix_deint_row_soa_guard2(r1 + (uint32_t)x * 3u, blk, &r1p[2], &g1p[2], &b1p[2]);
		mpix_deint_row_soa_guard2(r2 + (uint32_t)x * 3u, blk, &r2p[2], &g2p[2], &b2p[2]);
		mpix_deint_row_soa_guard2(r3 + (uint32_t)x * 3u, blk, &r3p[2], &g3p[2], &b3p[2]);
		mpix_deint_row_soa_guard2(r4 + (uint32_t)x * 3u, blk, &r4p[2], &g4p[2], &b4p[2]);

		for (int ch = 0; ch < 3; ++ch) {
			const uint8_t *a = (ch==0)? &r0p[2] : (ch==1)? &g0p[2] : &b0p[2];
			const uint8_t *b = (ch==0)? &r1p[2] : (ch==1)? &g1p[2] : &b1p[2];
			const uint8_t *c = (ch==0)? &r2p[2] : (ch==1)? &g2p[2] : &b2p[2];
			const uint8_t *d = (ch==0)? &r3p[2] : (ch==1)? &g3p[2] : &b3p[2];
			const uint8_t *e = (ch==0)? &r4p[2] : (ch==1)? &g4p[2] : &b4p[2];

			/* contiguous loads from SOA with +/-2 neighbors */
			uint8x16_t a2 = vldrbq_gather_offset_z_u8(a - 2, inc, p0);
			uint8x16_t a1 = vldrbq_gather_offset_z_u8(a - 1, inc, p0);
			uint8x16_t a0 = vldrbq_gather_offset_z_u8(a + 0, inc, p0);
			uint8x16_t a_1= vldrbq_gather_offset_z_u8(a + 1, inc, p0);
			uint8x16_t a_2= vldrbq_gather_offset_z_u8(a + 2, inc, p0);

			uint8x16_t b2 = vldrbq_gather_offset_z_u8(b - 2, inc, p0);
			uint8x16_t b1 = vldrbq_gather_offset_z_u8(b - 1, inc, p0);
			uint8x16_t b0 = vldrbq_gather_offset_z_u8(b + 0, inc, p0);
			uint8x16_t b_1= vldrbq_gather_offset_z_u8(b + 1, inc, p0);
			uint8x16_t b_2= vldrbq_gather_offset_z_u8(b + 2, inc, p0);

			uint8x16_t c2 = vldrbq_gather_offset_z_u8(c - 2, inc, p0);
			uint8x16_t c1 = vldrbq_gather_offset_z_u8(c - 1, inc, p0);
			uint8x16_t c0 = vldrbq_gather_offset_z_u8(c + 0, inc, p0);
			uint8x16_t c_1= vldrbq_gather_offset_z_u8(c + 1, inc, p0);
			uint8x16_t c_2= vldrbq_gather_offset_z_u8(c + 2, inc, p0);

			uint8x16_t d2 = vldrbq_gather_offset_z_u8(d - 2, inc, p0);
			uint8x16_t d1 = vldrbq_gather_offset_z_u8(d - 1, inc, p0);
			uint8x16_t d0 = vldrbq_gather_offset_z_u8(d + 0, inc, p0);
			uint8x16_t d_1= vldrbq_gather_offset_z_u8(d + 1, inc, p0);
			uint8x16_t d_2= vldrbq_gather_offset_z_u8(d + 2, inc, p0);

			uint8x16_t e2 = vldrbq_gather_offset_z_u8(e - 2, inc, p0);
			uint8x16_t e1 = vldrbq_gather_offset_z_u8(e - 1, inc, p0);
			uint8x16_t e0 = vldrbq_gather_offset_z_u8(e + 0, inc, p0);
			uint8x16_t e_1= vldrbq_gather_offset_z_u8(e + 1, inc, p0);
			uint8x16_t e_2= vldrbq_gather_offset_z_u8(e + 2, inc, p0);

			/* neighbors sum excluding center c0 */
			uint16x8_t neigh_lo = vaddq_u16(vaddq_u16(vmovlbq_u8(a2), vmovlbq_u8(a1)), vaddq_u16(vmovlbq_u8(a_1), vmovlbq_u8(a_2)));
			neigh_lo = vaddq_u16(neigh_lo, vaddq_u16(vmovlbq_u8(b2), vmovlbq_u8(b1)));
			neigh_lo = vaddq_u16(neigh_lo, vaddq_u16(vmovlbq_u8(b0), vmovlbq_u8(b_1)));
			neigh_lo = vaddq_u16(neigh_lo, vaddq_u16(vmovlbq_u8(b_2), vmovlbq_u8(c2)));
			neigh_lo = vaddq_u16(neigh_lo, vaddq_u16(vmovlbq_u8(c1), vmovlbq_u8(c_1)));
			neigh_lo = vaddq_u16(neigh_lo, vaddq_u16(vmovlbq_u8(c_2), vmovlbq_u8(d2)));
			neigh_lo = vaddq_u16(neigh_lo, vaddq_u16(vmovlbq_u8(d1), vmovlbq_u8(d0)));
			neigh_lo = vaddq_u16(neigh_lo, vaddq_u16(vmovlbq_u8(d_1), vmovlbq_u8(d_2)));
			neigh_lo = vaddq_u16(neigh_lo, vaddq_u16(vmovlbq_u8(e2), vaddq_u16(vmovlbq_u8(e1), vaddq_u16(vmovlbq_u8(e0), vaddq_u16(vmovlbq_u8(e_1), vmovlbq_u8(e_2))))));

			uint16x8_t neigh_hi = vaddq_u16(vaddq_u16(vmovltq_u8(a2), vmovltq_u8(a1)), vaddq_u16(vmovltq_u8(a_1), vmovltq_u8(a_2)));
			neigh_hi = vaddq_u16(neigh_hi, vaddq_u16(vmovltq_u8(b2), vmovltq_u8(b1)));
			neigh_hi = vaddq_u16(neigh_hi, vaddq_u16(vmovltq_u8(b0), vmovltq_u8(b_1)));
			neigh_hi = vaddq_u16(neigh_hi, vaddq_u16(vmovltq_u8(b_2), vmovltq_u8(c2)));
			neigh_hi = vaddq_u16(neigh_hi, vaddq_u16(vmovltq_u8(c1), vmovltq_u8(c_1)));
			neigh_hi = vaddq_u16(neigh_hi, vaddq_u16(vmovltq_u8(c_2), vmovltq_u8(d2)));
			neigh_hi = vaddq_u16(neigh_hi, vaddq_u16(vmovltq_u8(d1), vmovltq_u8(d0)));
			neigh_hi = vaddq_u16(neigh_hi, vaddq_u16(vmovltq_u8(d_1), vmovltq_u8(d_2)));
			neigh_hi = vaddq_u16(neigh_hi, vaddq_u16(vmovltq_u8(e2), vaddq_u16(vmovltq_u8(e1), vaddq_u16(vmovltq_u8(e0), vaddq_u16(vmovltq_u8(e_1), vmovltq_u8(e_2))))));

			uint16x8_t c_lo = vaddq_u16(vshlq_n_u16(vmovlbq_u8(c0), 4), vshlq_n_u16(vmovlbq_u8(c0), 3));
			uint16x8_t c_hi = vaddq_u16(vshlq_n_u16(vmovltq_u8(c0), 4), vshlq_n_u16(vmovltq_u8(c0), 3));

			uint16x8_t res_lo = vqsubq_u16(c_lo, neigh_lo);
			uint16x8_t res_hi = vqsubq_u16(c_hi, neigh_hi);

			uint8x16_t outv = vdupq_n_u8(0);
			outv = vqmovnbq_u16(outv, res_lo);
			outv = vqmovntq_u16(outv, res_hi);
			uint8x16_t chOffs = vaddq_n_u8(offs3, (uint8_t)ch);
			vstrbq_scatter_offset_p_s8((int8_t *)out_base, vreinterpretq_s8_u8(chOffs), vreinterpretq_s8_u8(outv), p0);
		}
	}

	uint16_t remain = width - x;
	if (remain >= 3) {
		uint16_t blk = (uint16_t)(remain - 2);
		uint8_t *out_base = out + (uint32_t)x * 3u;
		mve_pred16_t p0 = vctp8q(blk & 0xFF);
		uint8x16_t inc = mpix_make_inc_offsets();
		uint8x16_t offs3 = mpix_make_rgb_offsets();

		uint8_t r0p[16 + 4], g0p[16 + 4], b0p[16 + 4];
		uint8_t r1p[16 + 4], g1p[16 + 4], b1p[16 + 4];
		uint8_t r2p[16 + 4], g2p[16 + 4], b2p[16 + 4];
		uint8_t r3p[16 + 4], g3p[16 + 4], b3p[16 + 4];
		uint8_t r4p[16 + 4], g4p[16 + 4], b4p[16 + 4];
		mpix_deint_row_soa_guard2(r0 + (uint32_t)x * 3u, blk, &r0p[2], &g0p[2], &b0p[2]);
		mpix_deint_row_soa_guard2(r1 + (uint32_t)x * 3u, blk, &r1p[2], &g1p[2], &b1p[2]);
		mpix_deint_row_soa_guard2(r2 + (uint32_t)x * 3u, blk, &r2p[2], &g2p[2], &b2p[2]);
		mpix_deint_row_soa_guard2(r3 + (uint32_t)x * 3u, blk, &r3p[2], &g3p[2], &b3p[2]);
		mpix_deint_row_soa_guard2(r4 + (uint32_t)x * 3u, blk, &r4p[2], &g4p[2], &b4p[2]);

		for (int ch = 0; ch < 3; ++ch) {
			const uint8_t *a = (ch==0)? &r0p[2] : (ch==1)? &g0p[2] : &b0p[2];
			const uint8_t *b = (ch==0)? &r1p[2] : (ch==1)? &g1p[2] : &b1p[2];
			const uint8_t *c = (ch==0)? &r2p[2] : (ch==1)? &g2p[2] : &b2p[2];
			const uint8_t *d = (ch==0)? &r3p[2] : (ch==1)? &g3p[2] : &b3p[2];
			const uint8_t *e = (ch==0)? &r4p[2] : (ch==1)? &g4p[2] : &b4p[2];

			uint8x16_t a2 = vldrbq_gather_offset_z_u8(a - 2, inc, p0);
			uint8x16_t a1 = vldrbq_gather_offset_z_u8(a - 1, inc, p0);
			uint8x16_t a0 = vldrbq_gather_offset_z_u8(a + 0, inc, p0);
			uint8x16_t a_1= vldrbq_gather_offset_z_u8(a + 1, inc, p0);
			uint8x16_t a_2= vldrbq_gather_offset_z_u8(a + 2, inc, p0);

			uint8x16_t b2 = vldrbq_gather_offset_z_u8(b - 2, inc, p0);
			uint8x16_t b1 = vldrbq_gather_offset_z_u8(b - 1, inc, p0);
			uint8x16_t b0 = vldrbq_gather_offset_z_u8(b + 0, inc, p0);
			uint8x16_t b_1= vldrbq_gather_offset_z_u8(b + 1, inc, p0);
			uint8x16_t b_2= vldrbq_gather_offset_z_u8(b + 2, inc, p0);

			uint8x16_t c2 = vldrbq_gather_offset_z_u8(c - 2, inc, p0);
			uint8x16_t c1 = vldrbq_gather_offset_z_u8(c - 1, inc, p0);
			uint8x16_t c0 = vldrbq_gather_offset_z_u8(c + 0, inc, p0);
			uint8x16_t c_1= vldrbq_gather_offset_z_u8(c + 1, inc, p0);
			uint8x16_t c_2= vldrbq_gather_offset_z_u8(c + 2, inc, p0);

			uint8x16_t d2 = vldrbq_gather_offset_z_u8(d - 2, inc, p0);
			uint8x16_t d1 = vldrbq_gather_offset_z_u8(d - 1, inc, p0);
			uint8x16_t d0 = vldrbq_gather_offset_z_u8(d + 0, inc, p0);
			uint8x16_t d_1= vldrbq_gather_offset_z_u8(d + 1, inc, p0);
			uint8x16_t d_2= vldrbq_gather_offset_z_u8(d + 2, inc, p0);

			uint8x16_t e2 = vldrbq_gather_offset_z_u8(e - 2, inc, p0);
			uint8x16_t e1 = vldrbq_gather_offset_z_u8(e - 1, inc, p0);
			uint8x16_t e0 = vldrbq_gather_offset_z_u8(e + 0, inc, p0);
			uint8x16_t e_1= vldrbq_gather_offset_z_u8(e + 1, inc, p0);
			uint8x16_t e_2= vldrbq_gather_offset_z_u8(e + 2, inc, p0);

			uint16x8_t neigh_lo = vaddq_u16(vaddq_u16(vmovlbq_u8(a2), vmovlbq_u8(a1)), vaddq_u16(vmovlbq_u8(a_1), vmovlbq_u8(a_2)));
			neigh_lo = vaddq_u16(neigh_lo, vaddq_u16(vmovlbq_u8(b2), vmovlbq_u8(b1)));
			neigh_lo = vaddq_u16(neigh_lo, vaddq_u16(vmovlbq_u8(b0), vmovlbq_u8(b_1)));
			neigh_lo = vaddq_u16(neigh_lo, vaddq_u16(vmovlbq_u8(b_2), vmovlbq_u8(c2)));
			neigh_lo = vaddq_u16(neigh_lo, vaddq_u16(vmovlbq_u8(c1), vmovlbq_u8(c_1)));
			neigh_lo = vaddq_u16(neigh_lo, vaddq_u16(vmovlbq_u8(c_2), vmovlbq_u8(d2)));
			neigh_lo = vaddq_u16(neigh_lo, vaddq_u16(vmovlbq_u8(d1), vmovlbq_u8(d0)));
			neigh_lo = vaddq_u16(neigh_lo, vaddq_u16(vmovlbq_u8(d_1), vmovlbq_u8(d_2)));
			neigh_lo = vaddq_u16(neigh_lo, vaddq_u16(vmovlbq_u8(e2), vaddq_u16(vmovlbq_u8(e1), vaddq_u16(vmovlbq_u8(e0), vaddq_u16(vmovlbq_u8(e_1), vmovlbq_u8(e_2))))));

			uint16x8_t neigh_hi = vaddq_u16(vaddq_u16(vmovltq_u8(a2), vmovltq_u8(a1)), vaddq_u16(vmovltq_u8(a_1), vmovltq_u8(a_2)));
			neigh_hi = vaddq_u16(neigh_hi, vaddq_u16(vmovltq_u8(b2), vmovltq_u8(b1)));
			neigh_hi = vaddq_u16(neigh_hi, vaddq_u16(vmovltq_u8(b0), vmovltq_u8(b_1)));
			neigh_hi = vaddq_u16(neigh_hi, vaddq_u16(vmovltq_u8(b_2), vmovltq_u8(c2)));
			neigh_hi = vaddq_u16(neigh_hi, vaddq_u16(vmovltq_u8(c1), vmovltq_u8(c_1)));
			neigh_hi = vaddq_u16(neigh_hi, vaddq_u16(vmovltq_u8(c_2), vmovltq_u8(d2)));
			neigh_hi = vaddq_u16(neigh_hi, vaddq_u16(vmovltq_u8(d1), vmovltq_u8(d0)));
			neigh_hi = vaddq_u16(neigh_hi, vaddq_u16(vmovltq_u8(d_1), vmovltq_u8(d_2)));
			neigh_hi = vaddq_u16(neigh_hi, vaddq_u16(vmovltq_u8(e2), vaddq_u16(vmovltq_u8(e1), vaddq_u16(vmovltq_u8(e0), vaddq_u16(vmovltq_u8(e_1), vmovltq_u8(e_2))))));

			uint16x8_t c_lo = vaddq_u16(vshlq_n_u16(vmovlbq_u8(c0), 4), vshlq_n_u16(vmovlbq_u8(c0), 3));
			uint16x8_t c_hi = vaddq_u16(vshlq_n_u16(vmovltq_u8(c0), 4), vshlq_n_u16(vmovltq_u8(c0), 3));

			uint16x8_t res_lo = vqsubq_u16(c_lo, neigh_lo);
			uint16x8_t res_hi = vqsubq_u16(c_hi, neigh_hi);

			uint8x16_t outv = vdupq_n_u8(0);
			outv = vqmovnbq_u16(outv, res_lo);
			outv = vqmovntq_u16(outv, res_hi);
			uint8x16_t chOffs = vaddq_n_u8(offs3, (uint8_t)ch);
			vstrbq_scatter_offset_p_s8((int8_t *)out_base, vreinterpretq_s8_u8(chOffs), vreinterpretq_s8_u8(outv), p0);
		}

		x += blk;
		remain = width - x;
	}

	/* Scalar tail for last up to two pixels */
	for (; remain > 0; --remain, ++x) {
		uint16_t xm2 = (x >= 2) ? (uint16_t)(x - 2) : 0;
		uint16_t xm1 = (x >= 1) ? (uint16_t)(x - 1) : 0;
		uint16_t xp1 = (x + 1 < width) ? (uint16_t)(x + 1) : (uint16_t)(width - 1);
		uint16_t xp2 = (x + 2 < width) ? (uint16_t)(x + 2) : (uint16_t)(width - 1);
		for (int ch = 0; ch < 3; ++ch) {
			uint32_t sum = 0;
			/* sum neighbors around center (exclude center) */
			const uint8_t *rows[5] = { r0, r1, r2, r3, r4 };
			uint16_t xs[5] = { xm2, xm1, x, xp1, xp2 };
			for (int rr = 0; rr < 5; ++rr) {
				uint16_t xx = xs[rr];
				sum += rows[rr][xx*3 + ch];
			}
			/* add cross and diagonals excluding center */
			sum += r1[(xm2)*3 + ch] + r1[(xp2)*3 + ch] + r3[(xm2)*3 + ch] + r3[(xp2)*3 + ch];
			sum += r0[(xm1)*3 + ch] + r0[(xp1)*3 + ch] + r4[(xm1)*3 + ch] + r4[(xp1)*3 + ch];
			uint32_t cen = (uint32_t)r2[x*3 + ch];
			uint32_t res = (cen * 24u > sum) ? (cen * 24u - sum) : 0u;
			out[x*3+ch] = (uint8_t)(res > 255u ? 255u : res);
		}
	}
}


