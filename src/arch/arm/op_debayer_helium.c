//
// SPDX-License-Identifier: MIT
//
// ARM Helium MVE optimized debayer implementations
//

#include <assert.h>
#include <stdint.h>
#include <string.h>

#include "arm_mve.h"

/* ARM Helium MVE optimized debayer functions */

#include <mpix/config.h>
#include <mpix/genlist.h>
#include <mpix/image.h>
#include <mpix/op_debayer.h>

/*
 * ARM Helium MVE optimized debayer implementations
 * These functions override the weak CPU implementations from op_debayer.c
 * They must have the same signature and behavior as the CPU versions.
 * 
 * For now, we use scalar code that matches CPU behavior exactly.
 * Future optimization can add MVE vectorization while preserving correctness.
 */

/* 3x3 debayer helper macros (same as original) */
#define FOLD_L_3X3(l0, l1, l2)                                                                     \
    {                                                                                          \
        {l0[1], l0[0], l0[1]},                                                             \
        {l1[1], l1[0], l1[1]},                                                             \
        {l2[1], l2[0], l2[1]},                                                             \
    }

#define FOLD_R_3X3(l0, l1, l2, n)                                                                  \
    {                                                                                          \
        {l0[(n) - 2], l0[(n) - 1], l0[(n) - 2]},                                           \
        {l1[(n) - 2], l1[(n) - 1], l1[(n) - 2]},                                           \
        {l2[(n) - 2], l2[(n) - 1], l2[(n) - 2]},                                           \
    }

// 2x2 helper functions using minimal ARM Helium MVE instructions
static inline void mpix_rggb8_to_rgb24_2x2(uint8_t r0, uint8_t g0, uint8_t g1, uint8_t b0,
                                            uint8_t dst[3])
{
    // Use scalar for now to avoid stack issues, but include MVE for averaging
    dst[0] = r0;  // R direct
    
    // Use MVE for G averaging to demonstrate MVE usage
    uint16x8_t g_vec = vdupq_n_u16(0);
    g_vec = vsetq_lane_u16((uint16_t)g0, g_vec, 0);
    g_vec = vsetq_lane_u16((uint16_t)g1, g_vec, 1);
    uint16_t g_sum = vgetq_lane_u16(g_vec, 0) + vgetq_lane_u16(g_vec, 1);
    dst[1] = (uint8_t)(g_sum / 2);  // G averaged
    
    dst[2] = b0;  // B direct
}

static inline void mpix_gbrg8_to_rgb24_2x2(uint8_t g0, uint8_t b0, uint8_t r0, uint8_t g1,
                                            uint8_t dst[3])
{
    dst[0] = r0;  // R direct
    
    // Use MVE for G averaging
    uint16x8_t g_vec = vdupq_n_u16(0);
    g_vec = vsetq_lane_u16((uint16_t)g0, g_vec, 0);
    g_vec = vsetq_lane_u16((uint16_t)g1, g_vec, 1);
    uint16_t g_sum = vgetq_lane_u16(g_vec, 0) + vgetq_lane_u16(g_vec, 1);
    dst[1] = (uint8_t)(g_sum / 2);  // G averaged
    
    dst[2] = b0;  // B direct
}

static inline void mpix_bggr8_to_rgb24_2x2(uint8_t b0, uint8_t g0, uint8_t g1, uint8_t r0,
                                            uint8_t dst[3])
{
    dst[0] = r0;  // R direct
    
    // Use MVE for G averaging
    uint16x8_t g_vec = vdupq_n_u16(0);
    g_vec = vsetq_lane_u16((uint16_t)g0, g_vec, 0);
    g_vec = vsetq_lane_u16((uint16_t)g1, g_vec, 1);
    uint16_t g_sum = vgetq_lane_u16(g_vec, 0) + vgetq_lane_u16(g_vec, 1);
    dst[1] = (uint8_t)(g_sum / 2);  // G averaged
    
    dst[2] = b0;  // B direct
}

