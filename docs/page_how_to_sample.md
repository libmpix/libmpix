@page how_to_sample How to sample pixels
@brief Use libmpix to collect pixel values from the image.

libmpix supports collecting individual pixels from the image in RGB value without having to
convnert the entire image. Each pixel sampled will be individually converted.

## How to sample pixels from the image with libmpix

First load a buffer into an image struct, specifying the pixel format:

```c
struct mpix_image img;
struct mpix_format fmt = { .width = 640, .height = 480, .fourcc = MPIX_FMT_RGB24 };

mpix_image_from_buf(&img, buf, sizeof(buf), &fmt);
```

Then collect an individual pixel from the image.

For now only sampling from a random location is supported.

```c
uint8_t rgb_value[3];

mpix_image_sample_random_rgb(&img, rgb_value);
```

@note Statistics collection must be done before any other operation.

## Future development

This API is still unstable, as in the future, more advanced pixel collection is planned:

- Sampling data from arbitrary coordinates.

- Batch collection of data by appending them into a buffer, permitting to process them as regular
  @ref mpix_image.

- More strategies for selecting random pixels within a region of the image,
  collect an entire NxN tile at given coordinate.

- Collecting a grid of equally spaced pixels leveraging @ref MPIX_RESIZE_SUBSAMPLING as back-end.

Something else? Then [let us know](https://github.com/libmpix/libmpix/issues/new) what you needed.
