/**
 * SPDX-License-Identifier: Apache-2.0
 * @defgroup mpix_formats_h mpix/formats.h
 * @brief Pixel format definitions
 * @{
 */
#ifndef MPIX_FORMAT_H
#define MPIX_FORMAT_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <mpix/types.h>
#include <mpix/utils.h>

/**
 * @brief Generate a string out of a pixel format.
 *
 * @param fourcc Format as a 32-bit four character code.
 * @return A local nul-terminated string for this four character code.
 */
#define MPIX_FOURCC_TO_STR(fourcc)                                                                 \
	((char[5]){                                                                                 \
		(char)(((fourcc) >> 0) & 0xFF),                                                    \
		(char)(((fourcc) >> 8) & 0xFF),                                                    \
		(char)(((fourcc) >> 16) & 0xFF),                                                   \
		(char)(((fourcc) >> 24) & 0xFF),                                                   \
		'\0',                                                                              \
	})

/**
 * @brief Define a new pixel format, with defaults for most common values.
 *
 * @param a 1st character of the Four Character Code
 * @param b 2nd character of the Four Character Code
 * @param c 3rd character of the Four Character Code
 * @param d 4th character of the Four Character Code
 */
#define MPIX_FOURCC(a, b, c, d)                                                                    \
	((uint32_t)(a) | ((uint32_t)(b) << 8) | ((uint32_t)(c) << 16) | ((uint32_t)(d) << 24))

/**
 * @brief Convert an image and store it into a color palette.
 *
 * This is the reciproqual operation from @ref mpix_image_from_palette.
 *
 * @param img Image being processed.
 * @param fourcc The pixel format of the palette
 * @return 0 on success or negative error code.
 */
static inline uint8_t mpix_palette_bit_depth(uint32_t fourcc)
{
	char *str = MPIX_FOURCC_TO_STR(fourcc);

	if (strncmp(str, "PLT", 3) != 0 || !IN_RANGE(str[3], '1', '9')) {
		return 0;
	}

	return str[3] - '0';
}

/**
 * @brief Convert an image and store it into a color palette.
 *
 * This is the reciproqual operation from @ref mpix_image_from_palette.
 *
 * @param img Image being processed.
 * @param depth The color palette to use for the conversion.
 * @return 0 on success or negative error code.
 */
static inline uint32_t mpix_palette_fourcc(uint8_t bit_depth)
{
	if (!IN_RANGE(bit_depth, 1, 9)) {
		return 0;
	}

	return MPIX_FOURCC('P', 'L', 'T', '0' + bit_depth);
}

/**
 * @name RGB formats
 * Formats with red, green, blue channels, each pixel contain each channel.
 * @{
 */

/**
 * 8 bit RGB format with 3 or 2 bit per component
 *
 * @code{.unparsed}
 * | RrrGggBb | ...
 * @endcode
 */
#define MPIX_FMT_RGB332 MPIX_FOURCC('R', 'G', 'B', '1')

/**
 * 5 red bits [15:11], 6 green bits [10:5], 5 blue bits [4:0].
 * This 16-bit integer is then packed in little endian format over two bytes:
 *
 * @code{.unparsed}
 *   7......0 15.....8
 * | gggBbbbb RrrrrGgg | ...
 * @endcode
 */
#define MPIX_FMT_RGB565 MPIX_FOURCC('R', 'G', 'B', 'P')

/**
 * 5 red bits [15:11], 6 green bits [10:5], 5 blue bits [4:0].
 * This 16-bit integer is then packed in big endian format over two bytes:
 *
 * @code{.unparsed}
 *   15.....8 7......0
 * | RrrrrGgg gggBbbbb | ...
 * @endcode
 */
#define MPIX_FMT_RGB565X MPIX_FOURCC('R', 'G', 'B', 'R')

/**
 * 24 bit RGB format with 8 bit per component
 *
 * @code{.unparsed}
 * | Rggggggg Gggggggg Bbbbbbbb | ...
 * @endcode
 */
#define MPIX_FMT_RGB24 MPIX_FOURCC('R', 'G', 'B', '3')

/**
 * The first byte is empty (X) for each pixel.
 *
 * @code{.unparsed}
 * | Xxxxxxxx Rrrrrrrr Gggggggg Bbbbbbbb | ...
 * @endcode
 */
#define MPIX_FMT_XRGB32 MPIX_FOURCC('B', 'X', '2', '4')

/** @} */

