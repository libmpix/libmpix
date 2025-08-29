/* SPDX-License-Identifier: Apache-2.0 */

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#ifndef MPIX_DEBUG
#define MPIX_DEBUG 0
#endif

#include "arm_mve.h"

/*
 * ARM Helium MVE optimized Debayer (2x2) for Bayer -> RGB24 (AoS)
 *
 * Strategy:
 * - Process 32 pixels per chunk via 16 SIMD lanes representing even indices (x, x+2, ...).
 * - Compute even- and odd-index outputs separately with vector gathers from src0/src1.
 * - Use predication (vctp8q) for tails and clamp (+1) neighbor accesses at right border.
 * - Scatter-store into interleaved RGB24 using computed byte offsets (3*x + {0,1,2}).
 *
 * Note: width is guaranteed even by callers (assert kept for safety).
 */

static inline uint8x16_t mpix_vidup2_u8(void)
{
	/* 0,2,4,...,30 */
	return vidupq_n_u8(0, 2);
}

static inline uint8x16_t mpix_mul3_u8(uint8x16_t v)
{
	return vmulq_n_u8(v, 3);
}

static inline uint8x16_t mpix_pack_avg_u8(uint8x16_t a, uint8x16_t b)
{
	/* average = (a + b) >> 1 without rounding; widen to u16 to avoid overflow */
	uint16x8_t alo = vmovlbq_u8(a), ahi = vmovltq_u8(a);
	uint16x8_t blo = vmovlbq_u8(b), bhi = vmovltq_u8(b);
	uint16x8_t slo = vshrq_n_u16(vaddq_u16(alo, blo), 1);
	uint16x8_t shi = vshrq_n_u16(vaddq_u16(ahi, bhi), 1);
	uint8x16_t out = vqmovnbq_u16(vdupq_n_u8(0), slo);
	out = vqmovntq_u16(out, shi);
	return out;
}

/* Right-edge clamp helpers for gather offsets
 * - mpix_last_q_u8(rem): broadcast of (rem-1), i.e. max valid index within the current chunk
 * - mpix_last_q_minus1_u8(rem): broadcast of (rem-2) clamped to 0 when rem < 2
 */
static inline uint8x16_t mpix_last_q_u8(uint16_t rem)
{
	return vdupq_n_u8((uint8_t)(rem - 1));
}

static inline uint8x16_t mpix_last_q_minus1_u8(uint16_t rem)
{
	return vdupq_n_u8((uint8_t)(rem >= 2 ? (rem - 2) : 0));
}

static inline void mpix_scatter_rgb_block(uint8_t *dst_base3x, uint8x16_t off3x,
										  uint8x16_t r, uint8x16_t g, uint8x16_t b,
										  mve_pred16_t p)
{
	/* Store R,G,B to 3*x + {0,1,2} with predication */
	uint8x16_t off_g = vaddq_n_u8(off3x, 1);
	vstrbq_scatter_offset_p_s8((int8_t *)dst_base3x, vreinterpretq_s8_u8(off_g),
							   vreinterpretq_s8_u8(g), p);
	uint8x16_t off_b = vaddq_n_u8(off3x, 2);
	vstrbq_scatter_offset_p_s8((int8_t *)dst_base3x, vreinterpretq_s8_u8(off_b),
							   vreinterpretq_s8_u8(b), p);
	vstrbq_scatter_offset_p_s8((int8_t *)dst_base3x, vreinterpretq_s8_u8(off3x),
							   vreinterpretq_s8_u8(r), p);
}

/* ============ RGGB 2x2 ============ */

void mpix_convert_rggb8_to_rgb24_2x2(const uint8_t *src0, const uint8_t *src1, uint8_t *dst,
									 uint16_t width)
{
	assert(width >= 2 && (width % 2) == 0);

	uint16_t x = 0;
	while (x < width) {
		uint16_t rem = (uint16_t)(width - x);
		uint16_t chunk = rem > 32 ? 32 : rem;
		uint16_t pairs = (uint16_t)(chunk >> 1);

		mve_pred16_t p = vctp8q((uint32_t)pairs);
	uint8x16_t inc2 = mpix_vidup2_u8();
	uint8x16_t last = mpix_last_q_u8(chunk);

		const uint8_t *s0 = src0 + x;
		const uint8_t *s1 = src1 + x;
		uint8_t *d = dst + 3 * x;

		/* Even: rggb(r0,g0,g1,b0) -> R=r0, G=avg(g0,g1), B=b0 */
		uint8x16_t offE = inc2;
		uint8x16_t offE_p1 = vminq_u8(vaddq_n_u8(offE, 1), last);
		uint8x16_t rE = vldrbq_gather_offset_z_u8(s0, offE, p);
		uint8x16_t gE0 = vldrbq_gather_offset_z_u8(s0, offE_p1, p);
		uint8x16_t gE1 = vldrbq_gather_offset_z_u8(s1, offE, p);
		uint8x16_t bE = vldrbq_gather_offset_z_u8(s1, offE_p1, p);
		uint8x16_t gE = mpix_pack_avg_u8(gE0, gE1);

		/* Odd: grbg(g0,r0,b0,g1) -> R=r0, G=avg(g0,g1), B=b0 */
	uint8x16_t offO = vaddq_n_u8(offE, 1);
	uint8x16_t offO_p1 = vminq_u8(vaddq_n_u8(offO, 1), last);
	uint8x16_t rO_off  = offO_p1;
	uint8x16_t gO0_off = vsubq_n_u8(offO, 1);
	uint8x16_t gO1_off = offO_p1;

		uint8x16_t rO = vldrbq_gather_offset_z_u8(s0, rO_off, p);
		uint8x16_t gO0 = vldrbq_gather_offset_z_u8(s0, gO0_off, p);
		uint8x16_t gO1 = vldrbq_gather_offset_z_u8(s1, gO1_off, p);
		uint8x16_t bO = vldrbq_gather_offset_z_u8(s1, offO, p);
		uint8x16_t gO = mpix_pack_avg_u8(gO0, gO1);

		uint8x16_t off3E = mpix_mul3_u8(offE);
		mpix_scatter_rgb_block(d, off3E, rE, gE, bE, p);
		uint8x16_t off3O = vaddq_n_u8(off3E, 3);
		mpix_scatter_rgb_block(d, off3O, rO, gO, bO, p);

		x = (uint16_t)(x + (pairs << 1));
	}
}

/* ============ GBRG 2x2 ============ */

