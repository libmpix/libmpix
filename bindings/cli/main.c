/* SPDX-License-Identifier: Apache-2.0 */

#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

#include <mpix/formats.h>
#include <mpix/image.h>
#include <mpix/operation.h>
#include <mpix/print.h>
#include <mpix/ring.h>
#include <mpix/utils.h>

/* Image used through the processing */
struct mpix_image img;

/* Input bufferto be freed at the end of the processing */
uint8_t *src_buf;

/* Color palette for palette-based operations */
struct mpix_palette palette;

/* Global flags */
bool flag_verbose;
int flag_palette_cycles = 10;
int flag_palette_samples = 1000;

static int parse_llong(const char *arg, int32_t *param)
{
	char *end = NULL;
	long long n = strtoll(arg, &end, 10);
	*param = n;
	return (end == NULL || *end != '\0' || n < INT32_MIN || n > INT32_MAX) ? -ERANGE : 0;
}

static int parse_double_q10(const char *arg, int32_t *param)
{
	char  *end = NULL;
	double f = strtod(arg, &end);
	long long n = f * (1 << 10);
	*param = n;
	return (end == NULL || *end != '\0' || n < INT32_MIN || n > INT32_MAX) ? -ERANGE : 0;
}

static int parse_fourcc(const char *arg, int32_t *param)
{
	*param = mpix_enum(mpix_str_fmt, arg);
	return *param < 0 ? -EINVAL : 0;
}

