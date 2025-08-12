/**
 * SPDX-License-Identifier: Apache-2.0
 * @defgroup mpix_image_h mpix/image.h
 * @brief Main user API
 * @{
 */
#ifndef MPIX_IMAGE_H
#define MPIX_IMAGE_H

#include <mpix/formats.h>
#include <mpix/op.h>
#include <mpix/op_correction.h>
#include <mpix/op_kernel.h>
#include <mpix/op_palettize.h>
#include <mpix/op_resize.h>
#include <mpix/op_jpeg.h>
#include <mpix/stats.h>

/**
 * @brief Represent the image currently being processed
 *
 * When adding operations to an image, the buffer is not converted yet.
 *
 * The struct fields are meant to reflect the buffer after it got converted, so after adding
 * operations, there might be a mismatch between the data format of the buffer and the .
 */
struct mpix_image {
	/** Linked list of operations to be performed on this image */
	struct {
		/** First element of the list */
		struct mpix_base_op *first;
		/** Last element of of the list */
		struct mpix_base_op *last;
	} ops;
	/** Input or output buffer used with the conversion */
	uint8_t *buffer;
	/** Size of the input or output buffer */
	size_t size;
	/** Current pixel format of the image */
	uint32_t fourcc;
	/** Current width of the image */
	uint16_t width;
	/** Current height of the image */
	uint16_t height;
	/** In case an error occurred, this is set to a matching error code */
	int err;
	/** Whether to print a report once the image conversion is complete */
	uint8_t flag_print_ops:1;
};

/**
 * @brief Initialize an image from a memory buffer.
 *
 * @param img Image to initialize.
 * @param buf Memory containinig input image data to process.
 * @param size Total available size in the buffer, can be bigger/smaller than full width x height.
 * @param width Width of the complete image in pixels.
 * @param height Height of the complete image in pixels.
 * @param format Format of data in the buffer as a four-character-code.
 */
void mpix_image_from_buf(struct mpix_image *img, const uint8_t *buf, size_t size,
			 uint16_t width, uint16_t height, uint32_t format);

/**
 * @brief Convert an image and store it into the output buffer.
 *
 * @param img Image being processed.
 * @param buf Memory that receives the image data.
 * @param size Size of the buffer.
 * @return 0 on success or negative error code.
 */
int mpix_image_to_buf(struct mpix_image *img, uint8_t *buf, size_t size);

/**
 * @brief Free the intermediate buffers of an image.
 *
 * This is only required if not calling any export funcitons such as @ref mpix_image_to_buf.
 *
 * @param img Image for which to release resources. Only internal buffers are freed.
 */
void mpix_image_free(struct mpix_image *img);

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
void mpix_image_stats(struct mpix_image *img, struct mpix_stats *stats);

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
int mpix_image_sample_random_rgb(struct mpix_image *img, uint8_t rgb[3]);

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
int mpix_image_convert(struct mpix_image *img, uint32_t new_format);

/**
 * @brief Convert an image to an indexed color format.
 *
 * An operation is added to convert the image to an indexed pixel format given the input palette.
 *
 * If the palette has up to 2 colors, 8 pixels are packed per byte.
 * If the palette has up to 4 colors, 4 pixels are packed per byte.
 * If the palette has up to 16 colors, 2 pixels are packed per byte.
 * If the palette has up to 256 colors, 1 pixels are packed per byte.
 *
 * @param img Image to convert.
 * @param palette The color palette to use for the conversion.
 * @return 0 on success or negative error code.
 */
int mpix_image_palettize(struct mpix_image *img, struct mpix_palette *palette);

/**
 * @brief Convert an image from an indexed color format.
 *
 * An operation is added to convert the image from an indexed pixel format given the input palette.
 *
 * If the palette has up to 2 colors, 8 pixels are packed per byte.
 * If the palette has up to 4 colors, 4 pixels are packed per byte.
 * If the palette has up to 16 colors, 2 pixels are packed per byte.
 * If the palette has up to 256 colors, 1 pixels are packed per byte.
 *
 * @param img Image to convert.
 * @param palette The color palette to use for the conversion.
 * @return 0 on success or negative error code.
 */
int mpix_image_depalettize(struct mpix_image *img, struct mpix_palette *palette);

/**
 * @brief Initialize an image from a palette.
 *
 * This permits to process a color palette as if it was an RGB24 image, which leads to far fewer
 * data to process than the full frame.
 *
 * @param img Image to convert.
 * @param palette The color palette to use for the conversion.
 */
void mpix_image_from_palette(struct mpix_image *img, struct mpix_palette *palette);

/**
 * @brief Convert an image and store it into a color palette.
 *
 * This is the reciproqual operation from @ref mpix_image_from_palette.
 *
 * @param img Image being processed.
 * @param palette The color palette to use for the conversion.
 * @return 0 on success or negative error code.
 */
int mpix_image_to_palette(struct mpix_image *img, struct mpix_palette *palette);

