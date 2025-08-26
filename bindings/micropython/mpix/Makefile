# Location of top-level MicroPython directory
MPY_DIR = ../../micropython

# Architecture to build for (x86, x64, armv6m, armv7m, xtensa, xtensawin)
ARCH = x64

# The ABI version for .mpy files
MPY_ABI_VERSION := 6.3

# Location of deps
LIBMPIX_DIR := ../../..

DIST_DIR := ../../dist/$(ARCH)_$(MPY_ABI_VERSION)

# enable linking of libm etc
LINK_RUNTIME=1

# Name of module
MOD = mpix

# Source files (.c or .py)
SRC = mod_mpix.c

# Releases
DIST_FILE = $(DIST_DIR)/$(MOD)_$(CONFIG).mpy
$(DIST_DIR):
	mkdir -p $@

$(DIST_FILE): $(MOD).mpy $(DIST_DIR)
	cp $< $@

# Include to get the rules for compiling and linking the module
include $(MPY_DIR)/py/dynruntime.mk

CFLAGS += -I$(LIBMPIX_DIR)/include -I$(LIBMPIX_DIR)/src -Wno-error=unused-variable -Wno-error=multichar -Wdouble-promotion

dist: $(DIST_FILE)