void mpix_convert_gbrg8_to_rgb24_2x2(const uint8_t *src0, const uint8_t *src1, uint8_t *dst,
									 uint16_t width)
{
	assert(width >= 2 && (width % 2) == 0);

	uint16_t x = 0;
	while (x < width) {
		uint16_t rem = (uint16_t)(width - x);
		uint16_t chunk = rem > 32 ? 32 : rem;
		uint16_t pairs = (uint16_t)(chunk >> 1);

		mve_pred16_t p = vctp8q((uint32_t)pairs);
	uint8x16_t inc2 = mpix_vidup2_u8();
	uint8x16_t last = mpix_last_q_u8(chunk);

		const uint8_t *s0 = src0 + x;
		const uint8_t *s1 = src1 + x;
		uint8_t *d = dst + 3 * x;

		/* Even: gbrg(g0,b0,r0,g1) -> R=r0, G=avg(g0,g1), B=b0 */
		uint8x16_t offE = inc2;
		uint8x16_t offE_p1 = vminq_u8(vaddq_n_u8(offE, 1), last);
		uint8x16_t gE0 = vldrbq_gather_offset_z_u8(s0, offE, p);
		uint8x16_t bE  = vldrbq_gather_offset_z_u8(s0, offE_p1, p);
		uint8x16_t rE  = vldrbq_gather_offset_z_u8(s1, offE, p);
		uint8x16_t gE1 = vldrbq_gather_offset_z_u8(s1, offE_p1, p);
		uint8x16_t gE = mpix_pack_avg_u8(gE0, gE1);

		/* Odd: bggr(b0,g0,g1,r0) -> R=r0, G=avg(g0,g1), B=b0 */
	uint8x16_t offO = vaddq_n_u8(offE, 1);
	uint8x16_t offO_p1 = vminq_u8(vaddq_n_u8(offO, 1), last);
	uint8x16_t bO_off  = offO; /* src0[o] */
	uint8x16_t gO0_off = offO_p1;
	uint8x16_t gO1_off = offO; /* src1[o] */
	uint8x16_t rO_off  = offO_p1;

		uint8x16_t bO  = vldrbq_gather_offset_z_u8(s0, bO_off, p);
		uint8x16_t gO0 = vldrbq_gather_offset_z_u8(s0, gO0_off, p);
		uint8x16_t gO1 = vldrbq_gather_offset_z_u8(s1, gO1_off, p);
		uint8x16_t rO  = vldrbq_gather_offset_z_u8(s1, rO_off, p);
		uint8x16_t gO = mpix_pack_avg_u8(gO0, gO1);

		uint8x16_t off3E = mpix_mul3_u8(offE);
		mpix_scatter_rgb_block(d, off3E, rE, gE, bE, p);
		uint8x16_t off3O = vaddq_n_u8(off3E, 3);
		mpix_scatter_rgb_block(d, off3O, rO, gO, bO, p);

		x = (uint16_t)(x + (pairs << 1));
	}
}

/* ============ GRBG 2x2 ============ */

void mpix_convert_grbg8_to_rgb24_2x2(const uint8_t *src0, const uint8_t *src1, uint8_t *dst,
									 uint16_t width)
{
	assert(width >= 2 && (width % 2) == 0);

	uint16_t x = 0;
	while (x < width) {
		uint16_t rem = (uint16_t)(width - x);
		uint16_t chunk = rem > 32 ? 32 : rem;
		uint16_t pairs = (uint16_t)(chunk >> 1);

		mve_pred16_t p = vctp8q((uint32_t)pairs);
	uint8x16_t inc2 = mpix_vidup2_u8();
	uint8x16_t last = mpix_last_q_u8(chunk);

		const uint8_t *s0 = src0 + x;
		const uint8_t *s1 = src1 + x;
		uint8_t *d = dst + 3 * x;

		/* Even: grbg(g0,r0,b0,g1) -> R=r0, G=avg(g0,g1), B=b0 */
		uint8x16_t offE = inc2;
		uint8x16_t offE_p1 = vminq_u8(vaddq_n_u8(offE, 1), last);
		uint8x16_t gE0 = vldrbq_gather_offset_z_u8(s0, offE, p);
		uint8x16_t rE  = vldrbq_gather_offset_z_u8(s0, offE_p1, p);
		uint8x16_t bE  = vldrbq_gather_offset_z_u8(s1, offE, p);
		uint8x16_t gE1 = vldrbq_gather_offset_z_u8(s1, offE_p1, p);
		uint8x16_t gE = mpix_pack_avg_u8(gE0, gE1);

		/* Odd: rggb(r0,g0,g1,b0) */
	uint8x16_t offO = vaddq_n_u8(offE, 1);
	uint8x16_t offO_p1 = vminq_u8(vaddq_n_u8(offO, 1), last);
	uint8x16_t rO_off  = offO; /* src0[o] */
	uint8x16_t gO0_off = offO_p1;
	uint8x16_t gO1_off = offO; /* src1[o] */
	uint8x16_t bO_off  = offO_p1;

		uint8x16_t rO  = vldrbq_gather_offset_z_u8(s0, rO_off, p);
		uint8x16_t gO0 = vldrbq_gather_offset_z_u8(s0, gO0_off, p);
		uint8x16_t gO1 = vldrbq_gather_offset_z_u8(s1, gO1_off, p);
		uint8x16_t bO  = vldrbq_gather_offset_z_u8(s1, bO_off, p);
		uint8x16_t gO = mpix_pack_avg_u8(gO0, gO1);

		uint8x16_t off3E = mpix_mul3_u8(offE);
		mpix_scatter_rgb_block(d, off3E, rE, gE, bE, p);
		uint8x16_t off3O = vaddq_n_u8(off3E, 3);
		mpix_scatter_rgb_block(d, off3O, rO, gO, bO, p);

		x = (uint16_t)(x + (pairs << 1));
	}
}

static inline uint8x16_t mpix_avg4_u8(uint8x16_t a, uint8x16_t b, uint8x16_t c, uint8x16_t d)
{
	/* average of four u8 values: (a+b+c+d)>>2 using 16-bit accumulators */
	uint16x8_t alo = vmovlbq_u8(a), ahi = vmovltq_u8(a);
	uint16x8_t blo = vmovlbq_u8(b), bhi = vmovltq_u8(b);
	uint16x8_t clo = vmovlbq_u8(c), chi = vmovltq_u8(c);
	uint16x8_t dlo = vmovlbq_u8(d), dhi = vmovltq_u8(d);
	uint16x8_t slo = vaddq_u16(vaddq_u16(alo, blo), vaddq_u16(clo, dlo));
	uint16x8_t shi = vaddq_u16(vaddq_u16(ahi, bhi), vaddq_u16(chi, dhi));
	slo = vshrq_n_u16(slo, 2);
	shi = vshrq_n_u16(shi, 2);
	uint8x16_t out = vqmovnbq_u16(vdupq_n_u8(0), slo);
	out = vqmovntq_u16(out, shi);
	return out;
}

/* ============ RGGB 3x3 ============ */

