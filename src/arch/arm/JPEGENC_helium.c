/* SPDX-License-Identifier: Apache-2.0 */
/*
 * Copyright (c) 2025 Brilliant Labs Ltd.
 * Copyright (c) 2025 tinyVision.ai Inc.
 * Portions based on JPEGENC by Larry Bank,
 * Copyright (c) 2021 BitBank Software, Inc.
 */

 /*
 * Helium-optimized JPEG encoder core
 *
 * This file preserves the exact C API of JPEGENC.c while selectively
 * replacing hot paths with Cortex-M55 Helium (MVE) intrinsics-based
 * implementations. Initial version wires through to the scalar baseline
 * to maintain correctness; subsequent iterations will replace the bodies
 * with MVE code.
 */

#include "JPEGENC.h"
#include <arm_mve.h>
#include <string.h>
#include <stdio.h>

// Forward-declare baseline scalar routines we reuse for parts not yet
// converted. We'll weak-link to symbols from JPEGENC.c if needed, but
// the goal is to keep all implementations local here to avoid ODR issues.

// Constants copied via header references from JPEGENC.c; reused through the same tables
extern const unsigned char cZigZag[64];
extern const unsigned char cZigZag2[64];
extern const int iScaleBits[64];
extern const uint8_t hufftable[];
extern const unsigned char huffl_dc[28];
extern const unsigned char huffcr_dc[28];
extern const unsigned char huffl_ac[256];
extern const unsigned char huffcr_ac[256];

// Helpers kept identical to baseline
static void FlushCode(PIL_CODE *pPC)
{
    unsigned char c;
    while (pPC->iLen > 0)
    {
        c = (unsigned char) (pPC->ulAcc >> (REGISTER_WIDTH-8));
        *pPC->pOut++ = c;
        if (c == 0xff) *pPC->pOut++ = 0;
        pPC->ulAcc <<= 8;
        pPC->iLen -= 8;
    }
    pPC->iLen = 0;
}

// Quant table fix identical to baseline; kept scalar (one-time setup)
// Helium public wrappers to keep link separation from baseline
int JPEGEncodeBegin_Helium(JPEGE_IMAGE *pJPEG, JPEGENCODE *pEncode, int iWidth, int iHeight,
                           uint8_t ucPixelType, uint8_t ucSubSample, uint8_t ucQFactor);
int JPEGEncodeEnd_Helium(JPEGE_IMAGE *pJPEG);
int JPEGAddMCU_Helium(JPEGE_IMAGE *pJPEG, JPEGENCODE *pEncode, uint8_t *pPixels, int iPitch);

// We include the baseline header writing and state init by reusing the
// original C file’s bodies through a light wrapper inclusion. To avoid
// duplication here, we declare the functions as weak and provide local
// strong versions that may call into shared helpers above.

// Baseline header/IO handling is reused via calling the baseline functions; no extra includes.

// Helium replacements for hot kernels

// Vectorized RGB24 sampling to Y/Cb/Cr, 8x8 block, tail-safe via vctp8q
static void JPEGSample24_helium(unsigned char *pSrc, signed char *pMCU, int lsize, int cx, int cy)
{
    // Helium implementation for RGB888 input (B,G,R byte order), 4:4:4 sampling.
    // Formula matches baseline exactly:
    //  Y  = (((R*1225 + G*2404 + B*467) >> 12) - 128)
    //  Cb = ((B<<11) - R*691 - G*1357) >> 12
    //  Cr = ((R<<11) - G*1715 - B*333) >> 12

    // Fallback if cy/cx are zero
    if (cx <= 0 || cy <= 0) return;

    for (int y = 0; y < cy; y++) {
        unsigned char *s = pSrc + y * lsize;
        signed char *pY  = pMCU + y * 8;
        signed char *pCb = pY + 64;
        signed char *pCr = pY + 128;

        int x = 0;
        // Process 8 pixels per row (or a tail if cx < 8)
        int n = cx;
        // Temporary scalars for load; arithmetic is vectorized
        uint16_t r16[8], g16[8], b16[8];

        // Load up to 8 pixels B,G,Red from this row
        for (int i = 0; i < n; i++) {
            uint8_t b = s[3*i + 0];
            uint8_t g = s[3*i + 1];
            uint8_t r = s[3*i + 2];
            r16[i] = r;
            g16[i] = g;
            b16[i] = b;
        }
        // Zero-fill remaining lanes if any
        for (int i = n; i < 8; i++) {
            r16[i] = g16[i] = b16[i] = 0;
        }

        // Load into vectors (8x16-bit)
        int16x8_t vr16 = vldrhq_s16((int16_t*)r16);
        int16x8_t vg16 = vldrhq_s16((int16_t*)g16);
        int16x8_t vb16 = vldrhq_s16((int16_t*)b16);

    // Widen to 32-bit halves (sign-extend)
    int32x4_t vr_lo = vmovlbq_s16(vr16);
    int32x4_t vr_hi = vmovltq_s16(vr16);
    int32x4_t vg_lo = vmovlbq_s16(vg16);
    int32x4_t vg_hi = vmovltq_s16(vg16);
    int32x4_t vb_lo = vmovlbq_s16(vb16);
    int32x4_t vb_hi = vmovltq_s16(vb16);

        // Compute Y low 4 lanes
        int32x4_t y_lo = vmulq_n_s32(vr_lo, 1225);
        y_lo = vmlaq_n_s32(y_lo, vg_lo, 2404);
        y_lo = vmlaq_n_s32(y_lo, vb_lo, 467);
    y_lo = vshlq_s32(y_lo, vdupq_n_s32(-12));
        y_lo = vsubq_n_s32(y_lo, 128);
        // Compute Y high 4 lanes
        int32x4_t y_hi = vmulq_n_s32(vr_hi, 1225);
        y_hi = vmlaq_n_s32(y_hi, vg_hi, 2404);
        y_hi = vmlaq_n_s32(y_hi, vb_hi, 467);
    y_hi = vshlq_s32(y_hi, vdupq_n_s32(-12));
        y_hi = vsubq_n_s32(y_hi, 128);

        // Compute Cb = ((B<<11) - R*691 - G*1357) >> 12
        int32x4_t cb_lo = vshlq_n_s32(vb_lo, 11);
        cb_lo = vmlaq_n_s32(cb_lo, vr_lo, -691);
        cb_lo = vmlaq_n_s32(cb_lo, vg_lo, -1357);
    cb_lo = vshlq_s32(cb_lo, vdupq_n_s32(-12));
        int32x4_t cb_hi = vshlq_n_s32(vb_hi, 11);
        cb_hi = vmlaq_n_s32(cb_hi, vr_hi, -691);
        cb_hi = vmlaq_n_s32(cb_hi, vg_hi, -1357);
    cb_hi = vshlq_s32(cb_hi, vdupq_n_s32(-12));

        // Compute Cr = ((R<<11) - G*1715 - B*333) >> 12
        int32x4_t cr_lo = vshlq_n_s32(vr_lo, 11);
        cr_lo = vmlaq_n_s32(cr_lo, vg_lo, -1715);
        cr_lo = vmlaq_n_s32(cr_lo, vb_lo, -333);
    cr_lo = vshlq_s32(cr_lo, vdupq_n_s32(-12));
        int32x4_t cr_hi = vshlq_n_s32(vr_hi, 11);
        cr_hi = vmlaq_n_s32(cr_hi, vg_hi, -1715);
        cr_hi = vmlaq_n_s32(cr_hi, vb_hi, -333);
    cr_hi = vshlq_s32(cr_hi, vdupq_n_s32(-12));

        // Store back first cx results with saturation to int8
        // Interleave even/odd lanes back to contiguous order
        int32_t ybuf[8], cbbuf[8], crbuf[8];
        int32_t ylo[4], yhi[4], cblo[4], cbhi[4], crlo[4], crhi[4];
        vstrwq_s32(&ylo[0], y_lo);
        vstrwq_s32(&yhi[0], y_hi);
        vstrwq_s32(&cblo[0], cb_lo);
        vstrwq_s32(&cbhi[0], cb_hi);
        vstrwq_s32(&crlo[0], cr_lo);
        vstrwq_s32(&crhi[0], cr_hi);
        for (int k = 0; k < 4; k++) {
            ybuf[2*k + 0] = ylo[k];
            ybuf[2*k + 1] = yhi[k];
            cbbuf[2*k + 0] = cblo[k];
            cbbuf[2*k + 1] = cbhi[k];
            crbuf[2*k + 0] = crlo[k];
            crbuf[2*k + 1] = crhi[k];
        }

        for (int i = 0; i < n; i++) {
            int yv = ybuf[i];
            int cbv = cbbuf[i];
            int crv = crbuf[i];
            // Narrow to signed char (baseline simply casts)
            if (yv < -128) yv = -128; else if (yv > 127) yv = 127;
            if (cbv < -128) cbv = -128; else if (cbv > 127) cbv = 127;
            if (crv < -128) crv = -128; else if (crv > 127) crv = 127;
            pCb[i] = (signed char)cbv;
            pCr[i] = (signed char)crv;
            pY[i]  = (signed char)yv;
        }

        // Advance base pointers for next segment (we only have up to 8 pixels per row)
        (void)x; // quiet unused warning
    }
}

