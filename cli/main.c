#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>

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

static int parse_width_height(char *arg, uint16_t *width, uint16_t *height)
{
	unsigned long long ull;

	ull = strtoull(arg, &arg, 10);
	if (*arg != 'x' || ull > UINT16_MAX) {
		MPIX_ERR("Invalid width in <width>x<height> parameter '%s'", arg);
		return -EINVAL;
	}

	*width = ull;
	arg++;

	ull = strtoull(arg, &arg, 10);
	if (*arg != '\0' || ull > UINT16_MAX) {
		MPIX_ERR("Invalid height in <width>x<height> parameter '%s'", arg);
		return -EINVAL;
	}

	*height = ull;

	return 0;
}

static int cmd_read(int argc, char **argv)
{
	uint32_t fourcc = 0;
	uint16_t width = 0;
	uint16_t height = 0;
	long filesize;
	FILE *fp;
	size_t sz;
	int ret;

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
		ret = parse_width_height(argv[2], &width, &height);
		if (ret != 0) {
			return ret;
		}

		ret = str_get_value(mpix_str_fmt, argv[3], &fourcc);
		if (ret != 0) {
			return ret;
		}
	} else {
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
	int ret;

	if (argc != 3) {
		return -EINVAL;
	}

	ret = str_get_value(mpix_str_kernel, argv[1], &type);
	if (ret != 0) {
		return ret;
	}

	arg = argv[2];

	size = strtoul(arg, &arg, 10);
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
	unsigned long long size;
	char *arg;

	if (argc != 2) {
		return -EINVAL;
	}

	arg = argv[1];

	size = strtoul(arg, &arg, 10);
	if (*arg != '\0' || (size != 1 && size != 2 && size != 3)) {
		MPIX_ERR("Invalid debayer size '%s', must be 1, 2 or 3", argv[1]);
		return -EINVAL;
	}

	return mpix_image_debayer(&img, size);
}

static int cmd_palette(int argc, char **argv)
{
	unsigned long long bit_depth;
	unsigned long long optimization_cycles;
	char *arg;
	int ret;

	if (argc != 3) {
		return -EINVAL;
	}

	arg = argv[1];

	bit_depth = strtoul(arg, &arg, 10);
	if (*arg != '\0' || !IN_RANGE(bit_depth, 1, 8)) {
		MPIX_ERR("Invalid color bit detph '%s' (min=1, max=8)", argv[2]);
		return -EINVAL;
	}
	palette.fourcc = MPIX_FOURCC('P', 'L', 'T', '0' + bit_depth);

	arg = argv[2];

	optimization_cycles = strtoul(arg, &arg, 10);
	if (*arg != '\0' || optimization_cycles > 1000) {
		MPIX_ERR("Invalid number of optimization cycles '%s'", argv[2]);
		return -EINVAL;
	}

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
	uint32_t type;
	uint16_t width = 0;
	uint16_t height = 0;
	int ret;

	if (argc != 3) {
		return -EINVAL;
	}

	ret = str_get_value(mpix_str_resize, argv[1], &type);
	if (ret != 0) {
		return ret;
	}

	ret = parse_width_height(argv[2], &width, &height);
	if (ret != 0) {
		return ret;
	}

	return mpix_image_resize(&img, type, width, height);
}

static int cmd_correction(int argc, char **argv)
{
	union mpix_correction_any corr = {0};
	unsigned long long ull;
	uint32_t type;
	char *arg;
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

		arg = argv[2];

		ull = strtoull(arg, &arg, 10);
		if (*arg != '\0' || ull > UINT8_MAX) {
			MPIX_ERR("Invalid black level value '%s'", argv[2]);
			return -EINVAL;
		}
		corr.black_level.level = ull;

		break;
	case MPIX_CORRECTION_WHITE_BALANCE:
		if (argc != 4) {
			return -EINVAL;
		}

		arg = argv[2];

		ull = strtof(arg, &arg) * 1024;
		if (*arg != '\0' || ull > UINT16_MAX) {
			MPIX_ERR("Invalid red level value '%s'", argv[2]);
			return -EINVAL;
		}
		corr.white_balance.red_level = ull;

		arg = argv[3];

		ull = strtof(arg, &arg) * 1024;
		if (*arg != '\0' || ull > UINT16_MAX) {
			MPIX_ERR("Invalid blue level value '%s'", argv[3]);
			return -EINVAL;
		}
		corr.white_balance.blue_level = ull;

		break;
	case MPIX_CORRECTION_GAMMA:
		if (argc != 3) {
			return -EINVAL;
		}

		arg = argv[2];

		ull = 255 * strtof(arg, &arg);
		if (*arg != '\0' || ull > 255) {
			MPIX_ERR("Invalid gamma value '%s' (min=0.0, max=1.0)", ull);
			return -EINVAL;
		}
		corr.gamma.level = CLAMP(ull, 17, 255);

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
	{"read",	&cmd_read,	"read <file> [<width>x<height> <format>] ! ..."},
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