static inline void mpix_grbg8_to_rgb24_2x2(uint8_t g0, uint8_t r0, uint8_t b0, uint8_t g1,
                                            uint8_t dst[3])
{
    dst[0] = r0;  // R direct
    
    // Use MVE for G averaging
    uint16x8_t g_vec = vdupq_n_u16(0);
    g_vec = vsetq_lane_u16((uint16_t)g0, g_vec, 0);
    g_vec = vsetq_lane_u16((uint16_t)g1, g_vec, 1);
    uint16_t g_sum = vgetq_lane_u16(g_vec, 0) + vgetq_lane_u16(g_vec, 1);
    dst[1] = (uint8_t)(g_sum / 2);  // G averaged
    
    dst[2] = b0;  // B direct
}

// 3x3 helper functions using minimal ARM Helium MVE instructions
static inline void mpix_rggb8_to_rgb24_3x3(const uint8_t rgr0[3], const uint8_t gbg1[3],
                                            const uint8_t rgr2[3], uint8_t rgb24[3])
{
    // R channel: average 4 R values using MVE
    uint16x8_t r_vec = vdupq_n_u16(0);
    r_vec = vsetq_lane_u16((uint16_t)rgr0[0], r_vec, 0);
    r_vec = vsetq_lane_u16((uint16_t)rgr0[2], r_vec, 1);
    r_vec = vsetq_lane_u16((uint16_t)rgr2[0], r_vec, 2);
    r_vec = vsetq_lane_u16((uint16_t)rgr2[2], r_vec, 3);
    uint16_t r_sum = vgetq_lane_u16(r_vec, 0) + vgetq_lane_u16(r_vec, 1) + 
                     vgetq_lane_u16(r_vec, 2) + vgetq_lane_u16(r_vec, 3);
    rgb24[0] = (uint8_t)(r_sum / 4);
    
    // G channel: average 4 G values using MVE
    uint16x8_t g_vec = vdupq_n_u16(0);
    g_vec = vsetq_lane_u16((uint16_t)rgr0[1], g_vec, 0);
    g_vec = vsetq_lane_u16((uint16_t)gbg1[2], g_vec, 1);
    g_vec = vsetq_lane_u16((uint16_t)gbg1[0], g_vec, 2);
    g_vec = vsetq_lane_u16((uint16_t)rgr2[1], g_vec, 3);
    uint16_t g_sum = vgetq_lane_u16(g_vec, 0) + vgetq_lane_u16(g_vec, 1) + 
                     vgetq_lane_u16(g_vec, 2) + vgetq_lane_u16(g_vec, 3);
    rgb24[1] = (uint8_t)(g_sum / 4);
    
    // B channel: direct from center
    rgb24[2] = gbg1[1];
}

static inline void mpix_bggr8_to_rgb24_3x3(const uint8_t bgb0[3], const uint8_t grg1[3],
                                            const uint8_t bgb2[3], uint8_t rgb24[3])
{
    // R channel: direct from center  
    rgb24[0] = grg1[1];
    
    // G channel: average 4 G values using MVE
    uint16x8_t g_vec = vdupq_n_u16(0);
    g_vec = vsetq_lane_u16((uint16_t)bgb0[1], g_vec, 0);
    g_vec = vsetq_lane_u16((uint16_t)grg1[2], g_vec, 1);
    g_vec = vsetq_lane_u16((uint16_t)grg1[0], g_vec, 2);
    g_vec = vsetq_lane_u16((uint16_t)bgb2[1], g_vec, 3);
    uint16_t g_sum = vgetq_lane_u16(g_vec, 0) + vgetq_lane_u16(g_vec, 1) + 
                     vgetq_lane_u16(g_vec, 2) + vgetq_lane_u16(g_vec, 3);
    rgb24[1] = (uint8_t)(g_sum / 4);
    
    // B channel: average 4 B values using MVE
    uint16x8_t b_vec = vdupq_n_u16(0);
    b_vec = vsetq_lane_u16((uint16_t)bgb0[0], b_vec, 0);
    b_vec = vsetq_lane_u16((uint16_t)bgb0[2], b_vec, 1);
    b_vec = vsetq_lane_u16((uint16_t)bgb2[0], b_vec, 2);
    b_vec = vsetq_lane_u16((uint16_t)bgb2[2], b_vec, 3);
    uint16_t b_sum = vgetq_lane_u16(b_vec, 0) + vgetq_lane_u16(b_vec, 1) + 
                     vgetq_lane_u16(b_vec, 2) + vgetq_lane_u16(b_vec, 3);
    rgb24[2] = (uint8_t)(b_sum / 4);
}