// (test-only wrappers removed)

// Vectorized RGB24 (R,G,B order) sampling to Y/Cb/Cr, 8x8 block
static void JPEGSample24RGB_helium(unsigned char *pSrc, signed char *pMCU, int lsize, int cx, int cy)
{
    if (cx <= 0 || cy <= 0) return;

    for (int y = 0; y < cy; y++) {
        unsigned char *s = pSrc + y * lsize;
        signed char *pY  = pMCU + y * 8;
        signed char *pCb = pY + 64;
        signed char *pCr = pY + 128;

        int n = cx;
        uint16_t r16[8], g16[8], b16[8];
        for (int i = 0; i < n; i++) {
            uint8_t r = s[3*i + 0];
            uint8_t g = s[3*i + 1];
            uint8_t b = s[3*i + 2];
            r16[i] = r; g16[i] = g; b16[i] = b;
        }
        for (int i = n; i < 8; i++) r16[i] = g16[i] = b16[i] = 0;

        int16x8_t vr16 = vldrhq_s16((int16_t*)r16);
        int16x8_t vg16 = vldrhq_s16((int16_t*)g16);
        int16x8_t vb16 = vldrhq_s16((int16_t*)b16);

        int32x4_t vr_lo = vmovlbq_s16(vr16);
        int32x4_t vr_hi = vmovltq_s16(vr16);
        int32x4_t vg_lo = vmovlbq_s16(vg16);
        int32x4_t vg_hi = vmovltq_s16(vg16);
        int32x4_t vb_lo = vmovlbq_s16(vb16);
        int32x4_t vb_hi = vmovltq_s16(vb16);

        int32x4_t y_lo = vmulq_n_s32(vr_lo, 1225);
        y_lo = vmlaq_n_s32(y_lo, vg_lo, 2404);
        y_lo = vmlaq_n_s32(y_lo, vb_lo, 467);
        y_lo = vshlq_s32(y_lo, vdupq_n_s32(-12));
        y_lo = vsubq_n_s32(y_lo, 128);
        int32x4_t y_hi = vmulq_n_s32(vr_hi, 1225);
        y_hi = vmlaq_n_s32(y_hi, vg_hi, 2404);
        y_hi = vmlaq_n_s32(y_hi, vb_hi, 467);
        y_hi = vshlq_s32(y_hi, vdupq_n_s32(-12));
        y_hi = vsubq_n_s32(y_hi, 128);

        int32x4_t cb_lo = vshlq_n_s32(vb_lo, 11);
        cb_lo = vmlaq_n_s32(cb_lo, vr_lo, -691);
        cb_lo = vmlaq_n_s32(cb_lo, vg_lo, -1357);
        cb_lo = vshlq_s32(cb_lo, vdupq_n_s32(-12));
        int32x4_t cb_hi = vshlq_n_s32(vb_hi, 11);
        cb_hi = vmlaq_n_s32(cb_hi, vr_hi, -691);
        cb_hi = vmlaq_n_s32(cb_hi, vg_hi, -1357);
        cb_hi = vshlq_s32(cb_hi, vdupq_n_s32(-12));

        int32x4_t cr_lo = vshlq_n_s32(vr_lo, 11);
        cr_lo = vmlaq_n_s32(cr_lo, vg_lo, -1715);
        cr_lo = vmlaq_n_s32(cr_lo, vb_lo, -333);
        cr_lo = vshlq_s32(cr_lo, vdupq_n_s32(-12));
        int32x4_t cr_hi = vshlq_n_s32(vr_hi, 11);
        cr_hi = vmlaq_n_s32(cr_hi, vg_hi, -1715);
        cr_hi = vmlaq_n_s32(cr_hi, vb_hi, -333);
        cr_hi = vshlq_s32(cr_hi, vdupq_n_s32(-12));

        int32_t ylo[4], yhi[4], cblo[4], cbhi[4], crlo[4], crhi[4];
        vstrwq_s32(&ylo[0], y_lo);
        vstrwq_s32(&yhi[0], y_hi);
        vstrwq_s32(&cblo[0], cb_lo);
        vstrwq_s32(&cbhi[0], cb_hi);
        vstrwq_s32(&crlo[0], cr_lo);
        vstrwq_s32(&crhi[0], cr_hi);

        for (int i = 0; i < n; i++) {
            int yv = (i & 1) ? yhi[i>>1] : ylo[i>>1];
            int cbv = (i & 1) ? cbhi[i>>1] : cblo[i>>1];
            int crv = (i & 1) ? crhi[i>>1] : crlo[i>>1];
            if (yv < -128) yv = -128; else if (yv > 127) yv = 127;
            if (cbv < -128) cbv = -128; else if (cbv > 127) cbv = 127;
            if (crv < -128) crv = -128; else if (crv > 127) crv = 127;
            pCb[i] = (signed char)cbv;
            pCr[i] = (signed char)crv;
            pY[i]  = (signed char)yv;
        }
    }
}

// (test-only wrappers removed)