void mpix_convert_rggb8_to_rgb24_3x3(const uint8_t *i0, const uint8_t *i1, const uint8_t *i2,
									 uint8_t *o0, uint16_t w)
{
	assert(w >= 4 && (w % 2) == 0);

	static int dbg_calls = 0;

	/* Write first two pixels explicitly: x=0 (GRBG with left-fold), x=1 (RGGB) */
	{
		/* x=0: GRBG with left-fold */
		uint8_t g0 = i1[0];
		uint8_t b0 = i1[1];
		/* R = avg(i0[0], i2[0]) per GRBG(left-fold) */
		uint8_t r0 = (uint8_t)(((uint16_t)i0[0] + (uint16_t)i2[0]) >> 1);
		o0[0] = r0; o0[1] = g0; o0[2] = b0;
		/* x=1: RGGB using window [0..2] */
		uint16_t r1 = (uint16_t)i0[0] + i0[2] + i2[0] + i2[2];
		uint16_t g1 = (uint16_t)i0[1] + i1[2] + i1[0] + i2[1];
		o0[3] = (uint8_t)(r1 >> 2);
		o0[4] = (uint8_t)(g1 >> 2);
		o0[5] = i1[1];

		/* Debug: show input neighborhood and first two outputs */
		#if MPIX_DEBUG
		printf("DBG H3x3 RGGB: w=%u in0:%02x %02x %02x in1:%02x %02x %02x in2:%02x %02x %02x\n",
			w, i0[0], i0[1], i0[2], i1[0], i1[1], i1[2], i2[0], i2[1], i2[2]);
		printf("DBG H3x3 RGGB: out[x=0]=%02x,%02x,%02x out[x=1]=%02x,%02x,%02x\n",
			o0[0], o0[1], o0[2], o0[3], o0[4], o0[5]);
		#endif
	}

	uint16_t x = 2;
	while (x < w - 2) {
		uint16_t rem = (uint16_t)(w - x);
		uint16_t maxPairs = (uint16_t)((rem - 2) >> 1);
		uint16_t pairs = maxPairs > 16 ? 16 : maxPairs;

	mve_pred16_t p = vctp8q((uint32_t)pairs);
	uint8x16_t offE = mpix_vidup2_u8();
	uint8x16_t last = mpix_last_q_u8(rem);
	uint8x16_t offE_p1 = vminq_u8(vaddq_n_u8(offE, 1), last);
	uint8x16_t offE_p2 = vminq_u8(vaddq_n_u8(offE, 2), last);

		const uint8_t *t = i0 + x;
		const uint8_t *m = i1 + x;
		const uint8_t *b = i2 + x;
		uint8_t *d = o0 + 3 * x;

		/* Even lanes (absolute X even): GRBG with window start at X-1.
		   Use base pointers shifted by -1 so center is at offE_p1. */
		const uint8_t *tE = i0 + x - 1;
		const uint8_t *mE = i1 + x - 1;
		const uint8_t *bE = i2 + x - 1;
		uint8x16_t R_even = mpix_pack_avg_u8(vldrbq_gather_offset_z_u8(tE, offE_p1, p),
										  vldrbq_gather_offset_z_u8(bE, offE_p1, p));
		uint8x16_t G_even = vldrbq_gather_offset_z_u8(mE, offE_p1, p);
		uint8x16_t B_even = mpix_pack_avg_u8(vldrbq_gather_offset_z_u8(mE, offE, p),
										  vldrbq_gather_offset_z_u8(mE, offE_p2, p));

		/* Odd lanes (x+1): RGGB */
		uint8x16_t Rt0 = vldrbq_gather_offset_z_u8(t, offE, p);
		uint8x16_t Rt2 = vldrbq_gather_offset_z_u8(t, offE_p2, p);
		uint8x16_t Rb0 = vldrbq_gather_offset_z_u8(b, offE, p);
		uint8x16_t Rb2 = vldrbq_gather_offset_z_u8(b, offE_p2, p);
		uint8x16_t R_odd = mpix_avg4_u8(Rt0, Rt2, Rb0, Rb2);
		uint8x16_t G_odd = mpix_avg4_u8(vldrbq_gather_offset_z_u8(t, offE_p1, p),
										 vldrbq_gather_offset_z_u8(m, offE_p2, p),
										 vldrbq_gather_offset_z_u8(m, offE, p),
										 vldrbq_gather_offset_z_u8(b, offE_p1, p));
		uint8x16_t B_odd = vldrbq_gather_offset_z_u8(m, offE_p1, p);

		uint8x16_t off3E = mpix_mul3_u8(offE);
		mpix_scatter_rgb_block(d, off3E, R_even, G_even, B_even, p);
		uint8x16_t off3O = vaddq_n_u8(off3E, 3);
		mpix_scatter_rgb_block(d, off3O, R_odd, G_odd, B_odd, p);

	/* First block detailed debug: inspect lane0 outputs and verify memory writes */
	#if MPIX_DEBUG
	static int dbg_once_detail = 0;
		if (!dbg_once_detail && x == 1) {
			uint8_t buf[16];
			vstrbq_u8(buf, R_even); uint8_t re0 = buf[0];
			vstrbq_u8(buf, G_even); uint8_t ge0 = buf[0];
			vstrbq_u8(buf, B_even); uint8_t be0 = buf[0];
			vstrbq_u8(buf, R_odd);  uint8_t ro0 = buf[0];
			vstrbq_u8(buf, G_odd);  uint8_t go0 = buf[0];
			vstrbq_u8(buf, B_odd);  uint8_t bo0 = buf[0];
			uint8_t m_x_r = d[0],  m_x_g = d[1],  m_x_b = d[2];
			uint8_t m_x1_r = d[3], m_x1_g = d[4], m_x1_b = d[5];
			printf("DBG H3x3 GBRG: lane0 even->x=%u val=%02x,%02x,%02x | odd->x=%u val=%02x,%02x,%02x | mem[x]=%02x,%02x,%02x mem[x+1]=%02x,%02x,%02x\n",
				(unsigned)(x + 0), re0, ge0, be0,
				(unsigned)(x + 1), ro0, go0, bo0,
				m_x_r, m_x_g, m_x_b, m_x1_r, m_x1_g, m_x1_b);
			dbg_once_detail = 1;
		}
		#endif

		x = (uint16_t)(x + (pairs << 1));
	}

	/* Tail two pixels: x=w-2 (GRBG), x=w-1 (RGGB with right-fold) */
	{
		uint16_t xm2 = (uint16_t)(w - 2);
		/* x=w-2 GRBG uses window start j=w-3 -> center at w-2 */
		uint32_t o = (uint32_t)xm2 * 3u;
		uint8_t r = (uint8_t)(((uint16_t)i0[xm2] + (uint16_t)i2[xm2]) >> 1); /* top/bot at w-2 */
		uint8_t g = i1[xm2];                                                /* center at w-2 */
		uint8_t b = (uint8_t)(((uint16_t)i1[xm2 - 1] + (uint16_t)i1[xm2 + 1]) >> 1); /* w-3 & w-1 */
		o0[o + 0] = r; o0[o + 1] = g; o0[o + 2] = b;
		/* x=w-1 RGGB with right-fold */
		uint16_t w2 = (uint16_t)(w - 2);
		uint16_t w1 = (uint16_t)(w - 1);
		o = (uint32_t)w1 * 3u;
		uint8_t rr = (uint8_t)(((uint16_t)i0[w2] + (uint16_t)i2[w2]) >> 1);
		uint8_t gg = (uint8_t)((((uint16_t)i0[w1] + (uint16_t)i2[w1]) + ((uint16_t)i1[w2] << 1)) >> 2);
		o0[o + 0] = rr; o0[o + 1] = gg; o0[o + 2] = i1[w1];

		/* Debug: show tail two outputs */
		#if MPIX_DEBUG
		printf("DBG H3x3 RGGB: out[x=%u]=%02x,%02x,%02x out[x=%u]=%02x,%02x,%02x\n",
			xm2, o0[o - 3], o0[o - 2], o0[o - 1], w1, o0[o + 0], o0[o + 1], o0[o + 2]);
		#endif
	}

	if (dbg_calls < 2) {
		#if MPIX_DEBUG
		int first_bad = -1; uint8_t bad_val = 0;
		for (uint16_t j = 0; j < w; ++j) {
			uint8_t rv = o0[j * 3 + 0];
			if (rv != 0) { first_bad = j; bad_val = rv; break; }
		}
		if (first_bad >= 0) {
			printf("DBG H3x3 RGGB: first nonzero R at x=%d val=%02x\n", first_bad, bad_val);
		} else {
			printf("DBG H3x3 RGGB: R row all zero as expected\n");
		}
		#endif
	}
	dbg_calls++;
}