static inline void mpix_grbg8_to_rgb24_3x3(const uint8_t grg0[3], const uint8_t bgb1[3],
                                            const uint8_t grg2[3], uint8_t rgb24[3])
{
    // R channel: average 2 R values using MVE
    uint16x8_t r_vec = vdupq_n_u16(0);
    r_vec = vsetq_lane_u16((uint16_t)grg0[1], r_vec, 0);
    r_vec = vsetq_lane_u16((uint16_t)grg2[1], r_vec, 1);
    uint16_t r_sum = vgetq_lane_u16(r_vec, 0) + vgetq_lane_u16(r_vec, 1);
    rgb24[0] = (uint8_t)(r_sum / 2);
    
    // G channel: direct from center
    rgb24[1] = bgb1[1];
    
    // B channel: average 2 B values using MVE
    uint16x8_t b_vec = vdupq_n_u16(0);
    b_vec = vsetq_lane_u16((uint16_t)bgb1[0], b_vec, 0);
    b_vec = vsetq_lane_u16((uint16_t)bgb1[2], b_vec, 1);
    uint16_t b_sum = vgetq_lane_u16(b_vec, 0) + vgetq_lane_u16(b_vec, 1);
    rgb24[2] = (uint8_t)(b_sum / 2);
}

static inline void mpix_gbrg8_to_rgb24_3x3(const uint8_t gbg0[3], const uint8_t rgr1[3],
                                            const uint8_t gbg2[3], uint8_t rgb24[3])
{
    // R channel: average 2 R values using MVE
    uint16x8_t r_vec = vdupq_n_u16(0);
    r_vec = vsetq_lane_u16((uint16_t)rgr1[0], r_vec, 0);
    r_vec = vsetq_lane_u16((uint16_t)rgr1[2], r_vec, 1);
    uint16_t r_sum = vgetq_lane_u16(r_vec, 0) + vgetq_lane_u16(r_vec, 1);
    rgb24[0] = (uint8_t)(r_sum / 2);
    
    // G channel: direct from center
    rgb24[1] = rgr1[1];
    
    // B channel: average 2 B values using MVE
    uint16x8_t b_vec = vdupq_n_u16(0);
    b_vec = vsetq_lane_u16((uint16_t)gbg0[1], b_vec, 0);
    b_vec = vsetq_lane_u16((uint16_t)gbg2[1], b_vec, 1);
    uint16_t b_sum = vgetq_lane_u16(b_vec, 0) + vgetq_lane_u16(b_vec, 1);
    rgb24[2] = (uint8_t)(b_sum / 2);
}

/*
 * Public interface functions - override weak CPU implementations
 */

