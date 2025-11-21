@page architecture Architecture
@brief Internal structure of libmpix

SIMD brings image processing abilities to low power microcontrollers
[#1](https://www.synopsys.com/designware-ip/technical-bulletin/signal-processing-risc-v-dsp-extensions.html),
[#2](https://www.arm.com/technologies/helium),
[#3](https://bitbanksoftware.blogspot.com/2024/01/surprise-esp32-s3-has-few-simd.html).

**libmpix** mixes SIMD instructions, hardware accelerators and software to process video data as
efficiently as a chip permits, with RAM down to a few hundred kilobytes.

## Original idea

Color image sensors produce [bayer data](https://en.wikipedia.org/wiki/Bayer_filter),
which goes 3 times larger once converted to RGB. For instance in VGA (640x480) resolution:
`640 * 480 * 1 (raw) + 640 * 480 * 3 (rgb) = 1 228 800` bytes, which is more than what most
microcontrollers have.

By processing the image line by line, the large intermediate buffers are eliminated:

```
--1-line--> correct_white_balance() --2-lines-RAW--> debayer_2x2() --8-lines-RGB--> jpeg_compress()
```

But as seen above operations require different number of input lines.
Functions must be called different number of times per image:
 different number of times per image:
```
--1-line--> correct_white_balance() --+-2-lines--> debayer_2x2() --+-8-lines--> jpeg_compress()
--1-line--> correct_white_balance() --'                            |
--1-line--> correct_white_balance() --+-2-lines--> debayer_2x2() --+
--1-line--> correct_white_balance() --'                            |
--1-line--> correct_white_balance() --+-2-lines--> debayer_2x2() --+
--1-line--> correct_white_balance() --'                            |
--1-line--> correct_white_balance() --+-2-lines--> debayer_2x2() --'
--1-line--> correct_white_balance() --'

 [ repeat the above for the entire image ]
```

## Need for libmpix

This becomes complex to manage manually, so **libmpix** automates this by providing an API for
defining operations in a line-based fashion. Each operation (blue) define an input buffer (green),
and push data to the next operation.

![](page_architehcture_stream.drawio.png)

Performance-wise:

- Having this extra gearbox library between lines conversion functions adds some overhead,
  but this only happens once a full line is converted. For instance, every `640 * 3` bytes
  for VGA resolution, considered low impact.

- This plays well with SIMD instructions that rarely process data one *column* at a time,
  but instead work on contiguous bytes.

- This permits the transfer of the converted image to start as soon as the first line of data is
  converted, without waiting the full conversion.

## Detailed explanation

This diagram presents everything that the pipeline does and that the developer does not need to
manage anymore, as it is handled under the hood by libmpix, but helps understanding the entire
flow in detail, which helps debugging when writing new operations.

![](page_architehcture_detail.drawio.png)
