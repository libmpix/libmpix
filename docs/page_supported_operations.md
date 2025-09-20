@page supported_operations Supported operations
@brief List of image processing operations supported by libmpix

@note At the moment, the libmpix library only supports C implementation for each operation.
SIMD integration is [in progress](https://github.com/libmpix/libmpix/issues/3).

## Format definitions

Note that a presence on that list does not mean that the format can be converted in all direction.
Refer to the other lists below to see what is supported.

You may extend this list directly inside the applicatoin via @ref mpix_bits_per_pixel_cb.

@see mpix_formats_h with a list of all pixel formats defined by libmpix.

## Pixel format conversion

libmpix can perform pixel format conversion to support multiple input and output formats.

@see @ref how_to_convert

## Raw Bayer format conversion

libmpix can perform debayer operations to use
[raw image](https://en.wikipedia.org/wiki/Bayer_filter) as input.

The various options are:

- 3x3, a higher quality but more compute intensive variant
- 2x2, a lower quality but faster to process variant

@see @ref how_to_debayer

## Palette format conversion

libmpix can generate and read data in [indexed colors](https://en.wikipedia.org/wiki/Indexed_color).

The performance quickly goes down as approaching 256 colors (`PALETTE8`),
and the image loose most of the information as approaching 2 colors (`PALETTE1`).
Intermediate palette sizes are a trade-off of performance vs quality.

@see @ref how_to_palettize

## QOI format conversion

libmpix can convert files to the lossless [QOI format](https://qoiformat.org/) which is slightly
less efficient than PNG but lighter in CPU, memory and flash resources.

The output is byte-per-byte identical to the reference implementation.

@see @ref how_to_qoi

## Image correction

libmpix can perform image correction operations as found in most ISP pipelines:

@see @ref how_to_correct

## Image kernel processing

libmpix can perform several
[kernel image processing](https://en.wikipedia.org/wiki/Kernel_(image_processing))
operations to work on adjacent pixel affecting the sharpness, noise, blurriness...

@see @ref how_to_kernel

## Image resizing operations

libmpix can resize images using various strategies with a different trade-off between quality,
flexibility and performance.

@see @ref how_to_resize

## Image statistics collection

libmpix can collect selected statistics from an image.

Statistics collection are supported for any of the pixel sampling formats (see below).

The following types of statistics are stored into the @ref mpix_stats struct:

- luma (Y channel) histogram
- RGB channel average, minimum, maximum

@see @ref how_to_statistics

## Pixel value sampling

libmpix can collect RGB pixels from the image using the following strategy:

- Random sampling

And from the following input image formats:

- @ref MPIX_FMT_RGB24
- @ref MPIX_FMT_RGB565
- @ref MPIX_FMT_YUYV
- @ref MPIX_FMT_SRGGB8
- @ref MPIX_FMT_BGGR8
- @ref MPIX_FMT_SBGGR8
- @ref MPIX_FMT_SGBRG8
- @ref MPIX_FMT_SGRBG8

@see @ref how_to_sample
