/* SPDX-License-Identifier: Apache-2.0 */
/**
 * @defgroup mpix_image_h mpix/image.h
 * @brief Main user API
 * @{
 */
#ifndef MPIX_IMAGE_H
#define MPIX_IMAGE_H

#include <mpix/custom_api.h>
#include <mpix/formats.h>
#include <mpix/operation.h>
#include <mpix/pipeline.h>
#include <mpix/sample.h>
#include <mpix/stats.h>
#include <mpix/types.h>

/**
 * @brief Initialize an image from a memory buffer.
 *
 * @note The image fields will not be reset to zero, which is the responsibility of the caller.
 *
 * @param img Image to initialize.
 * @param buf Memory containinig input image data to process.
 * @param size Total available size in the buffer, can be bigger/smaller than full width x height.
 * @param width Width of the complete image in pixels.
 * @param height Height of the complete image in pixels.
 * @param format Format of data in the buffer as a four-character-code.
 */
static inline void mpix_image_from_buf(struct mpix_image *img, const uint8_t *buffer, size_t size,
				       const struct mpix_format *fmt)
{
	img->buffer = buffer;
	img->size = size;
	img->fmt = *fmt;
}

/**
 * @brief Convert an image and store it into the output buffer.
 *
 * @param img Image being processed.
 * @param buf Memory that receives the image data.
 * @param size Size of the buffer.
 * @return 0 on success or negative error code.
 */
static inline int mpix_image_to_buf(struct mpix_image *img, uint8_t *buffer, size_t size)
{
	if (mpix_op_append(img, MPIX_OP_END, sizeof(*img->last_op), size) == NULL) return -ENOMEM;
	img->last_op->ring.buffer = buffer;
	return mpix_pipeline_process(img->first_op, img->buffer, img->size);
}

/**
 * @brief Convert an image to a new pixel format.
 *
 * An operation is added to convert the image to a new pixel format.
 * If the operation to convert the image from the current format to a new format does not exist,
 * then the error flag is set, which can be accessed as @c img->err.
 *
 * In some cases, converting between two formats requires an intermediate conversion to RGB24.
 *
 * @param img Image to convert.
 * @param new_format A four-character-code (FOURCC) as defined by @c <zephyr/drivers/video.h>.
 * @return 0 on success or negative error code.
 */
static inline int mpix_image_convert(struct mpix_image *img, uint32_t new_format)
{
	int32_t p[] = { (int32_t)new_format };
	return mpix_pipeline_add(img, MPIX_OP_CONVERT, p, ARRAY_SIZE(p));
}

/**
 * @brief Convert an image to an indexed color format.
 *
 * An operation is added to convert the image to an indexed pixel format given the input palette.
 *
 * The size in bytes of the @p colors buffer is (3 << bit_depth).
 *
 * @param img Image to convert.
 * @param fourcc Palette format Four Character Code, starting with PLT and then a digit.
 * @return 0 on success or negative error code.
 */
static inline int mpix_image_palette_encode(struct mpix_image *img, uint32_t fourcc)
{
	int32_t p[] = { (int32_t)fourcc };
	return mpix_pipeline_add(img, MPIX_OP_PALETTE_ENCODE, p, ARRAY_SIZE(p));
}

/**
 * @brief Convert an image from an indexed color format.
 *
 * An operation is added to convert the image from an indexed pixel format given the input palette.
 * The palette must be set once per pipeline  before running it.
 *
 * @param img Image to convert.
 * @return 0 on success or negative error code.
 */
static inline int mpix_image_palette_decode(struct mpix_image *img)
{
	return mpix_pipeline_add(img, MPIX_OP_PALETTE_DECODE, NULL, 0);
}

/**
 * @brief Set the palette of every operation currently in the pipeline.
 *
 * If the palette has up to 2 colors, 8 pixels are packed per byte.
 * If the palette has up to 4 colors, 4 pixels are packed per byte.
 * If the palette has up to 16 colors, 2 pixels are packed per byte.
 * If the palette has up to 256 colors, 1 pixels are packed per byte.
 *
 * @see @ref mpix_pipeline_set_palette().
 *
 * @param img Image to assign a palette to.
 * @param palette The color palette in RGB24 pixel format.
 */
static inline int mpix_image_set_palette(struct mpix_image *img, struct mpix_palette *palette)
{
	return mpix_pipeline_set_palette(img->first_op, palette);
}

