// Include the header file to get access to the MicroPython API
#ifdef MICROPY_ENABLE_DYNRUNTIME
#include "py/dynruntime.h"
#else
#include "py/runtime.h"
#endif

#include <mpix/image.h>

#include <string.h>

#define DEBUG (0)


#ifdef MICROPY_ENABLE_DYNRUNTIME
// memset is used by some standard C constructs
#if !defined(__linux__)
void *memcpy(void *dst, const void *src, size_t n) {
    return mp_fun_table.memmove_(dst, src, n);
}
void *memset(void *s, int c, size_t n) {
    return mp_fun_table.memset_(s, c, n);
}
#endif
#endif

// MicroPython type
typedef struct _mp_obj_mod_mpix_t {
    mp_obj_base_t base;

    struct mpix_image image;
} mp_obj_mod_mpix_t;

#if MICROPY_ENABLE_DYNRUNTIME
mp_obj_full_type_t mod_mpix_type;
#else
static const mp_obj_type_t mod_mpix_type;
#endif


// TODO: add function for getting the shape of expected input. As a tuple

// Create a new instance
static mp_obj_t mod_mpix_new(mp_obj_t data_obj,
    mp_obj_t width_obj,
    mp_obj_t height_obj,
    mp_obj_t format_obj)
{

    // TODO: keep a pointer to data_obj, or explicity ref

    // Check model data
    mp_buffer_info_t bufinfo;
    // TOOD: use only READ?
    mp_get_buffer_raise(model_data_obj, &bufinfo, MP_BUFFER_RW);

#if DEBUG
    mp_printf(&mp_plat_print, "mpix-image-new data.typecode=%c \n", bufinfo.typecode);
#endif

    if (bufinfo.typecode != 'B') {
        mp_raise_ValueError(MP_ERROR_TEXT("model should be bytes"));
    }
    uint8_t *data_buffer = bufinfo.buf;
    const int data_length = bufinfo.len / sizeof(*data_buffer);

    // Construct object
    mp_obj_mod_mpix_t *o = mp_obj_malloc(mp_obj_mod_mpix_t, (mp_obj_type_t *)&mod_mpix_type);
    tm_mdl_t *model = &o->model;

    mp_int_t width = mp_obj_get_int(width_obj);
    mp_int_t height = mp_obj_get_int(height_obj);
    // FIXME: check that width/height are sane


    mpix_image_from_buf(&o->image, data_buffer, data_length, width, height, format);

#if 0
    // TODO: check .error 
    if (outputs != 1) {
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("only 1 output supported"));
    }
#endif

#if DEBUG
    mp_printf(&mp_plat_print, "cnn-new-done in.dims=(%d,%d,%d,%d) out.dims=(%d,%d,%d,%d) \n",
        o->input.dims, o->input.h, o->input.w, o->input.c,
        o->out_dims[0], o->out_dims[1], o->out_dims[2], o->out_dims[3]);
#endif

    return MP_OBJ_FROM_PTR(o);
}
static MP_DEFINE_CONST_FUN_OBJ_3(mod_mpix_new_obj, mod_mpix_new);

// Delete the instance
static mp_obj_t mod_mpix_del(mp_obj_t self_obj) {

    mp_obj_mod_mpix_t *o = MP_OBJ_TO_PTR(self_obj);
    tm_mdl_t *model = &o->model;

    mpix_image_free(struct mpix_image *img);

    m_del(uint8_t, o->model_buffer, o->model_buffer_length);
    m_del(uint8_t, o->data_buffer, o->data_buffer_length);

    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(mod_mpix_del_obj, mod_mpix_del);


#ifdef MICROPY_ENABLE_DYNRUNTIME // natmod
mp_map_elem_t mod_locals_dict_table[1];
static MP_DEFINE_CONST_DICT(mod_locals_dict, mod_locals_dict_table);

// This is the entry point and is called when the module is imported
mp_obj_t mpy_init(mp_obj_fun_bc_t *self, size_t n_args, size_t n_kw, mp_obj_t *args) {
    // This must be first, it sets up the globals dict and other things
    MP_DYNRUNTIME_INIT_ENTRY

    mp_store_global(MP_QSTR_new, MP_OBJ_FROM_PTR(&mod_mpix_new_obj));

    mod_mpix_type.base.type = (void*)&mp_fun_table.type_type;
    mod_mpix_type.flags = MP_TYPE_FLAG_ITER_IS_CUSTOM;
    mod_mpix_type.name = MP_QSTR_tinymaixcnn;
    // methods
    mod_locals_dict_table[0] = (mp_map_elem_t){ MP_OBJ_NEW_QSTR(MP_QSTR___del__), MP_OBJ_FROM_PTR(&mod_mpix_del_obj) };

    MP_OBJ_TYPE_SET_SLOT(&mod_mpix_type, locals_dict, (void*)&mod_locals_dict, 1);

    // This must be last, it restores the globals dict
    MP_DYNRUNTIME_INIT_EXIT
}
#else // extmod
#error "extmod not supported"
#endif

