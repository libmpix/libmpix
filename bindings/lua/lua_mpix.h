/* SPDX-License-Identifier: Apache-2.0 */

#ifndef LUA_MPIX_H
#define LUA_MPIX_H

#include <lua.h>
#include <mpix/image.h>

/** Image to be filled with data by the library user rather than the application */
extern struct mpix_image lua_mpix_image;

/** Open the mpix lua library */
int luaopen_mpix(lua_State *L);

#endif