/* ============ BGGR 3x3 ============ */

void mpix_convert_bggr8_to_rgb24_3x3(const uint8_t *i0, const uint8_t *i1, const uint8_t *i2,
									 uint8_t *o0, uint16_t w)
{
	assert(w >= 4 && (w % 2) == 0);

	/* Left border x=0: GBRG (left-fold) to match scalar semantics
	 * Equivalent folded indices: x0=1, x1=0, x2=1
	 * R = avg(i1[x0], i1[x2]) = i1[1]
	 * G = i1[x1] = i1[0]
	 * B = avg(i0[x1], i2[x1]) = avg(i0[0], i2[0])
	 */
	{
		uint8_t R0 = i1[1];
		uint8_t G0 = i1[0];
		uint8_t B0 = (uint8_t)((((uint16_t)i0[0]) + ((uint16_t)i2[0])) >> 1);
		o0[0] = R0; o0[1] = G0; o0[2] = B0;
	}

	uint16_t x = 0;
	while (x < w) {
		uint16_t rem = (uint16_t)(w - x);
	/* Even lanes (offE) write c1 = x+offE+1 (BGGR); odd lanes write c2 = x+offE+2 (GBRG)
	 * When rem==2 only c1 is valid; split predicates for c1/c2 accordingly.
	 */
		uint16_t pairs_c1 = (uint16_t)(rem >> 1);            /* floor(rem/2) */
		uint16_t pairs_c2 = (rem >= 3) ? (uint16_t)((rem - 1) >> 1) : 0; /* floor((rem-1)/2) */
		if (pairs_c1 == 0) break;
		uint16_t lanes1 = pairs_c1 > 16 ? 16 : pairs_c1;
		uint16_t lanes2 = pairs_c2 > 16 ? 16 : pairs_c2;

	mve_pred16_t p1 = vctp8q((uint32_t)lanes1);
	mve_pred16_t p2 = vctp8q((uint32_t)lanes2);
	uint8x16_t offE = mpix_vidup2_u8();
	uint8x16_t last1 = mpix_last_q_u8(rem);
	uint8x16_t last2 = mpix_last_q_minus1_u8(rem);
	uint8x16_t offE_p1 = vminq_u8(vaddq_n_u8(offE, 1), last1);
	/* Reuse clamped offE+2 for both c1 and c2 where applicable; offE+3 uses last1 */
	uint8x16_t offE_p2 = vminq_u8(vaddq_n_u8(offE, 2), last2);
	uint8x16_t offE_p3_c2 = vminq_u8(vaddq_n_u8(offE, 3), last1);

	const uint8_t *t = i0 + x;
	const uint8_t *m = i1 + x;
	const uint8_t *b = i2 + x;
	uint8_t *d = o0 + 3 * x;

		  /* Even (BGGR at j=x+offE+1):
			  R = mid[j]
			  G = avg(top[j], mid[j+1], mid[j-1], bot[j])
			  B = avg(top[j-1], top[j+1], bot[j-1], bot[j+1])
		  */
		uint8x16_t R_even = vldrbq_gather_offset_z_u8(m, offE_p1, p1);

		uint8x16_t G_t1 = vldrbq_gather_offset_z_u8(t, offE_p1, p1);
	uint8x16_t G_m2 = vldrbq_gather_offset_z_u8(m, offE_p2, p1);
		uint8x16_t G_m0 = vldrbq_gather_offset_z_u8(m, offE, p1);
		uint8x16_t G_b1 = vldrbq_gather_offset_z_u8(b, offE_p1, p1);
		uint8x16_t G_even = mpix_avg4_u8(G_t1, G_m2, G_m0, G_b1);

		uint8x16_t Bt0 = vldrbq_gather_offset_z_u8(t, offE, p1);
	uint8x16_t Bt2 = vldrbq_gather_offset_z_u8(t, offE_p2, p1);
		uint8x16_t Bb0 = vldrbq_gather_offset_z_u8(b, offE, p1);
	uint8x16_t Bb2 = vldrbq_gather_offset_z_u8(b, offE_p2, p1);
		uint8x16_t B_even = mpix_avg4_u8(Bt0, Bt2, Bb0, Bb2);

		  /* Odd (GBRG at j=x+offE+2):
			  R = avg(m[j-1], m[j+1]) = avg(m[offE_p1], m[offE_p3])
			  G = m[j]                = m[offE_p2]
			  B = avg(t[j], b[j])     = avg(t[offE_p2], b[offE_p2])
		  */
		  uint8x16_t R_odd = mpix_pack_avg_u8(vldrbq_gather_offset_z_u8(m, offE_p1, p2),
											  vldrbq_gather_offset_z_u8(m, offE_p3_c2, p2));
		  uint8x16_t G_odd = vldrbq_gather_offset_z_u8(m, offE_p2, p2);
		  uint8x16_t B_odd = mpix_pack_avg_u8(vldrbq_gather_offset_z_u8(t, offE_p2, p2),
											  vldrbq_gather_offset_z_u8(b, offE_p2, p2));

	/* 存储到中心列：c1 = x+offE+1, c2 = x+offE+2 */
	uint8x16_t off3E = mpix_mul3_u8(offE);
	uint8x16_t off3_c1 = vaddq_n_u8(off3E, 3); /* dst pixel = x + offE + 1 */
	mpix_scatter_rgb_block(d, off3_c1, R_even, G_even, B_even, p1);
	uint8x16_t off3_c2 = vaddq_n_u8(off3E, 6); /* dst pixel = x + offE + 2 */
	mpix_scatter_rgb_block(d, off3_c2, R_odd, G_odd, B_odd, p2);

		x = (uint16_t)(x + (lanes1 << 1));
	}

	/* Debug (once): first non-zero R/B columns to check channel alignment */
	static int dbg_calls_bggr = 0;
	if (dbg_calls_bggr < 2) {
		#if MPIX_DEBUG
		int first_r = -1, first_b = -1; uint8_t rv = 0, bv = 0;
		for (uint16_t j = 0; j < w; ++j) {
			uint8_t r = o0[j*3 + 0];
			uint8_t b = o0[j*3 + 2];
			if (first_r < 0 && r != 0) { first_r = j; rv = r; }
			if (first_b < 0 && b != 0) { first_b = j; bv = b; }
			if (first_r >= 0 && first_b >= 0) break;
		}
		if (first_r >= 0)
			printf("DBG H3x3 BGGR: first nonzero R at x=%d val=%02x\n", first_r, rv);
		else
			printf("DBG H3x3 BGGR: R row all zero as expected\n");
			printf("DBG H3x3 BGGR: B row all zero as expected\n");
		#endif
	}

	/* No explicit right-edge needed: when rem==2, the c1 path writes j=w-1. */

	/* One-shot scalar truth validation (debug only): BGGR row alternates (odd=BGGR, even=GBRG) */
	static int dbg_val_once_bggr = 0;
	#if MPIX_DEBUG
	if (!dbg_val_once_bggr) {
		for (uint16_t j = 0; j < w; ++j) {
			int x0 = (j == 0) ? 1 : (int)j - 1;
			int x1 = (j == 0) ? 0 : (int)j;
			int x2 = (j == 0) ? 1 : (int)j + 1;
			if (j == (uint16_t)(w - 1)) { x0 = (int)w - 2; x1 = (int)w - 1; x2 = (int)w - 2; }

			uint8_t expR, expG, expB;
			if ((j & 1u) != 0) {
				/* j奇: BGGR */
				expR = i1[x1];
				expG = (uint8_t)((((uint16_t)i0[x1]) + ((uint16_t)i1[x2]) + ((uint16_t)i1[x0]) + ((uint16_t)i2[x1])) >> 2);
				expB = (uint8_t)((((uint16_t)i0[x0]) + ((uint16_t)i0[x2]) + ((uint16_t)i2[x0]) + ((uint16_t)i2[x2])) >> 2);
			} else {
				/* j偶: GBRG */
				expR = (uint8_t)((((uint16_t)i1[x0]) + ((uint16_t)i1[x2])) >> 1);
				expG = i1[x1];
				expB = (uint8_t)((((uint16_t)i0[x1]) + ((uint16_t)i2[x1])) >> 1);
			}

			uint8_t gotR = o0[j*3 + 0];
			uint8_t gotG = o0[j*3 + 1];
			uint8_t gotB = o0[j*3 + 2];
			if (gotR != expR || gotG != expG || gotB != expB) {
				printf("DBG H3x3 BGGR: MISMATCH at x=%u got=%02x,%02x,%02x exp=%02x,%02x,%02x window t:%02x %02x %02x m:%02x %02x %02x b:%02x %02x %02x\n",
					j, gotR, gotG, gotB, expR, expG, expB,
					i0[x0], i0[x1], i0[x2], i1[x0], i1[x1], i1[x2], i2[x0], i2[x1], i2[x2]);
				break;
			}
		}
		dbg_val_once_bggr = 1;
	}
	#endif
}