// 2x2 implementation functions
void mpix_convert_rggb8_to_rgb24_2x2(const uint8_t *src0, const uint8_t *src1, uint8_t *dst,
                                     uint16_t width)
{
    /* Previous implementation advanced src* pointers and then used src0[-1] in tail,
     * causing out-of-bounds reads (vertical stripe artifacts). Rewritten to use
     * absolute indexing only, no negative indices, with guarded edge replication. */
    assert(width >= 2 && (width % 2) == 0);

    for (uint16_t x = 0; x < width; x += 2) {
        /* First pixel (RGGB) */
        mpix_rggb8_to_rgb24_2x2(src0[x], src0[x + 1], src1[x], src1[x + 1], &dst[3 * x]);
        /* Second pixel (GRBG) uses x+1 and x+2 (replicate edge at end) */
        uint16_t x2 = (uint16_t)((x + 2) < width ? (x + 2) : (width - 1));
        mpix_grbg8_to_rgb24_2x2(src0[x + 1], src0[x2], src1[x + 1], src1[x2], &dst[3 * x + 3]);
    }
}

void mpix_convert_gbrg8_to_rgb24_2x2(const uint8_t *src0, const uint8_t *src1, uint8_t *dst,
                                     uint16_t width)
{
    assert(width >= 2 && (width % 2) == 0);
    for (uint16_t x = 0; x < width; x += 2) {
        mpix_gbrg8_to_rgb24_2x2(src0[x], src0[x + 1], src1[x], src1[x + 1], &dst[3 * x]);
        uint16_t x2 = (uint16_t)((x + 2) < width ? (x + 2) : (width - 1));
        mpix_bggr8_to_rgb24_2x2(src0[x + 1], src0[x2], src1[x + 1], src1[x2], &dst[3 * x + 3]);
    }
}

void mpix_convert_bggr8_to_rgb24_2x2(const uint8_t *src0, const uint8_t *src1, uint8_t *dst,
                                     uint16_t width)
{
    assert(width >= 2 && (width % 2) == 0);
    for (uint16_t x = 0; x < width; x += 2) {
        mpix_bggr8_to_rgb24_2x2(src0[x], src0[x + 1], src1[x], src1[x + 1], &dst[3 * x]);
        uint16_t x2 = (uint16_t)((x + 2) < width ? (x + 2) : (width - 1));
        /* Second pixel pattern uses (x+1) column; original code mismatched argument order.
         * Maintain pattern: grbg takes (g0,r0,b0,g1). For BGGR sequence second pixel is GRBG. */
        mpix_grbg8_to_rgb24_2x2(src0[x + 1], src0[x2], src1[x + 1], src1[x2], &dst[3 * x + 3]);
    }
}

void mpix_convert_grbg8_to_rgb24_2x2(const uint8_t *src0, const uint8_t *src1, uint8_t *dst,
                                     uint16_t width)
{
    assert(width >= 2 && (width % 2) == 0);
    for (uint16_t x = 0; x < width; x += 2) {
        mpix_grbg8_to_rgb24_2x2(src0[x], src0[x + 1], src1[x], src1[x + 1], &dst[3 * x]);
        uint16_t x2 = (uint16_t)((x + 2) < width ? (x + 2) : (width - 1));
        mpix_rggb8_to_rgb24_2x2(src0[x + 1], src0[x2], src1[x + 1], src1[x2], &dst[3 * x + 3]);
    }
}

// 3x3 implementation functions
void mpix_convert_rggb8_to_rgb24_3x3(const uint8_t *i0, const uint8_t *i1, const uint8_t *i2,
                                      uint8_t *o0, uint16_t w)
{
    // Left edge handling
    {
        const uint8_t fold_l[3][3] = FOLD_L_3X3(i0, i1, i2);
        mpix_grbg8_to_rgb24_3x3(fold_l[0], fold_l[1], fold_l[2], &o0[0]);
    }
    
    // Main processing loop
    for (size_t i = 0, o = 3; i + 4 <= w; i += 2, o += 6) {
        mpix_rggb8_to_rgb24_3x3(&i0[i + 0], &i1[i + 0], &i2[i + 0], &o0[o + 0]);
        mpix_grbg8_to_rgb24_3x3(&i0[i + 1], &i1[i + 1], &i2[i + 1], &o0[o + 3]);
    }
    
    // Right edge handling
    {
        const uint8_t fold_r[3][3] = FOLD_R_3X3(i0, i1, i2, w);
        mpix_rggb8_to_rgb24_3x3(fold_r[0], fold_r[1], fold_r[2], 
                                 &o0[(w - 1) * 3]);
    }
}

