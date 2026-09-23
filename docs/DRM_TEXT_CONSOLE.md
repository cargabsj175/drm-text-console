# DRM Text Console for RK3032 — Menu interactivo de testing

> Consola de texto via DRM/KMS para la GStick 4K Lite (RK3032).
> Renderiza un menú interactivo en HDMI usando libdrm directo, sin fbcon.
> Teclado USB via evdev (`/dev/input/eventX`).

---

## 1. Resumen

| Campo | Valor |
|-------|-------|
| Binario | `drm-text-console` (≈243 KB, ARM ELF32, stripped; se genera con `make`) |
| Dependencias runtime | `libdrm.so.2`, `libc.so.6` (GLIBC ≥ 2.4) |
| Resolución | 1280×720 (configurable via DRM) |
| Fuente | VGA 8×16 embebida (96 chars ASCII imprimibles) |
| Input | `/dev/input/eventX` (evdev, auto-detecta teclado) |
| Toolchain | Bootlin 2018.11 (gcc 7.3, glibc 2.27) |

### Menú disponible

1. **System Info** — `uname -a`, `/etc/os-release`
2. **CPU / Memory** — `/proc/cpuinfo`, `free -h`
3. **Storage** — `df -h`
4. **Network** — `ip addr show`
5. **GPU / DRM** — `/dev/dri/`, conectores, Mali info
6. **Input Devices** — `/dev/input/`, nombres de dispositivos
7. **Boot Log** — `dmesg | tail`
8. **Shell** — ejecuta `/bin/sh` (exit para volver al menú)
9. **Game Menu** — lanza entradas de `/mnt/sdcard/drm-text-console.cfg`
10. **About / Credits**

### Navegación

| Tecla | Acción |
|-------|--------|
| ↑ / ↓ / ← / → | Mover selección |
| Enter / Enter keypad | Seleccionar |
| Escape | Salir / volver al menú principal |
| 1-8 | Seleccionar directo (menú principal) |

### Game Menu (menú de juegos, `/mnt/sdcard/drm-text-console.cfg`)

| Tecla | Acción |
|-------|--------|
| ↑ / ↓ | Mover selección |
| PgUp / PgDn o ← / → | Cambiar página (8 entradas/página) |
| Enter | Lanzar entrada |
| 1-9 | Seleccionar entrada directa (relativo a la página) |
| Escape | Volver al menú principal |

El `.cfg` acepta dos formatos de línea (`#` = comentario, campos separados por `|`):

```text
# 1) Juego RetroArch:  Nombre | core_libretro.so | ruta_rom
Flappy (Intv) | freeintv_libretro.so | /mnt/sdcard/roms/INTV/flappy.int
# 2) Comando genérico:  Nombre | /usr/bin/programa [args...]
MiniGUI Demo  | /usr/bin/game
```

Miniaturas opcionales en `/mnt/sdcard/thumbs/<sistema>.png` (fallback `No_image.png`); la
asociación core→sistema está en `core_sys_map[]` (src/drm-text-console.c).

---

## 2. Arquitectura

```
┌─────────────────────────────────────────────────────┐
│                   drm-text-console                  │
│                                                     │
│  ┌──────────┐   ┌──────────┐   ┌────────────────┐  │
│  │ DRM/KMS  │   │  Font    │   │  Input (evdev) │  │
│  │ (libdrm) │   │  8×16    │   │  /dev/input/   │  │
│  └────┬─────┘   └────┬─────┘   └───────┬────────┘  │
│       │              │                  │           │
│       ▼              ▼                  ▼           │
│  ┌──────────────────────────────────────────────┐   │
│  │              Framebuffer (RGB565)            │   │
│  │         /dev/dri/card0 via DRM dumb buffer   │   │
│  └──────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────┘
         │
         ▼
    ┌─────────┐
    │  HDMI   │
    │  720p   │
    └─────────┘
```

### Flujo de datos

1. Abre `/dev/dri/card0` y obtiene connector + CRTC + mode
2. Crea dumb buffer (1280×720×32bpp) via `DRM_IOCTL_MODE_CREATE_DUMB`
3. Mapea buffer a userspace via `DRM_IOCTL_MODE_MAP_DUMB` + `mmap`
4. Renderiza caracteres en el framebuffer usando fuente bitmap embebida
5. Lee eventos de teclado via `/dev/input/eventX` (EV_KEY)
6. Ejecuta comandos del menú via `popen()`

---

## 3. Compilación

