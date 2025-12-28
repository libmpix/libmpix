/* SPDX-License-Identifier: Apache-2.0 */

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "arm_mve.h"

#include <mpix/utils.h>
#include <mpix/types.h>
#include <mpix/formats.h>

/* -------------------- Common helpers (mirrors style in op_correction_helium.c) -------------------- */
static inline uint8x16_t mpix_rgb_offsets3(void)
{
    /* byte offsets for 16 pixels in RGB24: 0,3,6,...,45 */
    return vmulq_n_u8(vidupq_n_u8(0, 1), 3);
}

static inline mve_pred16_t mpix_tail_pred_u8(unsigned lanes)
{
    return vctp8q((uint32_t)lanes & 0xFF);
}

/* Palette depth helpers */
static inline uint8_t mpix_palette_depth_inline(const struct mpix_palette *palette)
{
    /* FOURCC is like 'PLTn' where n in '1'..'8' */
    const char *str = MPIX_FOURCC_TO_STR(palette->fourcc);
    return (uint8_t)(str[3] - '0');
}
static inline uint16_t mpix_palette_size_inline(const struct mpix_palette *palette)
{
    return (uint16_t)(1u << mpix_palette_depth_inline(palette));
}

/* Unroll factor control: 8-color block by default */
#ifndef MPIX_MVE_UNROLL8
#define MPIX_MVE_UNROLL8 1
#endif

#if defined(__GNUC__)
static inline void mpix_prefetch_read(const void *ptr)
{
    __builtin_prefetch(ptr, 0 /* read */, 2 /* temporal */);
}
#else
static inline void mpix_prefetch_read(const void *ptr) { (void)ptr; }
#endif

