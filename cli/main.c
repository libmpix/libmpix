#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

#include <mpix/formats.h>
#include <mpix/image.h>
#include <mpix/utils.h>
#include <mpix/str.h>

/* Image used through the processing */
struct mpix_image img;

/* Input bufferto be freed at the end of the processing */
uint8_t *src_buf;

/* Color palette for palette-based operations */
uint8_t palette_colors[256 * 3];
struct mpix_palette palette = {
	.colors = palette_colors,
};

/* Global flags */
bool flag_verbose;

static int str_get_value(const struct mpix_str *table, const char *name, uint32_t *val)
{
	int ret;

	ret = mpix_str_get_value(table, name, val);
	if (ret != 0) {
		MPIX_ERR("Unrecognized argument '%s', available:", name);
		for (size_t i = 0; table[i].name != NULL; i++) {
			MPIX_ERR("- %s", table[i].name);
		}
	}

	return ret;
}

static int cmd_read(int argc, char **argv)
{
	uint32_t fourcc = 0;
	uint16_t width = 0;
	uint16_t pitch = 0;
	uint16_t height = 0;
	long filesize;
	FILE *fp;
	size_t sz;
	char *arg;
	char *end;
	long long n;
	int ret;

	if (argc < 2) {
		MPIX_ERR("Usage: %s <filename>", argv[0]);
		return -EINVAL;
	}

	fp = fopen(argv[1], "r");
	if (fp == NULL) {
		MPIX_ERR("Failed to open '%s'", argv[1]);
		return -errno;
	}

	ret = fseek(fp, 0, SEEK_END);
	if (ret < 0) {
		MPIX_ERR("Failed to estimate '%s' file size", argv[1]);
		return -errno;
	}

	filesize = ftell(fp);
	if (filesize < 0) {
		MPIX_ERR("Failed to estimate '%s' file size", argv[1]);
		return -errno;
	}

	if (argc == 2) {
		char *ext;

		ext = strrchr(argv[1], '.');
		if (ext == NULL || strlen(ext) == 0) {
			MPIX_ERR("Could not parse extension: '%s'", argv[1]);
			return -EINVAL;
		}

		if (strcasecmp(ext, ".qoi") == 0) {
			fourcc = MPIX_FMT_QOI;
		} else {
			MPIX_ERR("Unsupported file extension: '%s'", ext);
			return -EINVAL;
		}

	} else if (argc == 4) {
		arg = argv[2];

		n = strtoll(arg, &end, 10);
		if (*arg == '\0' || *end != '\0' || n < 0 || n > UINT16_MAX) {
			MPIX_ERR("Invalid <width> '%s'", arg);
			return -EINVAL;
		}
		width = n;

		ret = str_get_value(mpix_str_fmt, argv[3], &fourcc);
		if (ret != 0) {
			return ret;
		}

		/* User has provided the width and we know the format to compute the height */
		pitch = width * mpix_bits_per_pixel(fourcc) / BITS_PER_BYTE;
		height = filesize / pitch;
		if (height < 1 || filesize / pitch > UINT16_MAX) {
			MPIX_ERR("Invalid width:%d provided, filesize:%llu does not match",
				 width, filesize);
			return -EINVAL;
		}
	} else {
		return -EINVAL;
	}

	ret = fseek(fp, 0, SEEK_SET);
	if (ret < 0) {
		MPIX_ERR("Failed to resume to start of '%s'", argv[1]);
		return -errno;
	}

	src_buf = malloc(filesize);
	if (src_buf == NULL) {
		return -errno;
	}

	sz = fread(src_buf, 1, filesize, fp);
	if (sz != (unsigned)filesize) {
		MPIX_ERR("Read only %u/%u bytes from '%s': %s",
			sz, filesize, argv[1], strerror(errno));
		return -errno;
	}

	fclose(fp);

	assert(fourcc != 0x00);

	mpix_image_from_buf(&img, src_buf, filesize, width, height, fourcc);
	img.flag_print_ops = flag_verbose;

	if (flag_verbose) {
		struct mpix_stats stats = {0};

		mpix_image_stats(&img, &stats);
		mpix_stats_print(&stats);
	}

	return img.err;
}

