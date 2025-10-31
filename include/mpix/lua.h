/* SPDX-License-Identifier: Apache-2.0 */

#ifndef LUA_MPIX_H
#define LUA_MPIX_H

#include <lua.h>
#include <mpix/types.h>

/** Max number of elements the lua pipeline can store */
#define LUA_MPIX_PIPELINE_SIZE 32

/** Open the mpix lua library */
int luaopen_mpix(lua_State *L);

/** Configure the image that libmpix lua binding will use */
void lua_mpix_set_image(struct mpix_image *img);

/** Run hooks, to call just before processing the pipeline built by Lua */
int lua_mpix_hooks(struct mpix_image *img, struct mpix_palette *palette);

/** Get the active pipeline definition */
int32_t *lua_mpix_get_pipeline(void);

/** Get the queried control number */
int32_t *lua_mpix_get_ctrls(void);

/** Set the statistics struct used by the pipeline */
void lua_mpix_set_stats(struct mpix_stats *stats);

#endif