// 4:2:0 subsampling for RGB24 (B,G,R order) over an 8x8 source region producing:
// - 8x8 Y (luma) written into pLUM
// - 4x4 Cb/Cr averages (of 2x2 pixels) written into pCb/pCr
static void JPEGSubSample24_helium(unsigned char *pSrc, signed char *pLUM, signed char *pCb, signed char *pCr,
                                   int lsize, int cx, int cy)
{
    // Convert an up-to-8x8 block; baseline halves cx/cy to count 2x2 groups
    if (cx <= 0 || cy <= 0) return;

    int nbx = (cx + 1) >> 1; // groups per row
    int nby = (cy + 1) >> 1; // group rows

    for (int gy = 0; gy < nby; gy++) {
        for (int gx = 0; gx < nbx; gx++) {
            // Gather 2x2 pixels starting at (2*gx, 2*gy)
            unsigned char *s0 = pSrc + (2*gy) * lsize + (2*gx) * 3;
            unsigned char *s1 = s0 + 3;          // right neighbor
            unsigned char *s2 = s0 + lsize;      // below
            unsigned char *s3 = s2 + 3;          // below-right

    // Build two 16-bit vectors for even (s0,s2) and odd (s1,s3) lanes,
        // then widen to 32-bit and compute Y/chroma separately to preserve lane order.
        uint16_t rE16[8], gE16[8], bE16[8];
        uint16_t rO16[8], gO16[8], bO16[8];
    // zero-fill all lanes first
    for (int k = 0; k < 8; k++) { rE16[k] = gE16[k] = bE16[k] = 0; rO16[k] = gO16[k] = bO16[k] = 0; }
    // Place at indices 0 and 2 so vmovlb picks [0,2] into lanes [0,1]
    // even lanes: s0 -> idx0, s2 -> idx2
    bE16[0] = s0[0]; gE16[0] = s0[1]; rE16[0] = s0[2];
    bE16[2] = s2[0]; gE16[2] = s2[1]; rE16[2] = s2[2];
    // odd lanes: s1 -> idx0, s3 -> idx2
    bO16[0] = s1[0]; gO16[0] = s1[1]; rO16[0] = s1[2];
    bO16[2] = s3[0]; gO16[2] = s3[1]; rO16[2] = s3[2];

        // Vectorized Y for even lanes (s0, s2)
        int16x8_t vr16e = vldrhq_s16((int16_t*)rE16);
        int16x8_t vg16e = vldrhq_s16((int16_t*)gE16);
        int16x8_t vb16e = vldrhq_s16((int16_t*)bE16);
        int32x4_t vre = vmovlbq_s16(vr16e);
        int32x4_t vge = vmovlbq_s16(vg16e);
        int32x4_t vbe = vmovlbq_s16(vb16e);
        int32x4_t ye = vmulq_n_s32(vre, 1225);
        ye = vmlaq_n_s32(ye, vge, 2404);
        ye = vmlaq_n_s32(ye, vbe, 467);
    ye = vshlq_s32(ye, vdupq_n_s32(-12));
        ye = vsubq_n_s32(ye, 128);

        // Vectorized Y for odd lanes (s1, s3)
        int16x8_t vr16o = vldrhq_s16((int16_t*)rO16);
        int16x8_t vg16o = vldrhq_s16((int16_t*)gO16);
        int16x8_t vb16o = vldrhq_s16((int16_t*)bO16);
        int32x4_t vro = vmovlbq_s16(vr16o);
        int32x4_t vgo = vmovlbq_s16(vg16o);
        int32x4_t vbo = vmovlbq_s16(vb16o);
        int32x4_t yo = vmulq_n_s32(vro, 1225);
        yo = vmlaq_n_s32(yo, vgo, 2404);
        yo = vmlaq_n_s32(yo, vbo, 467);
    yo = vshlq_s32(yo, vdupq_n_s32(-12));
        yo = vsubq_n_s32(yo, 128);

        // Extract and clamp Y lanes (order: y0=s0, y1=s1, y2=s2, y3=s3)
        int y0 = vgetq_lane_s32(ye, 0);
        int y2 = vgetq_lane_s32(ye, 1);
        int y1 = vgetq_lane_s32(yo, 0);
        int y3 = vgetq_lane_s32(yo, 1);
        if (y0 < -128) y0 = -128; else if (y0 > 127) y0 = 127;
        if (y1 < -128) y1 = -128; else if (y1 > 127) y1 = 127;
        if (y2 < -128) y2 = -128; else if (y2 > 127) y2 = 127;
        if (y3 < -128) y3 = -128; else if (y3 > 127) y3 = 127;
            pLUM[0] = (signed char)y0;
            pLUM[1] = (signed char)y1;
            pLUM[8] = (signed char)y2;
            pLUM[9] = (signed char)y3;

            // debug block moved after chroma computation

        // Vectorized raw chroma: compute for even and odd lanes, then sum both pairs and >>14
        int32x4_t cb_e = vshlq_n_s32(vbe, 11);
        cb_e = vmlaq_n_s32(cb_e, vre, -691);
        cb_e = vmlaq_n_s32(cb_e, vge, -1357);
        int32x4_t cr_e = vshlq_n_s32(vre, 11);
        cr_e = vmlaq_n_s32(cr_e, vge, -1715);
        cr_e = vmlaq_n_s32(cr_e, vbe, -333);

        int32x4_t cb_o = vshlq_n_s32(vbo, 11);
        cb_o = vmlaq_n_s32(cb_o, vro, -691);
        cb_o = vmlaq_n_s32(cb_o, vgo, -1357);
        int32x4_t cr_o = vshlq_n_s32(vro, 11);
        cr_o = vmlaq_n_s32(cr_o, vgo, -1715);
        cr_o = vmlaq_n_s32(cr_o, vbo, -333);

        int cb = (vgetq_lane_s32(cb_e, 0) + vgetq_lane_s32(cb_e, 1)
            + vgetq_lane_s32(cb_o, 0) + vgetq_lane_s32(cb_o, 1)) >> 14;
        int cr = (vgetq_lane_s32(cr_e, 0) + vgetq_lane_s32(cr_e, 1)
            + vgetq_lane_s32(cr_o, 0) + vgetq_lane_s32(cr_o, 1)) >> 14;
            if (cb < -128) cb = -128; else if (cb > 127) cb = 127;
            if (cr < -128) cr = -128; else if (cr > 127) cr = 127;
            *pCr++ = (signed char)cr; // baseline stores Cr then Cb
            *pCb++ = (signed char)cb;

            pLUM += 2; // advance 2 columns in Y block
        }
    // move to next group row (match baseline SubSample24):
    // baseline uses original cx, cy before halving: pCr += 8 - cx/2; pCb += 8 - cx/2; pLUM += 8 + (4 - cx/2)*2
    // Here nbx == cx/2 rounded up; for exact parity use the same expression with original cx
    pCr += 8 - ((cx + 1) >> 1);
    pCb += 8 - ((cx + 1) >> 1);
    pLUM += 8 + (4 - ((cx + 1) >> 1)) * 2;
    }
}

// 4:2:0 subsampling for RGB24 (R,G,B order)
static void JPEGSubSample24RGB_helium(unsigned char *pSrc, signed char *pLUM, signed char *pCb, signed char *pCr,
                                      int lsize, int cx, int cy)
{
    if (cx <= 0 || cy <= 0) return;

    int nbx = (cx + 1) >> 1; // groups per row
    int nby = (cy + 1) >> 1; // group rows

    for (int gy = 0; gy < nby; gy++) {
        for (int gx = 0; gx < nbx; gx++) {
            // Gather 2x2 pixels starting at (2*gx, 2*gy) in RGB order
            unsigned char *s0 = pSrc + (2*gy) * lsize + (2*gx) * 3;
            unsigned char *s1 = s0 + 3;          // right
            unsigned char *s2 = s0 + lsize;      // below
            unsigned char *s3 = s2 + 3;          // below-right

            uint16_t rE16[8], gE16[8], bE16[8];
            uint16_t rO16[8], gO16[8], bO16[8];
            for (int k = 0; k < 8; k++) { rE16[k] = gE16[k] = bE16[k] = 0; rO16[k] = gO16[k] = bO16[k] = 0; }
            // even lanes: s0 -> idx0, s2 -> idx2
            rE16[0] = s0[0]; gE16[0] = s0[1]; bE16[0] = s0[2];
            rE16[2] = s2[0]; gE16[2] = s2[1]; bE16[2] = s2[2];
            // odd lanes: s1 -> idx0, s3 -> idx2
            rO16[0] = s1[0]; gO16[0] = s1[1]; bO16[0] = s1[2];
            rO16[2] = s3[0]; gO16[2] = s3[1]; bO16[2] = s3[2];

            // Vectorized Y for even lanes (s0,s2)
            int16x8_t vr16e = vldrhq_s16((int16_t*)rE16);
            int16x8_t vg16e = vldrhq_s16((int16_t*)gE16);
            int16x8_t vb16e = vldrhq_s16((int16_t*)bE16);
            int32x4_t vre = vmovlbq_s16(vr16e);
            int32x4_t vge = vmovlbq_s16(vg16e);
            int32x4_t vbe = vmovlbq_s16(vb16e);
            int32x4_t ye = vmulq_n_s32(vre, 1225);
            ye = vmlaq_n_s32(ye, vge, 2404);
            ye = vmlaq_n_s32(ye, vbe, 467);
            ye = vshlq_s32(ye, vdupq_n_s32(-12));
            ye = vsubq_n_s32(ye, 128);

            // Vectorized Y for odd lanes (s1,s3)
            int16x8_t vr16o = vldrhq_s16((int16_t*)rO16);
            int16x8_t vg16o = vldrhq_s16((int16_t*)gO16);
            int16x8_t vb16o = vldrhq_s16((int16_t*)bO16);
            int32x4_t vro = vmovlbq_s16(vr16o);
            int32x4_t vgo = vmovlbq_s16(vg16o);
            int32x4_t vbo = vmovlbq_s16(vb16o);
            int32x4_t yo = vmulq_n_s32(vro, 1225);
            yo = vmlaq_n_s32(yo, vgo, 2404);
            yo = vmlaq_n_s32(yo, vbo, 467);
            yo = vshlq_s32(yo, vdupq_n_s32(-12));
            yo = vsubq_n_s32(yo, 128);

            int y0 = vgetq_lane_s32(ye, 0);
            int y2 = vgetq_lane_s32(ye, 1);
            int y1 = vgetq_lane_s32(yo, 0);
            int y3 = vgetq_lane_s32(yo, 1);
            if (y0 < -128) y0 = -128; else if (y0 > 127) y0 = 127;
            if (y1 < -128) y1 = -128; else if (y1 > 127) y1 = 127;
            if (y2 < -128) y2 = -128; else if (y2 > 127) y2 = 127;
            if (y3 < -128) y3 = -128; else if (y3 > 127) y3 = 127;
            pLUM[0] = (signed char)y0;
            pLUM[1] = (signed char)y1;
            pLUM[8] = (signed char)y2;
            pLUM[9] = (signed char)y3;

            // Cb/Cr raw and average
            int32x4_t cb_e = vshlq_n_s32(vbe, 11);
            cb_e = vmlaq_n_s32(cb_e, vre, -691);
            cb_e = vmlaq_n_s32(cb_e, vge, -1357);
            int32x4_t cr_e = vshlq_n_s32(vre, 11);
            cr_e = vmlaq_n_s32(cr_e, vge, -1715);
            cr_e = vmlaq_n_s32(cr_e, vbe, -333);
            int32x4_t cb_o = vshlq_n_s32(vbo, 11);
            cb_o = vmlaq_n_s32(cb_o, vro, -691);
            cb_o = vmlaq_n_s32(cb_o, vgo, -1357);
            int32x4_t cr_o = vshlq_n_s32(vro, 11);
            cr_o = vmlaq_n_s32(cr_o, vgo, -1715);
            cr_o = vmlaq_n_s32(cr_o, vbo, -333);

            int cb = (vgetq_lane_s32(cb_e, 0) + vgetq_lane_s32(cb_e, 1)
                   + vgetq_lane_s32(cb_o, 0) + vgetq_lane_s32(cb_o, 1)) >> 14;
            int cr = (vgetq_lane_s32(cr_e, 0) + vgetq_lane_s32(cr_e, 1)
                   + vgetq_lane_s32(cr_o, 0) + vgetq_lane_s32(cr_o, 1)) >> 14;
            if (cb < -128) cb = -128; else if (cb > 127) cb = 127;
            if (cr < -128) cr = -128; else if (cr > 127) cr = 127;
            *pCr++ = (signed char)cr; // Cr then Cb (match baseline ordering)
            *pCb++ = (signed char)cb;

            pLUM += 2;
        }
        pCr += 8 - ((cx + 1) >> 1);
        pCb += 8 - ((cx + 1) >> 1);
        pLUM += 8 + (4 - ((cx + 1) >> 1)) * 2;
    }
}