static int parse_enum(const char *arg, int32_t *param)
{
#if 0
#define PARSE_ENUM(X, x) \
	if (strcasecmp(arg, #x) == 0) { *param = MPIX_##X; return 0; }
MPIX_FOR_EACH_ENUM(PARSE_ENUM);
#endif
	return -EINVAL;
}

static int add_operation(enum mpix_op_type type, int argc, char **argv)
{
	int32_t params[10];
	int i;
	int err;

	/* Skip the command name */
	argc--, argv++;

	if (argc != mpix_params_nb(type)) {
		MPIX_ERR("invalid number of parameters: have %d, expected %d",
			 argc, mpix_params_nb(type));
		return -EINVAL;
	}

	for (i = 0; i < argc; i++) {
		err = parse_llong(argv[i], &params[i]);
		if (err == 0) {
			continue;
		}

		err = parse_double_q10(argv[i], &params[i]);
		if (err == 0) {
			continue;
		}

		err = parse_enum(argv[i], &params[i]);
		if (err == 0) {
			continue;
		}

		err = parse_fourcc(argv[i], &params[i]);
		if (err == 0) {
			continue;
		}

		MPIX_ERR("unrecongized integer/float/enum value: '%s'", argv[i]);
		return -EINVAL;
	}

	return mpix_pipeline_add(&img, type, params, i);
}

static int run_palette_hooks(void)
{
	int err;

	/* Get the first palette format, assuming there is only one for the pipeline */
	for (struct mpix_base_op *op = img.first_op; op != NULL; op = op->next) {
		if (op->type == MPIX_OP_PALETTE_ENCODE) {
			if (op->next == NULL) {
				break;
			}
			palette.fourcc = op->next->fmt.fourcc;
		}
		if (op->type == MPIX_OP_PALETTE_DECODE) {
			palette.fourcc = op->fmt.fourcc;
		}
	}
	if (palette.fourcc == 0) {
		if (flag_verbose) {
			MPIX_INF("no palette operation detected");
		}
		mpix_print_pipeline(img.first_op);
		return 0;
	}

	/* Optimize the palette as many time as requested */
	for (int i = 0; i < flag_palette_cycles; i++) {
		err = mpix_image_optimize_palette(&img, &palette, flag_palette_samples);
		if (err) {
			MPIX_ERR("failed to optimize the color palette (format=%s)",
				 MPIX_FOURCC_TO_STR(palette.fourcc));
			return err;
		}
	}

	/* Set the palette for all operations of the pipeline */
	err = mpix_pipeline_set_palette(img.first_op, &palette);
	if (err) {
		MPIX_ERR("failed to set the color palette: %s", strerror(-err));
		return err;
	}

	return 0;
}

static int run_hooks(void)
{
	int err;

	err = run_palette_hooks();
	if (err) {
		return err;
	}

	return 0;
}

static int add_read(int argc, char **argv)
{
	struct mpix_format fmt = {0};
	long filesize;
	FILE *fp;
	size_t sz;
	char *arg;
	int32_t n;
	int err;

	if (argc < 2) {
		MPIX_ERR("usage: %s <filename>", argv[0]);
		return -EINVAL;
	}

	fp = fopen(argv[1], "r");
	if (fp == NULL) {
		MPIX_ERR("failed to open '%s'", argv[1]);
		return -errno;
	}

	err = fseek(fp, 0, SEEK_END);
	if (err) {
		MPIX_ERR("failed to estimate '%s' file size", argv[1]);
		return -errno;
	}

	filesize = ftell(fp);
	if (filesize < 0) {
		MPIX_ERR("failed to estimate '%s' file size", argv[1]);
		return -errno;
	}

	if (argc == 2) {
		char *ext;

		ext = strrchr(argv[1], '.');
		if (ext == NULL || strlen(ext) == 0) {
			MPIX_ERR("could not parse extension: '%s'", argv[1]);
			return -EINVAL;
		}

		if (strcasecmp(ext, ".qoi") == 0) {
			fmt.fourcc = MPIX_FMT_QOI;
		} else {
			MPIX_ERR("unsupported file extension: '%s'", ext);
			return -EINVAL;
		}

	} else if (argc == 4) {
		arg = argv[2];

		/* Parse width */

		err = parse_llong(argv[2], &n);
		if (err || n < 0 || n > UINT16_MAX) {
			MPIX_ERR("invalid <width> '%s'", arg);
			return -EINVAL;
		}
		fmt.width = n;

		/* Parse fourcc */

		arg = argv[3];

		err = parse_fourcc(arg, &n);
		if (err) {
			return err;
		}
		fmt.fourcc = n;

		/* User has provided the width and we know the format to compute the height */
		fmt.height = filesize / mpix_format_pitch(&fmt);
		if (fmt.height < 1 || filesize / mpix_format_pitch(&fmt) > UINT16_MAX) {
			MPIX_ERR("invalid <width> %d provided, filesize %zu does not match",
				 fmt.width, filesize);
			return -EINVAL;
		}
	} else {
		return -EINVAL;
	}

	err = fseek(fp, 0, SEEK_SET);
	if (err) {
		MPIX_ERR("failed to resume to start of '%s'", argv[1]);
		return -errno;
	}

	src_buf = malloc(filesize);
	if (src_buf == NULL) {
		return -errno;
	}

	sz = fread(src_buf, 1, filesize, fp);
	if (sz != (unsigned)filesize) {
		MPIX_ERR("read only %zu/%zu bytes from '%s': %s",
			sz, filesize, argv[1], strerror(errno));
		return -errno;
	}

	fclose(fp);

	assert(fmt.fourcc != 0x00);

	mpix_image_from_buf(&img, src_buf, filesize, &fmt);

	if (flag_verbose) {
		struct mpix_stats stats = {0};

		mpix_image_stats(&img, &stats);
		mpix_print_stats(&stats);
	}

	return 0;
}

static int add_write(int argc, char **argv)
{
	FILE *fp;
	size_t filesize;
	uint8_t *dst_buf;
	size_t dst_size;
	uint8_t bits;
	size_t sz;
	int err;

	if (argc != 2) {
		return -EINVAL;
	}

	/* For variable pitch formats, assume 16 byte per pixel is enough */
	bits = mpix_bits_per_pixel(img.fmt.fourcc);
	bits = MAX(bits, BITS_PER_BYTE * 16);

	/* Estimate the image size */
	filesize = img.fmt.width * img.fmt.height * bits / BITS_PER_BYTE;
	if (filesize == 0) {
		MPIX_ERR("invalid image size (%ux%u) or format", img.fmt.width, img.fmt.height);
		return -EFBIG;
	}

	dst_buf = malloc(filesize);
	if (dst_buf == NULL) {
		MPIX_ERR("failed to allocate %zu bytes", filesize);
		return -errno;
	}

	err = run_hooks();
	if (err) {
		return err;
	}

	err = mpix_image_to_buf(&img, dst_buf, filesize);
	if (err) {
		MPIX_ERR("failed to convert the image");
		return err;
	}

	dst_size = mpix_ring_total_used(&img.last_op->ring);

	if (flag_verbose) {
		struct mpix_stats stats = {0};

		mpix_stats_from_buf(&stats, img.buffer, &img.fmt);
		mpix_print_stats(&stats);
		mpix_print_pipeline(img.first_op);
	}

	fp = fopen(argv[1], "w+");
	if (fp == NULL) {
		MPIX_ERR("failed to open %s", argv[1]);
		return -errno;
	}

	if (flag_verbose) {
		MPIX_INF("Writing %zu bytes to %s", dst_size, argv[1]);
	}

	sz = fwrite(dst_buf, 1, dst_size, fp);
	if (sz != dst_size) {
		MPIX_ERR("wrote only %zu/%zu bytes to '%s': %s (%p)",
			sz, filesize, argv[1], strerror(errno), fp);
		return -errno;
	}

	fclose(fp);
	free(src_buf);
	free(dst_buf);
	mpix_image_free(&img);

	return 0;
}

void exit_usage(void)
{
	fprintf(stderr, "Usage:\n");
	fprintf(stderr, " mpix [<flags>] <op> [<param>...] ! <op> [<param>...] ! ...\n");
	fprintf(stderr, "Flags:\n");
	fprintf(stderr, " -v, --verbose             Print debug informations while running.\n");
	fprintf(stderr, " -d, --palette-depth <n>   Set the palette bit depth.\n");
	fprintf(stderr, " -c, --palette-cycles <n>  Set the number of optimization cycles\n");
	fprintf(stderr, "Operations:\n");
	fprintf(stderr, " read <file> [<width> <format>]\n");
	fprintf(stderr, " write <file>\n");
#define USAGE_OP(X, x) \
	fprintf(stderr, " %s", #x); \
	for (size_t i = 0; i < mpix_params_nb_##x; i++) fprintf(stderr, " <p%zu>", i); \
	fprintf(stderr, "\n");
MPIX_FOR_EACH_OP(USAGE_OP)
	exit(1);
}

static int add_command(int argc, char **argv)
{
	if (strcasecmp(argv[0], "read") == 0) return add_read(argc, argv);
	if (strcasecmp(argv[0], "write") == 0) return add_write(argc, argv);
#define ADD_OPERATION(X, x) \
	if (strcasecmp(argv[0], #x) == 0) return add_operation(MPIX_OP_##X, argc, argv);
MPIX_FOR_EACH_OP(ADD_OPERATION)
	exit_usage();
	return 0;
}

int main(int argc, char **argv)
{
	int ret;

	/* Skip the 'mpix' command name */
	argv++;
	argc--;

	/* List all command usage */
	if (*argv == NULL) {
		exit_usage();
	}

	/* command line flags parsing */
	if (strcmp(argv[0], "-v") == 0 || strcmp(argv[0], "--verbose") == 0) {
		flag_verbose = true;
		argv++;
		argc--;
	}

	/* command line flags parsing */
	if (strcmp(argv[0], "-p") == 0) {
		flag_verbose = true;
		argv++;
		argc--;
	}

	/* List all command usage */
	if (*argv == NULL) {
		exit_usage();
	}

	/* Parse command line arguments */
	for (int argn; *argv != NULL; argv += argn, argc -= argn) {
		for (argn = 0; argv[argn] != NULL && strcmp(argv[argn], "!") != 0; argn++) {
			continue;
		}

		ret = add_command(argn, argv);
		if (ret == -EINVAL) {
			MPIX_ERR("invalid parameters when applying operation '%s'", argv[0]);
			return 1;
		}
		if (ret < 0) {
			MPIX_ERR("failed to add the operation '%s': %s", argv[0], strerror(-ret));
			return 1;
		}

		if (argv[argn] != NULL) {
			assert(strcmp(argv[argn], "!") == 0);
			argv++;
		}
	}

	return 0;
}