static int cmd_write(int argc, char **argv)
{
	FILE *fp;
	size_t filesize;
	uint8_t bits;
	uint8_t *dst_buf;
	size_t sz;

	if (argc != 2) {
		return -EINVAL;
	}

	/* For variable pitch formats, assume 16 byte per pixel is enough */
	bits = mpix_bits_per_pixel(img.fourcc);
	bits = MAX(bits, BITS_PER_BYTE * 16);

	/* Estimate the image size */
	filesize = img.width * img.height * bits / BITS_PER_BYTE;
	if (filesize == 0) {
		MPIX_ERR("Invalid image size %ux%u or format");
	}

	dst_buf = malloc(filesize);
	if (dst_buf == NULL) {
		MPIX_ERR("Failed to allocate %u bytes", filesize);
		return -errno;
	}

	mpix_image_to_buf(&img, dst_buf, filesize);
	if (img.err) {
		MPIX_ERR("Failed to convert the image");
		return img.err;
	}

	if (flag_verbose) {
		struct mpix_stats stats = {0};

		mpix_image_stats(&img, &stats);
		mpix_stats_print(&stats);
	}

	free(src_buf);

	fp = fopen(argv[1], "w+");
	if (fp == NULL) {
		MPIX_ERR("Failed to open %s", argv[1]);
		return -errno;
	}

	sz = fwrite(dst_buf, 1, img.size, fp);
	if (sz != img.size) {
		MPIX_ERR("Wrote only %u/%u bytes from '%s': %s",
			sz, filesize, argv[1], strerror(errno));
		return -errno;
	}

	fclose(fp);
	free(dst_buf);

	return 0;
}

static int cmd_qoi_encode(int argc, char **argv)
{
	if (argc != 1) {
		return -EINVAL;
	}

	return mpix_image_qoi_encode(&img);
}

static int cmd_jpeg_encode(int argc, char **argv)
{
	enum mpix_jpeg_quality quality;
	int ret;

	if (argc != 2) {
		return -EINVAL;
	}

	ret = str_get_value(mpix_str_jpeg_quality, argv[1], &quality);
	if (ret != 0) {
		return ret;
	}

	return mpix_image_jpeg_encode(&img, quality);
}

static int cmd_kernel(int argc, char **argv)
{
	enum mpix_kernel_type type;
	unsigned long int size;
	char *arg;
	char *end;
	int ret;

	if (argc != 3) {
		return -EINVAL;
	}

	ret = str_get_value(mpix_str_kernel, argv[1], &type);
	if (ret != 0) {
		return ret;
	}

	arg = argv[2];

	size = strtoul(arg, &end, 10);
	if (*arg != '\0' || (size != 3 && size != 5)) {
		MPIX_ERR("Invalid kernel size %s, must be 3 or 5", argv[2]);
		return -EINVAL;
	}

	return mpix_image_kernel(&img, type, size);
}

static int cmd_convert(int argc, char **argv)
{
	uint32_t fourcc;
	int ret;

	if (argc != 2) {
		return -EINVAL;
	}

	ret = str_get_value(mpix_str_fmt, argv[1], &fourcc);
	if (ret != 0) {
		return ret;
	}

	return mpix_image_convert(&img, fourcc);
}

static int cmd_debayer(int argc, char **argv)
{
	long long size;
	char *arg;
	char *end;

	if (argc != 2) {
		return -EINVAL;
	}

	arg = argv[1];

	size = strtoul(arg, &end, 10);
	if (*arg != '\0' || (size != 1 && size != 2 && size != 3)) {
		MPIX_ERR("Invalid debayer size '%s', must be 1, 2 or 3", argv[1]);
		return -EINVAL;
	}

	return mpix_image_debayer(&img, size);
}

static int cmd_palette(int argc, char **argv)
{
	unsigned int optimization_cycles;
	long long n;
	char *arg;
	char *end;
	int ret;

	if (argc != 3) {
		return -EINVAL;
	}

	arg = argv[1];

	/* Parse bit_depth */

	n = strtoul(arg, &end, 10);
	if (*arg == '\0' || *end != '\0' || n < 1 || n > 8) {
		MPIX_ERR("Invalid <bit_detph> '%s' (min=1, max=8)", arg);
		return -EINVAL;
	}
	palette.fourcc = MPIX_FOURCC('P', 'L', 'T', '0' + n);

	/* Parse optimization_cycles */

	arg = argv[2];

	n = strtoll(arg, &end, 10);
	if (*arg == '\0' || *end != '\0' || n < 0 || n > 1000) {
		MPIX_ERR("Invalid number of <optimization_cycles> '%s' (min=1, max=1000)", arg);
		return -EINVAL;
	}
	optimization_cycles = n;

	for (unsigned int i = 0; i < optimization_cycles; i++) {
		ret = mpix_image_optimize_palette(&img, &palette, img.width + img.height);
		if (ret != 0) {
			return ret;
		}
	}

	return 0;
}

static int cmd_palettize(int argc, char **argv)
{
	if (argc != 1) {
		return -EINVAL;
	}

	if (palette.fourcc == 0) {
		MPIX_ERR("You must create a color palette with the 'palette' command first");
		return -ENOBUFS;
	}

	return mpix_image_palettize(&img, &palette);
}