### Requisitos

- Toolchain: Bootlin `armv7-eabihf--glibc--stable-2018.11-1` (en `toolchain/`)
- Headers: `xf86drm.h`, `xf86drmMode.h`, `drm.h`, `drm_mode.h` (en `ra_source/ra/libdrm/`)
- Libs: `libdrm.so` (build2018 en `ra_source/ra/libdrm/build2018/`)

### Comandos

```bash
make              # cross-compile (rutas relativas al workspace: ../toolchain, ../ra_source)
make install      # copiar a ../work-rootfs-test/usr/bin/
make clean        # limpiar
```

> El repo espera estar dentro del workspace ROCKKCHIP (hermano de `toolchain/`,
> `ra_source/`, `work-rootfs-test/`). Se pueden sobreescribir con variables de
> entorno: `CROSS_COMPILE`, `SYSROOT`, `ROOTFS`, `LIBDRM_LIB`.

### Makefile (variables clave)

| Variable | Default | Descripción |
|----------|---------|-------------|
| `CROSS_COMPILE` | `../toolchain/.../arm-linux-` | Prefijo del toolchain |
| `SYSROOT` | `../toolchain/.../sysroot` | Sysroot del toolchain |
| `ROOTFS` | `../work-rootfs-test/` | Rootfs destino para install |
| `LIBDRM_SRC` | `../ra_source/ra/libdrm/` | Fuentes de libdrm (headers) |
| `LIBDRM_LIB` | `../ra_source/ra/libdrm/build2018/` | libdrm compilado (glibc 2.27) |

---

## 4. Integración en el rootfs

### Archivos modificados/en nuevos

| Archivo | Estado | Descripción |
|---------|--------|-------------|
| `usr/bin/drm-text-console` | **NUEVO** | Binario ARM (17,980 bytes) |
| `etc/init.d/S99testshell` | **NUEVO** | Init script que lanza el menú |
| `etc/init.d/S50ui` | **RENOMBRADO** a `.disabled` | Deshabilita RetroArch |

### Init script `S99testshell`

```sh
#!/bin/sh
# Espera a que /dev/dri/card0 aparezca (udevd)
# Configura HDMI via drm-hotplug.sh
# Lanza drm-text-console en foreground (bloquea)
case "$1" in
  start) exec /usr/bin/drm-text-console ;;
  stop)  killall -9 drm-text-console ;;
esac
```

### Orden de boot

```
init → rcS → S00bootlog → S01logging → S10udev → S20urandom → S21mountall.sh
     → S35heartbeat → S40network → S49usbdevice → S50dropbear → S99testshell
```

### Cambios en `work-rootfs-test/` (debug sin UART)

1. **`S00bootlog`** — log temprano a `/tmp/boot.log` (tmpfs).
2. **`S21mountall.sh`** — guajeado: si `/dev/block/by-name/misc` no existe,
   `OEM_CMD` queda vacío (antes `strings ""` colgaba rcS leyendo de stdin).
3. **`S35heartbeat`** — escribe `heartbeat.txt` en `/sdcard` / `/mnt/sdcard` / `/data`
   (best-effort; intenta montar p5 exFAT) para verificar el boot desde un PC.
4. **`S49usbdevice`** — `.usb_config` ahora solo comentarios → el gadget USB ADB queda
   **desactivado**; la única entrada USB queda libre para el teclado 2.4GHz.
5. **`S99testshell`** — sin `drm-hotplug.sh` (podía pelear por el master DRM); lanza
   `drm-text-console` y reintenta 3× antes de caer a `/bin/sh`.
6. **libdrm** — sustituido por el build2018 (ver "Binary no ejecuta").

---

## 5. Empaquetado

```bash
# Crear squashfs
mksquashfs work-rootfs-test/ new_rootfs_shell-tests.squashfs -comp gzip -noappend

# Verificar tamaño
ls -lh new_rootfs_shell-tests.squashfs
# ~22 MB (vs ~21 MB del original con RetroArch)

# Flashear a partición rootfs (p4, offset 0x1000000, 65 MiB)
sudo dd if=new_rootfs_shell-tests.squashfs of=/dev/sde4 bs=4M conv=fsync status=progress

# Zero-fill restante de la partición
# Calcular: bloques_4k = (tamaño_squashfs + 4095) / 4096
# Total bloques p4 = 65*1024*1024/4096 = 16640
# zero_fill = 16640 - bloques_squashfs
```

---

## 6. Solución de problemas