/* Argmin across palette entries for one vector of 16 RGB pixels. */
static inline __attribute__((always_inline, hot)) void mpix_rgb24_argmin_palette_u8(
    const uint8_t *inb, const uint8_t *pal, uint16_t pal_size,
    uint8x16_t offR, uint8x16_t offG, uint8x16_t offB, mve_pred16_t p,
    uint8_t *out_idx)
{
    /* Initialize best distance to max and best index to 0 (u16 lanes) */
    uint16x8_t best_lo = vdupq_n_u16(0xFFFF);
    uint16x8_t best_hi = vdupq_n_u16(0xFFFF);
    uint16x8_t best_idx_lo = vdupq_n_u16(0);
    uint16x8_t best_idx_hi = vdupq_n_u16(0);

    /* Load R,G,B vectors for current 16 pixels */
    uint8x16_t r = vldrbq_gather_offset_z_u8(inb, offR, p);
    uint8x16_t g = vldrbq_gather_offset_z_u8(inb, offG, p);
    uint8x16_t b = vldrbq_gather_offset_z_u8(inb, offB, p);

    uint16_t ci = 0;
#if MPIX_MVE_UNROLL8
    /* 8-candidate blocks */
    for (; ci + 7 < pal_size; ci += 8) {
        /* Prefetch next block */
        if (ci + 15 < pal_size) {
            const uint8_t *next = pal + 3u * (ci + 8);
            mpix_prefetch_read(next);
            mpix_prefetch_read(next + 32);
        }
        const uint8_t *pc0 = pal + 3u * (ci + 0);
        const uint8_t *pc1 = pal + 3u * (ci + 1);
        const uint8_t *pc2 = pal + 3u * (ci + 2);
        const uint8_t *pc3 = pal + 3u * (ci + 3);
        const uint8_t *pc4 = pal + 3u * (ci + 4);
        const uint8_t *pc5 = pal + 3u * (ci + 5);
        const uint8_t *pc6 = pal + 3u * (ci + 6);
        const uint8_t *pc7 = pal + 3u * (ci + 7);
        uint16x8_t idx_base = vdupq_n_u16((uint16_t)ci);

        /* candidates #0..#7 */
        for (int k = 0; k < 8; ++k) {
            const uint8_t *pc = (k==0?pc0:k==1?pc1:k==2?pc2:k==3?pc3:k==4?pc4:k==5?pc5:k==6?pc6:pc7);
            uint16x8_t idxv16 = vaddq_n_u16(idx_base, (uint16_t)k);
            uint8x16_t vcR = vdupq_n_u8(pc[0]);
            uint8x16_t vcG = vdupq_n_u8(pc[1]);
            uint8x16_t vcB = vdupq_n_u8(pc[2]);
            uint8x16_t dr8 = vabdq_u8(r, vcR);
            uint8x16_t dg8 = vabdq_u8(g, vcG);
            uint8x16_t db8 = vabdq_u8(b, vcB);
            uint16x8_t dr_lo = vmovlbq_u8(dr8), dr_hi = vmovltq_u8(dr8);
            uint16x8_t dg_lo = vmovlbq_u8(dg8), dg_hi = vmovltq_u8(dg8);
            uint16x8_t db_lo = vmovlbq_u8(db8), db_hi = vmovltq_u8(db8);
            uint16x8_t dist_lo = vqaddq_u16(vqaddq_u16(vmulq_u16(dr_lo, dr_lo), vmulq_u16(dg_lo, dg_lo)), vmulq_u16(db_lo, db_lo));
            uint16x8_t dist_hi = vqaddq_u16(vqaddq_u16(vmulq_u16(dr_hi, dr_hi), vmulq_u16(dg_hi, dg_hi)), vmulq_u16(db_hi, db_hi));
            mve_pred16_t plo = vcmphiq_u16(best_lo, dist_lo);
            best_lo = vpselq_u16(dist_lo, best_lo, plo);
            best_idx_lo = vpselq_u16(idxv16, best_idx_lo, plo);
            mve_pred16_t phi = vcmphiq_u16(best_hi, dist_hi);
            best_hi = vpselq_u16(dist_hi, best_hi, phi);
            best_idx_hi = vpselq_u16(idxv16, best_idx_hi, phi);
        }
        /* Early exit by sum-reduction (zero means all lanes zero) */
        if ((vaddvq_u16(best_lo) + vaddvq_u16(best_hi)) == 0u) {
            ci = pal_size;
            break;
        }
    }
#endif
    for (; ci + 3 < pal_size; ci += 4) {
        /* Prefetch the next block of palette entries */
        if (ci + 7 < pal_size) {
            const uint8_t *next = pal + 3u * (ci + 4);
            mpix_prefetch_read(next);
            mpix_prefetch_read(next + 32);
        }
        const uint8_t *pc0 = pal + 3u * (ci + 0);
        const uint8_t *pc1 = pal + 3u * (ci + 1);
        const uint8_t *pc2 = pal + 3u * (ci + 2);
        const uint8_t *pc3 = pal + 3u * (ci + 3);
        uint16x8_t idx_base = vdupq_n_u16((uint16_t)ci);

        /* candidate #0 */
        {
            uint16x8_t idxv16 = idx_base;
            uint8x16_t vcR = vdupq_n_u8(pc0[0]);
            uint8x16_t vcG = vdupq_n_u8(pc0[1]);
            uint8x16_t vcB = vdupq_n_u8(pc0[2]);
            uint8x16_t dr8 = vabdq_u8(r, vcR);
            uint8x16_t dg8 = vabdq_u8(g, vcG);
            uint8x16_t db8 = vabdq_u8(b, vcB);
            uint16x8_t dr_lo = vmovlbq_u8(dr8), dr_hi = vmovltq_u8(dr8);
            uint16x8_t dg_lo = vmovlbq_u8(dg8), dg_hi = vmovltq_u8(dg8);
            uint16x8_t db_lo = vmovlbq_u8(db8), db_hi = vmovltq_u8(db8);
            uint16x8_t dist_lo = vqaddq_u16(vqaddq_u16(vmulq_u16(dr_lo, dr_lo), vmulq_u16(dg_lo, dg_lo)), vmulq_u16(db_lo, db_lo));
            uint16x8_t dist_hi = vqaddq_u16(vqaddq_u16(vmulq_u16(dr_hi, dr_hi), vmulq_u16(dg_hi, dg_hi)), vmulq_u16(db_hi, db_hi));
            mve_pred16_t plo = vcmphiq_u16(best_lo, dist_lo);
            best_lo = vpselq_u16(dist_lo, best_lo, plo);
            best_idx_lo = vpselq_u16(idxv16, best_idx_lo, plo);
            mve_pred16_t phi = vcmphiq_u16(best_hi, dist_hi);
            best_hi = vpselq_u16(dist_hi, best_hi, phi);
            best_idx_hi = vpselq_u16(idxv16, best_idx_hi, phi);
        }

        /* candidate #1 */
        {
            uint16x8_t idxv16 = vaddq_n_u16(idx_base, 1);
            uint8x16_t vcR = vdupq_n_u8(pc1[0]);
            uint8x16_t vcG = vdupq_n_u8(pc1[1]);
            uint8x16_t vcB = vdupq_n_u8(pc1[2]);
            uint8x16_t dr8 = vabdq_u8(r, vcR);
            uint8x16_t dg8 = vabdq_u8(g, vcG);
            uint8x16_t db8 = vabdq_u8(b, vcB);
            uint16x8_t dr_lo = vmovlbq_u8(dr8), dr_hi = vmovltq_u8(dr8);
            uint16x8_t dg_lo = vmovlbq_u8(dg8), dg_hi = vmovltq_u8(dg8);
            uint16x8_t db_lo = vmovlbq_u8(db8), db_hi = vmovltq_u8(db8);
            uint16x8_t dist_lo = vqaddq_u16(vqaddq_u16(vmulq_u16(dr_lo, dr_lo), vmulq_u16(dg_lo, dg_lo)), vmulq_u16(db_lo, db_lo));
            uint16x8_t dist_hi = vqaddq_u16(vqaddq_u16(vmulq_u16(dr_hi, dr_hi), vmulq_u16(dg_hi, dg_hi)), vmulq_u16(db_hi, db_hi));
            mve_pred16_t plo = vcmphiq_u16(best_lo, dist_lo);
            best_lo = vpselq_u16(dist_lo, best_lo, plo);
            best_idx_lo = vpselq_u16(idxv16, best_idx_lo, plo);
            mve_pred16_t phi = vcmphiq_u16(best_hi, dist_hi);
            best_hi = vpselq_u16(dist_hi, best_hi, phi);
            best_idx_hi = vpselq_u16(idxv16, best_idx_hi, phi);
        }

        /* candidate #2 */
        {
            uint16x8_t idxv16 = vaddq_n_u16(idx_base, 2);
            uint8x16_t vcR = vdupq_n_u8(pc2[0]);
            uint8x16_t vcG = vdupq_n_u8(pc2[1]);
            uint8x16_t vcB = vdupq_n_u8(pc2[2]);
            uint8x16_t dr8 = vabdq_u8(r, vcR);
            uint8x16_t dg8 = vabdq_u8(g, vcG);
            uint8x16_t db8 = vabdq_u8(b, vcB);
            uint16x8_t dr_lo = vmovlbq_u8(dr8), dr_hi = vmovltq_u8(dr8);
            uint16x8_t dg_lo = vmovlbq_u8(dg8), dg_hi = vmovltq_u8(dg8);
            uint16x8_t db_lo = vmovlbq_u8(db8), db_hi = vmovltq_u8(db8);
            uint16x8_t dist_lo = vqaddq_u16(vqaddq_u16(vmulq_u16(dr_lo, dr_lo), vmulq_u16(dg_lo, dg_lo)), vmulq_u16(db_lo, db_lo));
            uint16x8_t dist_hi = vqaddq_u16(vqaddq_u16(vmulq_u16(dr_hi, dr_hi), vmulq_u16(dg_hi, dg_hi)), vmulq_u16(db_hi, db_hi));
            mve_pred16_t plo = vcmphiq_u16(best_lo, dist_lo);
            best_lo = vpselq_u16(dist_lo, best_lo, plo);
            best_idx_lo = vpselq_u16(idxv16, best_idx_lo, plo);
            mve_pred16_t phi = vcmphiq_u16(best_hi, dist_hi);
            best_hi = vpselq_u16(dist_hi, best_hi, phi);
            best_idx_hi = vpselq_u16(idxv16, best_idx_hi, phi);
        }

        /* candidate #3 */
        {
            uint16x8_t idxv16 = vaddq_n_u16(idx_base, 3);
            uint8x16_t vcR = vdupq_n_u8(pc3[0]);
            uint8x16_t vcG = vdupq_n_u8(pc3[1]);
            uint8x16_t vcB = vdupq_n_u8(pc3[2]);
            uint8x16_t dr8 = vabdq_u8(r, vcR);
            uint8x16_t dg8 = vabdq_u8(g, vcG);
            uint8x16_t db8 = vabdq_u8(b, vcB);
            uint16x8_t dr_lo = vmovlbq_u8(dr8), dr_hi = vmovltq_u8(dr8);
            uint16x8_t dg_lo = vmovlbq_u8(dg8), dg_hi = vmovltq_u8(dg8);
            uint16x8_t db_lo = vmovlbq_u8(db8), db_hi = vmovltq_u8(db8);
            uint16x8_t dist_lo = vqaddq_u16(vqaddq_u16(vmulq_u16(dr_lo, dr_lo), vmulq_u16(dg_lo, dg_lo)), vmulq_u16(db_lo, db_lo));
            uint16x8_t dist_hi = vqaddq_u16(vqaddq_u16(vmulq_u16(dr_hi, dr_hi), vmulq_u16(dg_hi, dg_hi)), vmulq_u16(db_hi, db_hi));
            mve_pred16_t plo = vcmphiq_u16(best_lo, dist_lo);
            best_lo = vpselq_u16(dist_lo, best_lo, plo);
            best_idx_lo = vpselq_u16(idxv16, best_idx_lo, plo);
            mve_pred16_t phi = vcmphiq_u16(best_hi, dist_hi);
            best_hi = vpselq_u16(dist_hi, best_hi, phi);
            best_idx_hi = vpselq_u16(idxv16, best_idx_hi, phi);
            /* Early exit: all lanes already zero distance (sum == 0) */
            if ((vaddvq_u16(best_lo) + vaddvq_u16(best_hi)) == 0u) {
                ci = pal_size;
                break;
            }
        }
    }

    /* odd tail */
    for (; ci < pal_size; ++ci) {
        const uint8_t *pc = pal + 3u * ci;
        uint16x8_t idxv16 = vdupq_n_u16((uint16_t)ci);
        uint8x16_t vcR = vdupq_n_u8(pc[0]);
        uint8x16_t vcG = vdupq_n_u8(pc[1]);
        uint8x16_t vcB = vdupq_n_u8(pc[2]);
        uint8x16_t dr8 = vabdq_u8(r, vcR);
        uint8x16_t dg8 = vabdq_u8(g, vcG);
        uint8x16_t db8 = vabdq_u8(b, vcB);
        uint16x8_t dr_lo = vmovlbq_u8(dr8), dr_hi = vmovltq_u8(dr8);
        uint16x8_t dg_lo = vmovlbq_u8(dg8), dg_hi = vmovltq_u8(dg8);
        uint16x8_t db_lo = vmovlbq_u8(db8), db_hi = vmovltq_u8(db8);
        uint16x8_t dist_lo = vqaddq_u16(vqaddq_u16(vmulq_u16(dr_lo, dr_lo), vmulq_u16(dg_lo, dg_lo)), vmulq_u16(db_lo, db_lo));
        uint16x8_t dist_hi = vqaddq_u16(vqaddq_u16(vmulq_u16(dr_hi, dr_hi), vmulq_u16(dg_hi, dg_hi)), vmulq_u16(db_hi, db_hi));
        mve_pred16_t plo = vcmphiq_u16(best_lo, dist_lo);
        best_lo = vpselq_u16(dist_lo, best_lo, plo);
        best_idx_lo = vpselq_u16(idxv16, best_idx_lo, plo);
        mve_pred16_t phi = vcmphiq_u16(best_hi, dist_hi);
        best_hi = vpselq_u16(dist_hi, best_hi, phi);
        best_idx_hi = vpselq_u16(idxv16, best_idx_hi, phi);
        if ((vaddvq_u16(best_lo) + vaddvq_u16(best_hi)) == 0u) {
            break;
        }
    }

    /* Pack u16 indices to u8 and store with predicate */
    uint8x16_t outv = vqmovnbq_u16(vdupq_n_u8(0), best_idx_lo);
    outv = vqmovntq_u16(outv, best_idx_hi);
    vst1q_p_u8(out_idx, outv, p);
}

