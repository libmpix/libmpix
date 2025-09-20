/* SPDX-License-Identifier: Apache-2.0 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <assert.h>

#include <mpix/genlist.h>
#include <mpix/image.h>
#include <mpix/low_level.h>
#include <mpix/print.h>
#include <mpix/ring.h>
#include <mpix/utils.h>

static uint8_t mpix_rgb24_to_256color(const uint8_t rgb24[3])
{
	return 16 + rgb24[0] * 6 / 256 * 36 + rgb24[1] * 6 / 256 * 6 + rgb24[2] * 6 / 256 * 1;
}

static uint8_t mpix_gray8_to_256color(uint8_t gray8)
{
	return 232 + gray8 * 24 / 256;
}

void mpix_print_2_rows_truecolor(const uint8_t *top, const uint8_t *bot, size_t width)
{
	for (uint16_t w = 0; w < width; w++) {
		printf("\e[48;2;%u;%u;%um\e[38;2;%u;%u;%um▄",
			top[w * 3 + 0], top[w * 3 + 1], top[w * 3 + 2],
			bot[w * 3 + 0], bot[w * 3 + 1], bot[w * 3 + 2]);
	}
}

void mpix_print_2_rows_256color(const uint8_t *top, const uint8_t *bot, size_t width)
{
	for (uint16_t w = 0; w < width; w++) {
		printf("\e[48;5;%um\e[38;5;%um▄",
			mpix_rgb24_to_256color(&top[w * 3]),
			mpix_rgb24_to_256color(&bot[w * 3]));
	}
}

void mpix_print_2_rows_256gray(const uint8_t *top, const uint8_t *bot, size_t width)
{
	for (uint16_t w = 0; w < width; w++) {
		printf("\e[48;5;%um\e[38;5;%um▄",
			mpix_gray8_to_256color(top[w]),
			mpix_gray8_to_256color(bot[w]));
	}
}

static void mpix_print_2x2(const uint8_t *top_1x2, const uint8_t *bot_1x2, uint32_t fourcc,
			   bool truecolor)
{
	uint8_t top_rgb[2 * 3];
	uint8_t bot_rgb[2 * 3];

	switch (fourcc) {
	case MPIX_FMT_RGB24:
		mpix_convert_rgb24_to_rgb24(top_1x2, top_rgb, 2);
		mpix_convert_rgb24_to_rgb24(bot_1x2, bot_rgb, 2);
		break;
	case MPIX_FMT_RGB565:
		mpix_convert_rgb565le_to_rgb24(top_1x2, top_rgb, 2);
		mpix_convert_rgb565le_to_rgb24(bot_1x2, bot_rgb, 2);
		break;
	case MPIX_FMT_RGB565X:
		mpix_convert_rgb565be_to_rgb24(top_1x2, top_rgb, 2);
		mpix_convert_rgb565be_to_rgb24(bot_1x2, bot_rgb, 2);
		break;
	case MPIX_FMT_RGB332:
		mpix_convert_rgb332_to_rgb24(top_1x2, top_rgb, 2);
		mpix_convert_rgb332_to_rgb24(bot_1x2, bot_rgb, 2);
		break;
	case MPIX_FMT_YUYV:
		mpix_convert_yuyv_to_rgb24_bt709(top_1x2, top_rgb, 2);
		mpix_convert_yuyv_to_rgb24_bt709(bot_1x2, bot_rgb, 2);
		break;
	case MPIX_FMT_YUV24:
		mpix_convert_yuv24_to_rgb24_bt709(top_1x2, top_rgb, 2);
		mpix_convert_yuv24_to_rgb24_bt709(bot_1x2, bot_rgb, 2);
		break;
	case MPIX_FMT_SRGGB8:
	case MPIX_FMT_SBGGR8:
	case MPIX_FMT_SGBRG8:
	case MPIX_FMT_SGRBG8:
	case MPIX_FMT_GREY:
		mpix_print_2_rows_256gray(top_1x2, bot_1x2, 2);
		return;
	default:
		printf("??");
		return;
	}

	if (truecolor) {
		mpix_print_2_rows_256color(top_rgb, bot_rgb, 2);
	} else {
		mpix_print_2_rows_truecolor(top_rgb, bot_rgb, 2);
	}
}

void mpix_print_2_rows(const uint8_t *top, const uint8_t *bot, int16_t width, uint32_t fourcc,
		       bool truecolor)
{
	uint8_t bytespp = mpix_bits_per_pixel(fourcc) / BITS_PER_BYTE;

	for (uint16_t w = 0; w + 2 <= width; w += 2) {
		mpix_print_2x2(&top[w * bytespp], &bot[w * bytespp], fourcc, truecolor);
	}
	printf("\e[m");
}

void mpix_print_buf(const uint8_t *src, size_t size, const struct mpix_format *fmt, bool truecolor)
{
	size_t pitch = mpix_format_pitch(fmt);
	uint8_t bitspp = mpix_bits_per_pixel(fmt->fourcc);
	size_t curr_size = 0;

	for (size_t h = 0; h + 2 <= fmt->height && curr_size < size; h += 2) {
		const uint8_t *top = &src[(h + 0) * pitch];
		const uint8_t *bot = &src[(h + 1) * pitch];;
		size_t next_size = MIN(size, (h + 2) * pitch);
		size_t this_width = (next_size - curr_size) * BITS_PER_BYTE / bitspp / 2;

		mpix_print_2_rows(top, bot, this_width, fmt->fourcc, truecolor);
		printf("\e[m│\n");
		curr_size = next_size;
	}
}

void mpix_hexdump_raw(const uint8_t *buf, size_t size)
{
	for (size_t i = 0; i < size; i++) {
		printf(" %02x", buf[i]);
	}
	printf("\n");
}

static void mpix_hexdump_raw8(const uint8_t *raw8, size_t size, uint16_t width, uint16_t height)
{
	for (uint16_t h = 0; h < height; h++) {
		for (uint16_t w = 0; w < width; w++) {
			size_t i = h * width * 1 + w * 1;

			if (i >= size) {
				printf("\e[m *** end of buffer at byte %zu ***\n", i);
				return;
			}

			printf(" %02x", raw8[i]);
		}
		printf(" row%u\n", h);
	}
}

static void mpix_hexdump_rgb24(const uint8_t *rgb24, size_t size, uint16_t width, uint16_t height)
{
	printf(" ");
	for (uint16_t w = 0; w < width; w++) {
		printf("col%-7u", w);
	}
	printf("\n");

	for (uint16_t w = 0; w < width; w++) {
		printf(" R  G  B  ");
	}
	printf("\n");

	for (uint16_t h = 0; h < height; h++) {
		for (uint16_t w = 0; w < width; w++) {
			size_t i = h * width * 3 + w * 3;

			if (i + 2 >= size) {
				printf("\e[m *** end of buffer at byte %zu ***\n", i);
				return;
			}

			printf(" %02x %02x %02x ", rgb24[i + 0], rgb24[i + 1], rgb24[i + 2]);
		}
		printf(" row%u\n", h);
	}
}

static void mpix_hexdump_rgb565(const uint8_t *rgb565, size_t size, uint16_t width, uint16_t height)
{
	printf(" ");
	for (uint16_t w = 0; w < width; w++) {
		printf("col%-4u", w);
	}
	printf("\n");

	for (uint16_t w = 0; w < width; w++) {
		printf(" RGB565");
	}
	printf("\n");

	for (uint16_t h = 0; h < height; h++) {
		for (uint16_t w = 0; w < width; w++) {
			size_t i = h * width * 2 + w * 2;

			if (i + 1 >= size) {
				printf("\e[m *** end of buffer at byte %zu ***\n", i);
				return;
			}

			printf(" %02x %02x ", rgb565[i + 0], rgb565[i + 1]);
		}
		printf(" row%u\n", h);
	}
}

static void mpix_hexdump_yuyv(const uint8_t *yuyv, size_t size, uint16_t width, uint16_t height)
{
	printf(" ");
	for (uint16_t w = 0; w < width; w++) {
		printf("col%-3u", w);
		if ((w + 1) % 2 == 0) {
			printf(" ");
		}
	}
	printf("\n");

	for (uint16_t w = 0; w < width; w++) {
		printf(" %c%u", "YUYV"[w % 2 * 2 + 0], w % 2);
		printf(" %c%u", "YUYV"[w % 2 * 2 + 1], w % 2);
		if ((w + 1) % 2 == 0) {
			printf(" ");
		}
	}
	printf("\n");

	for (uint16_t h = 0; h < height; h++) {
		for (uint16_t w = 0; w < width; w++) {
			size_t i = h * width * 2 + w * 2;

			if (i + 1 >= size) {
				printf("\e[m *** end of buffer at byte %zu ***\n", i);
				return;
			}

			printf(" %02x %02x", yuyv[i], yuyv[i + 1]);
			if ((w + 1) % 2 == 0) {
				printf(" ");
			}
		}
		printf(" row%u\n", h);
	}
}

void mpix_hexdump_buf(const uint8_t *buf, size_t size, const struct mpix_format *fmt)
{
	switch (fmt->fourcc) {
	case MPIX_FMT_YUYV:
		mpix_hexdump_yuyv(buf, size, fmt->width, fmt->height);
		break;
	case MPIX_FMT_RGB24:
		mpix_hexdump_rgb24(buf, size, fmt->width, fmt->height);
		break;
	case MPIX_FMT_RGB565:
		mpix_hexdump_rgb565(buf, size, fmt->width, fmt->height);
		break;
	case MPIX_FMT_SBGGR8:
	case MPIX_FMT_SRGGB8:
	case MPIX_FMT_SGRBG8:
	case MPIX_FMT_SGBRG8:
	case MPIX_FMT_GREY:
		mpix_hexdump_raw8(buf, size, fmt->width, fmt->height);
		break;
	default:
		mpix_hexdump_raw(buf, size);
		break;
	}
}

static void mpix_print_hist_scale(size_t size)
{
	for (uint16_t i = 0; i < size; i++) {
		uint8_t black[1] = { 0x00 };
		uint8_t scale[1] = { i * 256 / size };
		mpix_print_2_rows_256gray(black, scale, 1);
	}
	printf("\e[m\n");
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

			row0[0] = (r_hist[i] * height / max + 0 > h) ? 0xff : 0x00;
			row0[1] = (g_hist[i] * height / max + 0 > h) ? 0xff : 0x00;
			row0[2] = (b_hist[i] * height / max + 0 > h) ? 0xff : 0x00;

			row1[0] = (r_hist[i] * height / max + 1 > h) ? 0xff : 0x00;
			row1[1] = (g_hist[i] * height / max + 1 > h) ? 0xff : 0x00;
			row1[2] = (b_hist[i] * height / max + 1 > h) ? 0xff : 0x00;

			mpix_print_2_rows_256color(row0, row1, 1);
		}
		printf("\e[m| - %u\n", h * max / height);
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

			printf(bar_chars[CLAMP(1 + bar_height - h, 0, 8)]);
		}
		printf("| - %u\n", h * max / height);
	}

	/* This makes the graph look more intuitive, but reduces print speed on slow UARTs */
	mpix_print_hist_scale(y_hist_sz);
}

