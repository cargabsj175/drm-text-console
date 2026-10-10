# Makefile for drm-text-console (cross-compile for RK3032)
#
# Usage:
#   make                    # cross-compile with Bootlin 2018.11
#   make clean              # remove build artifacts
#   make install            # copy binary into the target rootfs (ROOTFS)
#
# The repo does NOT need to live inside the workspace that holds the toolchain
# and source siblings. Point DTC_WORKSPACE at that workspace (e.g. export it in
# ~/.bashrc) or override the individual paths. If DTC_WORKSPACE is unset, the
# legacy layout (repo directly inside the workspace) is assumed.
#
#   export DTC_WORKSPACE=/path/to/ROCKCHIP   # has toolchain/, ra_source/, work-rootfs-test/
#
# Individual overrides: CROSS_COMPILE, SYSROOT, ROOTFS, LIBDRM_SRC, LIBDRM_LIB.

DTC_WORKSPACE ?= $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/..)

CROSS_COMPILE ?= $(DTC_WORKSPACE)/toolchain/armv7-eabihf--glibc--stable-2018.11-1/bin/arm-linux-
CC   = $(CROSS_COMPILE)gcc
STRIP = $(CROSS_COMPILE)strip

SYSROOT ?= $(DTC_WORKSPACE)/toolchain/armv7-eabihf--glibc--stable-2018.11-1/arm-buildroot-linux-gnueabihf/sysroot
ROOTFS  ?= $(DTC_WORKSPACE)/work-rootfs-test

# DRM headers from the toolchain sysroot
DRM_INC = $(SYSROOT)/usr/include
LIBDRM_SRC ?= $(DTC_WORKSPACE)/ra_source/ra/libdrm
LIBDRM_LIB ?= $(DTC_WORKSPACE)/ra_source/ra/libdrm/build2018
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

.PHONY: all clean install check-toolchain

all: $(TARGET)

# Clear error if the toolchain does not resolve, instead of "arm-linux-gcc: not found".
check-toolchain:
	@command -v $(CC) >/dev/null 2>&1 || { \
		echo "ERROR: no encuentro el compilador $(CC)"; \
		echo "Define DTC_WORKSPACE (export DTC_WORKSPACE=/ruta/al/workspace) o CROSS_COMPILE."; \
		exit 1; }

$(TARGET): $(SRC) | check-toolchain
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)
	$(STRIP) $@
	@ls -la $@
	@readelf -V $@ | grep -oE 'GLIBC_[0-9.]+' | sort -uV | tail -1

clean:
	rm -f $(TARGET)

install: $(TARGET)
	install -d $(ROOTFS)/usr/bin
	install -m 0755 $(TARGET) $(ROOTFS)/usr/bin/$(TARGET)
	install -d $(ROOTFS)/usr/share/drm-text-console
	install -m 0644 share/fondo-menu-1080.png \
	           $(ROOTFS)/usr/share/drm-text-console/fondo.png
	@echo "Installed to $(ROOTFS)/usr/bin/$(TARGET)"
	@echo "Remember to rebuild squashfs: mksquashfs work-rootfs-test/ new_rootfs_shell-tests.squashfs -comp gzip -noappend"