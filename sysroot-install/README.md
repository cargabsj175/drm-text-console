# Instalacion en sysroot / rootfs

Este directorio prepara el entorno para empaquetar `drm-text-console` en un
rootfs de testing **sin flashear nada** (solo edita el directorio `work-rootfs-test`).

## Uso

```bash
# Instalar binario + libdrm + S99testshell en ../work-rootfs-test
./install-sysroot.sh

# Igual, pero ademas copia headers DRM al sysroot de la toolchain
INSTALL_DRM_HEADERS=1 ./install-sysroot.sh

# Destinos custom
ROOTFS=/ruta/rootfs LIBDRM_LIB=/ruta/libdrm ./install-sysroot.sh
```

## Que hace

| Paso | Accion | Detalle |
|------|--------|---------|
| 1 | `make` | Compila `src/drm-text-console.c` (rutas relativas al workspace) |
| 2 | Copia binario | `$ROOTFS/usr/bin/drm-text-console` |
| 3 | Sustituye `libdrm.so.2` | Rootfs: `libdrm.so.2.134.0` (GLIBC ≤ 2.17) en vez de la de fabrica `libdrm.so.2.4.0` (GLIBC_2.29, no carga el binario) |
| 4 | Init script | `$ROOTFS/etc/init.d/S99testshell` desde `examples/S99testshell` |

## Requisitos

- Workspace `../` con: `toolchain/` (Bootlin 2018.11), `ra_source/ra/libdrm/build2018/`,
  `work-rootfs-test/`.
- `mksquashfs` (host) solo para el paso manual de empaquetado.

## Despues

1. `mksquashfs ../work-rootfs-test ../new_rootfs_shell-tests.squashfs -comp gzip -noappend`
2. Verificar tamaño y flashear (manual, con `dd` a la particion rootfs p4) — esto ya es
   trabajo de laboratorio externo al repo.
3. Para proporciar texto visible en pantalla durante la depuración de boot, ver
   `examples/S99testshell` (log a `/tmp/boot.log`, heartbeat a `/sdcard`).