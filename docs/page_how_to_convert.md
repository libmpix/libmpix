@page how_to_convert How to convert pixel formats
@brief Converting between uncompressed pixel format conversion in libmpix

@note This guide is about uncompressed pixel formats. Different guide describe how to work with
compressed formats.

The input format is always specified when opening the image and does not need to be specified.
What remains to do is to call @ref mpix_image_convert and specify the output pixel format.

First load a buffer into an image struct, specifying the pixel format:

```c
struct mpix_image img;
struct mpix_format fmt = { .width = 640, .height = 480, .fourcc = MPIX_FMT_RGB24 };

mpix_image_from_buf(&img, buf, sizeof(buf), &fmt);
```

Then convert the image to the destination format:

```c
mpix_image_convert(&img, MPIX_FMT_GREY);
```

Add more processing steps as needed.

See @ref supported_operations for a list of all supported format conversions.