/* -------------------- RGB24 -> PALETTE8 -------------------- */
void mpix_convert_rgb24_to_palette8(const uint8_t *src, uint8_t *dst, uint16_t width,
                                    const struct mpix_palette *palette)
{
    const uint8_t *pal = palette->colors_rgb24;
    uint16_t pal_size = mpix_palette_size_inline(palette);

    uint8x16_t offs3 = mpix_rgb_offsets3();
    uint8x16_t offR = vaddq_n_u8(offs3, 0);
    uint8x16_t offG = vaddq_n_u8(offs3, 1);
    uint8x16_t offB = vaddq_n_u8(offs3, 2);

    uint16_t x = 0;
    mve_pred16_t pfull = vctp8q(16);
    for (; x + 16 <= width; x += 16, src += 16 * 3, dst += 16) {
        mpix_rgb24_argmin_palette_u8(src, pal, pal_size, offR, offG, offB, pfull, dst);
    }
    unsigned rem = (unsigned)width - x;
    if (rem) {
        mve_pred16_t p = mpix_tail_pred_u8(rem);
        mpix_rgb24_argmin_palette_u8(src, pal, pal_size, offR, offG, offB, p, dst);
    }
}

/* -------------------- PALETTE8 -> RGB24 -------------------- */
void mpix_convert_palette8_to_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width,
                                    const struct mpix_palette *palette)
{
    const uint8_t *pal = palette->colors_rgb24;
    /* Build contiguous channel LUTs pr/pg/pb to allow byte-indexed gathers (no scale) */
    uint16_t pal_sz = mpix_palette_size_inline(palette);
    uint8_t pr[256], pg[256], pb[256];
    for (uint16_t i = 0; i < pal_sz; ++i) {
        pr[i] = pal[3u * i + 0];
        pg[i] = pal[3u * i + 1];
        pb[i] = pal[3u * i + 2];
    }
    uint8x16_t offs3 = mpix_rgb_offsets3();
    uint8x16_t offR = vaddq_n_u8(offs3, 0);
    uint8x16_t offG = vaddq_n_u8(offs3, 1);
    uint8x16_t offB = vaddq_n_u8(offs3, 2);

    uint16_t x = 0;
    mve_pred16_t pfull = vctp8q(16);
    for (; x + 16 <= width; x += 16) {
    uint8x16_t idx = vld1q_u8(src + x);
    uint8_t *outb = dst + (uint32_t)x * 3u;
    uint8x16_t r = vldrbq_gather_offset_z_u8(pr, idx, pfull);
    uint8x16_t g = vldrbq_gather_offset_z_u8(pg, idx, pfull);
    uint8x16_t b = vldrbq_gather_offset_z_u8(pb, idx, pfull);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offR), vreinterpretq_s8_u8(r), pfull);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offG), vreinterpretq_s8_u8(g), pfull);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offB), vreinterpretq_s8_u8(b), pfull);
    }
    unsigned rem = (unsigned)width - x;
    if (rem) {
        mve_pred16_t p = mpix_tail_pred_u8(rem);
    uint8x16_t idx = vld1q_z_u8(src + x, p);
    uint8_t *outb = dst + (uint32_t)x * 3u;
    uint8x16_t r = vldrbq_gather_offset_z_u8(pr, idx, p);
    uint8x16_t g = vldrbq_gather_offset_z_u8(pg, idx, p);
    uint8x16_t b = vldrbq_gather_offset_z_u8(pb, idx, p);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offR), vreinterpretq_s8_u8(r), p);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offG), vreinterpretq_s8_u8(g), p);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offB), vreinterpretq_s8_u8(b), p);
    }
}

