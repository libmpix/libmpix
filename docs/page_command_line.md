@page command_line Command line tool
@brief The "mpix" command line tool for interactive pipelines

As a way to quickly run a libmpix operation on desktop, the `mpix` command line tool gives
access to all of the libmpix operations without having to recompile.

To install it:

```
git clone https://github.com/libmpix/libmpix
cd libmpix/cli
cmake -B build
cmake --build build
sudo cmake --build build --target install
```

The syntax is inspired from [`gst-launch-1.0`][1] from Gstreamer, where the `!` character
represents a pipe between two operations:

[1]: https://gstreamer.freedesktop.org/documentation/tools/gst-launch.html

```
Available commands:
 mpix [-v] read <file> [<width> <format>] ! ...
 mpix [-v] ... ! write <file>
 mpix [-v] ... ! convert <format> ! ...
 mpix [-v] ... ! debayer <size> ! ...
 mpix [-v] ... ! palette <bit_depth> <optimization_cycles> ! ...
 mpix [-v] ... ! palettize ! ...
 mpix [-v] ... ! depalettize ! ...
 mpix [-v] ... ! correction <type> <level1> [<level2>] ! ...
 mpix [-v] ... ! kernel <type> <size> ! ...
 mpix [-v] ... ! resize <type> <width>x<height> ! ...
 mpix [-v] ... ! qoi_encode ! ...
```

## Example: converting and resizing

- Read a raw image of size 640x480 (VGA) in YUYV pixel format.
- Convert the image to RGB24 pixel format
- Resize the image to a square of 128x128 pixel using the subsampling method
- Encode the image as QOI data and write the file out

```
mpix read /tmp/image.bin 640x480 YUYV ! convert RGB24 ! resize SUBSAMPLING 128x128 \
  ! qoi_encode ! write /tmp/image.qoi
```

## Example: color correction

- Read a raw image of size 640x480 (VGA) in RGB24 pixel format.
- Apply gamma correction with a GAMMA value of 0.8
- Apply a denoise kernel with correction level 5
- Encode the image as QOI data and write the file out

```
mpix read /tmp/image.bin 640x480 RGB24 ! correction GAMMA 0.8 ! kernel DENOISE 5 \
  ! qoi_encode ! write /tmp/image.qoi
```

## Example: palette effect

- Read a raw image of size 640x480 (VGA) in RGB24 pixel format.
- Compute a 4-bit palette from the image with 10 optimization cycles
- Convert the image to a reduced color palette format
- Convert the image back to RGB24
- Encode the image as QOI data and write the file out

```
mpix read /tmp/image.bin 640x480 RGB24 ! palette 4 10 ! palettize ! depalettize \
  ! qoi_encode ! write /tmp/image.qoi
```