### HDMI no muestra nada

1. Verificar que `/dev/dri/card0` existe (udevd debe correr primero)
2. Verificar con `kms-steal-crtc` si el CRTC está activo
3. Probar `drm-hotplug.sh` manualmente
4. Verificar cable HDMI y monitor

### Teclado no responde

1. Verificar `/dev/input/event*` con `evtest`
2. El script busca automáticamente el primer device con `KEY_A`
3. Dongle 2.4GHz: verificar que udev lo detecta

### Binary no ejecuta

1. Verificar arquitectura: `readelf -h drm-text-console | grep Machine` → ARM
2. Verificar dependencias: `readelf -d drm-text-console | grep NEEDED`
3. Verificar GLIBC: `readelf -V drm-text-console | grep GLIBC` → ≤ 2.4
4. **CRÍTICO — libdrm del rootfs:** la fábrica `work-rootfs/usr/lib/libdrm.so.2.4.0` exige
   `GLIBC_2.29` (`log2`); el binario (toolchain glibc 2.27) **no carga**. En el rootfs de
   test hay que sustituir el soname `libdrm.so.2` por el build propio `libdrm.so.2.134.0`
   (mesa/drm 2.4.134, max `GLIBC_2.17`, SONAME `libdrm.so.2`):
   ```
   cp -P ra_source/ra/libdrm/build2018/libdrm.so.2.134.0 work-rootfs-test/usr/lib/
   ln -sf libdrm.so.2.134.0 work-rootfs-test/usr/lib/libdrm.so.2
   ln -sf libdrm.so.2.134.0 work-rootfs-test/usr/lib/libdrm.so
   ```
   Verificar: `readelf --version-info... | grep GLIBC` ≤ 2.17.

### Shell no funciona (opción 8 del menú)

1. Verificar que `/bin/sh` existe y es ejecutable
2. El shell se ejecuta via `fork()+exec()`, hereda stdio
3. `exit` vuelve al menú

---

## 7. Limitaciones conocidas

- **No hay scrollback**: el buffer es un solo frame, no hay terminal virtual
- **No hay cursor parpadeante**: renderizado estático
- **No hay soporte Unicode**: solo ASCII 0x20-0x7F
- **Resolución fija**: 1280×720 (cambiar `TARGET_W`/`TARGET_H` y recompilar)
- **Un solo buffer**: no hay double-buffering (posible flicker)
- **popen() lento**: los comandos del menú se ejecutan secuencialmente

---

## 8. Roadmap futuro

- [ ] Double-buffering con page flip (elimina flicker)
- [ ] Soporte para resoluciones múltiples (auto-detectar)
- [ ] Terminal virtual completa (scrollback, cursor, ANSI escapes)
- [ ] Soporte para mouse/touchscreen
- [ ] Menú de configuración (brightness, audio, network)

---

## 9. Archivos

| Ruta | Descripción |
|------|-------------|
| `src/drm-text-console.c` | Fuente C (binario resultante ≈244 KB) |
| `src/generate.py` | Genera `src/drm-text-console.c` con la fuente embebida |
| `Makefile` | Makefile para cross-compilation (rutas relativas) |
| `third_party/` | Headers + libs estáticas ARM (libpng16, zlib) |
| `examples/S99testshell` | Init script de arranque (`/etc/init.d/S99testshell`) |
| `examples/drm-text-console.cfg` | Config de juegos (formato documentado arriba) |
| `sysroot-install/` | Scripts e instrucciones de instalación (sysroot/rootfs) |
| `docs/DRM_TEXT_CONSOLE.md` | Esta documentación |
| `../work-rootfs-test/` | Rootfs de testing (fuera del repo) |
| `../new_rootfs_shell-tests.squashfs` | Squashfs empaquetado (fuera del repo) |

---

## 10. Créditos

- **Fuente bitmap VGA 8×16**: estándar IBM VGA (CP437)
- **Librería DRM**: mesa/drm (libdrm 2.4.134, build2018)
- **Toolchain**: Bootlin ARM v7 glibc 2.27 (gcc 7.3.0)
- **Hardware**: Rockchip RK3032/RK3036, Mali-400 MP1

---

*Documento generado: 2026-09-21*
*Estado: kernel bota (devtmpfs+exFAT ok), card0 1280x720 visible, AddFB2 XRGB8888 fallaba EINVAL. v4: multi-formato + fallback legacy AddFB + dump de formatos de plano. Pendiente de test en hardware real.*
