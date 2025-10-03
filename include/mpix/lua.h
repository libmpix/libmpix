/* SPDX-License-Identifier: Apache-2.0 */

#ifndef LUA_MPIX_H
#define LUA_MPIX_H

#include <lua.h>
#include <mpix/types.h>

/** Open the mpix lua library */
int luaopen_mpix(lua_State *L);

/** Configure the image that libmpix lua binding will use */
void lua_mpix_set_image(struct mpix_image *img);

/** Run hooks, to call just before processing the pipeline built by Lua */
int lua_mpix_hooks(lua_State *L);

#endif
