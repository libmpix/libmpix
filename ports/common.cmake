# SPDX-License-Identifier: Apache-2.0

# CMake declarations common to all CMake-based ports

set_property(GLOBAL PROPERTY LIBMPIX_DIR ${LIBMPIX_DIR})

set(LIBMPIX_SRC
  ${LIBMPIX_DIR}/src/JPEGENC.c
  ${LIBMPIX_DIR}/src/auto.c
  ${LIBMPIX_DIR}/src/formats.c
  ${LIBMPIX_DIR}/src/image.c
  ${LIBMPIX_DIR}/src/op_convert.c
  ${LIBMPIX_DIR}/src/op_correction.c
  ${LIBMPIX_DIR}/src/op_crop.c
  ${LIBMPIX_DIR}/src/op_debayer.c
  ${LIBMPIX_DIR}/src/op_jpeg.c
  ${LIBMPIX_DIR}/src/op_kernel.c
  ${LIBMPIX_DIR}/src/op_palettize.c
  ${LIBMPIX_DIR}/src/op_qoi.c
  ${LIBMPIX_DIR}/src/op_resize.c
  ${LIBMPIX_DIR}/src/print.c
  ${LIBMPIX_DIR}/src/sample.c
  ${LIBMPIX_DIR}/src/stats.c
  ${LIBMPIX_DIR}/src/str.c
  ${LIBMPIX_DIR}/src/utils.c
)

function(libmpix_init target)
  get_property(genlist_src GLOBAL PROPERTY LIBMPIX_GENLIST_SRC)
  get_property(libmpix_dir GLOBAL PROPERTY LIBMPIX_DIR)
  set(genlist_py ${libmpix_dir}/scripts/genlist.py)
  set(genlist_inc ${CMAKE_CURRENT_BINARY_DIR}/include)
  set(genlist_h ${CMAKE_CURRENT_BINARY_DIR}/include/mpix/genlist.h)

  file(MAKE_DIRECTORY ${genlist_inc}/mpix)

  add_custom_command(
    COMMAND python ${genlist_py} ${genlist_src} >${genlist_h}
    OUTPUT ${genlist_h}
    DEPENDS ${genlist_src}
  )

  libmpix_add_include_directory(${target} ${genlist_inc})
  libmpix_add_include_directory(${target} ${libmpix_dir}/include)
  add_custom_target(libmpix_genlist_h DEPENDS ${genlist_h})
  add_dependencies(${target} libmpix_genlist_h)
endfunction()

function(libmpix_add_genlist_source ...)
  set_property(GLOBAL APPEND PROPERTY LIBMPIX_GENLIST_SRC ${ARGV})
endfunction()

libmpix_add_genlist_source(${LIBMPIX_SRC})