/* -------------------- Helpers for bit-pack/unpack using vector gather/scatter -------------------- */
/* Build constant offset vectors once for packing */
static inline uint8x16_t mpix_offs_step2(void) { return vmulq_n_u8(vidupq_n_u8(0, 1), 2); }
static inline uint8x16_t mpix_offs_step4(void) { return vmulq_n_u8(vidupq_n_u8(0, 1), 4); }

/* Pack 16 4-bit indices (in idx) into 8 bytes at dst, fully predicated on 8 lanes */
static inline void mpix_pack4x2_u8(uint8x16_t idx, uint8_t *dst)
{
    /* Register-only pack using gather/scatter to final dst; no temporaries. */
    uint8x16_t lane = vidupq_n_u8(0, 1);
    uint8x16_t offsets = vshrq_n_u8(lane, 1);              /* 0,0,1,1,2,2,...,7,7 */
    mve_pred16_t p_even = vcmpeqq_n_u8(vandq_u8(lane, vdupq_n_u8(1)), 0);
    mve_pred16_t p_odd  = vcmpeqq_n_u8(vandq_u8(lane, vdupq_n_u8(1)), 1);

    /* zero 8 destination bytes */
    vst1q_p_u8(dst, vdupq_n_u8(0), vctp8q(8));

    /* high nibble from even lanes */
    uint8x16_t hi = vshlq_n_u8(vandq_u8(idx, vdupq_n_u8(0x0F)), 4);
    vstrbq_scatter_offset_p_s8((int8_t *)dst, vreinterpretq_s8_u8(offsets),
                               vreinterpretq_s8_u8(hi), p_even);

    /* low nibble from odd lanes added into same bytes */
    uint8x16_t lo = vandq_u8(idx, vdupq_n_u8(0x0F));
    uint8x16_t acc = vldrbq_gather_offset_z_u8(dst, offsets, p_odd);
    acc = vaddq_u8(acc, lo);
    vstrbq_scatter_offset_p_s8((int8_t *)dst, vreinterpretq_s8_u8(offsets),
                               vreinterpretq_s8_u8(acc), p_odd);
}

