# Makefile for drm-text-console (cross-compile for RK3032)
#
# Usage:
#   make                    # cross-compile with Bootlin 2018.11
#   make clean              # remove build artifacts
#   make install            # copy binary into the target rootfs (ROOTFS)
#
# Paths are relative to this repo: sibling dirs live one level up
# (../toolchain, ../ra_source, ../work-rootfs-test). Override with env vars
# if the tree differs (CROSS_COMPILE, SYSROOT, ROOTFS, LIBDRM_LIB).

TOPDIR     = $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)
CROSS_COMPILE ?= $(TOPDIR)/toolchain/armv7-eabihf--glibc--stable-2018.11-1/bin/arm-linux-
CC   = $(CROSS_COMPILE)gcc
STRIP = $(CROSS_COMPILE)strip

SYSROOT = $(TOPDIR)/toolchain/armv7-eabihf--glibc--stable-2018.11-1/arm-buildroot-linux-gnueabihf/sysroot
ROOTFS  ?= $(TOPDIR)/work-rootfs-test

# DRM headers from the toolchain sysroot
DRM_INC = $(SYSROOT)/usr/include
LIBDRM_SRC = $(TOPDIR)/ra_source/ra/libdrm
LIBDRM_LIB = $(TOPDIR)/ra_source/ra/libdrm/build2018
# statically built libpng16 + zlib (ARM), see third_party/README
PNG_A = third_party/lib/libpng16.a
ZLIB_A = third_party/lib/libz.a

# CFLAGS: ARMv7-A + NEON + hardfloat, glibc 2.27 compatible
CFLAGS = -O2 -marm -march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard \
         -Wall -Wextra -Wno-unused-parameter \
         -I$(DRM_INC) -I$(LIBDRM_SRC) -I$(LIBDRM_SRC)/include -I$(LIBDRM_SRC)/include/drm \
         -Ithird_party/include \
         -fno-stack-protector -U_FORTIFY_SOURCE

# LDFLAGS: link against rootfs libs (libdrm.so.2 is on the target)
LDFLAGS = -L$(LIBDRM_LIB) -L$(ROOTFS)/usr/lib -L$(SYSROOT)/usr/lib -ldrm $(PNG_A) $(ZLIB_A) -lm

TARGET = drm-text-console
SRC    = src/drm-text-console.c

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	$(STRIP) $@
	@ls -la $@
	@readelf -V $@ | grep -oE 'GLIBC_[0-9.]+' | sort -uV | tail -1

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -d $(ROOTFS)/usr/bin
	install -m 0755 $(TARGET) $(ROOTFS)/usr/bin/$(TARGET)
	@echo "Installed to $(ROOTFS)/usr/bin/$(TARGET)"
	@echo "Remember to rebuild squashfs: mksquashfs work-rootfs-test/ new_rootfs_shell-tests.squashfs -comp gzip -noappend"