static int cmd_depalettize(int argc, char **argv)
{
	if (argc != 1) {
		return -EINVAL;
	}

	if (palette.fourcc == 0) {
		MPIX_ERR("You must create a color palette with the 'palette' command first");
		return -ENOBUFS;
	}

	return mpix_image_depalettize(&img, &palette);
}

static int cmd_resize(int argc, char **argv)
{
	char *arg;
	char *end;
	uint32_t type;
	uint16_t width = 0;
	uint16_t height = 0;
	long long n;
	int ret;

	if (argc != 3) {
		return -EINVAL;
	}

	/* Parse resize_type */

	arg = argv[1];

	ret = str_get_value(mpix_str_resize, arg, &type);
	if (ret != 0) {
		return ret;
	}

	arg = argv[2];

	/* Parse width */

	n = strtoll(arg, &end, 10);
	if (*arg == '\0' || *end != '\0' || n < 0 || n > UINT16_MAX) {
		MPIX_ERR("Invalid <width> '%s'", arg);
		return -EINVAL;
	}
	width = n;

	/* Parse height */

	arg = argv[3];

	n = strtoll(arg, &end, 10);
	if (*arg == '\0' || *end != '\0' || n < 0 || n > UINT16_MAX) {
		MPIX_ERR("Invalid <height> '%s'", arg);
		return -EINVAL;
	}
	height = n;

	return mpix_image_resize(&img, type, width, height);
}

static int cmd_crop(int argc, char **argv)
{
	uint16_t x_offset = 0, y_offset = 0;
	uint16_t crop_width = 0, crop_height = 0;
	long long n;
	char *arg;
	char *end;

	if (argc != 5) {
		return -EINVAL;
	}

	/* Parse x_offset */

	arg = argv[1];

	n = strtoll(arg, &end, 10);
	if (*arg == '\0' || *end != '\0' || n < 0 || n > UINT16_MAX) {
		MPIX_ERR("Invalid <x_offset> '%s'", arg);
		return -EINVAL;
	}
	x_offset = n;

	/* Parse y_offset */

	arg = argv[2];

	n = strtoll(arg, &end, 10);
	if (*arg == '\0' || *end != '\0' || n < 0 || n > UINT16_MAX) {
		MPIX_ERR("Invalid <y_offset> '%s'", arg);
		return -EINVAL;
	}
	y_offset = n;

	/* Parse crop_width */

	arg = argv[3];

	n = strtoll(arg, &end, 10);
	if (*arg == '\0' || *end != '\0' || n <= 0 || n > UINT16_MAX) {
		MPIX_ERR("Invalid <crop_width> '%s'", arg);
		return -EINVAL;
	}
	crop_width = n;

	/* Parse crop_height */

	arg = argv[4];

	n = strtoll(arg, &end, 10);
	if (*arg != '\0' || n == 0 || n < 0 || n > UINT16_MAX) {
		MPIX_ERR("Invalid <crop_height> '%s'", arg);
		return -EINVAL;
	}
	crop_height = n;

	return mpix_image_crop(&img, x_offset, y_offset, crop_width, crop_height);
}

static int cmd_correction(int argc, char **argv)
{
	union mpix_correction_any corr = {0};
	long long n;
	uint32_t type;
	char *arg;
	char *end;
	int ret;

	if (argc < 2) {
		return -EINVAL;
	}

	ret = str_get_value(mpix_str_correction, argv[1], &type);
	if (ret != 0) {
		return ret;
	}

	switch (type) {
	case MPIX_CORRECTION_BLACK_LEVEL:
		if (argc != 3) {
			return -EINVAL;
		}

		/* Parse black_level */

		arg = argv[2];

		n = strtoll(arg, &end, 10);
		if (*arg == '\0' || *end != '\0' || n > UINT8_MAX) {
			MPIX_ERR("Invalid <black_level> value '%s'", arg);
			return -EINVAL;
		}
		corr.black_level.level = n;

		break;
	case MPIX_CORRECTION_WHITE_BALANCE:
		if (argc != 4) {
			return -EINVAL;
		}

		/* Parse red_level */

		arg = argv[2];

		n = strtof(arg, &end) * 1024;
		if (*arg == '\0' || *end != '\0' || n < 0 || n > UINT16_MAX) {
			MPIX_ERR("Invalid <red_level> value '%s'", argv[2]);
			return -EINVAL;
		}
		corr.white_balance.red_level = n;

		/* Parse blue_level */

		arg = argv[3];

		n = strtof(arg, &end) * 1024;
		if (*argv[3] == '\0' || *arg != '\0' || n < 0 || n > UINT16_MAX) {
			MPIX_ERR("Invalid <blue_level> value '%s'", argv[3]);
			return -EINVAL;
		}
		corr.white_balance.blue_level = n;

		break;
	case MPIX_CORRECTION_GAMMA:
		if (argc != 3) {
			return -EINVAL;
		}

		/* Parse gamma */

		arg = argv[2];

		n = strtof(arg, &end) * 255;
		if (*arg == '\0' || *end != '\0' || n < 0 || n > 255) {
			MPIX_ERR("Invalid <gamma> value '%s' (min=0.0, max=1.0)", arg);
			return -EINVAL;
		}
		corr.gamma.level = CLAMP(n, 17, 255);

		break;
	case MPIX_CORRECTION_COLOR_MATRIX:
		if (argc != 2 + 9) {
			return -EINVAL;
		}

		/* Parse color matrix */

		for (int i = 2; i < 2 + 9; i++) {
			arg = argv[i];

			n = strtof(arg, &end) * (1 << MPIX_CORRECTION_SCALE_BITS);
			if (*end == '\0' || *arg != '\0' || n < INT16_MIN || n > INT16_MAX) {
				MPIX_ERR("Invalid CCM coefficient '%s'", argv[i]);
				return -EINVAL;
			}
			corr.color_matrix.levels[i - 2] = n;
		}

		break;
	default:
		MPIX_ERR("unknown correction type %s", argv[1]);
		return -EINVAL;
	}

	return mpix_image_correction(&img, type, &corr);
}

