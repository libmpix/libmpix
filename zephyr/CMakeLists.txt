# SPDX-License-Identifier: Apache-2.0

# CMake rules to build libmpix in an firmware image, for any board supporteded by Zephyr.

zephyr_library()

# Zephyr-specific implementations

function(libmpix_add_include_directory target directory)
  zephyr_include_directories(${directory})
endfunction()

# Common implementations

file(REAL_PATH .. LIBMPIX_DIR)
include(${LIBMPIX_DIR}/ports/common.cmake)

if(Zephyr_VERSION VERSION_EQUAL "3.6")
  message(STATUS "Using Zephyr 3.6.x port for libmpix")
  list(APPEND LIBMPIX_SRC ${LIBMPIX_DIR}/ports/zephyr_3_6/port.c)

elseif(Zephyr_VERSION VERSION_GREATER "3.6")
  message(STATUS "Using Zephyr main port for libmpix")
  list(APPEND LIBMPIX_SRC ${LIBMPIX_DIR}/ports/zephyr_main/port.c)

else()
  message(FATAL_ERROR "unable to find a supported Zephyr version")
endif()

zephyr_library_sources_ifdef(CONFIG_MPIX ${LIBMPIX_SRC})