/**
 * @name YUV formats
 * Formats with Luma (Y), Cb (U), and Cr (V) channels, sometimes with chroma subsampling.
 * @{
 */

/**
 * @brief 12-bit per pixel, 2 pixels every 3 bytes, 4-bit per component.
 *
 * 2 pixels shown below:
 *
 * @code{.unparsed}
 * | YyyyUuuu Vvvv|Yyyy UuuuVvvv| ...
 * @endcode
 */
#define MPIX_FMT_YUV12 MPIX_FOURCC('Y', 'U', 'V', 'C')

/**
 * @code{.unparsed}
 * | Yyyyyyyy Uuuuuuuu Vvvvvvvv | ...
 * @endcode
 */
#define MPIX_FMT_YUV24 MPIX_FOURCC('Y', 'U', 'V', '3')

/**
 * There is either a missing channel per pixel, U or V.
 * The value is to be averaged over 2 pixels to get the value of individual pixel.
 *
 * @code{.unparsed}
 * | Yyyyyyyy Uuuuuuuu | Yyyyyyyy Vvvvvvvv | ...
 * @endcode
 */
#define MPIX_FMT_YUYV MPIX_FOURCC('Y', 'U', 'Y', 'V')

/** @} */

/**
 * @name Luma-only formats
 * Formats with only a luma (Y) channel, grayscale.
 * @{
 */

/**
 * Same as Y8 (8-bit luma-only) following the standard FOURCC naming,
 * or L8 in some graphics libraries.
 *
 * @code{.unparsed}
 *   0          1          2          3
 * | Yyyyyyyy | Yyyyyyyy | Yyyyyyyy | Yyyyyyyy | ...
 * @endcode
 */
#define MPIX_FMT_GREY MPIX_FOURCC('G', 'R', 'E', 'Y')

/** @} */

/**
 * @name Bayer formats
 * Formats with bayer color filter array, one channel per pixel, red, green or blue.
 * @{
 */

/**
 * @code{.unparsed}
 *   0          1          2          3
 * | Bbbbbbbb | Gggggggg | Bbbbbbbb | Gggggggg | ...
 * | Gggggggg | Rrrrrrrr | Gggggggg | Rrrrrrrr | ...
 * @endcode
 */
#define MPIX_FMT_SBGGR8 MPIX_FOURCC('B', 'A', '8', '1')

/**
 * @code{.unparsed}
 *   0          1          2          3
 * | Gggggggg | Bbbbbbbb | Gggggggg | Bbbbbbbb | ...
 * | Rrrrrrrr | Gggggggg | Rrrrrrrr | Gggggggg | ...
 * @endcode
 */
#define MPIX_FMT_SGBRG8 MPIX_FOURCC('G', 'B', 'R', 'G')

/**
 * @code{.unparsed}
 *   0          1          2          3
 * | Gggggggg | Rrrrrrrr | Gggggggg | Rrrrrrrr | ...
 * | Bbbbbbbb | Gggggggg | Bbbbbbbb | Gggggggg | ...
 * @endcode
 */
#define MPIX_FMT_SGRBG8 MPIX_FOURCC('G', 'R', 'B', 'G')

/**
 * @code{.unparsed}
 *   0          1          2          3
 * | Rrrrrrrr | Gggggggg | Rrrrrrrr | Gggggggg | ...
 * | Gggggggg | Bbbbbbbb | Gggggggg | Bbbbbbbb | ...
 * @endcode
 */
#define MPIX_FMT_SRGGB8 MPIX_FOURCC('R', 'G', 'G', 'B')

/** @} */


/**
 * @name IR Bayer formats
 * Formats with bayer color filter array featuring Red, Gren, Blue, Infrared
 * @{
 */

/**
 * @code{.unparsed}
 *   0          1          2          3
 * | Rrrrrrrr | Gggggggg | Bbbbbbbb | Gggggggg |
 * | Gggggggg | Iiiiiiii | Gggggggg | Iiiiiiii |
 * | Bbbbbbbb | Gggggggg | Rrrrrrrr | Gggggggg |
 * | Gggggggg | Iiiiiiii | Gggggggg | Iiiiiiii |
 * @endcode
 */
#define MPIX_FMT_SRGGI8 MPIX_FOURCC('R', 'G', 'I', '8')

/**
 * @code{.unparsed}
 *   0          1          2          3
 * | Gggggggg | Rrrrrrrr | Gggggggg | Bbbbbbbb |
 * | Iiiiiiii | Gggggggg | Iiiiiiii | Gggggggg |
 * | Gggggggg | Bbbbbbbb | Gggggggg | Rrrrrrrr |
 * | Iiiiiiii | Gggggggg | Iiiiiiii | Gggggggg |
 * @endcode
 */
