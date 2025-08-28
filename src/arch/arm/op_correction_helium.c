/* SPDX-License-Identifier: Apache-2.0 */

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "arm_mve.h"

#include <mpix/op_correction.h>

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

/* -------------------- Black level -------------------- */

void mpix_correction_black_level_raw8(const uint8_t *src, uint8_t *dst, uint16_t width,
                                      uint16_t line_offset, union mpix_correction_any *corr)
{
    (void)line_offset;
    uint8_t level = corr->black_level.level;
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
                                       uint16_t line_offset, union mpix_correction_any *corr)
{
    (void)line_offset;
    uint8_t level = corr->black_level.level;
    uint8x16_t offs3 = mpix_rgb_offsets3();
    uint8x16_t offR = vaddq_n_u8(offs3, 0);
    uint8x16_t offG = vaddq_n_u8(offs3, 1);
    uint8x16_t offB = vaddq_n_u8(offs3, 2);
    mve_pred16_t p_full = vctp8q(16);
    uint8x16_t vlevel = vdupq_n_u8(level);

    uint16_t x = 0;
    for (; x + 16 <= width; x += 16) {
        uint8_t *outb = dst + (uint32_t)x * 3u;
        const uint8_t *inb = src + (uint32_t)x * 3u;
        uint8x16_t v;
        v = vldrbq_gather_offset_z_u8(inb, offR, p_full);
        v = vqsubq_u8(v, vlevel);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offR),
                                   vreinterpretq_s8_u8(v), p_full);
        v = vldrbq_gather_offset_z_u8(inb, offG, p_full);
        v = vqsubq_u8(v, vlevel);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offG),
                                   vreinterpretq_s8_u8(v), p_full);
        v = vldrbq_gather_offset_z_u8(inb, offB, p_full);
        v = vqsubq_u8(v, vlevel);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offB),
                                   vreinterpretq_s8_u8(v), p_full);
    }
    unsigned rem = (unsigned)width - x;
    if (rem) {
        uint8_t *outb = dst + (uint32_t)x * 3u;
        const uint8_t *inb = src + (uint32_t)x * 3u;
        mve_pred16_t p = mpix_tail_pred_u8(rem);
        uint8x16_t v;
        v = vldrbq_gather_offset_z_u8(inb, offR, p);
        v = vqsubq_u8(v, vdupq_n_u8(level));
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offR),
                                   vreinterpretq_s8_u8(v), p);
        v = vldrbq_gather_offset_z_u8(inb, offG, p);
        v = vqsubq_u8(v, vdupq_n_u8(level));
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offG),
                                   vreinterpretq_s8_u8(v), p);
        v = vldrbq_gather_offset_z_u8(inb, offB, p);
        v = vqsubq_u8(v, vdupq_n_u8(level));
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offB),
                                   vreinterpretq_s8_u8(v), p);
    }
}

/* -------------------- White balance (RGB24, Q10) -------------------- */

static inline uint8x16_t mpix_wb_channel_u8(uint8x16_t vin, uint16_t gain_q10)
{
    /* widen u8 -> u16 (lo/hi) */
    uint16x8_t lo16 = vmovlbq_u8(vin);
    uint16x8_t hi16 = vmovltq_u8(vin);

    /* multiply u16 * u16 -> u32 using vmull{b,t} */
    uint16x8_t g16 = vdupq_n_u16(gain_q10);
    uint32x4_t lo32a = vmullbq_int_u16(lo16, g16);
    uint32x4_t lo32b = vmulltq_int_u16(lo16, g16);
    uint32x4_t hi32a = vmullbq_int_u16(hi16, g16);
    uint32x4_t hi32b = vmulltq_int_u16(hi16, g16);

    /* rounding shift and narrow 32->16 in one step */
    uint16x8_t out16 = vqrshrnbq_n_u32(vdupq_n_u16(0), lo32a, MPIX_CORRECTION_SCALE_BITS);
    out16 = vqrshrntq_n_u32(out16, lo32b, MPIX_CORRECTION_SCALE_BITS);
    uint16x8_t out16_hi = vqrshrnbq_n_u32(vdupq_n_u16(0), hi32a, MPIX_CORRECTION_SCALE_BITS);
    out16_hi = vqrshrntq_n_u32(out16_hi, hi32b, MPIX_CORRECTION_SCALE_BITS);

    const uint16x8_t max255 = vdupq_n_u16(255);
    out16 = vminq_u16(out16, max255);
    out16_hi = vminq_u16(out16_hi, max255);

    uint8x16_t out8 = vdupq_n_u8(0);
    out8 = vqmovnbq_u16(out8, out16);
    out8 = vqmovntq_u16(out8, out16_hi);
    return out8;
}

