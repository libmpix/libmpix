@page how_to_kernel How to do kernel processing
@brief Use libmpix to apply blur, sharpen, denoise, edge detect...

[Image kernels](https://en.wikipedia.org/wiki/Kernel_(image_processing)) are operations that work
on the entire image tile-by-tile.

A small square of i.e. 3x3 or 5x5 pixels is processed at a time, and this square is shifted by one
pixel to the right repeatedly until it processed the full row, then shift one row below to process
the next line, and so forth over the entire image.

This tile-by-tile image processing is very similar to how Convolutional Neural Networks (CNN)
process images, as "convolution" is a common operation between kernel processing and CNN.

### How to perform kernel processing with libmpix

First load a buffer into an image struct, in one of the supported input format.
See @ref supported_operations for the list of all supported operations for each kernel.

```c
struct mpix_image img;
struct mpix_format fmt = { .width = 640, .height = 480, .fourcc = MPIX_FMT_RGB24 };

mpix_image_from_buf(&img, buf, sizeof(buf), &fmt);
```

Then, select the type of kernel operation you wish to perform on the image:

- @ref MPIX_KERNEL_IDENTITY
- @ref MPIX_KERNEL_EDGE_DETECT
- @ref MPIX_KERNEL_GAUSSIAN_BLUR
- @ref MPIX_KERNEL_SHARPEN
- @ref MPIX_KERNEL_DENOISE

Then call the operation on the image. For instance using the denoise kernel:

```c
mpix_image_denoise(&img, 2);
```
