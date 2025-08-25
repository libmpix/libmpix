# Crop Operation

The crop operation allows you to extract a rectangular region from an image.

## Usage

### API Function

```c
int mpix_image_crop(struct mpix_image *img, uint16_t x_offset, uint16_t y_offset,
                    uint16_t crop_width, uint16_t crop_height);
```

**Parameters:**
- `img`: Image to crop
- `x_offset`: X coordinate of the top-left corner of the crop region
- `y_offset`: Y coordinate of the top-left corner of the crop region
- `crop_width`: Width of the crop region in pixels
- `crop_height`: Height of the crop region in pixels

**Returns:** 0 on success or negative error code.

### Command Line Interface

```bash
mpix read <input_file> <width> <format> ! crop <x>,<y>,<width>,<height> ! write <output_file>
```

**Example:**
```bash
# Crop a 4x4 region starting at position (2,2) from an 8x8 RGB24 image
mpix read input.rgb24 8 RGB24 ! crop 2,2,4,4 ! write cropped.rgb24
```

## Supported Formats

The crop operation supports all uncompressed pixel formats:

- **8-bit formats:** GREY, RGB332
- **16-bit formats:** RGB565, RGB565X
- **24-bit formats:** RGB24, YUV24

## Validation

The crop operation validates the following:

1. **Bounds checking:** The crop region must be within the image boundaries
   - `x_offset + crop_width <= image_width`
   - `y_offset + crop_height <= image_height`

2. **Non-zero dimensions:** Both `crop_width` and `crop_height` must be greater than zero

3. **Format support:** The pixel format must be supported by the crop operation

## Error Handling

The function returns an error code if:

- **-EINVAL:** Invalid crop parameters (exceeds bounds or zero dimensions)
- **-ENOSYS:** Crop operation not available for the specified pixel format

## Performance

The crop operation is optimized for performance:

- Uses `memmove()` for efficient memory copying
- Processes data line by line to minimize memory usage
- Works with the streaming pipeline architecture for optimal throughput

## Example Usage

### Basic Crop

```c
#include <mpix/image.h>

struct mpix_image img;
uint8_t input_buffer[/* input data */];
uint8_t output_buffer[/* output data */];

// Initialize image
mpix_image_from_buf(&img, input_buffer, sizeof(input_buffer), 
                    640, 480, MPIX_FMT_RGB24);

// Crop a 320x240 region from the center
int ret = mpix_image_crop(&img, 160, 120, 320, 240);
if (ret == 0) {
    // Convert to output buffer
    mpix_image_to_buf(&img, output_buffer, sizeof(output_buffer));
}

mpix_image_free(&img);
```

### Error Handling

```c
int ret = mpix_image_crop(&img, x_offset, y_offset, width, height);
if (ret != 0) {
    switch (ret) {
    case -EINVAL:
        printf("Invalid crop parameters\n");
        break;
    case -ENOSYS:
        printf("Crop not supported for this format\n");
        break;
    default:
        printf("Crop failed with error %d\n", ret);
        break;
    }
}
```

## Implementation Details

The crop operation is implemented as a streaming operation that:

1. **Skips lines** before the crop region (y_offset)
2. **Processes lines** within the crop region by copying the specified horizontal portion
3. **Ignores lines** after the crop region

This approach ensures efficient memory usage and works well with the libmpix streaming architecture.
