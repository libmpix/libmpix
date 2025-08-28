# SPDX-License-Identifier: Apache-2.0

add_executable(test_palettize_helium
    ${LIBMPIX_DIR}/tests/palettize/main.c
)

target_link_libraries(test_palettize_helium 
    -Wl,--whole-archive mpix_core -Wl,--no-whole-archive
    -lm
)
