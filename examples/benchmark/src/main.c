#include <mpix/image.h>
#include <mpix/low_level.h>
#include <mpix/port.h>
#include <mpix/print.h>
#include <mpix/types.h>
#include <mpix/utils.h>
#include <mpix/stats.h>

#ifndef CONFIG_WIDTH
#define CONFIG_WIDTH 320
#endif

#ifndef CONFIG_HEIGHT
#define CONFIG_HEIGHT 240
#endif

#define WIDTH  CONFIG_WIDTH
#define HEIGHT CONFIG_HEIGHT

#define NUM_OPS_RUN 3

#define CHECK(x)                                                                                   \
	({                                                                                         \
		int e = (x);                                                                       \
		if (e) {                                                                           \
			fprintf(stderr, "%s: %s\n", #x, strerror(-e));                             \
			return e;                                                                  \
		}                                                                                  \
	})

#define BENCHMARK_DESC_AUTO(_name, _type, _fourcc, ...)                                            \
	(struct benchmark_desc)                                                                    \
	{                                                                                          \
		.name = (_name), .type = (_type), .params = (const int32_t[]){__VA_ARGS__},        \
		.params_nb = ((size_t)(sizeof((const int32_t[]){__VA_ARGS__}) / sizeof(int32_t))), \
		.fourcc = (_fourcc),                                                               \
	}

enum {R, G, B};

struct benchmark_desc {
	const char *name;
	enum mpix_op_type type;
	const int32_t *params;
	size_t params_nb;
	uint32_t fourcc;
};

uint8_t srcbuf[WIDTH * HEIGHT * 3];
uint8_t dstbuf[WIDTH * HEIGHT * 3];

static const struct benchmark_desc bench_tests[] = {
	/* correction */
	BENCHMARK_DESC_AUTO("correct_white_balance", MPIX_OP_CORRECT_WHITE_BALANCE, MPIX_FMT_RGB24),
	BENCHMARK_DESC_AUTO("correct_black_level", MPIX_OP_CORRECT_BLACK_LEVEL, MPIX_FMT_RGB24),
	BENCHMARK_DESC_AUTO("correct_gamma", MPIX_OP_CORRECT_GAMMA, MPIX_FMT_RGB24),
	BENCHMARK_DESC_AUTO("correct_combined_ccm", MPIX_OP_CORRECT_COLOR_MATRIX, MPIX_FMT_RGB24),

	/* kernel */
	BENCHMARK_DESC_AUTO("kernel_identity_3x3", MPIX_OP_KERNEL_CONVOLVE_3X3, MPIX_FMT_RGB24,
			    MPIX_KERNEL_IDENTITY),
	BENCHMARK_DESC_AUTO("kernel_identity_5x5", MPIX_OP_KERNEL_CONVOLVE_5X5, MPIX_FMT_RGB24,
			    MPIX_KERNEL_IDENTITY),
	BENCHMARK_DESC_AUTO("kernel_sharpen_3x3", MPIX_OP_KERNEL_CONVOLVE_3X3, MPIX_FMT_RGB24,
			    MPIX_KERNEL_SHARPEN),
	BENCHMARK_DESC_AUTO("kernel_sharpen_5x5", MPIX_OP_KERNEL_CONVOLVE_5X5, MPIX_FMT_RGB24,
			    MPIX_KERNEL_SHARPEN),
	BENCHMARK_DESC_AUTO("kernel_edgedetect_3x3", MPIX_OP_KERNEL_CONVOLVE_3X3, MPIX_FMT_RGB24,
			    MPIX_KERNEL_EDGE_DETECT),
	BENCHMARK_DESC_AUTO("kernel_edgedetect_5x5", MPIX_OP_KERNEL_CONVOLVE_5X5, MPIX_FMT_RGB24,
			    MPIX_KERNEL_EDGE_DETECT),
	BENCHMARK_DESC_AUTO("kernel_gaussianblur_3x3", MPIX_OP_KERNEL_CONVOLVE_3X3, MPIX_FMT_RGB24,
			    MPIX_KERNEL_GAUSSIAN_BLUR),
	BENCHMARK_DESC_AUTO("kernel_gaussianblur_5x5", MPIX_OP_KERNEL_CONVOLVE_5X5, MPIX_FMT_RGB24,
			    MPIX_KERNEL_GAUSSIAN_BLUR),
	BENCHMARK_DESC_AUTO("kernel_denoise_3x3", MPIX_OP_KERNEL_DENOISE_3X3, MPIX_FMT_RGB24),
	BENCHMARK_DESC_AUTO("kernel_denoise_5x5", MPIX_OP_KERNEL_DENOISE_5X5, MPIX_FMT_RGB24),

	/* resize */
	BENCHMARK_DESC_AUTO("resize_subsample", MPIX_OP_RESIZE_SUBSAMPLE, MPIX_FMT_RGB24, WIDTH,
			    HEIGHT),

	/* image encoding */
	BENCHMARK_DESC_AUTO("qoi_encode", MPIX_OP_QOI_ENCODE, MPIX_FMT_RGB24),
	BENCHMARK_DESC_AUTO("jpeg_encode", MPIX_OP_JPEG_ENCODE, MPIX_FMT_RGB24),

	/* format conversion */
	BENCHMARK_DESC_AUTO("rgb24->rgb24", MPIX_OP_CONVERT, MPIX_FMT_RGB24, MPIX_FMT_RGB24),
	BENCHMARK_DESC_AUTO("rgb24->rgb332", MPIX_OP_CONVERT, MPIX_FMT_RGB24, MPIX_FMT_RGB332),
	BENCHMARK_DESC_AUTO("rgb24->rgb565be", MPIX_OP_CONVERT, MPIX_FMT_RGB24, MPIX_FMT_RGB565X),
	BENCHMARK_DESC_AUTO("rgb24->rgb565le", MPIX_OP_CONVERT, MPIX_FMT_RGB24, MPIX_FMT_RGB565),
	BENCHMARK_DESC_AUTO("rgb24->yuv24", MPIX_OP_CONVERT, MPIX_FMT_RGB24, MPIX_FMT_YUV24),
	BENCHMARK_DESC_AUTO("rgb332->rgb24", MPIX_OP_CONVERT, MPIX_FMT_RGB332, MPIX_FMT_RGB24),
	BENCHMARK_DESC_AUTO("rgb24->yuyv", MPIX_OP_CONVERT, MPIX_FMT_RGB24, MPIX_FMT_YUYV),
	BENCHMARK_DESC_AUTO("rgb565le->rgb24", MPIX_OP_CONVERT, MPIX_FMT_RGB565, MPIX_FMT_RGB24),
	BENCHMARK_DESC_AUTO("rgb565be->rgb24", MPIX_OP_CONVERT, MPIX_FMT_RGB565X, MPIX_FMT_RGB24),
	BENCHMARK_DESC_AUTO("y8->rgb24", MPIX_OP_CONVERT, MPIX_FMT_GREY, MPIX_FMT_RGB24),
	BENCHMARK_DESC_AUTO("yuv24->rgb24", MPIX_OP_CONVERT, MPIX_FMT_YUV24, MPIX_FMT_RGB24),
	BENCHMARK_DESC_AUTO("yuv24->yuyv", MPIX_OP_CONVERT, MPIX_FMT_YUV24, MPIX_FMT_YUYV),
	BENCHMARK_DESC_AUTO("yuyv->rgb24", MPIX_OP_CONVERT, MPIX_FMT_YUV24, MPIX_FMT_RGB24),
	BENCHMARK_DESC_AUTO("yuyv->yuv24", MPIX_OP_CONVERT, MPIX_FMT_YUYV, MPIX_FMT_YUV24),

	/* debayer */
	BENCHMARK_DESC_AUTO("debayer_3x3", MPIX_OP_DEBAYER_3X3, MPIX_FMT_SRGGB8),
	BENCHMARK_DESC_AUTO("debayer_2x2", MPIX_OP_DEBAYER_2X2, MPIX_FMT_SRGGB8),
	BENCHMARK_DESC_AUTO("debayer_1x1", MPIX_OP_DEBAYER_1X1, MPIX_FMT_SRGGB8),
};

static void print_benchmark(struct mpix_base_op *head)
{
	char *name;
	if (head == NULL) {
		return;
	}

	mpix_port_printf("\noperation,resolution,format,used bytes,time\n");
	for (struct mpix_base_op *op = head; op != NULL; op = op->next) {
		switch (op->type) {
#undef MPIX_CASE_PRINT_OP
#define MPIX_CASE_PRINT_OP(X, x)                                                                   \
	case MPIX_OP_##X:                                                                          \
		name = #X;                                                                         \
		break;
MPIX_FOR_EACH_OP(MPIX_CASE_PRINT_OP)
		case MPIX_OP_END:
			name = "END";
			break;
		default:
			name = "UNKNOWN";
			break;
		}

		mpix_port_printf("%s,%ux%u,%s,%zu,%u\n", name, op->fmt.width, op->fmt.height,
				 MPIX_FOURCC_TO_STR(op->fmt.fourcc), mpix_ring_used_size(&op->ring),
				 op->total_time_us);
	}
}

int benchmark_palettize(void)
{
	struct mpix_format fmt = {.width = WIDTH, .height = HEIGHT, .fourcc = MPIX_FMT_RGB24};
	const int buf_size = mpix_format_pitch(&fmt) * fmt.height;

	/* Generate pseudo-random input data */
	for (int i = 0; i < fmt.height * fmt.width; ++i) {
		srcbuf[3 * i + R] = srcbuf[3 * i + G] = srcbuf[3 * i + B] = i * 9 + 17;
	}

	int depths[4] = {8, 4, 2, 1};

	for (int di = 0; di < 4; ++di) {
		int depth = depths[di];
		struct mpix_palette pal = {
			.fourcc = mpix_palette_fourcc(depth),
		};

		/* Generate a gray gradient initial palette */
		int n = 1 << depth;
		for (int i = 0; i < n; i++) {
			uint8_t v = (uint8_t)(i * 255 / (n - 1));
			pal.colors_rgb24[i] = RGB32(v, v, v);
		}

		struct mpix_image img = {};
		mpix_image_from_buf(&img, srcbuf, buf_size, &fmt);
		CHECK(mpix_image_palette_encode(&img, pal.fourcc));
		CHECK(mpix_image_palette_decode(&img));
		CHECK(mpix_image_set_palette(&img, &pal));
		CHECK(mpix_image_to_buf(&img, dstbuf, sizeof(dstbuf)));
		print_benchmark(img.first_op);
		mpix_image_free(&img);
	}
	return 0;
}

int benchmark_operation(enum mpix_op_type type, const int32_t *params, size_t params_nb,
			uint32_t format)
{
	struct mpix_image img = {};
	struct mpix_format fmt = {.width = WIDTH, .height = HEIGHT, .fourcc = format};
	size_t img_size = mpix_format_pitch(&fmt) * fmt.height, outbuf_size;

	mpix_image_from_buf(&img, srcbuf, img_size, &fmt);
	CHECK(mpix_pipeline_add(&img, type, params, params_nb));
	outbuf_size = mpix_format_pitch(&img.fmt) * img.fmt.height;
	/* for compressed images */
	if (outbuf_size == 0) {
		outbuf_size = sizeof(dstbuf);
	}
	CHECK(mpix_image_to_buf(&img, dstbuf, outbuf_size));
	print_benchmark(img.first_op);
	mpix_image_free(&img);
	return 0;
}

int main(void)
{
	size_t i, j;

	for (j = 0; j < NUM_OPS_RUN; ++j) {
		benchmark_palettize();
	}

	for (i = 0; i < ARRAY_SIZE(bench_tests); i++) {
		const struct benchmark_desc *t = &bench_tests[i];
		for (j = 0; j < NUM_OPS_RUN; ++j) {
			benchmark_operation(t->type, t->params, t->params_nb, t->fourcc);
		}
	}

	mpix_port_printf("Benchmark completed\n");
	return 0;
}
