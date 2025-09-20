/* SPDX-License-Identifier: Apache-2.0 */

#include <lua.h>
#include <lualib.h>
#include <lauxlib.h>

#include <mpix/image.h>

#include "lua_mpix.h"

#define WIDTH 34
#define HEIGHT 5

uint8_t buf[WIDTH * HEIGHT * 3];

int main(int argc, char **argv)
{
	struct mpix_format fmt = { .width = WIDTH, .height = HEIGHT, .fourcc = MPIX_FMT_RGB24 };
	lua_State *L = luaL_newstate();
	luaL_openlibs(L);
	luaopen_mpix(L);

	/* Initialize the image from the outside of the library */
	mpix_image_from_buf(&lua_mpix_image, buf, sizeof(buf), &fmt);

	/* Fill with test data that can easily be printed */
	for (size_t i = 0; i < sizeof(buf); i++) buf[i] = 'a' + (i / 2) % ('z' - 'a');

	if (luaL_dofile(L, "main.lua") != LUA_OK) {
		luaL_error(L, "%s", lua_tostring(L, -1));
		return 1;
	}

	lua_close(L);

	return 0;
}