/**
 * @brief Convert an image from a bayer array format to RGB24.
 *
 * An operation is added to convert the image to RGB24 using the specified window size, such
 * as 2x2 or 3x3.
 *
 * @note It is also possible to use @ref mpix_image_convert to convert from bayer to RGB24 but
 *       this does not allow to select the window size.
 *
 * @param img Image to convert.
 * @param window_size The window size for the conversion, usually 2 (faster) or 3 (higher quality).
 * @return 0 on success or negative error code.
 */
static inline int mpix_image_debayer(struct mpix_image *img, uint32_t window_size)
{
	enum mpix_op_type type = (window_size == 1) ? MPIX_OP_DEBAYER_1X1 :
				 (window_size == 2) ? MPIX_OP_DEBAYER_2X2 :
				 (window_size == 3) ? MPIX_OP_DEBAYER_3X3 : MPIX_NB_OP;
	return mpix_pipeline_add(img, type, NULL, 0);
}

/**
 * @brief Apply Color Correction Matrix (CCM) to the image
 *
 * This operation only adds the processing element. To control the correction, use
 * @ref mpix_image_ctrl_value() with @ref MPIX_CID_COLOR_MATRIX.
 *
 * @param img Image to correct.
 * @return 0 on success or negative error code.
 */
static inline int mpix_image_correct_color_matrix(struct mpix_image *img)
{
	return mpix_pipeline_add(img, MPIX_OP_CORRECT_COLOR_MATRIX, NULL, 0);
}

/**
 * @brief Apply White Balance Correction (AWB) to the image
 *
 * This operation only adds the processing element. To control the correction, use
 * @ref mpix_image_ctrl_value() with @ref MPIX_CID_WHITE_BALANCE.
 *
 * @param img Image to correct.
 * @return 0 on success or negative error code.
 */
static inline int mpix_image_correct_white_balance(struct mpix_image *img)
{
	return mpix_pipeline_add(img, MPIX_OP_CORRECT_WHITE_BALANCE, NULL, 0);
}

/**
 * @brief Apply Black Level Correction (BLC) to the image
 *
 * This operation only adds the processing element. To control the correction, use
 * @ref mpix_image_ctrl_value() with @ref MPIX_CID_GAMMA_LEVEL.
 *
 * @param img Image to correct.
 * @return 0 on success or negative error code.
 */
static inline int mpix_image_correct_black_level(struct mpix_image *img)
{
	return mpix_pipeline_add(img, MPIX_OP_CORRECT_BLACK_LEVEL, NULL, 0);
}

/**
 * @brief Apply Gamma Correction (GC) to the image
 *
 * This operation only adds the processing element. To control the correction, use
 * @ref mpix_image_ctrl_value() with @ref MPIX_CID_GAMMA_LEVEL.
 *
 * @param img Image to correct.
 * @return 0 on success or negative error code.
 */
static inline int mpix_image_correct_gamma(struct mpix_image *img)
{
	return mpix_pipeline_add(img, MPIX_OP_CORRECT_GAMMA, NULL, 0);
}

/**
 * @brief Encode an image to the QOI compressed image format
 *
 * @param img Image to convert to QOI format.
 * @return 0 on success or negative error code.
 */
static inline int mpix_image_qoi_encode(struct mpix_image *img)
{
	return mpix_pipeline_add(img, MPIX_OP_QOI_ENCODE, NULL, 0);
}

/**
 * @brief Compressed an image to the JPEG format.
 *
 * @param img Image to convert to JPEG.
 * @return 0 on success or negative error code.
 */
static inline int mpix_image_jpeg_encode(struct mpix_image *img)
{
	return mpix_pipeline_add(img, MPIX_OP_JPEG_ENCODE, NULL, 0);
}

/**
 * @brief Resize an image.
 *
 * An operation is added to change the image size. The aspect ratio is not preserved and the output
 * image size is exactly the same as requested.
 *
 * @param img Image to convert.
 * @param type Type of image resizing to apply.
 * @param width The new width in pixels.
 * @param height The new height in pixels.
 * @return 0 on success or negative error code.
 */
static inline int mpix_image_resize_subsample(struct mpix_image *img,
					      uint16_t width, uint16_t height)
{
	int32_t p[] = { width, height };
	return mpix_pipeline_add(img, MPIX_OP_RESIZE_SUBSAMPLE, p, ARRAY_SIZE(p));
}

/**
 * @brief Crop an image to a smaller region.
 *
 * An operation is added to crop the image to a specified rectangular region.
 * The crop region must be within the bounds of the original image.
 *
 * @param img Image to crop.
 * @param x_offset X coordinate of the top-left corner of the crop region.
 * @param y_offset Y coordinate of the top-left corner of the crop region.
 * @param crop_width Width of the crop region in pixels.
 * @param crop_height Height of the crop region in pixels.
 * @return 0 on success or negative error code.
 */