// 4:2:0 subsampling for RGB565 (16-bit, 5:6:5), per 2x2 block
static void JPEGSubSample16_helium(unsigned char *pSrc, signed char *pLUM, signed char *pCb, signed char *pCr,
                                   int lsize, int cx, int cy)
{
    if (cx <= 0 || cy <= 0) return;
    unsigned short *pUS = (unsigned short *)pSrc;
    int nbx = (cx + 1) >> 1;
    int nby = (cy + 1) >> 1;
    int pitch16 = lsize >> 1;

    for (int gy = 0; gy < nby; gy++) {
        for (int gx = 0; gx < nbx; gx++) {
            unsigned short u0 = pUS[(2*gy) * pitch16 + (2*gx) + 0];
            unsigned short u1 = pUS[(2*gy) * pitch16 + (2*gx) + 1];
            unsigned short u2 = pUS[(2*gy+1) * pitch16 + (2*gx) + 0];
            unsigned short u3 = pUS[(2*gy+1) * pitch16 + (2*gx) + 1];

            // Unpack to 8-bit R,G,B exactly like baseline
            uint8_t b0 = (uint8_t)(((u0 & 0x1f) << 3) | (u0 & 7));
            uint8_t g0 = (uint8_t)(((u0 & 0x7e0) >> 3) | ((u0 & 0x60) >> 5));
            uint8_t r0 = (uint8_t)(((u0 & 0xf800) >> 8) | ((u0 & 0x3800) >> 11));
            uint8_t b1 = (uint8_t)(((u1 & 0x1f) << 3) | (u1 & 7));
            uint8_t g1 = (uint8_t)(((u1 & 0x7e0) >> 3) | ((u1 & 0x60) >> 5));
            uint8_t r1 = (uint8_t)(((u1 & 0xf800) >> 8) | ((u1 & 0x3800) >> 11));
            uint8_t b2 = (uint8_t)(((u2 & 0x1f) << 3) | (u2 & 7));
            uint8_t g2 = (uint8_t)(((u2 & 0x7e0) >> 3) | ((u2 & 0x60) >> 5));
            uint8_t r2 = (uint8_t)(((u2 & 0xf800) >> 8) | ((u2 & 0x3800) >> 11));
            uint8_t b3 = (uint8_t)(((u3 & 0x1f) << 3) | (u3 & 7));
            uint8_t g3 = (uint8_t)(((u3 & 0x7e0) >> 3) | ((u3 & 0x60) >> 5));
            uint8_t r3 = (uint8_t)(((u3 & 0xf800) >> 8) | ((u3 & 0x3800) >> 11));

            // Place into vectors like RGB888 path
            uint16_t rE16[8]={0}, gE16[8]={0}, bE16[8]={0};
            uint16_t rO16[8]={0}, gO16[8]={0}, bO16[8]={0};
            rE16[0]=r0; gE16[0]=g0; bE16[0]=b0; rE16[2]=r2; gE16[2]=g2; bE16[2]=b2;
            rO16[0]=r1; gO16[0]=g1; bO16[0]=b1; rO16[2]=r3; gO16[2]=g3; bO16[2]=b3;

            int16x8_t vr16e = vldrhq_s16((int16_t*)rE16);
            int16x8_t vg16e = vldrhq_s16((int16_t*)gE16);
            int16x8_t vb16e = vldrhq_s16((int16_t*)bE16);
            int32x4_t vre = vmovlbq_s16(vr16e);
            int32x4_t vge = vmovlbq_s16(vg16e);
            int32x4_t vbe = vmovlbq_s16(vb16e);
            int32x4_t ye = vmulq_n_s32(vre, 1225);
            ye = vmlaq_n_s32(ye, vge, 2404);
            ye = vmlaq_n_s32(ye, vbe, 467);
            ye = vshlq_s32(ye, vdupq_n_s32(-12));
            ye = vsubq_n_s32(ye, 128);

            int16x8_t vr16o = vldrhq_s16((int16_t*)rO16);
            int16x8_t vg16o = vldrhq_s16((int16_t*)gO16);
            int16x8_t vb16o = vldrhq_s16((int16_t*)bO16);
            int32x4_t vro = vmovlbq_s16(vr16o);
            int32x4_t vgo = vmovlbq_s16(vg16o);
            int32x4_t vbo = vmovlbq_s16(vb16o);
            int32x4_t yo = vmulq_n_s32(vro, 1225);
            yo = vmlaq_n_s32(yo, vgo, 2404);
            yo = vmlaq_n_s32(yo, vbo, 467);
            yo = vshlq_s32(yo, vdupq_n_s32(-12));
            yo = vsubq_n_s32(yo, 128);

            int y0 = vgetq_lane_s32(ye, 0);
            int y2 = vgetq_lane_s32(ye, 1);
            int y1 = vgetq_lane_s32(yo, 0);
            int y3 = vgetq_lane_s32(yo, 1);
            if (y0 < -128) y0 = -128; else if (y0 > 127) y0 = 127;
            if (y1 < -128) y1 = -128; else if (y1 > 127) y1 = 127;
            if (y2 < -128) y2 = -128; else if (y2 > 127) y2 = 127;
            if (y3 < -128) y3 = -128; else if (y3 > 127) y3 = 127;
            pLUM[0] = (signed char)y0;
            pLUM[1] = (signed char)y1;
            pLUM[8] = (signed char)y2;
            pLUM[9] = (signed char)y3;

            int32x4_t cb_e = vshlq_n_s32(vbe, 11);
            cb_e = vmlaq_n_s32(cb_e, vre, -691);
            cb_e = vmlaq_n_s32(cb_e, vge, -1357);
            int32x4_t cr_e = vshlq_n_s32(vre, 11);
            cr_e = vmlaq_n_s32(cr_e, vge, -1715);
            cr_e = vmlaq_n_s32(cr_e, vbe, -333);
            int32x4_t cb_o = vshlq_n_s32(vbo, 11);
            cb_o = vmlaq_n_s32(cb_o, vro, -691);
            cb_o = vmlaq_n_s32(cb_o, vgo, -1357);
            int32x4_t cr_o = vshlq_n_s32(vro, 11);
            cr_o = vmlaq_n_s32(cr_o, vgo, -1715);
            cr_o = vmlaq_n_s32(cr_o, vbo, -333);
            int cb = (vgetq_lane_s32(cb_e,0) + vgetq_lane_s32(cb_e,1) + vgetq_lane_s32(cb_o,0) + vgetq_lane_s32(cb_o,1)) >> 14;
            int cr = (vgetq_lane_s32(cr_e,0) + vgetq_lane_s32(cr_e,1) + vgetq_lane_s32(cr_o,0) + vgetq_lane_s32(cr_o,1)) >> 14;
            if (cb < -128) cb = -128; else if (cb > 127) cb = 127;
            if (cr < -128) cr = -128; else if (cr > 127) cr = 127;
            *pCr++ = (signed char)cr;
            *pCb++ = (signed char)cb;

            pLUM += 2;
        }
        pCr += 8 - ((cx + 1) >> 1);
        pCb += 8 - ((cx + 1) >> 1);
        pLUM += 8 + (4 - ((cx + 1) >> 1)) * 2;
    }
}

