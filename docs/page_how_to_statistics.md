@page how_to_statistics How to do collect image statistics
@brief Use libmpix to collect statistics about the image.

In order to apply various color and contrast correction to an image or other purposes,
it is useful to collect statistics from the image.

## How to collect statistics with libmpix

First load a buffer into an image struct, specifying the pixel format:

```c
struct mpix_image img;
struct mpix_format fmt = { .width = 640, .height = 480, .fourcc = MPIX_FMT_RGB24 };

mpix_image_from_buf(&img, buf, sizeof(buf), &fmt);
```

Then several types of statistics can be gathered from the image in one function call.

This will sample the specified number of values from the image, and accumulate the information into
the statistics struct. For instance here for `1000` values.

```c
struct mpix_stats stats = { .nvals = 1000 };

mpix_stats_from_buf(&img, &stats);
```

The content of @ref mpix_stats can be browsed directly for any purpose.

@note Statistics collection must be done before any other operation.

Then derived statistics can optionally be computed out of the generated histograms:

```c
/* Get the mean lightness of the image */
val = mpix_stats_get_y_mean(&stats);
```

## How to support more statistics?

The statistics collection is somewhat minimal at the moment, as focused on providing the minimum.

- Applications will needvery different statistics depending on their goal. For custom statistics,
  refer to @ref how_to_sample for directly collecting pixel values from the image.

- libmpix strives to integrate classical computer vision building blocks including statistics,
  [let us know](https://github.com/libmpix/libmpix/issues/new) what you needed!
