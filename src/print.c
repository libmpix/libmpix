/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <assert.h>

#include <mpix/image.h>
#include <mpix/op_convert.h>
#include <mpix/print.h>
#include <mpix/utils.h>

static uint8_t mpix_rgb24_to_256color(const uint8_t rgb24[3])
{
	return 16 + rgb24[0] * 6 / 256 * 36 + rgb24[1] * 6 / 256 * 6 + rgb24[2] * 6 / 256 * 1;
}

static uint8_t mpix_gray8_to_256color(uint8_t gray8)
{
	return 232 + gray8 * 24 / 256;
}

void mpix_print_truecolor(const uint8_t row0[3], const uint8_t row1[3])
{
	mpix_port_printf("\e[48;2;%u;%u;%um\e[38;2;%u;%u;%um▄",
		row0[0], row0[1], row0[2], row1[0], row1[1], row1[2]);
}

void mpix_print_256color(const uint8_t row0[3], const uint8_t row1[3])
{
	mpix_port_printf("\e[48;5;%um\e[38;5;%um▄",
		mpix_rgb24_to_256color(row0), mpix_rgb24_to_256color(row1));
}

void mpix_print_256gray(uint8_t row0, uint8_t row1)
{
	mpix_port_printf("\e[48;5;%um\e[38;5;%um▄",
		mpix_gray8_to_256color(row0), mpix_gray8_to_256color(row1));
}

typedef void fn_print_t(const uint8_t row0[3], const uint8_t row1[3]);

typedef void fn_conv_t(const uint8_t *src, uint8_t *dst, uint16_t w);

static void mpix_print(const uint8_t *src, size_t size, uint16_t width, uint16_t height,
		       fn_print_t *fn_print, fn_conv_t *fn_conv, int bitspp, int npix)
{
	size_t pitch = width * bitspp / BITS_PER_BYTE;
	uint8_t nbytes = npix * bitspp  / BITS_PER_BYTE;

	for (size_t i = 0, h = 0; h + 2 <= height; h += 2) {
		for (size_t w = 0; w + npix <= width; w += npix, i += nbytes) {
			uint8_t rgb24a[3 * 2], rgb24b[3 * 2];

			assert(npix <= 2);

			if (i + pitch + 2 > size) {
				mpix_port_printf("\e[m *** early end of buffer***\n");
				return;
			}

			fn_conv(&src[i + pitch * 0], rgb24a, npix);
			fn_conv(&src[i + pitch * 1], rgb24b, npix);

			for (int n = 0; n < npix; n++) {
				fn_print(&rgb24a[n * 3], &rgb24b[n * 3]);
			}
		}
		mpix_port_printf("\e[m|\n");

		/* Skip the second h being printed at the same time */
		i += pitch;
	}
}

static void mpix_print_buf(const uint8_t *buffer, size_t size, uint16_t width, uint16_t height,
			   uint32_t fourcc, fn_print_t *fn)
{
	switch (fourcc) {
	case MPIX_FMT_RGB24:
		mpix_print(buffer, size, width, height, fn, mpix_convert_rgb24_to_rgb24,
			    mpix_bits_per_pixel(fourcc), 1);
		break;
	case MPIX_FMT_RGB565:
		mpix_print(buffer, size, width, height, fn, mpix_convert_rgb565le_to_rgb24,
			    mpix_bits_per_pixel(fourcc), 1);
		break;
	case MPIX_FMT_RGB565X:
		mpix_print(buffer, size, width, height, fn, mpix_convert_rgb565be_to_rgb24,
			    mpix_bits_per_pixel(fourcc), 1);
		break;
	case MPIX_FMT_RGB332:
		mpix_print(buffer, size, width, height, fn, mpix_convert_rgb332_to_rgb24,
			    mpix_bits_per_pixel(fourcc), 1);
		break;
	case MPIX_FMT_YUYV:
		mpix_print(buffer, size, width, height, fn, mpix_convert_yuyv_to_rgb24_bt709,
			    mpix_bits_per_pixel(fourcc), 2);
		break;
	case MPIX_FMT_YUV24:
		mpix_print(buffer, size, width, height, fn, mpix_convert_yuv24_to_rgb24_bt709,
			    mpix_bits_per_pixel(fourcc), 1);
		break;
	case MPIX_FMT_SRGGB8:
	case MPIX_FMT_SBGGR8:
	case MPIX_FMT_SGBRG8:
	case MPIX_FMT_SGRBG8:
	case MPIX_FMT_GREY:
		mpix_print(buffer, size, width, height, fn, mpix_convert_y8_to_rgb24_bt709,
			   mpix_bits_per_pixel(fourcc), 1);
		break;
	default:
		mpix_port_printf("Printing %s buffers not supported\n", MPIX_FOURCC_TO_STR(fourcc));
	}
}