void mpix_correction_white_balance_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width,
                                         uint16_t line_offset, union mpix_correction_any *corr)
{
    (void)line_offset;
    const uint16_t red_q10 = corr->white_balance.red_level;
    const uint16_t blue_q10 = corr->white_balance.blue_level;

    uint8x16_t offs3 = mpix_rgb_offsets3();
    uint8x16_t offR = vaddq_n_u8(offs3, 0);
    uint8x16_t offG = vaddq_n_u8(offs3, 1);
    uint8x16_t offB = vaddq_n_u8(offs3, 2);
    mve_pred16_t p_full = vctp8q(16);
    uint16_t x = 0;
    for (; x + 16 <= width; x += 16) {
        uint8_t *outb = dst + (uint32_t)x * 3u;
        const uint8_t *inb = src + (uint32_t)x * 3u;

        /* R */
        uint8x16_t r = vldrbq_gather_offset_z_u8(inb, offR, p_full);
        r = mpix_wb_channel_u8(r, red_q10);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offR),
                                   vreinterpretq_s8_u8(r), p_full);

        /* G unchanged */
        uint8x16_t g = vldrbq_gather_offset_z_u8(inb, offG, p_full);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offG),
                                   vreinterpretq_s8_u8(g), p_full);

        /* B */
        uint8x16_t b = vldrbq_gather_offset_z_u8(inb, offB, p_full);
        b = mpix_wb_channel_u8(b, blue_q10);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offB),
                                   vreinterpretq_s8_u8(b), p_full);
    }
    unsigned rem = (unsigned)width - x;
    if (rem) {
        uint8_t *outb = dst + (uint32_t)x * 3u;
        const uint8_t *inb = src + (uint32_t)x * 3u;
        mve_pred16_t p = mpix_tail_pred_u8(rem);

        uint8x16_t offR = vaddq_n_u8(offs3, 0);
        uint8x16_t r = vldrbq_gather_offset_z_u8(inb, offR, p);
        r = mpix_wb_channel_u8(r, red_q10);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offR),
                                   vreinterpretq_s8_u8(r), p);

        uint8x16_t offG = vaddq_n_u8(offs3, 1);
        uint8x16_t g = vldrbq_gather_offset_z_u8(inb, offG, p);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offG),
                                   vreinterpretq_s8_u8(g), p);

        uint8x16_t offB = vaddq_n_u8(offs3, 2);
        uint8x16_t b = vldrbq_gather_offset_z_u8(inb, offB, p);
        b = mpix_wb_channel_u8(b, blue_q10);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offB),
                                   vreinterpretq_s8_u8(b), p);
    }
}

/* -------------------- Gamma (LUT approach) -------------------- */

#define MPIX_GAMMA_STEP 4
#define MPIX_GAMMA_MIN 1
#define MPIX_GAMMA_MAX 16

/* Copied from scalar implementation to keep identical behavior */
static const uint8_t mpix_gamma_y_tab[] = {
    181, 197, 215, 234,
    128, 152, 181, 215,
    90, 117, 152, 197,
    64, 90, 128, 181,
    45, 69, 107, 165,
    32, 53, 90, 152,
    22, 41, 76, 139,
    16, 32, 64, 128,
    11, 24, 53, 117,
    8, 19, 45, 107,
    5, 14, 38, 98,
    4, 11, 32, 90,
    2, 8, 26, 82,
    2, 6, 22, 76,
    1, 5, 19, 69,
};
static const uint8_t mpix_gamma_x_tab[] = { 1, 4, 16, 64 };