/**
 * @brief Update the color palette after an input image buffer.
 *
 * This is performed on the original input image rather than the current state of the image.
 * The strategy used is the "naive k-mean", and only a single pass. Repeat the function several
 * times to improve accuracy.
 *
 * @param img Input image sampled to generate the palette.
 * @param palette The palette that will be updated with colors fitting the image better.
 * @param num_samples Number of samples to take from the input image.
 * @return 0 on success or negative error code.
 */
int mpix_image_optimize_palette(struct mpix_image *img, struct mpix_palette *palette,
				uint16_t num_samples);

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
int mpix_image_debayer(struct mpix_image *img, uint32_t window_size);

/**
 * @brief Encode an image to the QOI compressed image format
 *
 * @param img Image to convert to QOI format.
 * @return 0 on success or negative error code.
 */
int mpix_image_qoi_encode(struct mpix_image *img);

/**
 * @brief Compressed an image to the JPEG format.
 *
 * @param img Image to convert to JPEG.
 * @param quality The quality level to use for encoding.
 * @return 0 on success or negative error code.
 */
int mpix_image_jpeg_encode(struct mpix_image *img, enum mpix_jpeg_quality quality);

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
int mpix_image_resize(struct mpix_image *img, enum mpix_resize_type type,
		      uint16_t width, uint16_t height);

/**
 * @brief Apply a kernel operation on an image.
 *
 * Kernel operations are working on small blocks of typically 3x3 or 5x5 pixels, repeated over the
 * entire image to apply a desired effect on an image.
 *
 * @param img Image to convert.
 * @param kernel_type The type of kernel to apply as defined in @ref mpix_op_kernel_h
 * @param kernel_sz The size of the kernel operaiton, usually 3 or 5.
 * @return 0 on success or negative error code.
 */
int mpix_image_kernel(struct mpix_image *img, uint32_t kernel_type, int kernel_sz);

/**
 * @brief Apply an Image Signal Processing (ISP) correction operation to an image.
 *
 * Kernel operations are working on small blocks of typically 3x3 or 5x5 pixels, repeated over the
 * entire image to apply a desired effect on an image.
 *
 * @param img Image to convert.
 * @param type The type of ISP to apply as defined in @ref mpix_op_correction_h
 * @param corr The correction level to apply.
 * @return 0 on success or negative error code.
 */
int mpix_image_correction(struct mpix_image *img, uint32_t type, union mpix_correction_any *corr);

/**
 * @brief Print an image using higher quality TRUECOLOR terminal escape codes.
 *
 * @param img Image to print.
 */
void mpix_image_print_truecolor(struct mpix_image *img);

/**
 * @brief Print an image using higher speed 256COLOR terminal escape codes.
 *
 * @param img Image to print.
 */
void mpix_image_print_256color(struct mpix_image *img);

/**
 * @brief Print details about an image and its current list of operations.
 *
 * @param img Image to detail.
 */
void mpix_image_print_ops(struct mpix_image *img);

/**
 * @brief Print a hexdump of the image to the console for debug purpose.
 *
 * @param img Image to print.
 */
void mpix_image_hexdump(struct mpix_image *img);

/**
 * @brief Add a operation processing step to an image.
 * @internal
 *
 * @note This is a low-level function only needed to implement new operations.
 *
 * The operation step will not be processed immediately, but rather added to a linked list of
 * operations that will be performed at runtime.
 *
 * @param img Image to which add a processing step.
 * @param template Stream processing step to apply to the image.
 * @param op_sz Size of the operation struct to allocate.
 * @param buf_sz Size of the input buffer to allocate for this operation.
 * @param threshold Minimum number of bytes the operation needs to run one cycle.
 * @return 0 on success or negative error code.
 */
int mpix_image_append_op(struct mpix_image *img, const struct mpix_base_op *template,
			 size_t op_sz, size_t buf_sz, size_t threshold);

/**
 * @brief Add a operation processing step to an image for uncompressed input data.
 * @internal
 *
 * @note This is a low-level function only needed to implement new operations.
 *
 * The operation step will not be processed immediately, but rather added to a linked list of
 * operations that will be performed at runtime.
 *
 * Details such as buffer size or threshold value are deduced from the stream.
 *
 * @param img Image to which add a processing step.
 * @param template Stream processing step to apply to the image.
 * @param op_sz Size of the operation struct to allocate.
 * @return 0 on success or negative error code.
 */
int mpix_image_append_uncompressed_op(struct mpix_image *img, const struct mpix_base_op *op,
				      size_t op_sz);

/**
 * @brief Perform all the processing added to the
 * @internal
 *
 * @note This is a low-level function only needed to implement new operations.
 *
 * This is where all the image processing happens. The processing steps are not executed while they
 * are added to the pipeline, but only while this function is called.
 *
 * @param img Image to which one or multiple processing steps were added.
 * @return 0 on success or negative error code.
 */
int mpix_image_process(struct mpix_image *img);

/**
 * @brief Set the error code of the image an error on the image
 * @internal
 *
 * @param img Image to which one or multiple processing steps were added.
 * @return 0 on success or negative error code.
 */
int mpix_image_error(struct mpix_image *img, int err);

#endif /** @} */