// Helium-optimized YUYV -> 4:2:0 subsample (build full MCU)
static void JPEGSubSampleYUYV_helium(uint8_t *pImage, int8_t *pMCUData, int iPitch)
{
    // Output bins
    uint8_t *pY0 = (uint8_t *)pMCUData;
    uint8_t *pY1 = (uint8_t *)&pMCUData[64*1];
    uint8_t *pY2 = (uint8_t *)&pMCUData[64*2];
    uint8_t *pY3 = (uint8_t *)&pMCUData[64*3];
    uint8_t *pCr = (uint8_t *)&pMCUData[64*4];
    uint8_t *pCb = (uint8_t *)&pMCUData[64*5];

    uint8_t *s = pImage;

    for (int y = 0; y < 4; y++) { // 4 quadrants rows
        for (int x = 0; x < 4; x++) {
            // Copy Y for 2x2 block (scalar stores, contiguous in each block)
            pY0[0] = s[0]; pY0[1] = s[2];
            pY0[8] = s[iPitch + 0]; pY0[9] = s[iPitch + 2];

            pY1[0] = s[16]; pY1[1] = s[18];
            pY1[8] = s[iPitch + 16]; pY1[9] = s[iPitch + 18];

            pY2[0] = s[(iPitch*8)+0]; pY2[1] = s[(iPitch*8)+2];
            pY2[8] = s[(iPitch*9)+0]; pY2[9] = s[(iPitch*9)+2];

            pY3[0] = s[(iPitch*8)+16]; pY3[1] = s[(iPitch*8)+18];
            pY3[8] = s[(iPitch*9)+16]; pY3[9] = s[(iPitch*9)+18];

            // Compute vertical average for chroma (scalar): (a+b+1)>>1
            uint16_t cr0 = (uint16_t)s[1] + (uint16_t)s[iPitch+1] + 1; cr0 >>= 1;
            uint16_t cb0 = (uint16_t)s[3] + (uint16_t)s[iPitch+3] + 1; cb0 >>= 1;
            uint16_t cr1 = (uint16_t)s[17] + (uint16_t)s[iPitch+17] + 1; cr1 >>= 1;
            uint16_t cb1 = (uint16_t)s[19] + (uint16_t)s[iPitch+19] + 1; cb1 >>= 1;

            uint16_t cr2 = (uint16_t)s[(iPitch*8)+1] + (uint16_t)s[(iPitch*9)+1] + 1; cr2 >>= 1;
            uint16_t cb2 = (uint16_t)s[(iPitch*8)+3] + (uint16_t)s[(iPitch*9)+3] + 1; cb2 >>= 1;
            uint16_t cr3 = (uint16_t)s[(iPitch*8)+17] + (uint16_t)s[(iPitch*9)+17] + 1; cr3 >>= 1;
            uint16_t cb3 = (uint16_t)s[(iPitch*8)+19] + (uint16_t)s[(iPitch*9)+19] + 1; cb3 >>= 1;

            pCr[0] = (uint8_t)cr0; pCb[0] = (uint8_t)cb0;
            pCr[4] = (uint8_t)cr1; pCb[4] = (uint8_t)cb1;
            pCr[32] = (uint8_t)cr2; pCb[32] = (uint8_t)cb2;
            pCr[36] = (uint8_t)cr3; pCb[36] = (uint8_t)cb3;

            pCr++; pCb++;
            pY0 += 2; pY1 += 2; pY2 += 2; pY3 += 2;
            s += 4;
        }
        pCr += 4; pCb += 4;
        pY0 += 8; pY1 += 8; pY2 += 8; pY3 += 8;
        s += (iPitch*2) - 16;
    }

    // XOR adjust +/-128 across all 6*16 dwords
    uint32_t *pU32 = (uint32_t *)pMCUData;
    uint32x4_t mask = vdupq_n_u32(0x80808080u);
    for (int i = 0; i < 6*16; i += 4) {
        uint32x4_t v = vld1q_u32(&pU32[i]);
        v = veorq_u32(v, mask);
        vst1q_u32(&pU32[i], v);
    }
}