static inline uint8_t mpix_gamma_interp_u8(uint8_t raw8, const uint8_t *gamma_y,
                                           const uint8_t *gamma_x, size_t gamma_sz)
{
    uint8_t x0 = 0, x1 = 0, y0, y1;
    if (raw8 == 0) return 0;
    for (size_t i = 0;; i++) {
        if (i >= gamma_sz) { y0 = gamma_y[i - 1]; x1 = y1 = 0xff; break; }
        x1 = gamma_x[i];
        if (raw8 < x1) { y0 = gamma_y[i - 1]; y1 = gamma_y[i - 0]; break; }
        x0 = x1;
    }
    return (uint8_t)(((uint32_t)(x1 - raw8) * y0 + (uint32_t)(raw8 - x0) * y1) / (uint32_t)(x1 - x0));
}

static void mpix_build_gamma_lut(uint8_t lut[256], uint8_t level)
{
    const uint8_t *gy = &mpix_gamma_y_tab[(level - MPIX_GAMMA_MIN) * MPIX_GAMMA_STEP];
    for (int v = 0; v < 256; ++v) {
        lut[v] = mpix_gamma_interp_u8((uint8_t)v, gy, mpix_gamma_x_tab, sizeof(mpix_gamma_x_tab));
    }
}

void mpix_correction_gamma_raw8(const uint8_t *src, uint8_t *dst, uint16_t width,
                                uint16_t line_offset, union mpix_correction_any *corr)
{
    (void)line_offset;
    /* level is stored on 8b with <<5 in scalar path; replicate that logic */
    const uint8_t level = corr->gamma.level >> 5;
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
                                 uint16_t line_offset, union mpix_correction_any *corr)
{
    (void)line_offset;
    const uint8_t level = corr->gamma.level >> 5;
    static uint8_t s_lut[256];
    static uint8_t s_level;
    static uint8_t s_init;
    if (!s_init || s_level != level) {
        mpix_build_gamma_lut(s_lut, level);
        s_level = level;
        s_init = 1;
    }

    uint8x16_t offs3 = mpix_rgb_offsets3();
    uint8x16_t offR = vaddq_n_u8(offs3, 0);
    uint8x16_t offG = vaddq_n_u8(offs3, 1);
    uint8x16_t offB = vaddq_n_u8(offs3, 2);
    mve_pred16_t p_full = vctp8q(16);
    uint16_t x = 0;
    for (; x + 16 <= width; x += 16) {
        uint8_t *outb = dst + (uint32_t)x * 3u;
        const uint8_t *inb = src + (uint32_t)x * 3u;
        uint8x16_t v;
        v = vldrbq_gather_offset_z_u8(inb, offR, p_full);
        v = vldrbq_gather_offset_u8(s_lut, v);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offR),
                                   vreinterpretq_s8_u8(v), p_full);
        v = vldrbq_gather_offset_z_u8(inb, offG, p_full);
        v = vldrbq_gather_offset_u8(s_lut, v);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offG),
                                   vreinterpretq_s8_u8(v), p_full);
        v = vldrbq_gather_offset_z_u8(inb, offB, p_full);
        v = vldrbq_gather_offset_u8(s_lut, v);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offB),
                                   vreinterpretq_s8_u8(v), p_full);
    }
    unsigned rem = (unsigned)width - x;
    if (rem) {
        uint8_t *outb = dst + (uint32_t)x * 3u;
        const uint8_t *inb = src + (uint32_t)x * 3u;
        mve_pred16_t p = mpix_tail_pred_u8(rem);
        uint8x16_t v;
        v = vldrbq_gather_offset_z_u8(inb, offR, p);
        v = vldrbq_gather_offset_u8(s_lut, v);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offR),
                                   vreinterpretq_s8_u8(v), p);
        v = vldrbq_gather_offset_z_u8(inb, offG, p);
        v = vldrbq_gather_offset_u8(s_lut, v);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offG),
                                   vreinterpretq_s8_u8(v), p);
        v = vldrbq_gather_offset_z_u8(inb, offB, p);
        v = vldrbq_gather_offset_u8(s_lut, v);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offB),
                                   vreinterpretq_s8_u8(v), p);
    }
}

