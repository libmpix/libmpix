# Changelog

Changelog of breaking changes across versions.

## `v1.2.1`

* Fix the lua integration of palette encode/decode operations

## `v1.2.0`

* Introduce a new memory allocation feature to allow user to select where buffers will be
  allocated from by setting `img->mem_source` to one of the value from the enum (which might
  be an user-define value if above `MPIX_MEM_SOURCE_CUSTOM0`). Then, this value will be passed
  to the `mpix_port_alloc()` which can then decide on how to handle it.

## `v1.1.0`

* Rename `MPIX_CID_COLOR_MATRIX` into `MPIX_CID_COLOR_MATRIX_0` and `_1`, `_2`, `_3`... `_8`.
  The `mpix_image_ctrl_array()` function can still be used on `MPIX_CID_COLOR_MATRIX_0`
  with a size of 9

* Add a `size` parameter to `mpix_image_ctrl_array()` and remove `mpix_image_ctrl_size()`.
  Setting a control array now writes each value of the array to an incrementing control ID.