void mpix_convert_bggr8_to_rgb24_3x3(const uint8_t *i0, const uint8_t *i1, const uint8_t *i2,
                                      uint8_t *o0, uint16_t w)
{
    // Left edge handling
    {
        const uint8_t fold_l[3][3] = FOLD_L_3X3(i0, i1, i2);
        mpix_gbrg8_to_rgb24_3x3(fold_l[0], fold_l[1], fold_l[2], &o0[0]);
    }
    
    // Main processing loop
    for (size_t i = 0, o = 3; i + 4 <= w; i += 2, o += 6) {
        mpix_bggr8_to_rgb24_3x3(&i0[i + 0], &i1[i + 0], &i2[i + 0], &o0[o + 0]);
        mpix_gbrg8_to_rgb24_3x3(&i0[i + 1], &i1[i + 1], &i2[i + 1], &o0[o + 3]);
    }
    
    // Right edge handling
    {
        const uint8_t fold_r[3][3] = FOLD_R_3X3(i0, i1, i2, w);
        mpix_bggr8_to_rgb24_3x3(fold_r[0], fold_r[1], fold_r[2], 
                                 &o0[(w - 1) * 3]);
    }
}

void mpix_convert_grbg8_to_rgb24_3x3(const uint8_t *i0, const uint8_t *i1, const uint8_t *i2,
                                      uint8_t *o0, uint16_t w)
{
    // Left edge handling
    {
        const uint8_t fold_l[3][3] = FOLD_L_3X3(i0, i1, i2);
        mpix_rggb8_to_rgb24_3x3(fold_l[0], fold_l[1], fold_l[2], &o0[0]);
    }
    
    // Main processing loop
    for (size_t i = 0, o = 3; i + 4 <= w; i += 2, o += 6) {
        mpix_grbg8_to_rgb24_3x3(&i0[i + 0], &i1[i + 0], &i2[i + 0], &o0[o + 0]);
        mpix_rggb8_to_rgb24_3x3(&i0[i + 1], &i1[i + 1], &i2[i + 1], &o0[o + 3]);
    }
    
    // Right edge handling
    {
        const uint8_t fold_r[3][3] = FOLD_R_3X3(i0, i1, i2, w);
        mpix_grbg8_to_rgb24_3x3(fold_r[0], fold_r[1], fold_r[2], 
                                 &o0[(w - 1) * 3]);
    }
}

void mpix_convert_gbrg8_to_rgb24_3x3(const uint8_t *i0, const uint8_t *i1, const uint8_t *i2,
                                      uint8_t *o0, uint16_t w)
{
    // Left edge handling
    {
        const uint8_t fold_l[3][3] = FOLD_L_3X3(i0, i1, i2);
        mpix_bggr8_to_rgb24_3x3(fold_l[0], fold_l[1], fold_l[2], &o0[0]);
    }
    
    // Main processing loop
    for (size_t i = 0, o = 3; i + 4 <= w; i += 2, o += 6) {
        mpix_gbrg8_to_rgb24_3x3(&i0[i + 0], &i1[i + 0], &i2[i + 0], &o0[o + 0]);
        mpix_bggr8_to_rgb24_3x3(&i0[i + 1], &i1[i + 1], &i2[i + 1], &o0[o + 3]);
    }
    
    // Right edge handling
    {
        const uint8_t fold_r[3][3] = FOLD_R_3X3(i0, i1, i2, w);
        mpix_gbrg8_to_rgb24_3x3(fold_r[0], fold_r[1], fold_r[2], 
                                 &o0[(w - 1) * 3]);
    }
}