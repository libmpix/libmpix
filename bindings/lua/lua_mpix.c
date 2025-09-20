/* SPDX-License-Identifier: Apache-2.0 */

#include <stdio.h>
#include <lua.h>
#include <lauxlib.h>

#include <mpix/genlist.h>
#include <mpix/image.h>
#include <mpix/operation.h>
#include <mpix/print.h>

struct mpix_image lua_mpix_image;
struct mpix_palette lua_mpix_palette;
size_t lua_mpix_palette_cycles;

MPIX_REGISTER_OP(callback, P_BUFFER_SIZE, P_THRESHOLD);

struct mpix_operation {
	struct mpix_base_op base;
	/* Parameters */
	size_t threshold;
	/* Lua state to be filled before calling this function */
	lua_State *L;
};

int mpix_add_callback(struct mpix_image *img, const int32_t *params)
{
	struct mpix_operation *op;

	/* Parameter validation */
	if (params[P_BUFFER_SIZE] < 1 || params[P_THRESHOLD] < 1 ||
	    params[P_BUFFER_SIZE] < params[P_THRESHOLD]) {
		return -EINVAL;
	}

	/* Add an operation */
	op = mpix_op_append(img, MPIX_OP_CALLBACK, sizeof(*op), params[P_BUFFER_SIZE]);
	if (op == NULL) return -ENOMEM;

	/* Store parameters */
	op->threshold = params[P_THRESHOLD];

	return 0;
}

int mpix_run_callback(struct mpix_base_op *base)
{
	struct mpix_operation *op = (void *)base;
	const uint8_t *src;
	size_t sz = 0;

	/* Expected to be filled by the caller */
	if (op->L == NULL) return -ENOSYS;

	/* Check against P_THRESHOLD, but request the full buffer */
	MPIX_OP_INPUT_PEEK(base, &src, &sz);
	if (sz < op->threshold) return -EAGAIN;

	/* P1: callback, left on the top of the stack to call it */
	lua_pushvalue(op->L, -1);

	/* P2: the input buffer from this operation */
	lua_pushlstring(op->L, (const char *)src, sz);

	/* Call the function already prepared on the top of the stack */
	lua_call(op->L, 1, 0);

	/* Confirm that the bytes were read out of the buffer */
	MPIX_OP_INPUT_FLUSH(base, sz);

	return 0;
}

static int lua_mpix_op(lua_State *L, enum mpix_op_type type, const char *name)
{
	struct mpix_image *img = &lua_mpix_image;
	int32_t params[32];
	size_t nb = 0;
	int err;

	/* P*: fill all into a C array */
	for (nb = 0; nb < ARRAY_SIZE(params) && nb + 1 <= (size_t)lua_gettop(L); nb++) {
		params[nb] = lua_tointeger(L, nb + 1);
	}
	lua_pop(L, nb);

	/* Add the element to the pipeline */
	err = mpix_pipeline_add(img, type, params, nb);
	if (err == -EBADMSG) luaL_error(L, "invalid number of arguments for %s operation", name);
	if (err) luaL_error(L, "failed to add %s to the pipeline: %s", name, strerror(-err));

	return 0;
}

/* one wrapper function for every operation */
#define LUA_MPIX_IMAGE_FN(X, x) \
static int lua_mpix_##x(lua_State *L) { return lua_mpix_op(L, MPIX_OP_##X, #X); }
MPIX_FOR_EACH_OP(LUA_MPIX_IMAGE_FN)

static int lua_mpix_dump(lua_State *L)
{
	mpix_print_pipeline(lua_mpix_image.first_op);
	mpix_print_ctrls(lua_mpix_image.ctrls);
	return 0;
}

static int lua_mpix_free(lua_State *L)
{
	mpix_image_free(&lua_mpix_image);
	return 0;
}

static int lua_mpix_format(lua_State *L)
{
	struct mpix_image *img = &lua_mpix_image;

	mpix_image_format(&lua_mpix_image);

	/* R1: Table containing the width/height/format fields */
	lua_newtable(L);

	lua_pushinteger(L, img->fmt.width);
	lua_setfield(L, -2, "width");

	lua_pushinteger(L, img->fmt.height);
	lua_setfield(L, -2, "height");

	lua_pushinteger(L, img->fmt.fourcc);
	lua_setfield(L, -2, "pixel_format");

	lua_pushinteger(L, mpix_format_pitch(&img->fmt));
	lua_setfield(L, -2, "bytes_per_line");

	return 1;
}

static int lua_mpix_ctrl(lua_State *L)
{
	struct mpix_image *img = &lua_mpix_image;
	int err;

	/* P1: control id */
	int cid = lua_tointeger(L, 1);

	/* P2: control value */
	int32_t value = lua_tointeger(L, 2);

	/* Apply the control to the pipeline */
	err = mpix_image_ctrl_value(img, cid, value);
	if (err) luaL_error(L, "'%s' while applying control to the pipeline", strerror(-err));

	return 0;
}

static int lua_mpix_optimize_palette(lua_State *L)
{
	struct mpix_image *img = &lua_mpix_image;
	struct mpix_palette *palette = &lua_mpix_palette;
	int err;

	/* P1: number of samples for optimizing the palette */
	lua_Integer num_samples = lua_tointeger(L, 1);

	/* Optimize the palette as many samples as requested */
	err = mpix_image_optimize_palette(img, palette, num_samples);
	if (err) luaL_error(L, "failed to optimize the color palette (format=%s)",
			    MPIX_FOURCC_TO_STR(palette->fourcc));

	return 0;
}

static void lua_mpix_palette_hooks(lua_State *L)
{
	struct mpix_image *img = &lua_mpix_image;
	struct mpix_palette *palette = &lua_mpix_palette;
	int err;

	/* Try to find a palette through the image to get the fourcc */
	err = mpix_pipeline_get_palette_fourcc(img->first_op, palette);
	if (err) return; /* nothing to do */

	/* Apply it to all palette operations */
	err = mpix_pipeline_set_palette(img->first_op, palette);
	if (err) luaL_error(L, "failed to set the color palette: %s", strerror(-err));
}

static void lua_mpix_hooks(lua_State *L)
{
	lua_mpix_palette_hooks(L);
}

static int lua_mpix_run(lua_State *L)
{
	struct mpix_image *img = &lua_mpix_image;
	int err;

	/* P1: callback function, left on the top of the stack */

	/* To apply before running the pipeline */
	lua_mpix_hooks(L);

	if (img->last_op == NULL) {
		luaL_error(L, "empty pipeline, add operations to it first");
	}

	if (img->last_op->type != MPIX_OP_CALLBACK) {
		luaL_error(L, "the last operation must be: mpix.op.callback(buf_size, threshold)");
	}

	/* Store the Lua context on the last operation */
	((struct mpix_operation *)img->last_op)->L = L;

	/* Run the pipeline as long as there is room on the output buffer */
	err = mpix_pipeline_process(img->first_op, img->buffer, img->size);
	if (err) {
		mpix_print_pipeline(img->first_op);
		luaL_error(L, "failed to run the pipeline: %s", strerror(-err));
	}

	return 0;
}

static const struct luaL_Reg lua_mpix_reg[] = {
	{ "optimize_palette", lua_mpix_optimize_palette },
	{ "dump", lua_mpix_dump },
	{ "run", lua_mpix_run },
	{ "ctrl", lua_mpix_ctrl },
	{ "free", lua_mpix_free },
	{ "format", lua_mpix_format },
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

	/* Add 'mpix' as a library */
	lua_setglobal(L, "mpix");

	return 1;
}
