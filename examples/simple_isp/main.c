/* SPDX-License-Identifier: Apache-2.0 */

#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <mpix/image.h>
#include <mpix/posix.h>
#include <mpix/print.h>

#define CHECK(x) \
	({ int e = (x); if (e) { fprintf(stderr, "%s: %s\n", #x, strerror(-e)); return e; } })

int simple_isp_demo(uint8_t *buf, size_t size, const struct mpix_format *fmt)
{
	struct mpix_image img = {};
	int32_t color_matrix_q10[9] = {
		1.00 * (1 << 10), 0.00 * (1 << 10), 0.00 * (1 << 10),
		0.00 * (1 << 10), 1.00 * (1 << 10), 0.00 * (1 << 10),
		0.00 * (1 << 10), 0.00 * (1 << 10), 1.00 * (1 << 10),
	};

	mpix_image_from_buf(&img, buf, size, fmt);

	/* Add the operations to the pipeline */
	CHECK(mpix_image_correct_black_level(&img));
	CHECK(mpix_image_correct_gamma(&img));
	CHECK(mpix_image_correct_white_balance(&img));
	CHECK(mpix_image_correct_color_matrix(&img));
	CHECK(mpix_image_jpeg_encode(&img));

	/* Control the pipeline image tuning */
	CHECK(mpix_image_ctrl_value(&img, MPIX_CID_BLACK_LEVEL,  0));
	CHECK(mpix_image_ctrl_value(&img, MPIX_CID_RED_BALANCE,  1.3 * (1 << 10)));
	CHECK(mpix_image_ctrl_value(&img, MPIX_CID_BLUE_BALANCE, 1.7 * (1 << 10)));
	CHECK(mpix_image_ctrl_value(&img, MPIX_CID_GAMMA_LEVEL,  0.7 * (1 << 10)));
	CHECK(mpix_image_ctrl_array(&img, MPIX_CID_COLOR_MATRIX_0, color_matrix_q10,
				    ARRAY_SIZE(color_matrix_q10)));
	CHECK(mpix_image_ctrl_value(&img, MPIX_CID_JPEG_QUALITY, MPIX_JPEG_QUALITY_DEFAULT));

	/* Process the image and write it to standard output */
	CHECK(mpix_image_to_file(&img, STDOUT_FILENO, fmt->width * fmt->height * 2));

	/* Print the completed pipeline and release the memory */
	mpix_print_pipeline(img.first_op);
	mpix_image_free(&img);

	return 0;
}

int main(int argc, char **argv)
{
	const struct mpix_format fmt = { .width = 640, .height = 480, .fourcc = MPIX_FMT_RGB24 };

	FILE *fp;
	size_t size = mpix_format_pitch(&fmt) * fmt.height;
	uint8_t *buf;
	size_t n;

	if (argc != 2) {
		fprintf(stderr, "usage: %s input-file.raw >output-file.raw\n", argv[0]);
		return 1;
	}

	fp = fopen(argv[1], "r");
	if (fp == NULL) {
		perror(argv[1]);
		return 1;
	}

	buf = malloc(size);
	if (buf == NULL) {
		perror("allocating source buffer");
		return 1;
	}

	n = fread(buf, 1, size, fp);
	if (n != size) {
		perror(argv[1]);
		return 1;
	}

	fclose(fp);

	simple_isp_demo(buf, size, &fmt);

	free(buf);

	return 0;
}