/* -------------------- Color matrix (RGB24, 3x3, Q10) -------------------- */

static inline void mpix_ccm_pack_and_store(uint8_t *outb, uint8x16_t offs3,
                                           int channel, int32x4_t lo_a, int32x4_t lo_b,
                                           int32x4_t hi_a, int32x4_t hi_b, mve_pred16_t p)
{
    /* rounding shift and narrow s32 -> s16 in one step */
    /* rounding shift and narrow s32 -> s16 */
    int16x8_t v16lo = vqrshrnbq_n_s32(vdupq_n_s16(0), lo_a, MPIX_CORRECTION_SCALE_BITS);
    v16lo = vqrshrntq_n_s32(v16lo, lo_b, MPIX_CORRECTION_SCALE_BITS);
    int16x8_t v16hi = vqrshrnbq_n_s32(vdupq_n_s16(0), hi_a, MPIX_CORRECTION_SCALE_BITS);
    v16hi = vqrshrntq_n_s32(v16hi, hi_b, MPIX_CORRECTION_SCALE_BITS);

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
                                        uint16_t line_offset, union mpix_correction_any *corr)
{
    (void)line_offset;
    const int16_t *L = corr->color_matrix.levels; /* row-major 3x3 */

    uint8x16_t offs3 = mpix_rgb_offsets3();
    uint16_t x = 0;
    for (; x + 16 <= width; x += 16) {
        uint8_t *outb = dst + (uint32_t)x * 3u;
        const uint8_t *inb = src + (uint32_t)x * 3u;
        mve_pred16_t p = vctp8q(16);

        /* load channels */
        uint8x16_t r8 = vldrbq_gather_offset_z_u8(inb, vaddq_n_u8(offs3, 0), p);
        uint8x16_t g8 = vldrbq_gather_offset_z_u8(inb, vaddq_n_u8(offs3, 1), p);
        uint8x16_t b8 = vldrbq_gather_offset_z_u8(inb, vaddq_n_u8(offs3, 2), p);

        /* widen to s16 */
        int16x8_t r16_lo = vreinterpretq_s16_u16(vmovlbq_u8(r8));
        int16x8_t r16_hi = vreinterpretq_s16_u16(vmovltq_u8(r8));
        int16x8_t g16_lo = vreinterpretq_s16_u16(vmovlbq_u8(g8));
        int16x8_t g16_hi = vreinterpretq_s16_u16(vmovltq_u8(g8));
        int16x8_t b16_lo = vreinterpretq_s16_u16(vmovlbq_u8(b8));
        int16x8_t b16_hi = vreinterpretq_s16_u16(vmovltq_u8(b8));

        /* output R' = R*l0 + G*l1 + B*l2 using vmull{b,t} (s16*s16 -> s32) */
        int16_t l0 = L[0], l1 = L[1], l2 = L[2];
        int16x8_t L0v = vdupq_n_s16(l0);
        int16x8_t L1v = vdupq_n_s16(l1);
        int16x8_t L2v = vdupq_n_s16(l2);
        int32x4_t rr_ll = vaddq_s32(vmullbq_int_s16(r16_lo, L0v),
                                     vaddq_s32(vmullbq_int_s16(g16_lo, L1v), vmullbq_int_s16(b16_lo, L2v)));
        int32x4_t rr_lh = vaddq_s32(vmulltq_int_s16(r16_lo, L0v),
                                     vaddq_s32(vmulltq_int_s16(g16_lo, L1v), vmulltq_int_s16(b16_lo, L2v)));
        int32x4_t rr_hl = vaddq_s32(vmullbq_int_s16(r16_hi, L0v),
                                     vaddq_s32(vmullbq_int_s16(g16_hi, L1v), vmullbq_int_s16(b16_hi, L2v)));
        int32x4_t rr_hh = vaddq_s32(vmulltq_int_s16(r16_hi, L0v),
                                     vaddq_s32(vmulltq_int_s16(g16_hi, L1v), vmulltq_int_s16(b16_hi, L2v)));

        mpix_ccm_pack_and_store(outb, offs3, 0, rr_ll, rr_lh, rr_hl, rr_hh, p);

        /* output G' = R*l3 + G*l4 + B*l5 */
        int16_t l3 = L[3], l4 = L[4], l5 = L[5];
        int16x8_t L3v = vdupq_n_s16(l3);
        int16x8_t L4v = vdupq_n_s16(l4);
        int16x8_t L5v = vdupq_n_s16(l5);
        int32x4_t gg_ll = vaddq_s32(vmullbq_int_s16(r16_lo, L3v),
                                     vaddq_s32(vmullbq_int_s16(g16_lo, L4v), vmullbq_int_s16(b16_lo, L5v)));
        int32x4_t gg_lh = vaddq_s32(vmulltq_int_s16(r16_lo, L3v),
                                     vaddq_s32(vmulltq_int_s16(g16_lo, L4v), vmulltq_int_s16(b16_lo, L5v)));
        int32x4_t gg_hl = vaddq_s32(vmullbq_int_s16(r16_hi, L3v),
                                     vaddq_s32(vmullbq_int_s16(g16_hi, L4v), vmullbq_int_s16(b16_hi, L5v)));
        int32x4_t gg_hh = vaddq_s32(vmulltq_int_s16(r16_hi, L3v),
                                     vaddq_s32(vmulltq_int_s16(g16_hi, L4v), vmulltq_int_s16(b16_hi, L5v)));

        mpix_ccm_pack_and_store(outb, offs3, 1, gg_ll, gg_lh, gg_hl, gg_hh, p);

        /* output B' = R*l6 + G*l7 + B*l8 */
        int16_t l6 = L[6], l7 = L[7], l8 = L[8];
        int16x8_t L6v = vdupq_n_s16(l6);
        int16x8_t L7v = vdupq_n_s16(l7);
        int16x8_t L8v = vdupq_n_s16(l8);
        int32x4_t bb_ll = vaddq_s32(vmullbq_int_s16(r16_lo, L6v),
                                     vaddq_s32(vmullbq_int_s16(g16_lo, L7v), vmullbq_int_s16(b16_lo, L8v)));
        int32x4_t bb_lh = vaddq_s32(vmulltq_int_s16(r16_lo, L6v),
                                     vaddq_s32(vmulltq_int_s16(g16_lo, L7v), vmulltq_int_s16(b16_lo, L8v)));
        int32x4_t bb_hl = vaddq_s32(vmullbq_int_s16(r16_hi, L6v),
                                     vaddq_s32(vmullbq_int_s16(g16_hi, L7v), vmullbq_int_s16(b16_hi, L8v)));
        int32x4_t bb_hh = vaddq_s32(vmulltq_int_s16(r16_hi, L6v),
                                     vaddq_s32(vmulltq_int_s16(g16_hi, L7v), vmulltq_int_s16(b16_hi, L8v)));

        mpix_ccm_pack_and_store(outb, offs3, 2, bb_ll, bb_lh, bb_hl, bb_hh, p);
    }

    unsigned rem = (unsigned)width - x;
    if (rem) {
        uint8_t *outb = dst + (uint32_t)x * 3u;
        const uint8_t *inb = src + (uint32_t)x * 3u;
        mve_pred16_t p = mpix_tail_pred_u8(rem);

        uint8x16_t r8 = vldrbq_gather_offset_z_u8(inb, vaddq_n_u8(offs3, 0), p);
        uint8x16_t g8 = vldrbq_gather_offset_z_u8(inb, vaddq_n_u8(offs3, 1), p);
        uint8x16_t b8 = vldrbq_gather_offset_z_u8(inb, vaddq_n_u8(offs3, 2), p);

        int16x8_t r16_lo = vreinterpretq_s16_u16(vmovlbq_u8(r8));
        int16x8_t r16_hi = vreinterpretq_s16_u16(vmovltq_u8(r8));
        int16x8_t g16_lo = vreinterpretq_s16_u16(vmovlbq_u8(g8));
        int16x8_t g16_hi = vreinterpretq_s16_u16(vmovltq_u8(g8));
        int16x8_t b16_lo = vreinterpretq_s16_u16(vmovlbq_u8(b8));
        int16x8_t b16_hi = vreinterpretq_s16_u16(vmovltq_u8(b8));

    int16_t l0 = L[0], l1 = L[1], l2 = L[2];
    int16x8_t L0v = vdupq_n_s16(l0);
    int16x8_t L1v = vdupq_n_s16(l1);
    int16x8_t L2v = vdupq_n_s16(l2);
    int32x4_t rr_ll = vaddq_s32(vmullbq_int_s16(r16_lo, L0v),
                     vaddq_s32(vmullbq_int_s16(g16_lo, L1v), vmullbq_int_s16(b16_lo, L2v)));
    int32x4_t rr_lh = vaddq_s32(vmulltq_int_s16(r16_lo, L0v),
                     vaddq_s32(vmulltq_int_s16(g16_lo, L1v), vmulltq_int_s16(b16_lo, L2v)));
    int32x4_t rr_hl = vaddq_s32(vmullbq_int_s16(r16_hi, L0v),
                     vaddq_s32(vmullbq_int_s16(g16_hi, L1v), vmullbq_int_s16(b16_hi, L2v)));
    int32x4_t rr_hh = vaddq_s32(vmulltq_int_s16(r16_hi, L0v),
                     vaddq_s32(vmulltq_int_s16(g16_hi, L1v), vmulltq_int_s16(b16_hi, L2v)));
        mpix_ccm_pack_and_store(outb, offs3, 0, rr_ll, rr_lh, rr_hl, rr_hh, p);
    int16_t l3 = L[3], l4 = L[4], l5 = L[5];
    int16x8_t L3v = vdupq_n_s16(l3);
    int16x8_t L4v = vdupq_n_s16(l4);
    int16x8_t L5v = vdupq_n_s16(l5);
    int32x4_t gg_ll = vaddq_s32(vmullbq_int_s16(r16_lo, L3v),
                     vaddq_s32(vmullbq_int_s16(g16_lo, L4v), vmullbq_int_s16(b16_lo, L5v)));
    int32x4_t gg_lh = vaddq_s32(vmulltq_int_s16(r16_lo, L3v),
                     vaddq_s32(vmulltq_int_s16(g16_lo, L4v), vmulltq_int_s16(b16_lo, L5v)));
    int32x4_t gg_hl = vaddq_s32(vmullbq_int_s16(r16_hi, L3v),
                     vaddq_s32(vmullbq_int_s16(g16_hi, L4v), vmullbq_int_s16(b16_hi, L5v)));
    int32x4_t gg_hh = vaddq_s32(vmulltq_int_s16(r16_hi, L3v),
                     vaddq_s32(vmulltq_int_s16(g16_hi, L4v), vmulltq_int_s16(b16_hi, L5v)));
        mpix_ccm_pack_and_store(outb, offs3, 1, gg_ll, gg_lh, gg_hl, gg_hh, p);
    int16_t l6 = L[6], l7 = L[7], l8 = L[8];
    int16x8_t L6v = vdupq_n_s16(l6);
    int16x8_t L7v = vdupq_n_s16(l7);
    int16x8_t L8v = vdupq_n_s16(l8);
    int32x4_t bb_ll = vaddq_s32(vmullbq_int_s16(r16_lo, L6v),
                     vaddq_s32(vmullbq_int_s16(g16_lo, L7v), vmullbq_int_s16(b16_lo, L8v)));
    int32x4_t bb_lh = vaddq_s32(vmulltq_int_s16(r16_lo, L6v),
                     vaddq_s32(vmulltq_int_s16(g16_lo, L7v), vmulltq_int_s16(b16_lo, L8v)));
    int32x4_t bb_hl = vaddq_s32(vmullbq_int_s16(r16_hi, L6v),
                     vaddq_s32(vmullbq_int_s16(g16_hi, L7v), vmullbq_int_s16(b16_hi, L8v)));
    int32x4_t bb_hh = vaddq_s32(vmulltq_int_s16(r16_hi, L6v),
                     vaddq_s32(vmulltq_int_s16(g16_hi, L7v), vmulltq_int_s16(b16_hi, L8v)));
        mpix_ccm_pack_and_store(outb, offs3, 2, bb_ll, bb_lh, bb_hl, bb_hh, p);
    }
}
