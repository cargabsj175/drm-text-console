# drm-text-console

Consola de texto vía **DRM/KMS** para la GStick 4K Lite (Rockchip RK3032).
Renderiza un menú interactivo en HDMI usando `libdrm` directo (sin fbcon) y lee
teclado USB vía `evdev` (`/dev/input/eventX`).

## Layout del repo

```
.
├── src/
│   ├── drm-text-console.c     # fuente C (menu + Game Menu + PNG thumbs)
│   └── generate.py            # genera la fuente embebida (VGA 8x16)
├── docs/
│   └── DRM_TEXT_CONSOLE.md    # documentacion completa (uso, arquitectura, troubleshooting)
├── examples/
│   ├── S99testshell           # init script de arranque (/etc/init.d/S99testshell)
│   └── drm-text-console.cfg   # ejemplo de menu de juegos (tarjeta SD)
├── sysroot-install/
│   ├── install-sysroot.sh     # instala binario+runtime en el rootfs de testing
│   └── README.md              # instrucciones de instalacion en sysroot/rootfs
├── third_party/               # headers + libs estaticas ARM (libpng16, zlib)
└── Makefile
```

## Build rapido

```bash
make          # cross-compile con toolchain Bootlin 2018.11 (../toolchain)
make install  # copiar binario a ../work-rootfs-test/usr/bin/
```

El Makefile usa rutas **relativas al workspace** (`../toolchain`, `../ra_source`,
`../work-rootfs-test`). Sobreescribibles por entorno: `CROSS_COMPILE`, `SYSROOT`,
`ROOTFS`, `LIBDRM_LIB`.

## Instalacion en el rootfs

```bash
./sysroot-install/install-sysroot.sh
# reempaquetar squashfs (manual):
mksquashfs ../work-rootfs-test ../new_rootfs_shell-tests.squashfs -comp gzip -noappend
```

Detalles y opciones en [sysroot-install/README.md](sysroot-install/README.md).

## Dependencias

| Componente | Notas |
|------------|-------|
| `libdrm.so.2` | Build propio `libdrm.so.2.134.0` (mesa/drm, GLIBC ≤ 2.17); el del rootfs pide GLIBC_2.29 |
| `libpng16.a` + `libz.a` | Estaticos ARM en `third_party/lib/` (miniaturas PNG) |
| `libc.so.6` | GLIBC ≥ 2.4 |

## Motores de compatibilidad

- Menú principal: 10 items (System Info, CPU/Mem, Storage, Network, GPU/DRM, Input,
  Boot Log, Shell, Game Menu, About).
- **Game Menu**: `<nombre> | <core> | <rom>` o `<nombre> | <comando>` en
  `/mnt/sdcard/drm-text-console.cfg` (ver `examples/drm-text-console.cfg`). Lanza
  RetroArch `/usr/bin/retroarch --config /etc/retroarch.cfg -L <core> <rom>`.
- Miniaturas PNG por sistema en `/mnt/sdcard/thumbs/<sys>.png`.

## Licencia

MIT — ver `LICENSE`. Dependencias incluidas (libdrm, libpng, zlib) con licencias
permisivas; detalles en `THIRD_PARTY_NOTICES.md`.