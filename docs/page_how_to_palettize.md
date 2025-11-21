@page how_to_palettize How to palettize images
@brief Convert images from RGB to indexed color mode

The palettize and depalettize operations bring the ability to reduce the number of colors in the
image down to a compact palette.

First the image is imported from a buffer or another source.

```c
struct mpix_image img;

mpix_image_from_buf(&img, buf, sizeof(buf), 640, 480, MPIX_FMT_RGB24);
```

The palette need to be allocated. In some cases, stack space will be too small for the palette
buffer and need to be made a global array variable.

Here 3 is for 3 byte per pixel (RGB24) and 4 is because of the `PALETTE4` format is selected.

```c
uint8_t colors[3 << 4]
struct mpix_palette palette = {.colors = colors, fourcc = MPIX_FMT_PALETTE4};
```

Then the color palette can be generated from the content of the image.
The number `1000` refers to the number of pixels sampled from the image used to generate the
palette.

The algorithm used is k-mean. It can be run multiple times to run multiple k-mean cycles and
improve the palette to better match the image. However, the palette can remain the same across
several frames and in such case, a single call can be done per frame to continuously adjusting
the color palette to the new frames.

```c
mpix_image_optimize_palette(img, &palette, 2000);
```

Now that a palette is generated, it is possible to use it to encode the input image:

```c
mpix_image_palette_encode(&img, palette);
```

For getting RGB data back, it is possible to run the opposite operation with the same palette:

```c
mpix_image_palette_decode(&img);
```

See @ref supported_operations for the list of all palettization format.
The conversion is always between RGB24 and one of the palette size.