/* ============ GBRG 3x3 ============ */

void mpix_convert_gbrg8_to_rgb24_3x3(const uint8_t *i0, const uint8_t *i1, const uint8_t *i2,
									 uint8_t *o0, uint16_t w)
{
	assert(w >= 4 && (w % 2) == 0);

	static int dbg_calls_gbrg = 0;

	/* Left border: x=0 uses BGGR (left-fold) to match scalar FOLD_L_3X3 + mpix_bggr8_to_rgb24_3x3 */
	{
		/* R = grg1[1] = i1[0] */
		uint8_t R0 = i1[0];
		/* G = avg(bgb0[1], grg1[2], grg1[0], bgb2[1]) = avg(i0[0], i1[1], i1[1], i2[0]) */
		uint8_t G0 = (uint8_t) ((((uint16_t)i0[0] + (uint16_t)i2[0]) + ((uint16_t)i1[1] << 1)) >> 2);
		/* B = avg(bgb0[0], bgb0[2], bgb2[0], bgb2[2]) = avg(i0[1], i0[1], i2[1], i2[1]) */
		uint8_t B0 = (uint8_t) ((((uint16_t)i0[1] + (uint16_t)i2[1]) ) >> 1);
		o0[0] = R0; o0[1] = G0; o0[2] = B0;
	}

	uint16_t x = 1;
	while (x < (uint16_t)(w - 1)) {
		uint16_t rem = (uint16_t)(w - x);
	/* Cover up to x=w-2: we have floor(rem/2) even/odd lane pairs */
		uint16_t maxPairs = (uint16_t)(rem >> 1);
		if (maxPairs == 0) break;
		uint16_t pairs = maxPairs > 16 ? 16 : maxPairs;

		mve_pred16_t p = vctp8q((uint32_t)pairs);
		uint8x16_t offE = mpix_vidup2_u8();
	uint8x16_t last = mpix_last_q_u8(rem);
		uint8x16_t offE_p1 = vminq_u8(vaddq_n_u8(offE, 1), last);
		uint8x16_t offE_p2 = vminq_u8(vaddq_n_u8(offE, 2), last);

	/* Base pointers: even (GBRG) needs pos-1/pos+1 so use x-1; odd (BGGR) centers at x+offE+1 so use x. */
		const uint8_t *t_even = i0 + x - 1; /* for GBRG lanes (absolute pos = x+offE) */
		const uint8_t *m_even = i1 + x - 1;
		const uint8_t *b_even = i2 + x - 1;
		const uint8_t *t_odd  = i0 + x;     /* for BGGR lanes (absolute pos = x+offE+1) */
		const uint8_t *m_odd  = i1 + x;
		const uint8_t *b_odd  = i2 + x;
		uint8_t *d = o0 + 3 * x;

		/* Even lanes (absolute pos = x+offE, which is odd since x starts at 1): GBRG
		 * R = avg(m[pos-1], m[pos+1]) -> m_even[offE], m_even[offE_p2]
		 * G = m[pos]                  -> m_even[offE_p1]
		 * B = avg(t[pos], b[pos])     -> t_even[offE_p1], b_even[offE_p1]
		 */
		uint8x16_t R_even = mpix_pack_avg_u8(vldrbq_gather_offset_z_u8(m_even, offE, p),
								 vldrbq_gather_offset_z_u8(m_even, offE_p2, p));
		uint8x16_t G_even = vldrbq_gather_offset_z_u8(m_even, offE_p1, p);
		uint8x16_t B_even = mpix_pack_avg_u8(vldrbq_gather_offset_z_u8(t_even, offE_p1, p),
								 vldrbq_gather_offset_z_u8(b_even, offE_p1, p));

		/* Odd lanes: BGGR centered at pos = x + offE + 1
		 * R = grg1[1]                  -> m_odd[offE_p1]
		 * G = avg(bgb0[1], grg1[2], grg1[0], bgb2[1])
		 *   = avg(t[pos], m[pos+1], m[pos-1], b[pos]) -> t_odd[offE_p1], m_odd[offE_p2], m_odd[offE], b_odd[offE_p1]
		 * B = avg(bgb0[0], bgb0[2], bgb2[0], bgb2[2])
		 *   = avg(t[pos-1], t[pos+1], b[pos-1], b[pos+1]) -> t_odd[offE], t_odd[offE_p2], b_odd[offE], b_odd[offE_p2]
		 */
		uint8x16_t R_odd = vldrbq_gather_offset_z_u8(m_odd, offE_p1, p);
		uint8x16_t G_odd = mpix_avg4_u8(vldrbq_gather_offset_z_u8(t_odd, offE_p1, p),
								  vldrbq_gather_offset_z_u8(m_odd, offE_p2, p),
								  vldrbq_gather_offset_z_u8(m_odd, offE, p),
								  vldrbq_gather_offset_z_u8(b_odd, offE_p1, p));
		uint8x16_t B_odd = mpix_avg4_u8(vldrbq_gather_offset_z_u8(t_odd, offE, p),
								  vldrbq_gather_offset_z_u8(t_odd, offE_p2, p),
								  vldrbq_gather_offset_z_u8(b_odd, offE, p),
								  vldrbq_gather_offset_z_u8(b_odd, offE_p2, p));

		uint8x16_t off3E = mpix_mul3_u8(offE);
		mpix_scatter_rgb_block(d, off3E, R_even, G_even, B_even, p);
		uint8x16_t off3O = vaddq_n_u8(off3E, 3);
		mpix_scatter_rgb_block(d, off3O, R_odd, G_odd, B_odd, p);

		x = (uint16_t)(x + (pairs << 1));
	}

	/* Right border: x=w-1 uses GBRG (right-fold) to match scalar FOLD_R_3X3 + mpix_gbrg8_to_rgb24_3x3 */
	{
		uint16_t w1 = (uint16_t)(w - 1);
		uint16_t w2 = (uint16_t)(w - 2);
		uint32_t o = (uint32_t)w1 * 3u;
		/* R = avg(rgr1[0], rgr1[2]) = avg(i1[w-2], i1[w-2]) = i1[w-2] */
		uint8_t Rr = i1[w2];
		/* G = rgr1[1] = i1[w-1] */
		uint8_t Gr = i1[w1];
		/* B = avg(gbg0[1], gbg2[1]) = avg(i0[w-1], i2[w-1]) */
		uint8_t Br = (uint8_t) ((((uint16_t)i0[w1] + (uint16_t)i2[w1])) >> 1);
		o0[o + 0] = Rr; o0[o + 1] = Gr; o0[o + 2] = Br;
	}

	/* One-shot scalar validator (debug): first mismatch and neighborhood */
	static int dbg_val_once = 0;
	#if MPIX_DEBUG
	if (!dbg_val_once) {
		for (uint16_t j = 0; j < w; ++j) {
			/* 构造折叠后的 3 列索引 */
			int x0 = (j == 0) ? 1 : (int)j - 1;
			int x1 = (j == 0) ? 0 : (int)j;
			int x2 = (j == 0) ? 1 : (int)j + 1;
			if (j == (uint16_t)(w - 1)) { x0 = (int)w - 2; x1 = (int)w - 1; x2 = (int)w - 2; }

			uint8_t expR, expG, expB;
			if ((j & 1u) != 0) {
				expR = (uint8_t)((((uint16_t)i1[x0]) + ((uint16_t)i1[x2])) >> 1);
				expG = i1[x1];
				expB = (uint8_t)((((uint16_t)i0[x1]) + ((uint16_t)i2[x1])) >> 1);
			} else {
				/* 偶列：BGGR 公式 */
				expR = i1[x1];
				expG = (uint8_t)((((uint16_t)i0[x1]) + ((uint16_t)i1[x2]) + ((uint16_t)i1[x0]) + ((uint16_t)i2[x1])) >> 2);
				expB = (uint8_t)((((uint16_t)i0[x0]) + ((uint16_t)i0[x2]) + ((uint16_t)i2[x0]) + ((uint16_t)i2[x2])) >> 2);
			}

			uint8_t gotR = o0[j*3 + 0];
			uint8_t gotG = o0[j*3 + 1];
			uint8_t gotB = o0[j*3 + 2];
			if (gotR != expR || gotG != expG || gotB != expB) {
				printf("DBG H3x3 GBRG: MISMATCH at x=%u got=%02x,%02x,%02x exp=%02x,%02x,%02x window t:%02x %02x %02x m:%02x %02x %02x b:%02x %02x %02x\n",
					j, gotR, gotG, gotB, expR, expG, expB,
					i0[x0], i0[x1], i0[x2], i1[x0], i1[x1], i1[x2], i2[x0], i2[x1], i2[x2]);
				break;
			}
		}
		dbg_val_once = 1;
	}
	#endif /* MPIX_DEBUG */

	if (dbg_calls_gbrg < 2) {
		#if MPIX_DEBUG
		int first_bad = -1; uint8_t bad_val = 0;
		for (uint16_t j = 0; j < w; ++j) {
			uint8_t rv = o0[j * 3 + 0];
			if (rv != 0) { first_bad = j; bad_val = rv; break; }
		}
		if (first_bad >= 0) printf("DBG H3x3 GBRG: first nonzero R at x=%d val=%02x\n", first_bad, bad_val);
		else printf("DBG H3x3 GBRG: R row all zero as expected\n");

		/* 额外：标量真值校验整行，按 gbrg/bggr 交替生成并比对（调试专用） */
		static inline void ref_gbrg3x3(const uint8_t gbg0[3], const uint8_t rgr1[3], const uint8_t gbg2[3], uint8_t rgb[3]) {
			rgb[0] = (uint8_t)((((uint16_t)rgr1[0] + rgr1[2])) >> 1);
			rgb[1] = rgr1[1];
			rgb[2] = (uint8_t)((((uint16_t)gbg0[1] + gbg2[1])) >> 1);
		}
		static inline void ref_bggr3x3(const uint8_t bgb0[3], const uint8_t grg1[3], const uint8_t bgb2[3], uint8_t rgb[3]) {
			rgb[0] = grg1[1];
			rgb[1] = (uint8_t)((((uint16_t)bgb0[1] + grg1[2] + grg1[0] + bgb2[1])) >> 2);
			rgb[2] = (uint8_t)((((uint16_t)bgb0[0] + bgb0[2] + bgb2[0] + bgb2[2])) >> 2);
		}
		uint16_t W = w > 256 ? 256 : w;
		for (uint16_t j = 0; j < W; ++j) {
			uint8_t exp[3];
			if (j == 0) {
				/* 左边界：BGGR(left-fold) */
				uint8_t T[3] = { i0[1], i0[0], i0[1] };
				uint8_t M[3] = { i1[1], i1[0], i1[1] };
				uint8_t B[3] = { i2[1], i2[0], i2[1] };
				ref_bggr3x3(T,M,B,exp);
			} else if (j + 1 == W) {
				/* 右边界：GBRG(right-fold) */
				uint16_t w1 = (uint16_t)(W - 1), w2 = (uint16_t)(W - 2);
				uint8_t T[3] = { i0[w2], i0[w1], i0[w2] };
				uint8_t M[3] = { i1[w2], i1[w1], i1[w2] };
				uint8_t B[3] = { i2[w2], i2[w1], i2[w2] };
				ref_gbrg3x3(T,M,B,exp);
			} else if ((j & 1) == 0) {
				/* 偶数列：GBRG */
				uint8_t T[3] = { i0[j-1], i0[j], i0[j+1] };
				uint8_t M[3] = { i1[j-1], i1[j], i1[j+1] };
				uint8_t B[3] = { i2[j-1], i2[j], i2[j+1] };
				ref_gbrg3x3(T,M,B,exp);
			} else {
				/* 奇数列：BGGR */
				uint8_t T[3] = { i0[j-1], i0[j], i0[j+1] };
				uint8_t M[3] = { i1[j-1], i1[j], i1[j+1] };
				uint8_t B[3] = { i2[j-1], i2[j], i2[j+1] };
				ref_bggr3x3(T,M,B,exp);
			}
			uint8_t R = o0[j*3+0], G = o0[j*3+1], Bv = o0[j*3+2];
			if (R!=exp[0] || G!=exp[1] || Bv!=exp[2]) {
				printf("DBG VERIFY GBRG: mismatch at x=%u, got=%02x,%02x,%02x exp=%02x,%02x,%02x pattern=%s\n",
					j, R,G,Bv, exp[0],exp[1],exp[2], (j==0)?"LBGGR":(j+1==W)?"RGBRG":((j&1)?"BGGR":"GBRG"));
				uint16_t jm1 = (j==0)?1:(uint16_t)(j-1);
				uint16_t jp1 = (j+1>=W)?(uint16_t)(W-2):(uint16_t)(j+1);
				printf("DBG VERIFY GBRG: T=%02x %02x %02x M=%02x %02x %02x B=%02x %02x %02x\n",
					i0[jm1], i0[j], i0[jp1], i1[jm1], i1[j], i1[jp1], i2[jm1], i2[j], i2[jp1]);
				break;
			}
		}
		#endif /* MPIX_DEBUG */
	}
	dbg_calls_gbrg++;
}