/* Unpack up to 8 bytes (bytes<=8) to 16 4-bit indices; safe for tails. */
static inline uint8x16_t mpix_unpack2x4_u8(const uint8_t *src, unsigned bytes)
{
    /* Replicate each source byte into two lanes (i>>1), then select hi/lo nibble to even/odd lanes. */
    uint8x16_t lane = vidupq_n_u8(0, 1);
    uint8x16_t offsets = vshrq_n_u8(lane, 1);              /* 0,0,1,1,2,2,...,7,7 */
    /* Predicate to only read available bytes: bytes > offsets */
    mve_pred16_t p = vcmphiq_u8(vdupq_n_u8((uint8_t)bytes), offsets);
    mve_pred16_t p_even = vcmpeqq_n_u8(vandq_u8(lane, vdupq_n_u8(1)), 0);
    mve_pred16_t p_odd  = vcmpeqq_n_u8(vandq_u8(lane, vdupq_n_u8(1)), 1);
    uint8x16_t rep = vldrbq_gather_offset_z_u8(src, offsets, p);
    uint8x16_t zeros = vdupq_n_u8(0);
    uint8x16_t hi = vshrq_n_u8(rep, 4);
    uint8x16_t lo = vandq_u8(rep, vdupq_n_u8(0x0F));
    uint8x16_t even_part = vpselq_u8(hi, zeros, p_even);
    uint8x16_t odd_part  = vpselq_u8(lo, zeros, p_odd);
    return vorrq_u8(even_part, odd_part);
}

/* Pack 16 2-bit indices into 4 bytes */
static inline void mpix_pack4x2b_u8(uint8x16_t idx, uint8_t *dst)
{
    /* Register-only pack for 2bpp using gather/scatter into final dst. */
    uint8x16_t lane = vidupq_n_u8(0, 1);
    uint8x16_t off4 = vshrq_n_u8(lane, 2);                 /* 0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3 */
    mve_pred16_t p0 = vcmpeqq_n_u8(vandq_u8(lane, vdupq_n_u8(3)), 0);
    mve_pred16_t p1 = vcmpeqq_n_u8(vandq_u8(lane, vdupq_n_u8(3)), 1);
    mve_pred16_t p2 = vcmpeqq_n_u8(vandq_u8(lane, vdupq_n_u8(3)), 2);
    mve_pred16_t p3 = vcmpeqq_n_u8(vandq_u8(lane, vdupq_n_u8(3)), 3);

    /* zero 4 destination bytes */
    vst1q_p_u8(dst, vdupq_n_u8(0), vctp8q(4));

    uint8x16_t idx2 = vandq_u8(idx, vdupq_n_u8(0x03));

    /* bit positions 6,4,2,0 for lanes 0,1,2,3 mod 4 */
    {
        uint8x16_t contrib = vshlq_n_u8(idx2, 6);
        uint8x16_t acc = vldrbq_gather_offset_z_u8(dst, off4, p0);
        acc = vaddq_u8(acc, contrib);
        vstrbq_scatter_offset_p_s8((int8_t *)dst, vreinterpretq_s8_u8(off4),
                                   vreinterpretq_s8_u8(acc), p0);
    }
    {
        uint8x16_t contrib = vshlq_n_u8(idx2, 4);
        uint8x16_t acc = vldrbq_gather_offset_z_u8(dst, off4, p1);
        acc = vaddq_u8(acc, contrib);
        vstrbq_scatter_offset_p_s8((int8_t *)dst, vreinterpretq_s8_u8(off4),
                                   vreinterpretq_s8_u8(acc), p1);
    }
    {
        uint8x16_t contrib = vshlq_n_u8(idx2, 2);
        uint8x16_t acc = vldrbq_gather_offset_z_u8(dst, off4, p2);
        acc = vaddq_u8(acc, contrib);
        vstrbq_scatter_offset_p_s8((int8_t *)dst, vreinterpretq_s8_u8(off4),
                                   vreinterpretq_s8_u8(acc), p2);
    }
    {
        uint8x16_t contrib = idx2; /* << 0 */
        uint8x16_t acc = vldrbq_gather_offset_z_u8(dst, off4, p3);
        acc = vaddq_u8(acc, contrib);
        vstrbq_scatter_offset_p_s8((int8_t *)dst, vreinterpretq_s8_u8(off4),
                                   vreinterpretq_s8_u8(acc), p3);
    }
}

