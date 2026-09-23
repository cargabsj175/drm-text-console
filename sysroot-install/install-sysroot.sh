#!/bin/sh
#
# install-sysroot.sh - Instala drm-text-console y su runtime en sysroot/rootfs
#
# Pasos:
#   1. Compila el binario (make) con rutas del workspace
#   2. Instala el binario en el rootfs destino (ROOTFS, default ../work-rootfs-test)
#   3. Sustituye libdrm.so.2 del rootfs por el build propio (evita GLIBC_2.29)
#   4. Instala el init script S99testshell (ejemplo en examples/S99testshell)
#   5. Opcional: copia headers de libdrm al sysroot de la toolchain
#
# No flashea nada; solo prepara el rootfs para reempaquetar squashfs.

set -e

# Workspace: hermano de este repo
WS=$(cd "$(dirname "$0")/.." && pwd)
ROOTFS=${ROOTFS:-"$WS/work-rootfs-test"}
SYSROOT=${SYSROOT:-"$WS/toolchain/armv7-eabihf--glibc--stable-2018.11-1/arm-buildroot-linux-gnueabihf/sysroot"}
LIBDRM_LIB=${LIBDRM_LIB:-"$WS/ra_source/ra/libdrm/build2018"}
LIBDRM_SRC=${LIBDRM_SRC:-"$WS/ra_source/ra/libdrm"}
REPO=$(cd "$(dirname "$0")" && pwd)
LIBDEST="${LIBDRM_LIB}/libdrm.so.2.134.0"

echo "==> 1/4 Compilando binario"
make -C "$REPO" CROSS_COMPILE="$WS/toolchain/armv7-eabihf--glibc--stable-2018.11-1/bin/arm-linux-"

echo "==> 2/4 Instalando binario en $ROOTFS/usr/bin"
install -d "$ROOTFS/usr/bin"
install -m 0755 "$REPO/drm-text-console" "$ROOTFS/usr/bin/drm-text-console"

echo "==> 3/4 Sustituyendo libdrm.so.2 del rootfs"
if [ -e "$LIBDEST" ]; then
    cp -P "$LIBDEST" "$ROOTFS/usr/lib/"
    ln -sf libdrm.so.2.134.0 "$ROOTFS/usr/lib/libdrm.so.2"
    ln -sf libdrm.so.2.134.0 "$ROOTFS/usr/lib/libdrm.so"
    echo "    libdrm -> libdrm.so.2.134.0 (GLIBC <= 2.17, SONAME libdrm.so.2)"
else
    echo "    [WARN] no existe $LIBDEST; se deja libdrm del rootfs"
fi

echo "==> 4/4 Instalando init script S99testshell"
if [ -e "$REPO/examples/S99testshell" ]; then
    install -d "$ROOTFS/etc/init.d"
    install -m 0755 "$REPO/examples/S99testshell" "$ROOTFS/etc/init.d/S99testshell"
else
    echo "    [WARN] falta $REPO/examples/S99testshell"
fi

# Opcional: copiar headers DRM al sysroot para compilar sin ra_source
if [ -n "$INSTALL_DRM_HEADERS" ]; then
    echo "=> [opt] Copiando headers DRM a $SYSROOT/usr/include/drm"
    install -d "$SYSROOT/usr/include/drm"
    cp -f "$LIBDRM_SRC"/include/drm/*.h "$SYSROOT/usr/include/drm/"
    cp -f "$LIBDRM_SRC"/include/libdrm/*.h "$SYSROOT/usr/include/" 2>/dev/null || true
fi

echo "==> Listo."
echo "Reempaquetar squashfs (manual, no se flashea nada):"
echo "  mksquashfs $ROOTFS $WS/new_rootfs_shell-tests.squashfs -comp gzip -noappend"