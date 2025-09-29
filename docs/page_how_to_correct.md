@page how_to_correct How to correct images
@brief Use libmpix to apply image correction to improve contrasts and colors

Raw images coming from a sensor are typically too dark or over-exposed, with green colors and other
visible defects.

Some of the defects cannot be corrected (too much noise, over-exposure), and some are expected and
corrected as part of any classical image correction pipeline.

![](docs/img/snapshot_raw_imx219_zoom_in.png)

If the image arrives without such defects, this suggests that the correction is included inside
the image sensor or the video acquisition hardware.

The input format is always specified when opening the image and does not need to be specified.
What remains to do is to call @ref mpix_image_convert and specify the output pixel format.

## How to do image correction on libmpix

First load a buffer into an image struct, specifying the pixel format:

```c
struct mpix_image img;

mpix_image_from_buf(&img, buf, sizeof(buf), 640, 480, MPIX_FMT_RGB24);
```

Each type of correction have their own struct, all of which are represented as members of
the @ref mpix_correction_all struct (convenient to store all parameters) and
@ref mpix_correction_any (used to control a particular operation).

To control the correction level, you may adjust each parameter to fit a particular camera and
light condition manually:

- `any.black_level.level` is the value to subtract to every pixel to set the black back to 0.
  This can be set to the minimum value you observe in an image and rarely needs to be updated.

- `any.white_balance.red_level` and `.blue_level` are defined as a proportion to the green channel,
  with 1.0 mapped to 1024. Let's try to apply x2 to both blue and red to correct the green tint:
  `1024 * 2 = 2048`.

- `any.gamma_level` gamma correction to non-linearly increase the brightness: dark/bright colors
  remain dark/bright but intermediate tones become brighter to reflect eye's natural light
  sensitivity. Contrasts will appear more natural and accurate.

```c
struct mpix_correction_any bl = { .black_level = { .level = 0x0f } };
struct mpix_correction_any wb = { .white_balance = { .red_level = 2048, .blue_level = 2048 } };
struct mpix_correction_any gc = { .gamma = { .level = 240 } };
```

Then, apply each step of the correction you wish to the image:

```c
mpix_image_correct_black_level(&img);
mpix_image_correct_white_balance(&img);
mpix_image_correct_gamma(&img);
```

It is now possible to apply extra correction to it (where `1 << 10` is to convert to fixed-point):

```c
mpix_image_ctrl_value(&img, MPIX_CID_BLACK_LEVEL, 0x0f);
mpix_image_ctrl_value(&img, MPIX_CID_RED_BALANCE, 1.3 * (1 << 10));
mpix_image_ctrl_value(&img, MPIX_CID_BLUE_BALANCE, 1.2 * (1 << 10));
mpix_image_ctrl_value(&img, MPIX_CID_GAMMA, 0.6 * (1 << 10));
```

The image will now be corrected using each of the steps specified.

See @ref supported_operations for a list of all supported image correction operations.
