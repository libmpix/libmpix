# Changelog

Changelog of breaking changes across versions.

## v1.1.0

* Rename MPIX_CID_COLOR_MATRIX into MPIX_CID_COLOR_MATRIX_0 and _1, _2, _3... _8
  The mpix_image_ctrl_array() function can still be used on MPIX_CID_COLOR_MATRIX_0
  with a size of 9

* Add a `size` parameter to mpix_image_ctrl_array() and remove mpix_image_ctrl_size().
  Setting a control array now writes each value of the array to an incrementing control ID.