// AAN FDCT with MVE: row pass vectorized (4 rows in parallel), column pass scalar
static void JPEGFDCT_helium(signed char *pMCUSrc, signed short *pMCUDest)
{
    // Row pass: 16-bit butterfly, widen to 32-bit only for constant multiplies
    for (int y = 0; y < 8; y += 4) {
        // Build 8 vectors (columns 0..7), each holds 4 rows in lanes 0,2,4,6
        int16_t C0[8]={0},C1[8]={0},C2[8]={0},C3[8]={0},C4[8]={0},C5[8]={0},C6[8]={0},C7[8]={0};
        for (int r = 0; r < 4; r++) {
            signed char *s = pMCUSrc + (y + r) * 8;
            int lane = r<<1; // 0,2,4,6
            C0[lane]=s[0]; C1[lane]=s[1]; C2[lane]=s[2]; C3[lane]=s[3];
            C4[lane]=s[4]; C5[lane]=s[5]; C6[lane]=s[6]; C7[lane]=s[7];
        }
        int16x8_t V0 = vldrhq_s16(C0);
        int16x8_t V1 = vldrhq_s16(C1);
        int16x8_t V2 = vldrhq_s16(C2);
        int16x8_t V3 = vldrhq_s16(C3);
        int16x8_t V4 = vldrhq_s16(C4);
        int16x8_t V5 = vldrhq_s16(C5);
        int16x8_t V6 = vldrhq_s16(C6);
        int16x8_t V7 = vldrhq_s16(C7);

        // 16-bit butterfly
        int16x8_t tmp0 = vaddq_s16(V0, V7);
        int16x8_t tmp1 = vaddq_s16(V1, V6);
        int16x8_t tmp2 = vaddq_s16(V2, V5);
        int16x8_t tmp3 = vaddq_s16(V3, V4);
        int16x8_t tmp7 = vsubq_s16(V0, V7);
        int16x8_t tmp6 = vsubq_s16(V1, V6);
        int16x8_t tmp5 = vsubq_s16(V2, V5);
        int16x8_t tmp4 = vsubq_s16(V3, V4);

        int16x8_t tmp10 = vaddq_s16(tmp0, tmp3);
        int16x8_t tmp13 = vsubq_s16(tmp0, tmp3);
        int16x8_t tmp11 = vaddq_s16(tmp1, tmp2);
        int16x8_t tmp12 = vsubq_s16(tmp1, tmp2);

        int16x8_t D0s = vaddq_s16(tmp10, tmp11);
        int16x8_t D4s = vsubq_s16(tmp10, tmp11);

        // Widen for constants
        int32x4_t tmp12w = vmovlbq_s16(tmp12); // lanes 0,2,4,6
        int32x4_t tmp13w = vmovlbq_s16(tmp13);
        int32x4_t t10w = vmovlbq_s16(vaddq_s16(tmp4, tmp5));
        int32x4_t t11w = vmovlbq_s16(vaddq_s16(tmp5, tmp6));
        int32x4_t t12w = vmovlbq_s16(vaddq_s16(tmp6, tmp7));

        int32x4_t z1 = vmulq_n_s32(vaddq_s32(tmp12w, tmp13w), 181);
        z1 = vshlq_s32(z1, vdupq_n_s32(-8));
        int32x4_t D2w = vaddq_s32(tmp13w, z1);
        int32x4_t D6w = vsubq_s32(tmp13w, z1);

        int32x4_t z5 = vmulq_n_s32(vsubq_s32(t10w, t12w), 98);
        int32x4_t z2 = vaddq_s32(z5, vmulq_n_s32(t10w, 139));
        z2 = vshlq_s32(z2, vdupq_n_s32(-8));
        int32x4_t z4 = vaddq_s32(z5, vmulq_n_s32(t12w, 334));
        z4 = vshlq_s32(z4, vdupq_n_s32(-8));
        int32x4_t z3 = vmulq_n_s32(t11w, 181);
        z3 = vshlq_s32(z3, vdupq_n_s32(-8));
        int32x4_t tmp7w = vmovlbq_s16(tmp7);
        int32x4_t z11 = vaddq_s32(tmp7w, z3);
        int32x4_t z13 = vsubq_s32(tmp7w, z3);
        int32x4_t D5w = vaddq_s32(z13, z2);
        int32x4_t D3w = vsubq_s32(z13, z2);
        int32x4_t D1w = vaddq_s32(z11, z4);
        int32x4_t D7w = vsubq_s32(z11, z4);

        // Store 4 rows: extract lanes at indices 0,2,4,6
        int16_t d0s[8], d4s[8];
        int32_t d1v[4], d2v[4], d3v[4], d5v[4], d6v[4], d7v[4];
        vstrhq_s16(d0s, D0s); vstrhq_s16(d4s, D4s);
        vstrwq_s32(d1v, D1w); vstrwq_s32(d2v, D2w); vstrwq_s32(d3v, D3w);
        vstrwq_s32(d5v, D5w); vstrwq_s32(d6v, D6w); vstrwq_s32(d7v, D7w);
        for (int r = 0; r < 4; r++) {
            signed short *drow = pMCUDest + (y + r) * 8;
            int idx = r<<1; // 0,2,4,6
            drow[0] = (short)d0s[idx];
            drow[4] = (short)d4s[idx];
            drow[1] = (short)d1v[r];
            drow[2] = (short)d2v[r];
            drow[3] = (short)d3v[r];
            drow[5] = (short)d5v[r];
            drow[6] = (short)d6v[r];
            drow[7] = (short)d7v[r];
        }
    }

    // Column pass: process 4 columns in parallel; 16-bit butterfly then widen for constants
    for (int c = 0; c < 8; c += 4) {
        // Build 8 vectors of 4 columns each into even lanes 0,2,4,6
        int16_t R0[8]={0},R1[8]={0},R2[8]={0},R3[8]={0},R4[8]={0},R5[8]={0},R6[8]={0},R7[8]={0};
        for (int k = 0; k < 4; k++) {
            int lane = k<<1; // 0,2,4,6
            R0[lane] = pMCUDest[0*8 + (c+k)];
            R1[lane] = pMCUDest[1*8 + (c+k)];
            R2[lane] = pMCUDest[2*8 + (c+k)];
            R3[lane] = pMCUDest[3*8 + (c+k)];
            R4[lane] = pMCUDest[4*8 + (c+k)];
            R5[lane] = pMCUDest[5*8 + (c+k)];
            R6[lane] = pMCUDest[6*8 + (c+k)];
            R7[lane] = pMCUDest[7*8 + (c+k)];
        }
        int16x8_t C0 = vldrhq_s16(R0);
        int16x8_t C1 = vldrhq_s16(R1);
        int16x8_t C2 = vldrhq_s16(R2);
        int16x8_t C3 = vldrhq_s16(R3);
        int16x8_t C4 = vldrhq_s16(R4);
        int16x8_t C5 = vldrhq_s16(R5);
        int16x8_t C6 = vldrhq_s16(R6);
        int16x8_t C7 = vldrhq_s16(R7);

        // 16-bit butterfly along rows
        int16x8_t tmp0 = vaddq_s16(C0, C7);
        int16x8_t tmp1 = vaddq_s16(C1, C6);
        int16x8_t tmp2 = vaddq_s16(C2, C5);
        int16x8_t tmp3 = vaddq_s16(C3, C4);
        int16x8_t tmp7 = vsubq_s16(C0, C7);
        int16x8_t tmp6 = vsubq_s16(C1, C6);
        int16x8_t tmp5 = vsubq_s16(C2, C5);
        int16x8_t tmp4 = vsubq_s16(C3, C4);

        int16x8_t tmp10 = vaddq_s16(tmp0, tmp3);
        int16x8_t tmp13 = vsubq_s16(tmp0, tmp3);
        int16x8_t tmp11 = vaddq_s16(tmp1, tmp2);
        int16x8_t tmp12 = vsubq_s16(tmp1, tmp2);

        int16x8_t D0s = vaddq_s16(tmp10, tmp11);
        int16x8_t D4s = vsubq_s16(tmp10, tmp11);

        // Widen lanes 0..3 for constant multiplies
        int32x4_t tmp12w = vmovlbq_s16(tmp12);
        int32x4_t tmp13w = vmovlbq_s16(tmp13);
        int32x4_t t10w = vmovlbq_s16(vaddq_s16(tmp4, tmp5));
        int32x4_t t11w = vmovlbq_s16(vaddq_s16(tmp5, tmp6));
        int32x4_t t12w = vmovlbq_s16(vaddq_s16(tmp6, tmp7));

        int32x4_t z1 = vmulq_n_s32(vaddq_s32(tmp12w, tmp13w), 181);
        z1 = vshlq_s32(z1, vdupq_n_s32(-8));
        int32x4_t D2w = vaddq_s32(tmp13w, z1);
        int32x4_t D6w = vsubq_s32(tmp13w, z1);

        int32x4_t z5 = vmulq_n_s32(vsubq_s32(t10w, t12w), 98);
        int32x4_t z2 = vaddq_s32(z5, vmulq_n_s32(t10w, 139));
        z2 = vshlq_s32(z2, vdupq_n_s32(-8));
        int32x4_t z4 = vaddq_s32(z5, vmulq_n_s32(t12w, 334));
        z4 = vshlq_s32(z4, vdupq_n_s32(-8));
        int32x4_t z3 = vmulq_n_s32(t11w, 181);
        z3 = vshlq_s32(z3, vdupq_n_s32(-8));
        int32x4_t tmp7w = vmovlbq_s16(tmp7);
        int32x4_t z11 = vaddq_s32(tmp7w, z3);
        int32x4_t z13 = vsubq_s32(tmp7w, z3);
        int32x4_t D5w = vaddq_s32(z13, z2);
        int32x4_t D3w = vsubq_s32(z13, z2);
        int32x4_t D1w = vaddq_s32(z11, z4);
        int32x4_t D7w = vsubq_s32(z11, z4);

        // Store back results for rows 0..7 at columns c..c+3
        int16_t d0s[8], d4s[8];
        int32_t d1v[4], d2v[4], d3v[4], d5v[4], d6v[4], d7v[4];
        vstrhq_s16(d0s, D0s); vstrhq_s16(d4s, D4s);
        vstrwq_s32(d1v, D1w); vstrwq_s32(d2v, D2w); vstrwq_s32(d3v, D3w);
        vstrwq_s32(d5v, D5w); vstrwq_s32(d6v, D6w); vstrwq_s32(d7v, D7w);
        for (int k = 0; k < 4; k++) {
            int idx = k<<1; // match even-lane packing
            pMCUDest[0*8 + (c+k)] = (short)d0s[idx];
            pMCUDest[1*8 + (c+k)] = (short)d1v[k];
            pMCUDest[2*8 + (c+k)] = (short)d2v[k];
            pMCUDest[3*8 + (c+k)] = (short)d3v[k];
            pMCUDest[4*8 + (c+k)] = (short)d4s[idx];
            pMCUDest[5*8 + (c+k)] = (short)d5v[k];
            pMCUDest[6*8 + (c+k)] = (short)d6v[k];
            pMCUDest[7*8 + (c+k)] = (short)d7v[k];
        }
    }
}