/* Unpack up to 4 bytes (bytes<=4) to 16 2-bit indices; safe for tails. */
static inline uint8x16_t mpix_unpack4x2b_u8(const uint8_t *src, unsigned bytes)
{
    uint8x16_t lane = vidupq_n_u8(0, 1);
    uint8x16_t off4 = vshrq_n_u8(lane, 2);                 /* 0,0,0,0,1,1,1,1,2,2,2,2,3,3,3,3 */
    /* Predicate to read only available bytes: bytes > off4 */
    mve_pred16_t p = vcmphiq_u8(vdupq_n_u8((uint8_t)bytes), off4);
    mve_pred16_t p0 = vcmpeqq_n_u8(vandq_u8(lane, vdupq_n_u8(3)), 0);
    mve_pred16_t p1 = vcmpeqq_n_u8(vandq_u8(lane, vdupq_n_u8(3)), 1);
    mve_pred16_t p2 = vcmpeqq_n_u8(vandq_u8(lane, vdupq_n_u8(3)), 2);
    mve_pred16_t p3 = vcmpeqq_n_u8(vandq_u8(lane, vdupq_n_u8(3)), 3);
    uint8x16_t rep = vldrbq_gather_offset_z_u8(src, off4, p);
    uint8x16_t zeros = vdupq_n_u8(0);
    uint8x16_t a = vshrq_n_u8(rep, 6);
    uint8x16_t b = vandq_u8(vshrq_n_u8(rep, 4), vdupq_n_u8(0x03));
    uint8x16_t c = vandq_u8(vshrq_n_u8(rep, 2), vdupq_n_u8(0x03));
    uint8x16_t d = vandq_u8(rep, vdupq_n_u8(0x03));
    uint8x16_t part0 = vpselq_u8(a, zeros, p0);
    uint8x16_t part1 = vpselq_u8(b, zeros, p1);
    uint8x16_t part2 = vpselq_u8(c, zeros, p2);
    uint8x16_t part3 = vpselq_u8(d, zeros, p3);
    return vorrq_u8(vorrq_u8(part0, part1), vorrq_u8(part2, part3));
}

/* -------------------- RGB24 -> PALETTE4/2/1 -------------------- */
void mpix_convert_rgb24_to_palette4(const uint8_t *src, uint8_t *dst, uint16_t width,
                                    const struct mpix_palette *palette)
{
    /* Support any width (tail handled below) */

    const uint8_t *pal = palette->colors_rgb24;
    uint16_t pal_size = mpix_palette_size_inline(palette);

    uint8x16_t offs3 = mpix_rgb_offsets3();
    uint8x16_t offR = vaddq_n_u8(offs3, 0);
    uint8x16_t offG = vaddq_n_u8(offs3, 1);
    uint8x16_t offB = vaddq_n_u8(offs3, 2);

    uint16_t x = 0;
    for (; x + 16 <= width; x += 16, src += 16 * 3, dst += 8) {
        uint8_t idx[16];
        mve_pred16_t pfull = vctp8q(16);
        mpix_rgb24_argmin_palette_u8(src, pal, pal_size, offR, offG, offB, pfull, idx);
        mpix_pack4x2_u8(vld1q_u8(idx), dst);
    }
    unsigned rem = (unsigned)width - x;
    if (rem) {
        /* process tail as full argmin then pack lowest bytes */
        uint8_t tmp_idx[16] = {0};
        mve_pred16_t p = mpix_tail_pred_u8(rem);
        mpix_rgb24_argmin_palette_u8(src, pal, pal_size, offR, offG, offB, p, tmp_idx);
        /* pack only needed (ceil(rem/2)) bytes */
        uint8_t packed[8] = {0};
        mpix_pack4x2_u8(vld1q_u8(tmp_idx), packed);
        memcpy(dst, packed, (rem + 1) / 2);
    }
}

void mpix_convert_rgb24_to_palette2(const uint8_t *src, uint8_t *dst, uint16_t width,
                                    const struct mpix_palette *palette)
{
    /* Support any width (tail handled below) */

    const uint8_t *pal = palette->colors_rgb24;
    uint16_t pal_size = mpix_palette_size_inline(palette);

    uint8x16_t offs3 = mpix_rgb_offsets3();
    uint8x16_t offR = vaddq_n_u8(offs3, 0);
    uint8x16_t offG = vaddq_n_u8(offs3, 1);
    uint8x16_t offB = vaddq_n_u8(offs3, 2);

    uint16_t x = 0;
    for (; x + 16 <= width; x += 16, src += 16 * 3, dst += 4) {
        uint8_t idx[16];
        mve_pred16_t pfull = vctp8q(16);
        mpix_rgb24_argmin_palette_u8(src, pal, pal_size, offR, offG, offB, pfull, idx);
        mpix_pack4x2b_u8(vld1q_u8(idx), dst);
    }
    unsigned rem = (unsigned)width - x;
    if (rem) {
        uint8_t tmp_idx[16] = {0};
        mve_pred16_t p = mpix_tail_pred_u8(rem);
        mpix_rgb24_argmin_palette_u8(src, pal, pal_size, offR, offG, offB, p, tmp_idx);
        uint8_t packed[4] = {0};
        mpix_pack4x2b_u8(vld1q_u8(tmp_idx), packed);
        memcpy(dst, packed, (rem + 3) / 4);
    }
}