#define MPIX_FMT_SGRIG8 MPIX_FOURCC('G', 'R', 'I', '8')

/**
 * @code{.unparsed}
 *   0          1          2          3
 * @endcode
 */
#define MPIX_FMT_SBGGI8 MPIX_FOURCC('B', 'G', 'I', '8')

/**
 * @code{.unparsed}
 *   0          1          2          3
 * @endcode
 */
#define MPIX_FMT_SGBIG8 MPIX_FOURCC('G', 'B', 'I', '8')

/**
 * @code{.unparsed}
 *   0          1          2          3
 * @endcode
 */
#define MPIX_FMT_SGIRG8 MPIX_FOURCC('G', 'I', 'R', '8')

/**
 * @code{.unparsed}
 *   0          1          2          3
 * @endcode
 */
#define MPIX_FMT_SIGGR8 MPIX_FOURCC('I', 'G', 'R', '8')

/**
 * @code{.unparsed}
 *   0          1          2          3
 * @endcode
 */
#define MPIX_FMT_SGIBG8 MPIX_FOURCC('G', 'I', 'B', '8')

/**
 * @code{.unparsed}
 *   0          1          2          3
 * @endcode
 */
#define MPIX_FMT_SIGGB8 MPIX_FOURCC('I', 'G', 'B', '8')

/** @} */

/**
 * @name Indexed color formats
 * Formats where each pixel is a reference to a color palette.
 * @{
 */

/**
 * 1-bit color palette pixel format, 2 colors in total like a bitmap.
 * 8 pixels shown below:
 *
 * @code{.unparsed}
 *   0 1 2 3 4 5 6 7
 * | P|P|P|P|P|P|P|P | ...
 * @endcode
 */
#define MPIX_FMT_PALETTE1 MPIX_FOURCC('P', 'L', 'T', '1')

/**
 * 2-bit color palette pixel format, 4 colors in total.
 * 8 pixels shown below:
 *
 * @code{.unparsed}
 *   0  1  2  3    4  5  6  7
 * | Pp|Pp|Pp|Pp | Pp|Pp|Pp|Pp | ...
 * @endcode
 */
#define MPIX_FMT_PALETTE2 MPIX_FOURCC('P', 'L', 'T', '2')

/**
 * 3-bit color palette pixel format, 8 colors in total.
 * Padded by 1 bit to fit a 4 bit packing.
 * 8 pixels shown below:
 *
 * @code{.unparsed}
 *    0    1      2    3      4    5      6    7
 * | -Ppp|-Ppp | -Ppp|-Ppp | -Ppp|-Ppp | -Ppp|-Ppp | ...
 * @endcode
 */
#define MPIX_FMT_PALETTE3 MPIX_FOURCC('P', 'L', 'T', '3')

/**
 * 4-bit color palette pixel format, 16 colors in total.
 * 8 pixels shown below:
 *
 * @code{.unparsed}
 *   0    1      2    3       4    5      6    7
 * | Pppp|Pppp | Pppp|Pppp  | Pppp|Pppp | Pppp|Pppp | ...
 * @endcode
 */
#define MPIX_FMT_PALETTE4 MPIX_FOURCC('P', 'L', 'T', '4')

/**
 * 5-bit color palette pixel format, 32 colors in total.
 * Padded by 3 bit to fit a 8 bit packing.
 * 8 pixels shown below:
 *
 * @code{.unparsed}
 *      0          1          2          3          4          5          6          7
 * | ---Ppppp | ---Ppppp | ---Ppppp | ---Ppppp | ---Ppppp | ---Ppppp | ---Ppppp | ---Ppppp | ...
 * @endcode
 */
#define MPIX_FMT_PALETTE5 MPIX_FOURCC('P', 'L', 'T', '5')

/**
 * 6-bit color palette pixel format, 64 colors in total.
 * Padded by 2 bit to fit a 8 bit packing.
 * 8 pixels shown below:
 *
 * @code{.unparsed}
 *     0          1          2          3          4          5          6          7
 * | --Pppppp | --Pppppp | --Pppppp | --Pppppp | --Pppppp | --Pppppp | --Pppppp | --Pppppp | ...
 * @endcode
 */
#define MPIX_FMT_PALETTE6 MPIX_FOURCC('P', 'L', 'T', '6')

