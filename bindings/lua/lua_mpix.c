/* SPDX-License-Identifier: Apache-2.0 */

#include <stdio.h>
#include <lua.h>
#include <lauxlib.h>

#include <mpix/genlist.h>
#include <mpix/lua.h>
#include <mpix/image.h>
#include <mpix/operation.h>
#include <mpix/print.h>

/* Ping-pong buffers for the pipeline */
static int32_t lua_mpix_pipeline_a[LUA_MPIX_PIPELINE_SIZE];
static int32_t lua_mpix_pipeline_b[LUA_MPIX_PIPELINE_SIZE];
static volatile int32_t *lua_mpix_pipeline = lua_mpix_pipeline_a;

/* Ping-pong buffers for the statistics */
static struct mpix_stats lua_mpix_stats_a;
static struct mpix_stats lua_mpix_stats_b;
static volatile struct mpix_stats *lua_mpix_stats = &lua_mpix_stats_a;

/* Controls used for tuning the image */
static volatile int32_t lua_mpix_ctrls[MPIX_NB_CID];

static int lua_mpix_op(lua_State *L, enum mpix_op_type type, const char *name)
{
	enum { PIPELINE = 1, PARAMS = 2 };
	lua_Integer len;

	/* P1: pipeline array */
	lua_len(L, PIPELINE);
	len = lua_tointeger(L, -1);

	/* P2+: parameters */
	if (lua_gettop(L) - PARAMS != mpix_params_nb(type)) {
		luaL_error(L, "Expected %d parameters for operation %s, got %d",
			mpix_params_nb(type), name, lua_gettop(L) - PARAMS);
	}

	/* Push the operation type to the pipeline array */
	lua_pushinteger(L, type);
	lua_seti(L, PIPELINE, ++len);

	/* Push the parameters to the pipeline array */
	for (int i = 0; i < mpix_params_nb(type); i++) {
		lua_Integer par;

		/* Append the parameter to the table */
		par = luaL_checkinteger(L, PARAMS + i);
		lua_pushinteger(L, par);
		lua_seti(L, PIPELINE, ++len);
	}

	return 0;
}

/* one wrapper function for every operation */
#define LUA_MPIX_IMAGE_FN(X, x) \
static int lua_mpix_##x(lua_State *L){ return lua_mpix_op(L, MPIX_OP_##X, #X); }
MPIX_FOR_EACH_OP(LUA_MPIX_IMAGE_FN)

static int lua_mpix_set_pipeline(lua_State *L)
{
	enum { PIPELINE = 1 };
	volatile int32_t *pipeline = lua_mpix_pipeline;
	lua_Integer len;

	/* Work on the inactive buffer */
	if (pipeline == lua_mpix_pipeline_a) {
		pipeline = lua_mpix_pipeline_b;
	}
	if (pipeline == lua_mpix_pipeline_b) {
		pipeline = lua_mpix_pipeline_a;
	}

	lua_len(L, PIPELINE);
	len = lua_tointeger(L, -1);

	if (len > LUA_MPIX_PIPELINE_SIZE) {
		luaL_error(L, "too many operations (%d), max is %d", len, pipeline[0]);
	}

	/* Lua 1-based offset match the pipeline array offsets as first element is the size */
	for (int i = 1; i <= len; i++) {
		lua_Integer v;

		lua_geti(L, PIPELINE, i);
		v = luaL_checkinteger(L, -1);

		if (v < INT32_MIN || v > INT32_MAX) {
			luaL_error(L, "value %d not in range [%d, %d]", v, INT32_MIN, INT32_MAX);
		}

		pipeline[i] = v;
	}
	pipeline[0] = len;

	/* Atomically swap the active buffer */
	lua_mpix_pipeline = pipeline;

	return 0;
}

static int lua_mpix_set_ctrl(lua_State *L)
{
	enum { CTRL_ID = 1, VALUE };

	/* P1: control id */
	lua_Integer cid = luaL_checkinteger(L, CTRL_ID);
	if (cid < 0 || cid > MPIX_NB_CID) {
		luaL_error(L, "CID value %d out of range [%d, %d]", cid, 0, MPIX_NB_CID - 1);
	}

	/* P2: control value */
	int32_t value = luaL_checkinteger(L, VALUE);

	/* Apply the control to the pipeline */
	lua_mpix_ctrls[cid] = value;

	return 0;
}

static int lua_mpix_get_stats(lua_State *L)
{
	enum { R, G, B };
	struct mpix_stats *stats = (void *)lua_mpix_stats;

	lua_newtable(L);

	lua_newtable(L);
	for (size_t i = 0; i < ARRAY_SIZE(stats->y_histogram); i++) {
		lua_pushinteger(L, stats->y_histogram[i]);
		lua_rawseti(L, -2, i + 1);
	}
	lua_setfield(L, -2, "y_histogram");

	lua_newtable(L);
	for (size_t i = 0; i < ARRAY_SIZE(stats->y_histogram_vals); i++) {
		lua_pushinteger(L, stats->y_histogram_vals[i]);
		lua_rawseti(L, -2, i + 1);
	}
	lua_setfield(L, -2, "y_histogram_vals");

	lua_pushinteger(L, stats->y_histogram_total);
	lua_setfield(L, -2, "y_histogram_total");

	lua_pushinteger(L, stats->rgb_average[R]);
	lua_setfield(L, -2, "rgb_average_r");

	lua_pushinteger(L, stats->rgb_average[G]);
	lua_setfield(L, -2, "rgb_average_g");

	lua_pushinteger(L, stats->rgb_average[B]);
	lua_setfield(L, -2, "rgb_average_b");

	lua_pushinteger(L, stats->rgb_min[R]);
	lua_setfield(L, -2, "rgb_min_r");

	lua_pushinteger(L, stats->rgb_min[G]);
	lua_setfield(L, -2, "rgb_min_g");

	lua_pushinteger(L, stats->rgb_min[B]);
	lua_setfield(L, -2, "rgb_min_b");

	lua_pushinteger(L, stats->rgb_max[R]);
	lua_setfield(L, -2, "rgb_max_r");

	lua_pushinteger(L, stats->rgb_max[G]);
	lua_setfield(L, -2, "rgb_max_g");

	lua_pushinteger(L, stats->rgb_max[B]);
	lua_setfield(L, -2, "rgb_max_b");

	lua_pushinteger(L, stats->nvals);
	lua_setfield(L, -2, "nvals");

	return 1;
}