void mpix_print_buf_truecolor(const uint8_t *buffer, size_t size, uint16_t width,
			      uint16_t height, uint32_t fourcc)
{
	mpix_print_buf(buffer, size, width, height, fourcc, mpix_print_truecolor);
}

void mpix_print_buf_256color(const uint8_t *buffer, size_t size, uint16_t width,
				  uint16_t height, uint32_t fourcc)
{
	mpix_print_buf(buffer, size, width, height, fourcc, mpix_print_256color);
}

void mpix_image_print_truecolor(struct mpix_image *img)
{
	mpix_print_buf_truecolor(img->buffer, img->size, img->width, img->height, img->fourcc);
}

void mpix_image_print_256color(struct mpix_image *img)
{
	mpix_print_buf_256color(img->buffer, img->size, img->width, img->height, img->fourcc);
}

static void mpix_hexdump_raw(const uint8_t *buf, size_t size)
{
	for (size_t i = 0; i < size;) {
		printf("%08zx:", i);
		for (int n = 0; n < 32 && i < size; n++, i++) {
			printf(" %02x", buf[i]);
		}

		printf("\n");
	}
}

static void mpix_hexdump_raw8(const uint8_t *raw8, size_t size, uint16_t width, uint16_t height)
{
	for (uint16_t h = 0; h < height; h++) {
		for (uint16_t w = 0; w < width; w++) {
			size_t i = h * width * 1 + w * 1;

			if (i >= size) {
				mpix_port_printf("\e[m *** early end of buffer at %zu bytes ***\n",
					    size);
				return;
			}

			mpix_port_printf(" %02x", raw8[i]);
		}
		mpix_port_printf(" row%u\n", h);
	}
}

static void mpix_hexdump_rgb24(const uint8_t *rgb24, size_t size, uint16_t width, uint16_t height)
{
	mpix_port_printf(" ");
	for (uint16_t w = 0; w < width; w++) {
		mpix_port_printf("col%-7u", w);
	}
	mpix_port_printf("\n");

	for (uint16_t w = 0; w < width; w++) {
		mpix_port_printf(" R  G  B  ");
	}
	mpix_port_printf("\n");

	for (uint16_t h = 0; h < height; h++) {
		for (uint16_t w = 0; w < width; w++) {
			size_t i = h * width * 3 + w * 3;

			if (i + 2 >= size) {
				mpix_port_printf("\e[m *** early end of buffer at %zu bytes ***\n",
					    size);
				return;
			}

			mpix_port_printf(" %02x %02x %02x ", rgb24[i + 0], rgb24[i + 1], rgb24[i + 2]);
		}
		mpix_port_printf(" row%u\n", h);
	}
}

static void mpix_hexdump_rgb565(const uint8_t *rgb565, size_t size, uint16_t width, uint16_t height)
{
	mpix_port_printf(" ");
	for (uint16_t w = 0; w < width; w++) {
		mpix_port_printf("col%-4u", w);
	}
	mpix_port_printf("\n");

	for (uint16_t w = 0; w < width; w++) {
		mpix_port_printf(" RGB565");
	}
	mpix_port_printf("\n");

	for (uint16_t h = 0; h < height; h++) {
		for (uint16_t w = 0; w < width; w++) {
			size_t i = h * width * 2 + w * 2;

			if (i + 1 >= size) {
				mpix_port_printf("\e[m *** early end of buffer at %zu bytes ***\n",
					    size);
				return;
			}

			mpix_port_printf(" %02x %02x ", rgb565[i + 0], rgb565[i + 1]);
		}
		mpix_port_printf(" row%u\n", h);
	}
}

static void mpix_hexdump_yuyv(const uint8_t *yuyv, size_t size, uint16_t width, uint16_t height)
{
	mpix_port_printf(" ");
	for (uint16_t w = 0; w < width; w++) {
		mpix_port_printf("col%-3u", w);
		if ((w + 1) % 2 == 0) {
			mpix_port_printf(" ");
		}
	}
	mpix_port_printf("\n");

	for (uint16_t w = 0; w < width; w++) {
		mpix_port_printf(" %c%u", "YUYV"[w % 2 * 2 + 0], w % 2);
		mpix_port_printf(" %c%u", "YUYV"[w % 2 * 2 + 1], w % 2);
		if ((w + 1) % 2 == 0) {
			mpix_port_printf(" ");
		}
	}
	mpix_port_printf("\n");

	for (uint16_t h = 0; h < height; h++) {
		for (uint16_t w = 0; w < width; w++) {
			size_t i = h * width * 2 + w * 2;

			if (i + 1 >= size) {
				mpix_port_printf("\e[m *** early end of buffer at %zu bytes ***\n",
					    size);
				return;
			}

			mpix_port_printf(" %02x %02x", yuyv[i], yuyv[i + 1]);
			if ((w + 1) % 2 == 0) {
				mpix_port_printf(" ");
			}
		}
		mpix_port_printf(" row%u\n", h);
	}
}