// (test-only fdct wrapper removed)

// Quantize with vector multiply by inverted Q; initial pass defers to scalar
static int JPEGQuantize_helium(JPEGE_IMAGE *pJPEG, signed short *pMCUSrc, int iTable)
{
    // Vectorized version of baseline JPEGQuantize using MVE intrinsics.
    // Replicates rounding behavior: res = sign(d) * (((|d| + Q/2) * invQ) >> 16)
    // where invQ is the precomputed 65536/Q in sQuantTable[i+128].

    const int base = iTable * DCTSIZE;
    signed short *q = (signed short *)&pJPEG->sQuantTable[base];

    int i;

    // First half: i = 0..31
    for (i = 0; i < 32; i += 8) {
        int idx = i;
        // Load inputs
        int16x8_t d = vldrhq_s16(&pMCUSrc[idx]);
        int16x8_t qv = vldrhq_s16(&q[idx]);
        int16x8_t q2 = vshrq_n_s16(qv, 1);
        int16x8_t ad = vabsq_s16(d);
        int16x8_t t = vaddq_s16(q2, ad);
        // invQ is stored at offset +128 shorts from q base
        uint16x8_t inv = vldrhq_u16((uint16_t *)&q[idx + 128]);
        // Multiply high: (t * inv) >> 16
        int16x8_t mh = vmulhq_s16(t, vreinterpretq_s16_u16(inv));
        // Apply sign of d without predicates: mask = d>>15 (arith), res = (mh ^ mask) - mask
        int16x8_t mask = vshrq_n_s16(d, 15);
        int16x8_t res = veorq_s16(mh, mask);
        res = vsubq_s16(res, mask);
        vstrhq_s16(&pMCUSrc[idx], res);
    }

    // Second half (and index 32): i = 32..63, compute and store values; sum handled separately
    for (i = 32; i < 64; i += 8) {
        int idx = i;
        int16x8_t d = vldrhq_s16(&pMCUSrc[idx]);
        int16x8_t qv = vldrhq_s16(&q[idx]);
        int16x8_t q2 = vshrq_n_s16(qv, 1);
        int16x8_t ad = vabsq_s16(d);
        int16x8_t t = vaddq_s16(q2, ad);
        uint16x8_t inv = vldrhq_u16((uint16_t *)&q[idx + 128]);
        int16x8_t mh = vmulhq_s16(t, vreinterpretq_s16_u16(inv));
        int16x8_t mask = vshrq_n_s16(d, 15);
        int16x8_t res = veorq_s16(mh, mask);
        res = vsubq_s16(res, mask);
        vstrhq_s16(&pMCUSrc[idx], res);
    }

    int sum = 0;
    for (i = 33; i < 64; i++) {
        sum += pMCUSrc[i];
    }
    return (sum == 0);
}

// (test-only quantize wrapper removed)

// Dispatchers used by AddMCU path
static void get_mcu_11_dispatch(unsigned char *pImage, JPEGE_IMAGE *pPage, int iPitch)
{
    // Use Helium sampling for RGB888 in 4:4:4, fallback to baseline otherwise
    if (pPage->ucPixelType == JPEGE_PIXEL_RGB888) {
        // Always 8x8 in this path
        JPEGSample24_helium(pImage, pPage->MCUc, iPitch, 8, 8);
    } else if (pPage->ucPixelType == JPEGE_PIXEL_RGB24) {
        JPEGSample24RGB_helium(pImage, pPage->MCUc, iPitch, 8, 8);
    } else {
        extern void JPEGGetMCU11(unsigned char*, JPEGE_IMAGE*, int);
        JPEGGetMCU11(pImage, pPage, iPitch);
    }
}

static void get_mcu_22_dispatch(unsigned char *pImage, JPEGE_IMAGE *pPage, int iPitch)
{
    if (pPage->ucPixelType == JPEGE_PIXEL_RGB888) {
        // Replicate the baseline 4:2:0 path for RGB888 with Helium subsampling
        signed char *pMCUData = pPage->MCUc;
        int cx = 8, cy = 8; // as in baseline
        // upper left
        JPEGSubSample24_helium(pImage, pMCUData, &pMCUData[DCTSIZE*4], &pMCUData[DCTSIZE*5], iPitch, cx, cy);
    // upper right: Y block 1, chroma block still top row next 4 cols
    JPEGSubSample24_helium(pImage + 8*3, &pMCUData[DCTSIZE*1], &pMCUData[4 + DCTSIZE*4], &pMCUData[4 + DCTSIZE*5], iPitch, cx, cy);
    // lower left: Y block 2, chroma block next row first 4 cols
    JPEGSubSample24_helium(pImage + 8*iPitch, &pMCUData[DCTSIZE*2], &pMCUData[32 + DCTSIZE*4], &pMCUData[32 + DCTSIZE*5], iPitch, cx, cy);
    // lower right: Y block 3, chroma block next row next 4 cols
    JPEGSubSample24_helium(pImage + 8*iPitch + 8*3, &pMCUData[DCTSIZE*3], &pMCUData[36 + DCTSIZE*4], &pMCUData[36 + DCTSIZE*5], iPitch, cx, cy);
    } else if (pPage->ucPixelType == JPEGE_PIXEL_RGB24) {
        signed char *pMCUData = pPage->MCUc;
        int cx = 8, cy = 8;
        JPEGSubSample24RGB_helium(pImage, pMCUData, &pMCUData[DCTSIZE*4], &pMCUData[DCTSIZE*5], iPitch, cx, cy);
        JPEGSubSample24RGB_helium(pImage + 8*3, &pMCUData[DCTSIZE*1], &pMCUData[4 + DCTSIZE*4], &pMCUData[4 + DCTSIZE*5], iPitch, cx, cy);
        JPEGSubSample24RGB_helium(pImage + 8*iPitch, &pMCUData[DCTSIZE*2], &pMCUData[32 + DCTSIZE*4], &pMCUData[32 + DCTSIZE*5], iPitch, cx, cy);
        JPEGSubSample24RGB_helium(pImage + 8*iPitch + 8*3, &pMCUData[DCTSIZE*3], &pMCUData[36 + DCTSIZE*4], &pMCUData[36 + DCTSIZE*5], iPitch, cx, cy);
    } else if (pPage->ucPixelType == JPEGE_PIXEL_RGB565) {
        signed char *pMCUData = pPage->MCUc;
        int cx = 8, cy = 8;
        JPEGSubSample16_helium(pImage, pMCUData, &pMCUData[DCTSIZE*4], &pMCUData[DCTSIZE*5], iPitch, cx, cy);
        JPEGSubSample16_helium(pImage + 8*2, &pMCUData[DCTSIZE*1], &pMCUData[4 + DCTSIZE*4], &pMCUData[4 + DCTSIZE*5], iPitch, cx, cy);
        JPEGSubSample16_helium(pImage + 8*iPitch, &pMCUData[DCTSIZE*2], &pMCUData[32 + DCTSIZE*4], &pMCUData[32 + DCTSIZE*5], iPitch, cx, cy);
        JPEGSubSample16_helium(pImage + 8*iPitch + 8*2, &pMCUData[DCTSIZE*3], &pMCUData[36 + DCTSIZE*4], &pMCUData[36 + DCTSIZE*5], iPitch, cx, cy);
    } else if (pPage->ucPixelType == JPEGE_PIXEL_YUYV) {
        JPEGSubSampleYUYV_helium(pImage, pPage->MCUc, iPitch);
    } else {
        extern void JPEGGetMCU22(unsigned char*, JPEGE_IMAGE*, int);
        JPEGGetMCU22(pImage, pPage, iPitch);
    }
}