static const struct luaL_Reg lua_mpix_reg[] = {
	{ "set_ctrl", lua_mpix_set_ctrl },
	{ "set_pipeline", lua_mpix_set_pipeline },
	{ "get_stats", lua_mpix_get_stats },
	{ NULL, NULL },
};

static const struct luaL_Reg lua_mpix_op_reg[] = {
#define MPIX_IMAGE_REG(X, x) \
	{ #x, lua_mpix_##x },
MPIX_FOR_EACH_OP(MPIX_IMAGE_REG)
	{ NULL, NULL },
};

int luaopen_mpix(lua_State *L)
{
	/* Table for the module */
	lua_newtable(L);

	/* Add top-level functions */
	luaL_setfuncs(L, lua_mpix_reg, 0);

	/* Add table of formats */
	lua_newtable(L);
	for (int i = 0; mpix_str_fmt[i].name != NULL; i++) {
		lua_pushinteger(L, mpix_str_fmt[i].value);
		lua_setfield(L, -2, mpix_str_fmt[i].name);
	}
	lua_setfield(L, -2, "fmt");

	/* Add table of control IDs */
	lua_newtable(L);
	for (int i = 0; i < MPIX_NB_CID; i++) {
		lua_pushinteger(L, i);
		lua_setfield(L, -2, mpix_str_cid[i]);
	}
	lua_setfield(L, -2, "cid");

	/* Add table of kernel convolution operations */
	lua_newtable(L);
	for (int i = 0; i < MPIX_NB_KERNEL; i++) {
		lua_pushinteger(L, i);
		lua_setfield(L, -2, mpix_str_kernel[i]);
	}
	lua_setfield(L, -2, "kernel");

	/* Add table of operation functions */
	lua_newtable(L);
	luaL_setfuncs(L, lua_mpix_op_reg, 0);
	lua_setfield(L, -2, "op");

	/* Return the module to i.e. store a as global */
	return 1;
}

int32_t *lua_mpix_get_pipeline(void)
{
	return (int32_t *)lua_mpix_pipeline;
}

int32_t *lua_mpix_get_ctrls(void)
{
	return (int32_t *)lua_mpix_ctrls;
}

void lua_mpix_set_stats(struct mpix_stats *stats)
{
	/* Copy to the inactive buffer and mark it as active */
	if (lua_mpix_stats == &lua_mpix_stats_a) {
		memcpy(&lua_mpix_stats_b, stats, sizeof(lua_mpix_stats_b));
		lua_mpix_stats = &lua_mpix_stats_b;
	}
	if (lua_mpix_stats == &lua_mpix_stats_b) {
		memcpy(&lua_mpix_stats_a, stats, sizeof(lua_mpix_stats_a));
		lua_mpix_stats = &lua_mpix_stats_a;
	}
}

static int lua_mpix_palette_hooks(struct mpix_image *img, struct mpix_palette *palette)
{
	struct mpix_image palette_img = {};
	int err;

	/* Try to find a palette through the image to get the fourcc */
	err = mpix_pipeline_get_palette_fourcc(img->first_op, palette);
	if (err) return 0; /* nothing to do */

	/* Turn the color palette into an image to apply the correction on it */
	mpix_image_from_palette(&palette_img, palette);

	/* Apply the color corretction to the palette */
	for (struct mpix_base_op *op = img->first_op; op != NULL; op = op->next) {
		switch (op->type) {
		case MPIX_OP_CORRECT_BLACK_LEVEL:
		case MPIX_OP_CORRECT_COLOR_MATRIX:
		case MPIX_OP_CORRECT_GAMMA:
		case MPIX_OP_CORRECT_WHITE_BALANCE:
			err = mpix_pipeline_add(&palette_img, op->type, NULL, 0);
			if (err) return err;
			break;
		default:
			break;
		}
	}

	/* Transfer all controls present on both */
	for (int cid = 0; cid < MPIX_NB_CID; cid++) {
		if (img->ctrls[cid] != NULL && palette_img.ctrls[cid] != NULL) {
			*palette_img.ctrls[cid] = *img->ctrls[cid];
		}
	}

	/* Apply the image correction to the color palette to get accurate colors */
	err = mpix_image_to_palette(&palette_img, palette);
	mpix_image_free(&palette_img);
	if (err) return err;

	/* Apply it to all palette operations */
	err = mpix_pipeline_set_palette(img->first_op, palette);
	if (err) return err;

	return 0;
}

int lua_mpix_hooks(struct mpix_image *img, struct mpix_palette *palette)
{
	int err;

	err = lua_mpix_palette_hooks(img, palette);
	if (err) return err;

	return 0;
}
