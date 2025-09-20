/**
 * SPDX-License-Identifier: Apache-2.0
 * @defgroup mpix_lowlevel_h mpix/lowlevel.h
 * @brief Low-level operations implemented by libmpix for direct buffer manipulation
 * @{
 */
#ifndef MPIX_LOW_LEVEL_H
#define MPIX_LOW_LEVEL_H

#include <stdint.h>

/* Utils */
uint8_t mpix_rgb24_get_luma_bt709(const uint8_t rgb24[3]);

/* Pixel conversion */
void mpix_convert_rgb24_to_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width);
void mpix_convert_grey_to_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width);
void mpix_convert_rgb24_to_rgb332(const uint8_t *src, uint8_t *dst, uint16_t width);
void mpix_convert_rgb24_to_rgb565be(const uint8_t *src, uint8_t *dst, uint16_t width);
void mpix_convert_rgb24_to_rgb565le(const uint8_t *src, uint8_t *dst, uint16_t width);
void mpix_convert_rgb24_to_y8_bt709(const uint8_t *src, uint8_t *dst, uint16_t width);
void mpix_convert_rgb24_to_yuv24_bt709(const uint8_t *src, uint8_t *dst, uint16_t width);
void mpix_convert_rgb24_to_yuyv_bt709(const uint8_t *src, uint8_t *dst, uint16_t width);
void mpix_convert_rgb332_to_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width);
void mpix_convert_rgb565be_to_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width);
void mpix_convert_rgb565le_to_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width);
void mpix_convert_y8_to_rgb24_bt709(const uint8_t *src, uint8_t *dst, uint16_t width);
void mpix_convert_yuv24_to_rgb24_bt709(const uint8_t *src, uint8_t *dst, uint16_t width);
void mpix_convert_yuv24_to_yuyv(const uint8_t *src, uint8_t *dst, uint16_t width);
void mpix_convert_yuyv_to_rgb24_bt709(const uint8_t *src, uint8_t *dst, uint16_t width);
void mpix_convert_yuyv_to_yuv24(const uint8_t *src, uint8_t *dst, uint16_t width);

/* Cropping */
void mpix_crop_frame_raw24(const uint8_t *src_buf, uint16_t src_width, uint16_t src_height,
			   uint8_t *dst_buf, uint16_t x_offset, uint16_t y_offset,
			   uint16_t crop_width, uint16_t crop_height);
void mpix_crop_frame_raw16(const uint8_t *src_buf, uint16_t src_width, uint16_t src_height,
			   uint8_t *dst_buf, uint16_t x_offset, uint16_t y_offset,
			   uint16_t crop_width, uint16_t crop_height);
void mpix_crop_frame_raw8(const uint8_t *src_buf, uint16_t src_width, uint16_t src_height,
			  uint8_t *dst_buf, uint16_t x_offset, uint16_t y_offset,
			  uint16_t crop_width, uint16_t crop_height);

/* Debayer */
void mpix_debayer_3x3(const uint8_t *src[3], uint8_t *dst, uint16_t width, uint32_t fourcc);
void mpix_debayer_2x2(const uint8_t *src[2], uint8_t *dst, uint16_t width, uint32_t fourcc);
void mpix_debayer_1x1(const uint8_t *src, uint8_t *dst, uint16_t width);

/* Correction */
void mpix_correct_black_level_raw8(const uint8_t *src, uint8_t *dst, uint16_t width,
				   uint8_t black_level);
void mpix_correct_color_matrix_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width,
				     int32_t matrix_q10[9]);
void mpix_correct_fused_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width, uint8_t black_level,
			      uint16_t gamma_level_q10, int32_t color_matrix_q10[9]);
void mpix_correct_gamma_raw8(const uint8_t *src, uint8_t *dst, uint16_t width, uint16_t gamma_q10);
void mpix_correct_gamma_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width, uint16_t gamma_q10);
void mpix_correct_white_balance_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width,
				      int32_t red_level_q10, int32_t blue_level_q10);

/* Kernel */
void mpix_kernel_identity_rgb24_3x3(const uint8_t *in[3], uint8_t *out, uint16_t width);
void mpix_kernel_identity_rgb24_5x5(const uint8_t *in[5], uint8_t *out, uint16_t width);
void mpix_kernel_sharpen_rgb24_3x3(const uint8_t *in[3], uint8_t *out, uint16_t width);
void mpix_kernel_sharpen_rgb24_5x5(const uint8_t *in[5], uint8_t *out, uint16_t width);
void mpix_kernel_edgedetect_rgb24_3x3(const uint8_t *in[3], uint8_t *out, uint16_t width);
void mpix_kernel_gaussianblur_rgb24_3x3(const uint8_t *in[3], uint8_t *out, uint16_t width);
void mpix_kernel_median_rgb24_3x3(const uint8_t *in[3], uint8_t *out, uint16_t width);
void mpix_kernel_median_rgb24_5x5(const uint8_t *in[5], uint8_t *out, uint16_t width);

/* Palette */
void mpix_convert_rgb24_to_palette8(const uint8_t *src, uint8_t *dst, uint16_t width,
				    const uint8_t colors_rgb24[3 << 8]);
void mpix_convert_palette8_to_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width,
				    const uint8_t colors_rgb24[3 << 8]);
void mpix_convert_rgb24_to_palette4(const uint8_t *src, uint8_t *dst, uint16_t width,
				    const uint8_t colors_rgb24[3 << 4]);
void mpix_convert_palette4_to_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width,
				    const uint8_t colors_rgb24[3 << 4]);
void mpix_convert_rgb24_to_palette2(const uint8_t *src, uint8_t *dst, uint16_t width,
				    const uint8_t colors_rgb24[3 << 2]);
void mpix_convert_palette2_to_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width,
				    const uint8_t colors_rgb24[3 << 2]);
void mpix_convert_rgb24_to_palette1(const uint8_t *src, uint8_t *dst, uint16_t width,
				    const uint8_t colors_rgb24[3 << 1]);
void mpix_convert_palette1_to_rgb24(const uint8_t *src, uint8_t *dst, uint16_t width,
				    const uint8_t colors_rgb24[3 << 1]);
uint8_t mpix_palette_encode(const uint8_t rgb[3], const uint8_t colors_rgb24[], uint8_t bit_depth);

/* Resize */
void mpix_resize_frame_raw24(const uint8_t *src_buf, uint16_t src_width, uint16_t src_height,
			     uint8_t *dst_buf, uint16_t dst_width, uint16_t dst_height);
void mpix_resize_frame_raw16(const uint8_t *src_buf, uint16_t src_width, uint16_t src_height,
			     uint8_t *dst_buf, uint16_t dst_width, uint16_t dst_height);
void mpix_resize_frame_raw8(const uint8_t *src_buf, uint16_t src_width, uint16_t src_height,
			    uint8_t *dst_buf, uint16_t dst_width, uint16_t dst_height);

#endif /** @} */