struct {
	const char *name;
	int (*fn)(int argc, char **argv);
	const char *usage;
} cmds[] = {
	/* File I/O operations */
	{"read",	&cmd_read,	"read <file> [<width> <format>] ! ..."},
	{"write",	&cmd_write,	"... ! write <file>"},

	/* Conversion operations */
	{"convert",	&cmd_convert,	"... ! convert <format> ! ..."},
	{"debayer",	&cmd_debayer,	"... ! debayer <size> ! ..."},

	/* Color palette operations */
	{"palette",	&cmd_palette,	"... ! palette <bit_depth> <optimization_cycles> ! ..."},
	{"palettize",	&cmd_palettize,	"... ! palettize ! ..."},
	{"depalettize",	&cmd_depalettize, "... ! depalettize ! ..."},

	/* Transformation operations */
	{"correction",	&cmd_correction, "... ! correction <type> <level1> [<level2>] ! ..."},
	{"kernel",	&cmd_kernel,	"... ! kernel <type> <size> ! ..."},

	/* Size-related operations */
	{"resize",	&cmd_resize,	"... ! resize <type> <width>x<height> ! ..."},
	{"crop",	&cmd_crop,	"... ! crop <x> <y> <width> <height> ! ..."},

	/* Compression operations */
	{"qoi_encode",	&cmd_qoi_encode, "... ! qoi_encode ! ..."},
	{"jpeg_encode",	&cmd_jpeg_encode, "... ! jpeg_encode <quality> ! ..."},
};

void usage(void)
{
	fprintf(stderr, "Available commands:\n");
	for (size_t i = 0; i < ARRAY_SIZE(cmds); i++) {
		fprintf(stderr, " mpix [-v] %s\n", cmds[i].usage);
	}
}

int main(int argc, char **argv)
{
	int ret;

	/* Skip the 'mpix' command name */
	argv++;
	argc--;

	/* List all command usage */
	if (*argv == NULL) {
		usage();
		return 0;
	}

	/* command line flags parsing */
	if (strcmp(argv[0], "-v") == 0) {
		flag_verbose = true;
		argv++;
		argc--;
	}

	/* List all command usage */
	if (*argv == NULL) {
		usage();
		return 0;
	}

	/* Parse command line arguments */
	for (int argn; *argv != NULL; argv += argn, argc -= argn) {
		size_t i;

		for (argn = 0; argv[argn] != NULL && strcmp(argv[argn], "!") != 0; argn++) {
			continue;
		}

		for (i = 0; i < ARRAY_SIZE(cmds); i++) {
			if (strcmp(argv[0], cmds[i].name) == 0) {
				break;
			}
		}
		if (i == ARRAY_SIZE(cmds)) {
			MPIX_ERR("Unknown command '%s'", argv[0]);
			usage();
			return 1;
		}

		ret = cmds[i].fn(argn, argv);
		if (ret == -EINVAL) {
			MPIX_ERR("usage: mpix %s", cmds[i].usage);
			return 1;
		}
		if (ret < 0) {
			MPIX_ERR("operation '%s' failed: %s", argv[0], strerror(-ret));
			return 1;
		}

		if (argv[argn] != NULL) {
			assert(strcmp(argv[argn], "!") == 0);
			argv++;
		}
	}

	return 0;
}