static inline int mpix_image_crop(struct mpix_image *img, uint16_t x_offset, uint16_t y_offset,
				  uint16_t crop_width, uint16_t crop_height)
{
	int32_t p[] = { x_offset, y_offset, crop_width, crop_height };
	return mpix_pipeline_add(img, MPIX_OP_CROP, p, ARRAY_SIZE(p));
}

/**
 * @brief Apply a sharpen operation to an image.
 * @see MPIX_KERNEL_SHARPEN
 * @param img Image to work on.
 * @param level The pixel size of operation
 * @return 0 on success or negative error code.
 */
static inline int mpix_image_sharpen(struct mpix_image *img, uint8_t level)
{
	int32_t p[] = { MPIX_KERNEL_SHARPEN };
	enum mpix_op_type type = (level == 3) ? MPIX_OP_KERNEL_CONVOLVE_3X3 :
				 (level == 5) ? MPIX_OP_KERNEL_CONVOLVE_5X5 : MPIX_NB_OP;
	return mpix_pipeline_add(img, type, p, ARRAY_SIZE(p));
}

/**
 * @brief Apply a denoise operation to an image.
 * @param img Image to work on.
 * @param level The pixel size of operation
 * @return 0 on success or negative error code.
 */
static inline int mpix_image_denoise(struct mpix_image *img, uint8_t level)
{
	enum mpix_op_type type = (level == 3) ? MPIX_OP_KERNEL_DENOISE_3X3 :
				 (level == 5) ? MPIX_OP_KERNEL_DENOISE_5X5 : MPIX_NB_OP;
	return mpix_pipeline_add(img, type, NULL, 0);
}

/**
 * @brief Apply an edge detection operation to an image.
 * @see MPIX_KERNEL_EDGE_DETECT
 * @param img Image to work on.
 * @param level The pixel size of operation
 * @return 0 on success or negative error code.
 */
static inline int mpix_image_edge_detect(struct mpix_image *img, uint8_t level)
{
	int32_t p[] = { MPIX_KERNEL_EDGE_DETECT };
	enum mpix_op_type type = (level == 3) ? MPIX_OP_KERNEL_CONVOLVE_3X3 :
				 (level == 5) ? MPIX_OP_KERNEL_CONVOLVE_5X5 : MPIX_NB_OP;
	return mpix_pipeline_add(img, type, p, ARRAY_SIZE(p));
}

/**
 * @brief Apply a gaussian blur operation to an image.
 * @see MPIX_KERNEL_GAUSSIAN_BLUR
 * @param img Image to work on.
 * @param level The pixel size of operation
 * @return 0 on success or negative error code.
 */
static inline int mpix_image_gaussian_blur(struct mpix_image *img, uint8_t level)
{
	int32_t p[] = { MPIX_KERNEL_GAUSSIAN_BLUR };
	enum mpix_op_type type = (level == 3) ? MPIX_OP_KERNEL_CONVOLVE_3X3 :
				 (level == 5) ? MPIX_OP_KERNEL_CONVOLVE_5X5 : MPIX_NB_OP;
	return mpix_pipeline_add(img, type, p, ARRAY_SIZE(p));
}

/**
 * @brief Initialize an image from a palette.
 *
 * This permits to process a color palette as if it was an RGB24 image, which leads to far fewer
 * data to process than the full frame.
 *
 * @param img Image to convert.
 * @param palette The color palette to use for the conversion.
 */
static inline void mpix_image_from_palette(struct mpix_image *img, const struct mpix_palette *palette)
{
	uint16_t n = 1 << mpix_palette_bit_depth(palette->fourcc);
	struct mpix_format fmt = { .width = n, .height = 1, .fourcc = MPIX_FMT_RGB24 };
	mpix_image_from_buf(img, palette->colors_rgb24, n * 3, &fmt);
}

/**
 * @brief Convert an image and store it into a color palette.
 *
 * This is the reciproqual operation from @ref mpix_image_from_palette.
 *
 * @param img Image being processed.
 * @param palette The color palette to use for the conversion.
 * @return 0 on success or negative error code.
 */
static inline int mpix_image_to_palette(struct mpix_image *img, struct mpix_palette *palette)
{
	uint16_t n = 1 << mpix_palette_bit_depth(palette->fourcc);
	return (n == 0) ? -EINVAL : mpix_image_to_buf(img, palette->colors_rgb24, n * 3);
}

