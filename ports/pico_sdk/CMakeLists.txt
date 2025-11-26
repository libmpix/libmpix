# SPDX-License-Identifier: Apache-2.0

add_library(libmpix INTERFACE)

function(libmpix_add_include_directory target directory)
  target_include_directories(${target} PRIVATE ${directory})
endfunction()

file(REAL_PATH ../.. LIBMPIX_DIR)
include(${LIBMPIX_DIR}/ports/common.cmake)

target_compile_options(libmpix INTERFACE -O3 -Wall -Wextra)

target_sources(libmpix INTERFACE
  ${LIBMPIX_SRC}
  ${LIBMPIX_DIR}/ports/pico_sdk/port.c
)
