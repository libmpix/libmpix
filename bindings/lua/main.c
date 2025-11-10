/* SPDX-License-Identifier: Apache-2.0 */

#include <stdio.h>
#include <unistd.h>

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <mpix/image.h>
#include <mpix/lua.h>
#include <mpix/posix.h>
#include <mpix/print.h>

int main(int argc, char **argv)
{
	const struct mpix_format fmt = { .width = 640, .height = 480, .fourcc = MPIX_FMT_RGB24 };
	struct mpix_palette palette = {};
	size_t size = mpix_format_pitch(&fmt) * fmt.height;
	uint8_t *buf;
	int err;

	if (argc != 2) {
		fprintf(stderr, "usage: %s input-file.raw >output-file.raw\n", argv[0]);
		return EXIT_FAILURE;
	}

	/* Read the input file and store it into the image */
	{
		FILE *fp = fopen(argv[1], "r");
		if (fp == NULL) {
			perror(argv[1]);
			return EXIT_FAILURE;
		}

		buf = malloc(size);
		if (buf == NULL) {
			perror("allocating source buffer");
			return EXIT_FAILURE;
		}

		size_t n = fread(buf, 1, size, fp);
		if (n != size) {
			perror(argv[1]);
			return EXIT_FAILURE;
		}

		fclose(fp);
	}

	struct mpix_image img = {};
	struct mpix_stats stats = {};

	/* Initialize the image with this buffer */
	mpix_image_from_buf(&img, buf, size, &fmt);

	/* Compute statistics and submit it to the Lua library */
	mpix_image_stats(&img, &stats);
	lua_mpix_set_stats(&stats);

	/* Configure the image processing with lua */
	{
		/* Configure the image processing the Lua API */
		lua_State *L = luaL_newstate();
		luaL_openlibs(L);

		/* Load the "mpix" lua library with the image */
		luaopen_mpix(L);
		lua_setglobal(L, "mpix");

		/* Run a script to configure a pipeline into mpix_lua_image */
		if (luaL_dofile(L, "main.lua") != LUA_OK) {
			luaL_error(L, "%s", lua_tostring(L, -1));
			return EXIT_FAILURE;
		}

		/* All the state is now contained within the pipeline, no more scripting needed */
		lua_close(L);

		/* Get the pipeline constructed by lua */
		int32_t *pipeline = lua_mpix_get_pipeline();
		err = mpix_pipeline_add_array(&img, &pipeline[1], pipeline[0]);
		if (err) {
			MPIX_ERR("Failed to apply the pipeline");
			return EXIT_FAILURE;
		}

		/* Apply every control value */
		int32_t *ctrls = lua_mpix_get_ctrls();
		for (int i = 0; i < MPIX_NB_CID; i++) {
			mpix_image_ctrl_value(&img, i, ctrls[i]);
		}

		/* Run hooks to complete the configuration */
		lua_mpix_hooks(&img, &palette);
	}

	/* Convert the image to the output buffer */
	if (mpix_image_to_file(&img, STDOUT_FILENO, 1024 * 1024) != 0) {
		perror("running the pipeline");
	}

	/* Show the pipeline with the timestamps */
	mpix_print_pipeline(img.first_op);

	mpix_image_free(&img);

	free(buf);

	return 0;
}