void mpix_convert_rgb24_to_palette1(const uint8_t *src, uint8_t *dst, uint16_t width,
                                    const struct mpix_palette *palette)
{
    /* Support any width (tail handled below) */

    const uint8_t *pal = palette->colors_rgb24;
    uint16_t pal_size = mpix_palette_size_inline(palette);

    uint8x16_t offs3 = mpix_rgb_offsets3();
    uint8x16_t offR = vaddq_n_u8(offs3, 0);
    uint8x16_t offG = vaddq_n_u8(offs3, 1);
    uint8x16_t offB = vaddq_n_u8(offs3, 2);

    uint16_t x = 0;
    for (; x + 16 <= width; x += 16, src += 16 * 3, dst += 2) {
    uint8_t idx[16];
    mve_pred16_t pfull = vctp8q(16);
    mpix_rgb24_argmin_palette_u8(src, pal, pal_size, offR, offG, offB, pfull, idx);
    /* vector pack 1bpp using per-lane shifts */
    uint8x16_t idxv = vld1q_u8(idx);
    uint8x16_t bits = vandq_u8(idxv, vdupq_n_u8(1));
    /* shift counts: positive = left shift, negative = right shift (to zero-out other half) */
    static const int8_t sh_lo_tbl[16] = { 7,6,5,4,3,2,1,0, -8,-8,-8,-8,-8,-8,-8,-8 };
    static const int8_t sh_hi_tbl[16] = { -8,-8,-8,-8,-8,-8,-8,-8, 7,6,5,4,3,2,1,0 };
    int8x16_t sh_lo = vld1q_s8(sh_lo_tbl);
    int8x16_t sh_hi = vld1q_s8(sh_hi_tbl);
    uint8x16_t contrib_lo = vshlq_u8(bits, vreinterpretq_s8_u8(vreinterpretq_u8_s8(sh_lo)));
    uint8x16_t contrib_hi = vshlq_u8(bits, vreinterpretq_s8_u8(vreinterpretq_u8_s8(sh_hi)));
    uint8_t b0 = (uint8_t)vaddvq_u8(contrib_lo);
    uint8_t b1 = (uint8_t)vaddvq_u8(contrib_hi);
    dst[0] = b0; dst[1] = b1;
    }
    unsigned rem = (unsigned)width - x;
    if (rem) {
        uint8_t tmp_idx[16] = {0};
        mve_pred16_t p = mpix_tail_pred_u8(rem);
        mpix_rgb24_argmin_palette_u8(src, pal, pal_size, offR, offG, offB, p, tmp_idx);
        size_t bytes = (rem + 7) / 8; /* 1 or 2 */
        uint8_t b0 = 0, b1 = 0;
        unsigned n0 = rem > 8 ? 8u : rem;
        for (unsigned i = 0; i < n0; ++i) { b0 |= (uint8_t)((tmp_idx[i] & 1u) << (7 - i)); }
        if (rem > 8) {
            unsigned n1 = rem - 8;
            for (unsigned i = 0; i < n1; ++i) { b1 |= (uint8_t)((tmp_idx[8 + i] & 1u) << (7 - i)); }
        }
        if (bytes >= 1) dst[0] = b0;
        if (bytes >= 2) dst[1] = b1;
    }
}

/* -------------------- PALETTE4/2/1 -> RGB24 -------------------- */
void mpix_convert_palette4_to_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width,
                                    const struct mpix_palette *palette)
{
    /* Support any width (tail handled below) */

    const uint8_t *pal = palette->colors_rgb24;
    uint16_t pal_sz = mpix_palette_size_inline(palette);
    uint8_t pr[256], pg[256], pb[256];
    for (uint16_t i = 0; i < pal_sz; ++i) { pr[i]=pal[3u*i+0]; pg[i]=pal[3u*i+1]; pb[i]=pal[3u*i+2]; }

    uint8x16_t offs3 = mpix_rgb_offsets3();
    uint8x16_t offR = vaddq_n_u8(offs3, 0);
    uint8x16_t offG = vaddq_n_u8(offs3, 1);
    uint8x16_t offB = vaddq_n_u8(offs3, 2);

    uint16_t x = 0;
    while (x + 16 <= width) {
        uint8x16_t idx = mpix_unpack2x4_u8(src, 8u); /* consumes 8 bytes */
        uint8_t *outb = dst + (uint32_t)x * 3u;
        mve_pred16_t pfull = vctp8q(16);
        uint8x16_t r = vldrbq_gather_offset_z_u8(pr, idx, pfull);
        uint8x16_t g = vldrbq_gather_offset_z_u8(pg, idx, pfull);
        uint8x16_t b = vldrbq_gather_offset_z_u8(pb, idx, pfull);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offR), vreinterpretq_s8_u8(r), pfull);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offG), vreinterpretq_s8_u8(g), pfull);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offB), vreinterpretq_s8_u8(b), pfull);
        src += 8; x += 16;
    }
    unsigned rem = (unsigned)width - x;
    if (rem) {
        unsigned bytes = (unsigned)((rem + 1) / 2);
        /* Safe gather directly from src with predicate on bytes */
        uint8x16_t idx = mpix_unpack2x4_u8(src, bytes);
        uint8_t *outb = dst + (uint32_t)x * 3u;
        mve_pred16_t p = mpix_tail_pred_u8(rem);
        uint8x16_t r = vldrbq_gather_offset_z_u8(pr, idx, p);
        uint8x16_t g = vldrbq_gather_offset_z_u8(pg, idx, p);
        uint8x16_t b = vldrbq_gather_offset_z_u8(pb, idx, p);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offR), vreinterpretq_s8_u8(r), p);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offG), vreinterpretq_s8_u8(g), p);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offB), vreinterpretq_s8_u8(b), p);
    }
}