/* ============ GRBG 3x3 ============ */

void mpix_convert_grbg8_to_rgb24_3x3(const uint8_t *i0, const uint8_t *i1, const uint8_t *i2,
									 uint8_t *o0, uint16_t w)
{
	assert(w >= 4 && (w % 2) == 0);

	static int dbg_calls_grbg = 0;

	/* 左边界: x=0 使用 RGGB(left-fold)，与标量保持一致 */
	{
		uint8_t R0 = (uint8_t)((((uint16_t)i0[1]) + ((uint16_t)i2[1])) >> 1);
		uint8_t G0 = (uint8_t)((((uint16_t)i0[0] + (uint16_t)i2[0]) + ((uint16_t)i1[1] << 1)) >> 2);
		uint8_t B0 = i1[0];
		o0[0] = R0; o0[1] = G0; o0[2] = B0;
	}

	/* Main loop starts at x=1 so even lanes (x+offE) cover 1,3,5,... matching scalar */
	uint16_t x = 1;
	while (x < (uint16_t)(w - 1)) {
		uint16_t rem = (uint16_t)(w - x);
	/* Cover up to x=w-2: floor(rem/2) even/odd lane pairs */
		uint16_t maxPairs = (uint16_t)(rem >> 1);
		if (maxPairs == 0) break;
		uint16_t pairs = maxPairs > 16 ? 16 : maxPairs;

		mve_pred16_t p = vctp8q((uint32_t)pairs);
		uint8x16_t offE = mpix_vidup2_u8();
	uint8x16_t last = mpix_last_q_u8(rem);
		uint8x16_t offE_p1 = vminq_u8(vaddq_n_u8(offE, 1), last);
		uint8x16_t offE_p2 = vminq_u8(vaddq_n_u8(offE, 2), last);

		/* 为 GRBG 偶槽位 (绝对位置 pos = x+offE) 使用 x-1 基指针；
		 * 为 RGGB 奇槽位 (绝对位置 pos = x+offE+1) 使用 x 基指针。
		 */
		const uint8_t *t_even = i0 + x - 1; /* for GRBG lanes */
		const uint8_t *m_even = i1 + x - 1;
		const uint8_t *b_even = i2 + x - 1;
		const uint8_t *t_odd  = i0 + x;     /* for RGGB lanes */
		const uint8_t *m_odd  = i1 + x;
		const uint8_t *b_odd  = i2 + x;
		uint8_t *d = o0 + 3 * x;

		/* Even lanes: GRBG at pos = x+offE
		 * R = avg(t[pos], b[pos])           -> t_even[offE_p1], b_even[offE_p1]
		 * G = m[pos]                        -> m_even[offE_p1]
		 * B = avg(m[pos-1], m[pos+1])      -> m_even[offE], m_even[offE_p2]
		 */
		uint8x16_t R_even = mpix_pack_avg_u8(vldrbq_gather_offset_z_u8(t_even, offE_p1, p),
								 vldrbq_gather_offset_z_u8(b_even, offE_p1, p));
		uint8x16_t G_even = vldrbq_gather_offset_z_u8(m_even, offE_p1, p);
		uint8x16_t B_even = mpix_pack_avg_u8(vldrbq_gather_offset_z_u8(m_even, offE, p),
								 vldrbq_gather_offset_z_u8(m_even, offE_p2, p));

		/* Odd lanes: RGGB at pos = x+offE+1
		 * R = avg(t[pos-1], t[pos+1], b[pos-1], b[pos+1]) -> t_odd[offE], t_odd[offE_p2], b_odd[offE], b_odd[offE_p2]
		 * G = avg(t[pos], m[pos-1], m[pos+1], b[pos])     -> t_odd[offE_p1], m_odd[offE], m_odd[offE_p2], b_odd[offE_p1]
		 * B = m[pos]                                      -> m_odd[offE_p1]
		 */
		uint8x16_t R_odd = mpix_avg4_u8(vldrbq_gather_offset_z_u8(t_odd, offE, p),
								  vldrbq_gather_offset_z_u8(t_odd, offE_p2, p),
								  vldrbq_gather_offset_z_u8(b_odd, offE, p),
								  vldrbq_gather_offset_z_u8(b_odd, offE_p2, p));
		uint8x16_t G_odd = mpix_avg4_u8(vldrbq_gather_offset_z_u8(t_odd, offE_p1, p),
								  vldrbq_gather_offset_z_u8(m_odd, offE, p),
								  vldrbq_gather_offset_z_u8(m_odd, offE_p2, p),
								  vldrbq_gather_offset_z_u8(b_odd, offE_p1, p));
		uint8x16_t B_odd = vldrbq_gather_offset_z_u8(m_odd, offE_p1, p);

		uint8x16_t off3E = mpix_mul3_u8(offE);
		mpix_scatter_rgb_block(d, off3E, R_even, G_even, B_even, p);
		uint8x16_t off3O = vaddq_n_u8(off3E, 3);
		mpix_scatter_rgb_block(d, off3O, R_odd, G_odd, B_odd, p);

		x = (uint16_t)(x + (pairs << 1));
	}

	/* 右边界: x=w-1 使用 GRBG(right-fold) */
	{
		uint16_t w1 = (uint16_t)(w - 1);
		uint16_t w2 = (uint16_t)(w - 2);
		uint32_t o = (uint32_t)w1 * 3u;
		uint8_t Rr = (uint8_t)((((uint16_t)i0[w1]) + ((uint16_t)i2[w1])) >> 1);
		uint8_t Gr = i1[w1];
		uint8_t Br = i1[w2];
		o0[o + 0] = Rr; o0[o + 1] = Gr; o0[o + 2] = Br;
	}

	if (dbg_calls_grbg < 6) {
		#if MPIX_DEBUG
		/* 额外：按标量公式生成整行真值，与 Helium 输出逐像素比对，输出首个不一致点 */
		/* 标量 3x3 辅助：直接照抄 op_debayer.c 的公式（不改变输出，仅用于校验） */
		static inline void ref_rggb3x3(const uint8_t rgr0[3], const uint8_t gbg1[3], const uint8_t rgr2[3], uint8_t rgb[3]) {
			rgb[0] = (uint8_t)((((uint16_t)rgr0[0] + rgr0[2] + rgr2[0] + rgr2[2]) ) >> 2);
			rgb[1] = (uint8_t)((((uint16_t)rgr0[1] + gbg1[2] + gbg1[0] + rgr2[1]) ) >> 2);
			rgb[2] = gbg1[1];
		}
		static inline void ref_grbg3x3(const uint8_t grg0[3], const uint8_t bgb1[3], const uint8_t grg2[3], uint8_t rgb[3]) {
			rgb[0] = (uint8_t)((((uint16_t)grg0[1] + grg2[1])) >> 1);
			rgb[1] = bgb1[1];
			rgb[2] = (uint8_t)((((uint16_t)bgb1[0] + bgb1[2])) >> 1);
		}
		/* 生成真值行 */
		uint8_t exp_rgb[3 * 256]; /* w<=256 假定足够；若更大，可裁剪到前 256 */
		uint16_t W = w > 256 ? 256 : w;
		/* x=0: RGGB(left-fold) */
		{
			uint8_t r0[3] = { i0[1], i0[0], i0[1] };
			uint8_t m0[3] = { i1[1], i1[0], i1[1] };
			uint8_t b0[3] = { i2[1], i2[0], i2[1] };
			ref_rggb3x3(r0, m0, b0, &exp_rgb[0]);
		}
		/* 中间交替：j=1..W-2 */
		for (uint16_t j = 1; j + 1 < W; ++j) {
			uint8_t T[3] = { i0[j-1], i0[j], i0[j+1] };
			uint8_t M[3] = { i1[j-1], i1[j], i1[j+1] };
			uint8_t B[3] = { i2[j-1], i2[j], i2[j+1] };
			uint8_t *dst = &exp_rgb[j*3];
			if ((j & 1) == 1) { /* 奇数列 -> GRBG */
				ref_grbg3x3(T, M, B, dst);
			} else { /* 偶数列 -> RGGB */
				ref_rggb3x3(T, M, B, dst);
			}
		}
		/* x=W-1: GRBG(right-fold) */
		if (W >= 2) {
			uint16_t w1 = (uint16_t)(W - 1);
			uint16_t w2 = (uint16_t)(W - 2);
			uint8_t T[3] = { i0[w2], i0[w1], i0[w2] };
			uint8_t M[3] = { i1[w2], i1[w1], i1[w2] };
			uint8_t B[3] = { i2[w2], i2[w1], i2[w2] };
			ref_grbg3x3(T, M, B, &exp_rgb[w1*3]);
		}
		/* 比对并打印首个不一致 */
		for (uint16_t j = 0; j < W; ++j) {
			uint8_t R = o0[j*3+0], G = o0[j*3+1], Bv = o0[j*3+2];
			uint8_t eR = exp_rgb[j*3+0], eG = exp_rgb[j*3+1], eB = exp_rgb[j*3+2];
			if (R!=eR || G!=eG || Bv!=eB) {
				printf("DBG VERIFY GRBG: mismatch at x=%u, got=%02x,%02x,%02x exp=%02x,%02x,%02x pattern=%s\n",
					j, R,G,Bv, eR,eG,eB, (j&1)?"GRBG":"RGGB");
				/* 打印该 x 邻域 */
				uint16_t jm1 = (j==0)?1:(uint16_t)(j-1);
				uint16_t jp1 = (j+1>=W)?(uint16_t)(W-2):(uint16_t)(j+1);
				printf("DBG VERIFY GRBG: T=%02x %02x %02x M=%02x %02x %02x B=%02x %02x %02x\n",
					i0[jm1], i0[j], i0[jp1], i1[jm1], i1[j], i1[jp1], i2[jm1], i2[j], i2[jp1]);
				break;
			}
		}
		#endif
	}
	dbg_calls_grbg++;
}

