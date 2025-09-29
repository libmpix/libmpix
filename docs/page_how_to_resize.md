@page how_to_resize How to do resize an image
@brief Use libmpix to downscale/upscale an image.

Resizing an image an image can be done according to several strategies with trade-offs betwen
speed and quality of the result.

In case the resizing strategy you wished to use (such as blending) is not available, then
[let us know](https://github.com/libmpix/libmpix/issues/new).

## How to resize images in libmpix

First load a buffer into an image struct, specifying any supported pixel format:

```c
struct mpix_image img;
struct mpix_format fmt = { .width = 640, .height = 480, .fourcc = MPIX_FMT_RGB24 };

mpix_image_from_buf(&img, buf, sizeof(buf), &fmt);
```

Then choose the type of resize operation you wish to use:

- @ref MPIX_RESIZE_SUBSAMPLING which can take any resolution but lower quality,

Then apply the resize operation using the selected strategy:

```c
mpix_image_resize(&img, MPIX_RESIZE_SUBSAMPLING);
```

See @ref supported_operations for the list of all supported resize types for each format.