void mpix_convert_palette2_to_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width,
                                    const struct mpix_palette *palette)
{
    /* Support any width (tail handled below) */

    const uint8_t *pal = palette->colors_rgb24;
    uint16_t pal_sz = mpix_palette_size_inline(palette);
    uint8_t pr[256], pg[256], pb[256];
    for (uint16_t i = 0; i < pal_sz; ++i) { pr[i]=pal[3u*i+0]; pg[i]=pal[3u*i+1]; pb[i]=pal[3u*i+2]; }

    uint8x16_t offs3 = mpix_rgb_offsets3();
    uint8x16_t offR = vaddq_n_u8(offs3, 0);
    uint8x16_t offG = vaddq_n_u8(offs3, 1);
    uint8x16_t offB = vaddq_n_u8(offs3, 2);

    uint16_t x = 0;
    while (x + 16 <= width) {
        uint8x16_t idx = mpix_unpack4x2b_u8(src, 4u); /* consumes 4 bytes */
        uint8_t *outb = dst + (uint32_t)x * 3u;
        mve_pred16_t pfull = vctp8q(16);
        uint8x16_t r = vldrbq_gather_offset_z_u8(pr, idx, pfull);
        uint8x16_t g = vldrbq_gather_offset_z_u8(pg, idx, pfull);
        uint8x16_t b = vldrbq_gather_offset_z_u8(pb, idx, pfull);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offR), vreinterpretq_s8_u8(r), pfull);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offG), vreinterpretq_s8_u8(g), pfull);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offB), vreinterpretq_s8_u8(b), pfull);
        src += 4; x += 16;
    }
    unsigned rem = (unsigned)width - x;
    if (rem) {
        unsigned bytes = (unsigned)((rem + 3) / 4);
        /* Direct gather from src with predicate on bytes */
        uint8x16_t idx = mpix_unpack4x2b_u8(src, bytes);
        uint8_t *outb = dst + (uint32_t)x * 3u;
        mve_pred16_t p = mpix_tail_pred_u8(rem);
        uint8x16_t r = vldrbq_gather_offset_z_u8(pr, idx, p);
        uint8x16_t g = vldrbq_gather_offset_z_u8(pg, idx, p);
        uint8x16_t b = vldrbq_gather_offset_z_u8(pb, idx, p);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offR), vreinterpretq_s8_u8(r), p);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offG), vreinterpretq_s8_u8(g), p);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offB), vreinterpretq_s8_u8(b), p);
    }
}

void mpix_convert_palette1_to_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width,
                                    const struct mpix_palette *palette)
{
    /* Support any width (tail handled below) */

    const uint8_t *pal = palette->colors_rgb24;
    uint16_t pal_sz = mpix_palette_size_inline(palette);
    uint8_t pr[256], pg[256], pb[256];
    for (uint16_t i = 0; i < pal_sz; ++i) { pr[i]=pal[3u*i+0]; pg[i]=pal[3u*i+1]; pb[i]=pal[3u*i+2]; }

    uint8x16_t offs3 = mpix_rgb_offsets3();
    uint8x16_t offR = vaddq_n_u8(offs3, 0);
    uint8x16_t offG = vaddq_n_u8(offs3, 1);
    uint8x16_t offB = vaddq_n_u8(offs3, 2);

    uint16_t x = 0;
    while (x + 16 <= width) {
        /* Expand two bytes into 16 indices (scalar expand acceptable for packing stage) */
        uint8_t buf[16];
        uint8_t b0 = src[0], b1 = src[1];
        for (int i = 0; i < 8; ++i) buf[i]     = (uint8_t)((b0 >> (7 - i)) & 0x1);
        for (int i = 0; i < 8; ++i) buf[8 + i] = (uint8_t)((b1 >> (7 - i)) & 0x1);
        uint8x16_t idx = vld1q_u8(buf);

        uint8_t *outb = dst + (uint32_t)x * 3u;
        mve_pred16_t pfull = vctp8q(16);
        uint8x16_t r = vldrbq_gather_offset_z_u8(pr, idx, pfull);
        uint8x16_t g = vldrbq_gather_offset_z_u8(pg, idx, pfull);
        uint8x16_t b = vldrbq_gather_offset_z_u8(pb, idx, pfull);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offR), vreinterpretq_s8_u8(r), pfull);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offG), vreinterpretq_s8_u8(g), pfull);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offB), vreinterpretq_s8_u8(b), pfull);
        src += 2; x += 16;
    }
    unsigned rem = (unsigned)width - x;
    if (rem) {
        /* bytes to consume */
        size_t bytes = (rem + 7) / 8; /* 1 or 2 */
        uint8_t b0 = src[0];
        uint8_t b1 = (bytes > 1) ? src[1] : 0;
        uint8_t buf[16] = {0};
        for (unsigned i = 0; i < (rem > 8 ? 8u : rem); ++i) buf[i]     = (uint8_t)((b0 >> (7 - i)) & 0x1);
        if (rem > 8) {
            unsigned tail = rem - 8;
            for (unsigned i = 0; i < tail; ++i) buf[8 + i] = (uint8_t)((b1 >> (7 - i)) & 0x1);
        }
        uint8x16_t idx = vld1q_u8(buf);
        uint8_t *outb = dst + (uint32_t)x * 3u;
        mve_pred16_t p = mpix_tail_pred_u8(rem);
        uint8x16_t r = vldrbq_gather_offset_z_u8(pr, idx, p);
        uint8x16_t g = vldrbq_gather_offset_z_u8(pg, idx, p);
        uint8x16_t bb = vldrbq_gather_offset_z_u8(pb, idx, p);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offR), vreinterpretq_s8_u8(r), p);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offG), vreinterpretq_s8_u8(g), p);
        vstrbq_scatter_offset_p_s8((int8_t *)outb, vreinterpretq_s8_u8(offB), vreinterpretq_s8_u8(bb), p);
    }
}