/**
 * 7-bit color palette pixel format, 128 colors in total.
 * Padded by 1 bit to fit a 8 bit packing.
 * 8 pixels shown below:
 *
 * @code{.unparsed}
 *    0          1          2          3          4          5          6          7
 * | -Ppppppp | -Ppppppp | -Ppppppp | -Ppppppp | -Ppppppp | -Ppppppp | -Ppppppp | -Ppppppp | ...
 * @endcode
 */
#define MPIX_FMT_PALETTE7 MPIX_FOURCC('P', 'L', 'T', '7')

/**
 * 8-bit color palette pixel format, 256 colors in total.
 * 8 pixels shown below:
 *
 * @code{.unparsed}
 *   0          1          2          3          4          5          6          7
 * | Pppppppp | Pppppppp | Pppppppp | Pppppppp | Pppppppp | Pppppppp | Pppppppp | Pppppppp | ...
 * @endcode
 */
#define MPIX_FMT_PALETTE8 MPIX_FOURCC('P', 'L', 'T', '8')

/** @} */

/**
 * @name Compressed formats
 * Lossy and non-lossy compressed formats.
 * The bits per pixel is defined as zero.
 * @{
 */

/**
 * Compressed frame using the JPEG format.
 * Multiple JPEG frames can be sent back-to-back to make an MJPEG stream.
 */
#define MPIX_FMT_JPEG MPIX_FOURCC('J', 'P', 'E', 'G')

/**
 * Compressed frame using the QOI format.
 * Multiple QOI frames can be sent back-to-back to make an QOIF stream.
 */
#define MPIX_FMT_QOI MPIX_FOURCC('Q', 'O', 'I', 'F')

/** @} */

/**
 * @brief Get the average number of bits per pixel of a pixel format.
 *
 * For compressed/variablle pitch formats, the convention is that the result is 0.
 *
 * @return Average number of bits per pixel.
 */
static inline uint8_t mpix_bits_per_pixel(uint32_t fourcc)
{
	switch (fourcc) {

	/* RGB formats */
	case MPIX_FMT_RGB332:
		return 8;
	case MPIX_FMT_RGB565:
	case MPIX_FMT_RGB565X:
		return 16;
	case MPIX_FMT_RGB24:
		return 24;

	/* YUV formats */
	case MPIX_FMT_YUV12:
		return 12;
	case MPIX_FMT_YUV24:
		return 24;
	case MPIX_FMT_YUYV:
		return 16;
	case MPIX_FMT_GREY:
		return 8;

	/* Bayer formats */
	case MPIX_FMT_SRGGB8:
	case MPIX_FMT_SBGGR8:
	case MPIX_FMT_SGBRG8:
	case MPIX_FMT_SGRBG8:
		return 8;

	/* Indexed formats */
	case MPIX_FMT_PALETTE1:
		return 1;
	case MPIX_FMT_PALETTE2:
		return 2;
	case MPIX_FMT_PALETTE3:
	case MPIX_FMT_PALETTE4:
		return 4;
	case MPIX_FMT_PALETTE5:
	case MPIX_FMT_PALETTE6:
	case MPIX_FMT_PALETTE7:
	case MPIX_FMT_PALETTE8:
		return 8;

	/* Compressed formats */
	case MPIX_FMT_JPEG:
	case MPIX_FMT_QOI:
		return 0;

	/* Unhandled format */
	default:
		return 0;
	}
}

/**
 * @brief Get the pitch for a given pixel format
 *
 * This assumes a format without padding, and returns 0 for variable pitch formats, such as
 * compressed formats.
 *
 * @return Number of bytes used to store a line of this format.
 */
static inline size_t mpix_format_pitch(const struct mpix_format *fmt)
{
	return mpix_bits_per_pixel(fmt->fourcc) * fmt->width / BITS_PER_BYTE;
}

/**
 * @brief Get the pixel format of the buffer if looking one line below
 *
 * @return The new four character code (FourCC) or zero if not a bayer format.
 */
static inline uint32_t mpix_format_line_down(uint32_t fourcc)
{
	return	fourcc == MPIX_FMT_SRGGB8 ? MPIX_FMT_SGBRG8 :
		fourcc == MPIX_FMT_SBGGR8 ? MPIX_FMT_SGRBG8 :
		fourcc == MPIX_FMT_SGBRG8 ? MPIX_FMT_SRGGB8 :
		fourcc == MPIX_FMT_SGRBG8 ? MPIX_FMT_SBGGR8 :
		fourcc;
}

#endif /** @} */