/**
 * @brief Optimize a color palette after the values from the image.
 *
 * This is performed on the original input image rather than the current state of the image.
 * The strategy used is the "naive k-mean", and only a single pass. Repeat the function as
 * desired to optimize more.
 *
 * @param img Input image sampled to generate the palette.
 * @param palette The palette that will be updated with colors fitting the image better.
 * @param num_samples Number of samples to take from the input image.
 * @return 0 on success or negative error code.
 */
int mpix_image_optimize_palette(struct mpix_image *img, struct mpix_palette *palette,
				uint16_t num_samples);

/**
 * @brief Free the resources of an image.
 *
 * This frees all intermediate buffers allocated automatically, but not the @c buffer field of
 * @p img which is managed by the caller.
 *
 * @note The image fields will all be set to zero.
 *
 * @param img Image to free.
 */
static inline void mpix_image_free(struct mpix_image *img)
{
	mpix_pipeline_free(img->first_op);
	img->first_op = img->last_op = NULL;
	memset(img->ctrls, 0x00, sizeof(img->ctrls));
}

/**
 * @brief Get the input format of an image, matching the inage input buffer
 *
 * @param img The image to inspect.
 * @return The apropriate image format struct.
 */
static inline struct mpix_format *mpix_image_format(struct mpix_image *img)
{
	return img->first_op == NULL ? &img->fmt : &img->first_op->fmt;
}

/**
 * @brief Collect a random RGB pixel from an image.
 *
 * The image can have any format from this list:
 *
 * - @ref MPIX_FMT_RGB24,
 * - @ref MPIX_FMT_RGB565,
 * - @ref MPIX_FMT_YUYV,
 * - @ref MPIX_FMT_SRGGB8,
 * - @ref MPIX_FMT_BGGR8,
 * - @ref MPIX_FMT_SBGGR8,
 * - @ref MPIX_FMT_SGBRG8,
 * - @ref MPIX_FMT_SGRBG8,
 *
 * @param img Image to sample a value from.
 * @param rgb Buffer to 3 bytes filled with the red, green, blue value from the image.
 */
static inline int mpix_image_sample_random_rgb(struct mpix_image *img, uint8_t rgb[3])
{
	return mpix_sample_random_rgb(img->buffer, mpix_image_format(img), rgb);
}

/**
 * @brief Collect statistics from an image.
 *
 * The image buffer is used to collect statistics into a @p stats structure.
 *
 * If the @p stats field @c nval is non-zero, this number of pixels will be collected randomly
 * from the image to generate statistics such as histogram and .
 *
 * @param img Image to convert.
 * @param stats Statistics filled from the image.
 */
static inline void mpix_image_stats(struct mpix_image *img, struct mpix_stats *stats)
{
	mpix_stats_from_buf(stats, img->buffer, mpix_image_format(img));
}

/**
 * @brief Set a control to a pipeline
 *
 * This is to be set after pipeline elements are added, so that their respective controls are
 * added to the image.
 *
 * @param img Image to apply the control to
 * @param cid Control ID to set if present
 * @param val Value to set this control to
 * @return 0 on sucess, or negative error code
 */
static inline int mpix_image_ctrl_value(struct mpix_image *img, enum mpix_control_id cid,
					    int32_t value)
{
	if (cid > MPIX_NB_CID) return -ERANGE;
	if (img->ctrls[cid] == NULL) return -ENOENT;
	img->ctrls[cid][0] = value;
	return 0;
}

/**
 * @brief Set a control to a pipeline
 *
 * This is to be set after pipeline elements are added, so that their respective controls are
 * added to the image.
 *
 * @param img Image to apply the control to
 * @param cid Control ID to set if present
 * @param val Value to set this control to
 * @return 0 on sucess, or negative error code
 */
static inline int mpix_image_ctrl_array(struct mpix_image *img, enum mpix_control_id cid,
					    int32_t *array, size_t size)
{
	if (cid + size > MPIX_NB_CID) return -ERANGE;
	for (size_t i = 0; i < size; i++) {
		if (img->ctrls[cid + i] == NULL) return -ENOENT;
		*img->ctrls[cid + i] = array[i];
	}
	return 0;
}

/**
 * @brief Return the output buffer and its size, resetting it to zero.
 *
 * This clears the output buffer while preserving all the operations of the pipeline for
 * another round to happen after it.
 *
 * @param img Image to flush the output.
 * @param size Pointer set to the size of the output buffer.
 * @return Pointer to the buffer or NULL.
 */
static inline int mpix_image_read_output(struct mpix_image *img, const uint8_t **buf, size_t *size)
{
	if (img->last_op == NULL) return -EINVAL;
	mpix_op_input_all(img->last_op, buf, size);
	return (*size == 0) ? -ENODATA : 0;
}

#endif /** @} */