void mpix_hexdump(const uint8_t *buf, size_t size, uint16_t width, uint16_t height,
		  uint32_t fourcc)
{
	switch (fourcc) {
	case MPIX_FMT_YUYV:
		mpix_hexdump_yuyv(buf, size, width, height);
		break;
	case MPIX_FMT_RGB24:
		mpix_hexdump_rgb24(buf, size, width, height);
		break;
	case MPIX_FMT_RGB565:
		mpix_hexdump_rgb565(buf, size, width, height);
		break;
	case MPIX_FMT_SBGGR8:
	case MPIX_FMT_SRGGB8:
	case MPIX_FMT_SGRBG8:
	case MPIX_FMT_SGBRG8:
	case MPIX_FMT_GREY:
		mpix_hexdump_raw8(buf, size, width, height);
		break;
	default:
		mpix_hexdump_raw(buf, size);
		break;
	}
}

static void mpix_print_hist_scale(size_t size)
{
	for (uint16_t i = 0; i < size; i++) {
		mpix_print_256gray(0, i * 256 / size);
	}
	mpix_port_printf("\e[m\n");
}

void mpix_print_rgb_hist(const uint16_t *r_hist, const uint16_t *g_hist, const uint16_t *b_hist,
			 size_t size, uint16_t height)
{
	uint32_t max = 1;

	for (size_t i = 0; i < size; i++) {
		max = r_hist[i] > max ? r_hist[i] : max;
		max = g_hist[i] > max ? g_hist[i] : max;
		max = b_hist[i] > max ? b_hist[i] : max;
	}

	for (uint16_t h = height; h > 1; h--) {
		for (size_t i = 0; i < size / 3; i++) {
			uint8_t row0[3];
			uint8_t row1[3];

			row0[0] = (r_hist[i] * height / max > h - 0) ? 0xff : 0x00;
			row0[1] = (g_hist[i] * height / max > h - 0) ? 0xff : 0x00;
			row0[2] = (b_hist[i] * height / max > h - 0) ? 0xff : 0x00;
			row1[0] = (r_hist[i] * height / max > h - 1) ? 0xff : 0x00;
			row1[1] = (g_hist[i] * height / max > h - 1) ? 0xff : 0x00;
			row1[2] = (b_hist[i] * height / max > h - 1) ? 0xff : 0x00;

			mpix_print_256color(row0, row1);
		}
		mpix_port_printf("\e[m| - %u\n", h * max / height);
	}

	mpix_print_hist_scale(size / 3);
}

void mpix_print_y_hist(const uint16_t *y_hist, size_t y_hist_sz, uint16_t height)
{
	static const char *const bar_chars[] = {" ", "▁", "▂", "▃", "▄", "▅", "▆", "▇", "█"};
	uint32_t max = 1;

	for (size_t i = 0; i < y_hist_sz; i++) {
		max = y_hist[i] > max ? y_hist[i] : max;
	}

	for (uint16_t h = height * 8; h >= 8; h -= 8) {
		for (size_t i = 0; i < y_hist_sz; i++) {
			uint16_t bar_height = height * 8 * y_hist[i] / max;

			mpix_port_printf(bar_chars[CLAMP(1 + bar_height - h, 0, 8)]);
		}
		mpix_port_printf("| - %u\n", h * max / height);
	}

	/* This makes the graph look more intuitive, but reduces print speed on slow UARTs */
	mpix_print_hist_scale(y_hist_sz);
}

void mpix_image_print_ops(struct mpix_image *img)
{
	if (!img->flag_print_ops) {
		return;
	}

	for (struct mpix_base_op *op = img->ops.first; op != NULL; op = op->next) {
		mpix_port_printf(
			" %-30s %ux%u %s -> %s, window %u lines, threshold %u bytes, "
			"runtime %u us\n",
			op->name, op->width, op->height, MPIX_FOURCC_TO_STR(op->fourcc_src),
			MPIX_FOURCC_TO_STR(op->fourcc_dst), op->window_size, op->threshold,
			op->total_time_us);
	}
}