void mpix_print_op(struct mpix_base_op *op)
{
	char *name;

	switch (op->type) {
#define MPIX_CASE_PRINT_OP(X, x) \
	case MPIX_OP_##X: name = #X; break;
MPIX_FOR_EACH_OP(MPIX_CASE_PRINT_OP)
	case MPIX_OP_INVAL: name = "INVAL"; break;
	case MPIX_OP_END: name = "END"; break;
	default: name = "UNKNOWN"; break;
	}

	printf("[op] %-24s %4ux%-4u %s %8zu bytes / %-8zu line %-4u runtime %u us\n",
		name, op->fmt.width, op->fmt.height, MPIX_FOURCC_TO_STR(op->fmt.fourcc),
		mpix_ring_used_size(&op->ring), op->ring.size, op->line_offset, op->total_time_us);
}

void mpix_print_pipeline(struct mpix_base_op *op)
{
	printf("[pipeline]\n");
	for (; op != NULL; op = op->next) {
		mpix_print_op(op);
	}
}

void mpix_print_stats(struct mpix_stats *stats)
{
	uint8_t *rgb = stats->rgb_average;

	printf("Average #%02x%02x%02x ", rgb[0], rgb[1], rgb[2]);
	mpix_print_2_rows_truecolor(rgb, rgb, 1);

	printf(" \x1b[m for %u values sampled\n", stats->nvals);
	mpix_print_y_hist(stats->y_histogram, ARRAY_SIZE(stats->y_histogram), 10);
}

void mpix_print_ctrls(int32_t *ctrls[])
{
	for (size_t i = 0; i < MPIX_NB_CID; i++) {
		if (ctrls[i] != NULL) {
			printf("[ctrl] %s = %d\n", mpix_str_cid[i], *ctrls[i]);
		}
	}
}