// Public API implementations: reuse baseline control, swap hot loops
int JPEGAddMCU_Helium(JPEGE_IMAGE *pJPEG, JPEGENCODE *pEncode, uint8_t *pPixels, int iPitch)
{
    int bSparse;
    if (pEncode->y >= pJPEG->iHeight) {
        pJPEG->iError = JPEGE_INVALID_PARAMETER;
        return JPEGE_INVALID_PARAMETER;
    }
    if (pJPEG->ucPixelType == JPEGE_PIXEL_GRAYSCALE) {
        extern void JPEGGetMCU(unsigned char*, int, signed char*);
        JPEGGetMCU(pPixels, iPitch, pJPEG->MCUc);
        JPEGFDCT_helium(pJPEG->MCUc, pJPEG->MCUs);
        bSparse = JPEGQuantize_helium(pJPEG, pJPEG->MCUs, 0);
        extern int JPEGEncodeMCU(int, JPEGE_IMAGE*, signed short*, int, int);
        pJPEG->iDCPred0 = JPEGEncodeMCU(0, pJPEG, pJPEG->MCUs, pJPEG->iDCPred0, bSparse);
        if (pEncode->x >= (pJPEG->iWidth - pEncode->cx)) {
            FlushCode(&pJPEG->pc);
            *(pJPEG->pc.pOut)++ = 0xff;
            *(pJPEG->pc.pOut)++ = (unsigned char) (0xd0 + (pJPEG->iRestart & 7));
            pJPEG->iRestart++;
            pJPEG->iDCPred0 = 0;
            pEncode->x = 0;
            pEncode->y += pEncode->cy;
            if (pEncode->y >= pJPEG->iHeight && pJPEG->pOutput != NULL)
                pJPEG->iDataSize = (int)(pJPEG->pc.pOut - pJPEG->pOutput);
        } else {
            pEncode->x += pEncode->cx;
        }
    } else {
        if (pJPEG->ucSubSample == JPEGE_SUBSAMPLE_444) {
            get_mcu_11_dispatch(pPixels, pJPEG, iPitch);
            extern int JPEGEncodeMCU(int, JPEGE_IMAGE*, signed short*, int, int);
            JPEGFDCT_helium(&pJPEG->MCUc[0*DCTSIZE], pJPEG->MCUs);
            bSparse = JPEGQuantize_helium(pJPEG, pJPEG->MCUs, 0);
            pJPEG->iDCPred0 = JPEGEncodeMCU(0, pJPEG, pJPEG->MCUs, pJPEG->iDCPred0, bSparse);

            JPEGFDCT_helium(&pJPEG->MCUc[1*DCTSIZE], pJPEG->MCUs);
            bSparse = JPEGQuantize_helium(pJPEG, pJPEG->MCUs, 1);
            pJPEG->iDCPred1 = JPEGEncodeMCU(1, pJPEG, pJPEG->MCUs, pJPEG->iDCPred1, bSparse);

            JPEGFDCT_helium(&pJPEG->MCUc[2*DCTSIZE], pJPEG->MCUs);
            bSparse = JPEGQuantize_helium(pJPEG, pJPEG->MCUs, 1);
            pJPEG->iDCPred2 = JPEGEncodeMCU(1, pJPEG, pJPEG->MCUs, pJPEG->iDCPred2, bSparse);
        } else { // 4:2:0
            get_mcu_22_dispatch(pPixels, pJPEG, iPitch);
            extern int JPEGEncodeMCU(int, JPEGE_IMAGE*, signed short*, int, int);
            JPEGFDCT_helium(&pJPEG->MCUc[0*DCTSIZE], pJPEG->MCUs);
            bSparse = JPEGQuantize_helium(pJPEG, pJPEG->MCUs, 0);
            pJPEG->iDCPred0 = JPEGEncodeMCU(0, pJPEG, pJPEG->MCUs, pJPEG->iDCPred0, bSparse);

            JPEGFDCT_helium(&pJPEG->MCUc[1*DCTSIZE], pJPEG->MCUs);
            bSparse = JPEGQuantize_helium(pJPEG, pJPEG->MCUs, 0);
            pJPEG->iDCPred0 = JPEGEncodeMCU(0, pJPEG, pJPEG->MCUs, pJPEG->iDCPred0, bSparse);

            JPEGFDCT_helium(&pJPEG->MCUc[2*DCTSIZE], pJPEG->MCUs);
            bSparse = JPEGQuantize_helium(pJPEG, pJPEG->MCUs, 0);
            pJPEG->iDCPred0 = JPEGEncodeMCU(0, pJPEG, pJPEG->MCUs, pJPEG->iDCPred0, bSparse);

            JPEGFDCT_helium(&pJPEG->MCUc[3*DCTSIZE], pJPEG->MCUs);
            bSparse = JPEGQuantize_helium(pJPEG, pJPEG->MCUs, 0);
            pJPEG->iDCPred0 = JPEGEncodeMCU(0, pJPEG, pJPEG->MCUs, pJPEG->iDCPred0, bSparse);

            JPEGFDCT_helium(&pJPEG->MCUc[4*DCTSIZE], pJPEG->MCUs);
            bSparse = JPEGQuantize_helium(pJPEG, pJPEG->MCUs, 1);
            pJPEG->iDCPred1 = JPEGEncodeMCU(1, pJPEG, pJPEG->MCUs, pJPEG->iDCPred1, bSparse);

            JPEGFDCT_helium(&pJPEG->MCUc[5*DCTSIZE], pJPEG->MCUs);
            bSparse = JPEGQuantize_helium(pJPEG, pJPEG->MCUs, 1);
            pJPEG->iDCPred2 = JPEGEncodeMCU(1, pJPEG, pJPEG->MCUs, pJPEG->iDCPred2, bSparse);
        }

        if (pEncode->x >= (pJPEG->iWidth - pEncode->cx)) {
            FlushCode(&pJPEG->pc);
            *(pJPEG->pc.pOut)++ = 0xff;
            *(pJPEG->pc.pOut)++ = (unsigned char) (0xd0 + (pJPEG->iRestart & 7));
            pJPEG->iRestart++;
            pJPEG->iDCPred0 = 0; pJPEG->iDCPred1 = 0; pJPEG->iDCPred2 = 0;
            pEncode->x = 0;
            pEncode->y += pEncode->cy;
            if (pEncode->y >= pJPEG->iHeight && pJPEG->pOutput != NULL)
                pJPEG->iDataSize = (int)(pJPEG->pc.pOut - pJPEG->pOutput);
        } else {
            pEncode->x += pEncode->cx;
        }
    }
    return JPEGE_SUCCESS;
}

// For now, we reuse the baseline Begin/End; they are usually I/O/header heavy
// For the first iteration, use baseline End/Begin via weak-link fallback through
// their actual implementations in JPEGENC.c; we simply declare them and call.
extern int JPEGEncodeEnd(JPEGE_IMAGE*);
int JPEGEncodeEnd_Helium(JPEGE_IMAGE *pJPEG)
{
    return JPEGEncodeEnd(pJPEG);
}

extern int JPEGEncodeBegin(JPEGE_IMAGE*, JPEGENCODE*, int, int, uint8_t, uint8_t, uint8_t);
int JPEGEncodeBegin_Helium(JPEGE_IMAGE *pJPEG, JPEGENCODE *pEncode, int iWidth, int iHeight,
                           uint8_t ucPixelType, uint8_t ucSubSample, uint8_t ucQFactor)
{
    return JPEGEncodeBegin(pJPEG, pEncode, iWidth, iHeight, ucPixelType, ucSubSample, ucQFactor);
}

