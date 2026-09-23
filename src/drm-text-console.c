/*
 * drm-text-console.c - Consola de texto via DRM/KMS para RK3032
 *
 * Renderiza menu interactivo de testing en HDMI usando libdrm directo.
 * Lee teclado USB via /dev/input/eventX (evdev).
 * Cross-compile con Bootlin 2018.11: arm-linux-gcc -O2 -o drm-text-console ...
 *
 * Dependencias runtime: libdrm.so.2, libc.so.6 (GLIBC >= 2.4)
 * Hardware: Mali-400 + DRM/KMS (1280x720@60)
 *
 * Autor: NeonatoX - 2026-09-21
 * Licencia: MIT
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <signal.h>
#include <dirent.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <poll.h>
#include <linux/input.h>
#include <stdarg.h>
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <png.h>

/* --- Configuracion --- */
#define BUILD_VSN   9
#define TARGET_W    1280
#define TARGET_H    720
#define FONT_W      8
#define FONT_H      16
#define COLS        (TARGET_W / FONT_W)
#define ROWS        (TARGET_H / FONT_H)
#define DRM_DEV     "/dev/dri/card0"
#define INPUT_GLOB  "/dev/input/"
#define MAX_CMD     256
#define MAX_OUTPUT  (ROWS * COLS)

/* --- Colores RGB565 --- */
#define COLOR_BG        0x0000
#define COLOR_FG        0xFFFF
#define COLOR_HL        0xF81F  /* magenta highlight */
#define COLOR_DIM       0x4208  /* gris oscuro */
#define COLOR_TITLE     0x07FF  /* cyan */
#define COLOR_OK        0x07E0  /* verde */
#define COLOR_WARN      0xFFE0  /* amarillo */
#define COLOR_ERR       0xF800  /* rojo */

/* --- Font data (VGA 8x16, 96 printable ASCII 0x20-0x7F) --- */
static const unsigned char font_data[] = {
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x18,0x3c,0x3c,0x3c,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00,
    0x00,0x66,0x66,0x66,0x24,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x6c,0x6c,0xfe,0x6c,0x6c,0xfe,0x6c,0x6c,0x00,0x00,0x00,0x00,0x00,
    0x18,0x18,0x7c,0xc6,0xc2,0xc0,0x7c,0x06,0x06,0x86,0xc6,0x7c,0x18,0x18,0x00,0x00,
    0x00,0x00,0x00,0x00,0xc2,0xc6,0x0c,0x18,0x30,0x60,0xc6,0x86,0x00,0x00,0x00,0x00,
    0x00,0x00,0x38,0x6c,0x6c,0x38,0x76,0xdc,0xcc,0xcc,0xcc,0x76,0x00,0x00,0x00,0x00,
    0x00,0x30,0x30,0x30,0x60,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x0c,0x18,0x30,0x30,0x30,0x30,0x30,0x30,0x18,0x0c,0x00,0x00,0x00,0x00,
    0x00,0x00,0x30,0x18,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x18,0x30,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x66,0x3c,0xff,0x3c,0x66,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x7e,0x18,0x18,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x18,0x30,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xfe,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x02,0x06,0x0c,0x18,0x30,0x60,0xc0,0x80,0x00,0x00,0x00,0x00,
    0x00,0x00,0x7c,0xc6,0xc6,0xce,0xde,0xf6,0xe6,0xc6,0xc6,0x7c,0x00,0x00,0x00,0x00,
    0x00,0x00,0x18,0x38,0x78,0x18,0x18,0x18,0x18,0x18,0x18,0x7e,0x00,0x00,0x00,0x00,
    0x00,0x00,0x7c,0xc6,0x06,0x0c,0x18,0x30,0x60,0xc0,0xc6,0xfe,0x00,0x00,0x00,0x00,
    0x00,0x00,0x7c,0xc6,0x06,0x06,0x3c,0x06,0x06,0x06,0xc6,0x7c,0x00,0x00,0x00,0x00,
    0x00,0x00,0x0c,0x1c,0x3c,0x6c,0xcc,0xfe,0x0c,0x0c,0x0c,0x1e,0x00,0x00,0x00,0x00,
    0x00,0x00,0xfe,0xc0,0xc0,0xc0,0xfc,0x06,0x06,0x06,0xc6,0x7c,0x00,0x00,0x00,0x00,
    0x00,0x00,0x38,0x60,0xc0,0xc0,0xfc,0xc6,0xc6,0xc6,0xc6,0x7c,0x00,0x00,0x00,0x00,
    0x00,0x00,0xfe,0xc6,0x06,0x06,0x0c,0x18,0x30,0x30,0x30,0x30,0x00,0x00,0x00,0x00,
    0x00,0x00,0x7c,0xc6,0xc6,0xc6,0x7c,0xc6,0xc6,0xc6,0xc6,0x7c,0x00,0x00,0x00,0x00,
    0x00,0x00,0x7c,0xc6,0xc6,0xc6,0x7e,0x06,0x06,0x06,0x0c,0x78,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x18,0x18,0x00,0x00,0x00,0x18,0x18,0x30,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x0c,0x18,0x30,0x60,0xc0,0x60,0x30,0x18,0x0c,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x7e,0x00,0x00,0x7e,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x60,0x30,0x18,0x0c,0x06,0x0c,0x18,0x30,0x60,0x00,0x00,0x00,0x00,
    0x00,0x00,0x7c,0xc6,0xc6,0x0c,0x18,0x18,0x18,0x00,0x18,0x18,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x7c,0xc6,0xc6,0xde,0xde,0xde,0xdc,0xc0,0x7c,0x00,0x00,0x00,0x00,
    0x00,0x00,0x10,0x38,0x6c,0xc6,0xc6,0xfe,0xc6,0xc6,0xc6,0xc6,0x00,0x00,0x00,0x00,
    0x00,0x00,0xfc,0x66,0x66,0x66,0x7c,0x66,0x66,0x66,0x66,0xfc,0x00,0x00,0x00,0x00,
    0x00,0x00,0x3c,0x66,0xc2,0xc0,0xc0,0xc0,0xc0,0xc2,0x66,0x3c,0x00,0x00,0x00,0x00,
    0x00,0x00,0xf8,0x6c,0x66,0x66,0x66,0x66,0x66,0x66,0x6c,0xf8,0x00,0x00,0x00,0x00,
    0x00,0x00,0xfe,0x66,0x62,0x68,0x78,0x68,0x60,0x62,0x66,0xfe,0x00,0x00,0x00,0x00,
    0x00,0x00,0xfe,0x66,0x62,0x68,0x78,0x68,0x60,0x60,0x60,0xf0,0x00,0x00,0x00,0x00,
    0x00,0x00,0x3c,0x66,0xc2,0xc0,0xc0,0xde,0xc6,0xc6,0x66,0x3a,0x00,0x00,0x00,0x00,
    0x00,0x00,0xc6,0xc6,0xc6,0xc6,0xfe,0xc6,0xc6,0xc6,0xc6,0xc6,0x00,0x00,0x00,0x00,
    0x00,0x00,0x3c,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3c,0x00,0x00,0x00,0x00,
    0x00,0x00,0x1e,0x0c,0x0c,0x0c,0x0c,0x0c,0xcc,0xcc,0xcc,0x78,0x00,0x00,0x00,0x00,
    0x00,0x00,0xe6,0x66,0x6c,0x6c,0x78,0x78,0x6c,0x66,0x66,0xe6,0x00,0x00,0x00,0x00,
    0x00,0x00,0xf0,0x60,0x60,0x60,0x60,0x60,0x60,0x62,0x66,0xfe,0x00,0x00,0x00,0x00,
    0x00,0x00,0xc6,0xee,0xfe,0xfe,0xd6,0xc6,0xc6,0xc6,0xc6,0xc6,0x00,0x00,0x00,0x00,
    0x00,0x00,0xc6,0xe6,0xf6,0xfe,0xde,0xce,0xc6,0xc6,0xc6,0xc6,0x00,0x00,0x00,0x00,
    0x00,0x00,0x7c,0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0x7c,0x00,0x00,0x00,0x00,
    0x00,0x00,0xfc,0x66,0x66,0x66,0x7c,0x60,0x60,0x60,0x60,0xf0,0x00,0x00,0x00,0x00,
    0x00,0x00,0x7c,0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0xd6,0xde,0x7c,0x0c,0x0e,0x00,0x00,
    0x00,0x00,0xfc,0x66,0x66,0x66,0x7c,0x6c,0x66,0x66,0x66,0xe6,0x00,0x00,0x00,0x00,
    0x00,0x00,0x7c,0xc6,0xc6,0x60,0x38,0x0c,0x06,0xc6,0xc6,0x7c,0x00,0x00,0x00,0x00,
    0x00,0x00,0xff,0xdb,0x99,0x18,0x18,0x18,0x18,0x18,0x18,0x3c,0x00,0x00,0x00,0x00,
    0x00,0x00,0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0x7c,0x00,0x00,0x00,0x00,
    0x00,0x00,0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0x6c,0x38,0x10,0x00,0x00,0x00,0x00,
    0x00,0x00,0xc6,0xc6,0xc6,0xc6,0xd6,0xd6,0xd6,0xfe,0xee,0x6c,0x00,0x00,0x00,0x00,
    0x00,0x00,0xc6,0xc6,0x6c,0x7c,0x38,0x38,0x7c,0x6c,0xc6,0xc6,0x00,0x00,0x00,0x00,
    0x00,0x00,0xc6,0xc6,0xc6,0x6c,0x38,0x18,0x18,0x18,0x18,0x3c,0x00,0x00,0x00,0x00,
    0x00,0x00,0xfe,0xc6,0x86,0x0c,0x18,0x30,0x60,0xc2,0xc6,0xfe,0x00,0x00,0x00,0x00,
    0x00,0x00,0x3c,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x30,0x3c,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x80,0xc0,0x60,0x30,0x18,0x0c,0x06,0x02,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x3c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x0c,0x3c,0x00,0x00,0x00,0x00,
    0x10,0x38,0x6c,0xc6,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xff,0x00,0x00,0x00,
    0x30,0x18,0x0c,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x78,0x0c,0x7c,0xcc,0xcc,0xcc,0x76,0x00,0x00,0x00,0x00,
    0x00,0x00,0xe0,0x60,0x60,0x78,0x6c,0x66,0x66,0x66,0x66,0x7c,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x7c,0xc6,0xc0,0xc0,0xc0,0xc6,0x7c,0x00,0x00,0x00,0x00,
    0x00,0x00,0x1c,0x0c,0x0c,0x3c,0x6c,0xcc,0xcc,0xcc,0xcc,0x76,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x7c,0xc6,0xfe,0xc0,0xc0,0xc6,0x7c,0x00,0x00,0x00,0x00,
    0x00,0x00,0x1c,0x36,0x32,0x30,0x78,0x30,0x30,0x30,0x30,0x78,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x76,0xcc,0xcc,0xcc,0xcc,0xcc,0x7c,0x0c,0xcc,0x78,0x00,
    0x00,0x00,0xe0,0x60,0x60,0x6c,0x76,0x66,0x66,0x66,0x66,0xe6,0x00,0x00,0x00,0x00,
    0x00,0x00,0x18,0x18,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x3c,0x00,0x00,0x00,0x00,
    0x00,0x00,0x06,0x06,0x00,0x0e,0x06,0x06,0x06,0x06,0x06,0x06,0x66,0x66,0x3c,0x00,
    0x00,0x00,0xe0,0x60,0x60,0x66,0x6c,0x78,0x78,0x6c,0x66,0xe6,0x00,0x00,0x00,0x00,
    0x00,0x00,0x38,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x18,0x3c,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0xec,0xfe,0xd6,0xd6,0xd6,0xd6,0xc6,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0xdc,0x66,0x66,0x66,0x66,0x66,0x66,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x7c,0xc6,0xc6,0xc6,0xc6,0xc6,0x7c,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0xdc,0x66,0x66,0x66,0x66,0x66,0x7c,0x60,0x60,0xf0,0x00,
    0x00,0x00,0x00,0x00,0x00,0x76,0xcc,0xcc,0xcc,0xcc,0xcc,0x7c,0x0c,0x0c,0x1e,0x00,
    0x00,0x00,0x00,0x00,0x00,0xdc,0x76,0x66,0x60,0x60,0x60,0xf0,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x7c,0xc6,0x60,0x38,0x0c,0xc6,0x7c,0x00,0x00,0x00,0x00,
    0x00,0x00,0x10,0x30,0x30,0xfc,0x30,0x30,0x30,0x30,0x36,0x1c,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0x76,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0xc6,0xc6,0xc6,0xc6,0xc6,0x6c,0x38,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0xc6,0xc6,0xd6,0xd6,0xd6,0xfe,0x6c,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0xc6,0x6c,0x38,0x38,0x38,0x6c,0xc6,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0xc6,0xc6,0xc6,0xc6,0xc6,0xc6,0x7e,0x06,0x0c,0xf8,0x00,
    0x00,0x00,0x00,0x00,0x00,0xfe,0xcc,0x18,0x30,0x60,0xc6,0xfe,0x00,0x00,0x00,0x00,
    0x00,0x00,0x0e,0x18,0x18,0x18,0x70,0x18,0x18,0x18,0x18,0x0e,0x00,0x00,0x00,0x00,
    0x00,0x00,0x18,0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x18,0x18,0x00,0x00,0x00,0x00,
    0x00,0x00,0x70,0x18,0x18,0x18,0x0e,0x18,0x18,0x18,0x18,0x70,0x00,0x00,0x00,0x00,
    0x00,0x00,0x76,0xdc,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
};

/* --- DRM formats (fourcc, from drm_fourcc.h) --- */
#define DRM_FORMAT_XRGB8888 0x34325258  /* 'X''R''2''4' */
#define DRM_FORMAT_ARGB8888 0x34325241  /* 'A''R''2''4' */
#define DRM_FORMAT_RGB888   0x34324752  /* 'R''G''2''4' */
#define DRM_FORMAT_BGR888   0x34324252  /* 'B''G''2''4' */
#define DRM_FORMAT_RGB565   0x36314752  /* 'R''G''1''6' */
#define DRM_FORMAT_BGR565   0x36314742  /* 'B''G''1''6' */

/* --- State --- */
static int drm_fd = -1;
static uint32_t conn_id = 0, crtc_id = 0, fb_id = 0;
static uint32_t width = 1280, height = 720;
static drmModeModeInfo drm_mode;
static drmModeRes *resources = NULL;
static drmModeConnector *conn = NULL;
static uint8_t *fb_mem = NULL;
static uint32_t fb_pitch = 0, fb_size = 0;
static uint32_t fb_format = DRM_FORMAT_XRGB8888;
static int fb_bpp = 32;
static volatile int running = 1;
static int menu_sel = 0;

static void sighandler(int sig) { (void)sig; running = 0; }

/* --- Persistent log to /sdcard (live diagnostics) --- */
static int persist_fd = -1;

static void plog(const char *fmt, ...) {
    va_list ap;
    fprintf(stderr, "[dtc] ");
    va_start(ap, fmt); vfprintf(stderr, fmt, ap); va_end(ap);
    fprintf(stderr, "\n");
    if (persist_fd >= 0) {
        dprintf(persist_fd, "[dtc] ");
        va_start(ap, fmt); vdprintf(persist_fd, fmt, ap); va_end(ap);
        dprintf(persist_fd, "\n");
        fsync(persist_fd);
    }
}

static void persist_open(void) {
    /* IMPORTANT: /sdcard may be tmpfs (volatile). Prefer the exFAT p5 at
     * /mnt/sdcard so the log survives a reboot and is readable on a PC. */
    const char *cands[] = { "/mnt/sdcard/dtcon.log", "/sdcard/dtcon.log",
                            "/data/dtcon.log", NULL };
    for (int i = 0; cands[i]; i++) {
        int fd = open(cands[i], O_WRONLY | O_APPEND | O_CREAT, 0644);
        if (fd >= 0) { persist_fd = fd; plog("persist log: %s", cands[i]); break; }
    }
}

/* --- Pixel conversion (RGB565 -> format) --- */
static inline uint32_t rgb565_to_xrgb(uint16_t c) {
    uint32_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
    r = (r << 3) | (r >> 2);
    g = (g << 2) | (g >> 4);
    b = (b << 3) | (b >> 2);
    return (r << 16) | (g << 8) | b;
}

static inline void put_pixel(int x, int y, uint16_t color) {
    if (x < 0 || x >= TARGET_W || y < 0 || y >= TARGET_H) return;
    if (fb_bpp == 32) {
        uint32_t *p = (uint32_t *)(fb_mem + (size_t)y * fb_pitch + (size_t)x * 4);
        *p = rgb565_to_xrgb(color);
    } else {
        uint16_t *p = (uint16_t *)(fb_mem + (size_t)y * fb_pitch + (size_t)x * 2);
        *p = color;
    }
}

static void fill_rect(int x, int y, int w, int h, uint16_t color) {
    for (int j = y; j < y + h && j < TARGET_H; j++)
        for (int i = x; i < x + w && i < TARGET_W; i++)
            put_pixel(i, j, color);
}

static void draw_char(int col, int row, char ch, uint16_t fg, uint16_t bg) {
    int idx = (unsigned char)ch - 0x20;
    if (idx < 0 || idx >= 96) idx = 0;
    const unsigned char *glyph = &font_data[idx * FONT_H];
    int cx = col * FONT_W;
    int cy = row * FONT_H;
    for (int y = 0; y < FONT_H; y++) {
        unsigned char bits = glyph[y];
        for (int x = 0; x < FONT_W; x++) {
            uint16_t c = (bits & (0x80 >> x)) ? fg : bg;
            put_pixel(cx + x, cy + y, c);
        }
    }
}

static void draw_string(int col, int row, const char *s, uint16_t fg, uint16_t bg) {
    while (*s && col < COLS) {
        draw_char(col++, row, *s++, fg, bg);
    }
}

static void draw_string_center(int row, const char *s, uint16_t fg, uint16_t bg) {
    int len = strlen(s);
    int col = (COLS - len) / 2;
    if (col < 0) col = 0;
    draw_string(col, row, s, fg, bg);
}

static void clear_screen(uint16_t bg) {
    fill_rect(0, 0, TARGET_W, TARGET_H, bg);
}

static void draw_box(int x, int y, int w, int h, uint16_t fg) {
    for (int i = x; i < x + w; i++) { put_pixel(i, y, fg); put_pixel(i, y + w - 1, fg); }
    for (int j = y; j < y + h; j++) { put_pixel(x, j, fg); put_pixel(x + w - 1, j, fg); }
}

/* --- PNG thumbnails (libpng16 + zlib, linked statically) --- */
/* Decode a PNG file into a malloc'd RGBA8 buffer. Returns 0 on success. */
static int png_load_rgba(const char *path, uint8_t **out_px, int *out_w, int *out_h) {
    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    png_structp png = png_create_read_struct(PNG_LIBPNG_VER_STRING,
                                             NULL, NULL, NULL);
    if (!png) { fclose(f); return -1; }
    png_infop info = png_create_info_struct(png);
    if (!info) { png_destroy_read_struct(&png, NULL, NULL); fclose(f); return -1; }
    if (setjmp(png_jmpbuf(png))) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(f);
        return -1;
    }
    png_init_io(png, f);
    png_read_info(png, info);
    int w = png_get_image_width(png, info);
    int h = png_get_image_height(png, info);
    int has_tRNS = png_get_valid(png, info, PNG_INFO_tRNS) != 0;
    png_byte color_type = png_get_color_type(png, info);
    png_byte bit_depth = png_get_bit_depth(png, info);

    if (bit_depth == 16) png_set_strip_16(png);
    if (color_type == PNG_COLOR_TYPE_PALETTE) png_set_palette_to_rgb(png);
    if (color_type == PNG_COLOR_TYPE_GRAY && bit_depth < 8)
        png_set_expand_gray_1_2_4_to_8(png);
    if (has_tRNS) png_set_tRNS_to_alpha(png);
    if (color_type == PNG_COLOR_TYPE_RGB ||
        color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_PALETTE)
        png_set_filler(png, 0xFF, PNG_FILLER_AFTER);
    if (color_type == PNG_COLOR_TYPE_GRAY ||
        color_type == PNG_COLOR_TYPE_GRAY_ALPHA)
        png_set_gray_to_rgb(png);
    png_read_update_info(png, info);

    size_t rowbytes = png_get_rowbytes(png, info);
    uint8_t *pixels = malloc((size_t)h * rowbytes);
    if (!pixels) {
        png_destroy_read_struct(&png, &info, NULL);
        fclose(f);
        return -1;
    }
    png_bytep *rows = malloc((size_t)h * sizeof(png_bytep));
    if (!rows) {
        free(pixels);
        png_destroy_read_struct(&png, &info, NULL);
        fclose(f);
        return -1;
    }
    for (int y = 0; y < h; y++) rows[y] = pixels + (size_t)y * rowbytes;
    png_read_image(png, rows);
    free(rows);
    png_destroy_read_struct(&png, &info, NULL);
    fclose(f);
    *out_px = pixels;
    *out_w = w;
    *out_h = h;
    return 0;
}

static inline uint16_t rgba_to_565(const uint8_t *p) {
    return (uint16_t)(((p[0] >> 3) << 11) | ((p[1] >> 2) << 5) | (p[2] >> 3));
}

/* Draw an RGBA8 image scaled (nearest) into rect (x,y,w,h) with a bg fill. */
static void draw_image_scaled(int x, int y, int dw, int dh,
                              const uint8_t *px, int iw, int ih,
                              uint16_t bg) {
    if (!px || iw <= 0 || ih <= 0 || dw <= 0 || dh <= 0) return;
    fill_rect(x, y, dw, dh, bg);
    for (int j = 0; j < dh; j++) {
        int sy = (int)((int64_t)j * ih / dh);
        if (sy >= ih) sy = ih - 1;
        const uint8_t *src = px + (size_t)sy * iw * 4;
        for (int i = 0; i < dw; i++) {
            int sx = (int)((int64_t)i * iw / dw);
            if (sx >= iw) sx = iw - 1;
            const uint8_t *p = src + sx * 4;
            if (p[3] < 128) continue;          /* skip transparent */
            put_pixel(x + i, y + j, rgba_to_565(p));
        }
    }
}

static void draw_hline(int y, uint16_t color) {
    for (int x = 0; x < TARGET_W; x++) put_pixel(x, y, color);
}

/* --- DRM setup --- */
static int find_connector(void) {
    resources = drmModeGetResources(drm_fd);
    if (!resources) { fprintf(stderr, "drmModeGetResources failed\n"); return -1; }

    for (int i = 0; i < resources->count_connectors; i++) {
        conn = drmModeGetConnector(drm_fd, resources->connectors[i]);
        if (!conn) continue;
        if (conn->connection == DRM_MODE_CONNECTED && conn->count_modes > 0) {
            conn_id = conn->connector_id;
            drm_mode = conn->modes[0];
            width = drm_mode.hdisplay;
            height = drm_mode.vdisplay;
            /* find encoder/crtc */
            for (int j = 0; j < conn->count_encoders; j++) {
                drmModeEncoder *enc = drmModeGetEncoder(drm_fd, conn->encoders[j]);
                if (enc) {
                    crtc_id = enc->crtc_id;
                    drmModeFreeEncoder(enc);
                    if (crtc_id) break;
                }
            }
            if (!crtc_id) {
                /* use first CRTC as fallback */
                if (resources->count_crtcs > 0)
                    crtc_id = resources->crtcs[0];
            }
            fprintf(stderr, "[drm] Connector %u: %dx%d %s\n",
                    conn_id, width, height, drm_mode.name);
            return 0;
        }
        drmModeFreeConnector(conn);
        conn = NULL;
    }
    fprintf(stderr, "[drm] No connected connector found\n");
    return -1;
}

static int create_framebuffer(void) {
    /* Diagnostic: dump the formats the driver advertises per plane */
    drmModePlaneResPtr pres = drmModeGetPlaneResources(drm_fd);
    if (pres) {
        for (int i = 0; i < pres->count_planes; i++) {
            drmModePlane *p = drmModeGetPlane(drm_fd, pres->planes[i]);
            if (!p) continue;
            fprintf(stderr, "[drm] plane %u: %d formats:",
                    p->plane_id, p->count_formats);
            for (int j = 0; j < p->count_formats; j++)
                fprintf(stderr, " 0x%08x", p->formats[j]);
            fprintf(stderr, "\n");
            drmModeFreePlane(p);
        }
        drmModeFreePlaneResources(pres);
    }

    /* Try several format/bpp combos via dumb buffers + AddFB2 */
    struct { uint32_t fmt; int bpp; const char *name; } tries[] = {
        { DRM_FORMAT_XRGB8888, 32, "XRGB8888" },
        { DRM_FORMAT_RGB565,   16, "RGB565"   },
        { DRM_FORMAT_ARGB8888, 32, "ARGB8888" },
        { DRM_FORMAT_RGB888,   24, "RGB888"   },
    };

    struct drm_mode_create_dumb creq = {0};
    struct drm_mode_map_dumb mreq = {0};
    int ok = -1;

    for (size_t t = 0; t < sizeof(tries)/sizeof(tries[0]); t++) {
        creq = (struct drm_mode_create_dumb){0};
        creq.width = width;
        creq.height = height;
        creq.bpp = tries[t].bpp;
        if (ioctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
            fprintf(stderr, "[drm] CREATE_DUMB %s failed: %s\n",
                    tries[t].name, strerror(errno));
            continue;
        }
        mreq = (struct drm_mode_map_dumb){0};
        mreq.handle = creq.handle;
        if (ioctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
            fprintf(stderr, "[drm] MAP_DUMB %s failed: %s\n",
                    tries[t].name, strerror(errno));
            ioctl(drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &creq);
            continue;
        }
        fb_mem = mmap(0, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED,
                      drm_fd, mreq.offset);
        if (fb_mem == MAP_FAILED) {
            fprintf(stderr, "[drm] mmap %s failed: %s\n",
                    tries[t].name, strerror(errno));
            ioctl(drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &creq);
            fb_mem = NULL;
            continue;
        }

        uint32_t handles[4] = { creq.handle, 0, 0, 0 };
        uint32_t pitches[4] = { creq.pitch, 0, 0, 0 };
        uint32_t offsets[4] = { 0, 0, 0, 0 };
        if (drmModeAddFB2(drm_fd, width, height, tries[t].fmt,
                          handles, pitches, offsets, &fb_id, 0) == 0) {
            ok = 0;
            fb_pitch = creq.pitch;
            fb_size = creq.size;
            fb_format = tries[t].fmt;
            fb_bpp = tries[t].bpp;
            fprintf(stderr, "[drm] AddFB2 OK with %s (%dx%d, pitch=%u, size=%u)\n",
                    tries[t].name, width, height, fb_pitch, fb_size);
            break;
        }
        fprintf(stderr, "[drm] AddFB2 %s failed: %s\n",
                tries[t].name, strerror(errno));
        munmap(fb_mem, creq.size);
        fb_mem = NULL;
        ioctl(drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &creq);
    }

    /* Fallback: legacy drmModeAddFB (depth/bpp) if AddFB2 was rejected */
    if (ok < 0) {
        struct { int depth; int bpp; const char *name; } lg[] = {
            { 24, 32, "legacy 24/32" },
            { 16, 16, "legacy 16/16" },
        };
        for (size_t t = 0; t < sizeof(lg)/sizeof(lg[0]); t++) {
            creq = (struct drm_mode_create_dumb){0};
            creq.width = width;
            creq.height = height;
            creq.bpp = lg[t].bpp;
            if (ioctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
                fprintf(stderr, "[drm] CREATE_DUMB %s failed: %s\n",
                        lg[t].name, strerror(errno));
                continue;
            }
            if (drmModeAddFB(drm_fd, width, height, lg[t].depth, lg[t].bpp,
                             creq.pitch, creq.handle, &fb_id) == 0) {
                ok = 0;
                fb_pitch = creq.pitch;
                fb_size = creq.size;
                fb_format = (lg[t].bpp == 16) ? DRM_FORMAT_RGB565
                                              : DRM_FORMAT_XRGB8888;
                fb_bpp = lg[t].bpp;
                fprintf(stderr, "[drm] legacy AddFB OK %s (pitch=%u, size=%u)\n",
                        lg[t].name, fb_pitch, fb_size);
                mreq = (struct drm_mode_map_dumb){0};
                mreq.handle = creq.handle;
                if (ioctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq) < 0) {
                    fprintf(stderr, "[drm] MAP_DUMB %s failed: %s\n",
                            lg[t].name, strerror(errno));
                    ioctl(drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &creq);
                    fb_id = 0;
                    ok = -1;
                    continue;
                }
                fb_mem = mmap(0, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED,
                              drm_fd, mreq.offset);
                if (fb_mem == MAP_FAILED) {
                    fprintf(stderr, "[drm] mmap %s failed: %s\n",
                            lg[t].name, strerror(errno));
                    ioctl(drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &creq);
                    fb_id = 0;
                    fb_mem = NULL;
                    ok = -1;
                    continue;
                }
                break;
            }
            fprintf(stderr, "[drm] legacy AddFB %s failed: %s\n",
                    lg[t].name, strerror(errno));
            ioctl(drm_fd, DRM_IOCTL_MODE_DESTROY_DUMB, &creq);
        }
    }

    if (ok < 0) {
        fprintf(stderr, "[drm] No usable framebuffer format\n");
        return -1;
    }

    memset(fb_mem, 0, fb_size);

    /* Set CRTC */
    if (drmModeSetCrtc(drm_fd, crtc_id, fb_id, 0, 0, &conn_id, 1, &drm_mode)) {
        fprintf(stderr, "drmModeSetCrtc failed: %s\n", strerror(errno));
        return -1;
    }

    fprintf(stderr, "[drm] Mode set: %s\n", drm_mode.name);
    return 0;
}

static void cleanup_drm(void) {
    if (fb_mem) { munmap(fb_mem, fb_size); fb_mem = NULL; }
    if (fb_id) { drmModeRmFB(drm_fd, fb_id); fb_id = 0; }
    if (drm_fd >= 0) close(drm_fd);
}

/* --- Input (evdev): open ALL event devices, poll() all, map keys + gamepad --- */

#define MX_INPUT 8
static int input_fds[MX_INPUT];
static int input_absaxis[MX_INPUT];  /* 1 if we may use ABS_X/ABS_Y (gamepad) */
static int input_count = 0;
static char input_names[MX_INPUT][48];
static int last_ev_type = -1, last_ev_code = -1, last_ev_val = 0;
static int ev_count = 0;
static char input_status[96] = "waiting...";

/* virtual key codes returned by read_key() */
#define VK_UP     0x101
#define VK_DOWN   0x102
#define VK_LEFT   0x103
#define VK_RIGHT  0x104
#define VK_ENTER  0x105
#define VK_ESC    0x106
#define VK_NUM(_n) (0x200 + (_n))  /* VK_NUM(1)..VK_NUM(10) */

static int map_key(const struct input_event *ev, int from_absaxis) {
    if (ev->type == EV_KEY && ev->value == 1) {
        switch (ev->code) {
        case KEY_UP: case BTN_DPAD_UP: case BTN_TOP: case BTN_TOP2:
            return VK_UP;
        case KEY_DOWN: case BTN_DPAD_DOWN: case BTN_BASE: case BTN_BASE2:
            return VK_DOWN;
        case KEY_LEFT: case BTN_DPAD_LEFT: case BTN_THUMBL:
            return VK_LEFT;
        case KEY_RIGHT: case BTN_DPAD_RIGHT: case BTN_THUMBR:
            return VK_RIGHT;
        case KEY_ENTER: case KEY_KPENTER:
        case BTN_A: case BTN_START: case BTN_MODE:
            return VK_ENTER;
        case KEY_ESC: case KEY_BACKSPACE:
        case BTN_B: case BTN_SELECT:
            return VK_ESC;
        default:
            if (ev->code >= KEY_1 && ev->code <= KEY_9)
                return VK_NUM(ev->code - KEY_1 + 1);
            if (ev->code == KEY_0)
                return VK_NUM(10);
            if (ev->code >= BTN_MOUSE) {
                /* mouse buttons: treat as ENTER (handy fallback) */
                return VK_ENTER;
            }
            return -1;
        }
    }
    if (ev->type == EV_ABS && ev->value != 0 && from_absaxis) {
        switch (ev->code) {
        case ABS_HAT0X: return (ev->value < 0) ? VK_LEFT : VK_RIGHT;
        case ABS_HAT0Y: return (ev->value < 0) ? VK_UP : VK_DOWN;
        case ABS_X: return (ev->value >  200) ? VK_RIGHT : (ev->value < -200 ? VK_LEFT : -1);
        case ABS_Y: return (ev->value >  200) ? VK_DOWN  : (ev->value < -200 ? VK_UP : -1);
        default: return -1;
        }
    }
    return -1;
}

/* Printable ASCII mapping for shell line editing. evdev key codes are NOT
 * alphabetical (KEY_Q=16..KEY_P=25, KEY_A=30..KEY_L=38, KEY_Z=44..KEY_M=50),
 * so we use an explicit QWERTY layout table. */
static char key_to_ascii(unsigned int code) {
    static const char qwerty[0x80] = {
        [KEY_Q]='q',[KEY_W]='w',[KEY_E]='e',[KEY_R]='r',[KEY_T]='t',
        [KEY_Y]='y',[KEY_U]='u',[KEY_I]='i',[KEY_O]='o',[KEY_P]='p',
        [KEY_A]='a',[KEY_S]='s',[KEY_D]='d',[KEY_F]='f',[KEY_G]='g',
        [KEY_H]='h',[KEY_J]='j',[KEY_K]='k',[KEY_L]='l',
        [KEY_Z]='z',[KEY_X]='x',[KEY_C]='c',[KEY_V]='v',[KEY_B]='b',
        [KEY_N]='n',[KEY_M]='m',
        [KEY_1]='1',[KEY_2]='2',[KEY_3]='3',[KEY_4]='4',[KEY_5]='5',
        [KEY_6]='6',[KEY_7]='7',[KEY_8]='8',[KEY_9]='9',[KEY_0]='0',
        [KEY_SPACE]=' ',[KEY_MINUS]='-',[KEY_EQUAL]='=',
        [KEY_LEFTBRACE]='[',[KEY_RIGHTBRACE]=']',[KEY_BACKSLASH]='\\',
        [KEY_SEMICOLON]=';',[KEY_APOSTROPHE]='\'',[KEY_GRAVE]='`',
        [KEY_COMMA]=',',[KEY_DOT]='.',[KEY_SLASH]='/',
        [KEY_TAB]='\t',[KEY_BACKSPACE]='\b',
        [KEY_ENTER]='\n',[KEY_KPENTER]='\n',
        [KEY_KP1]='1',[KEY_KP2]='2',[KEY_KP3]='3',[KEY_KP4]='4',[KEY_KP5]='5',
        [KEY_KP6]='6',[KEY_KP7]='7',[KEY_KP8]='8',[KEY_KP9]='9',[KEY_KP0]='0',
        [KEY_KPMINUS]='-',[KEY_KPPLUS]='+',[KEY_KPDOT]='.',[KEY_KPSLASH]='/',
        [KEY_KPASTERISK]='*',
    };
    if (code < sizeof(qwerty)) return qwerty[code];
    return 0;
}

static void log_caps(int fd, const char *path) {
    unsigned long bits[64] = {0};
    char name[64] = "?";
    if (ioctl(fd, EVIOCGNAME(sizeof(name)-1), name) >= 0)
        name[sizeof(name)-1] = 0;
    plog("[input] %s name='%s'", path, name);
    if (ioctl(fd, EVIOCGBIT(0, sizeof(bits)), bits) >= 0) {
        if (bits[0] & (1UL << EV_KEY)) {
            plog("[input] %s EV_KEY cap bits:", path);
            unsigned long kbits[(EV_MAX+63)/64] = {0};
            if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(kbits)), kbits) >= 0) {
                int key_max = (int)sizeof(kbits)*8;
                char line[512]; int ln = 0;
                ln += snprintf(line+ln, sizeof(line)-ln, "  codes:");
                for (int i = 0; i < key_max; i++) {
                    if ((kbits[i >> 6] & (1UL << (i & 63))) && i >= KEY_ESC && i <= BTN_THUMBR)
                        ln += snprintf(line+ln, sizeof(line)-ln, " %d", i);
                }
                plog("[input] %s%s", path, line);
            }
        }
        if (bits[0] & (1UL << EV_ABS)) plog("[input] %s EV_ABS", path);
        if (bits[0] & (1UL << EV_REL)) plog("[input] %s EV_REL", path);
    }
}

/* --- Embedded shell (sh via pipes rendered to the DRM framebuffer) --- */
#define SHELL_BUF_MAX 256
static char shell_line[SHELL_BUF_MAX];
static int  shell_len = 0;
#define SHBUF_MAX (ROWS * COLS * 2)   /* ring of rendered text */
#define SH_MARG_X  2                   /* left/right margin (chars) */
#define SH_MARG_Y  2                   /* top margin rows */
#define SH_USABLE  (COLS - 4 * SH_MARG_X)   /* usable text width */
static char sh_line_buf[SHBUF_MAX];
static int  sh_line_used = 0;
static int  sh_dirty = 1;      /* redraw only when content changes */
static pid_t sh_pid = -1;
static int  sh_tty_out = -1, sh_tty_in = -1;

static void sh_reset_buffer(void) {
    sh_line_used = 0;
    memset(sh_line_buf, 0, sizeof(sh_line_buf));
    sh_dirty = 1;
}

static void sh_append(const char *s, int n) {
    if (n <= 0) return;
    if (sh_line_used + n >= (int)sizeof(sh_line_buf)) {
        int keep = (int)sizeof(sh_line_buf) - 1 - n;
        if (keep > 0) memmove(sh_line_buf, sh_line_buf + sh_line_used - keep, keep);
        sh_line_used = keep;
    }
    memcpy(sh_line_buf + sh_line_used, s, n);
    sh_line_used += n;
    sh_dirty = 1;
}

/* word wrap a line at the usable width and draw with margins:
 * returns number of screen rows consumed */
static int sh_draw_wrap(int row, const char *s) {
    int rows = 0;
    while (*s && row < ROWS - 4) {
        int n = 0;
        while (s[n] && s[n] != '\n' && n < SH_USABLE) n++;
        char tmp[SH_USABLE + 1];
        memcpy(tmp, s, n); tmp[n] = 0;
        if (n > 0) draw_string(SH_MARG_X, row, tmp, COLOR_FG, COLOR_BG);
        rows++;
        row++;
        s += n;
        if (*s == '\n') { s++; if (!*s) break; }
    }
    return rows;
}

static void sh_draw(void) {
    clear_screen(COLOR_BG);
    draw_box(0, 0, TARGET_W, TARGET_H, COLOR_FG);      /* white border */
    draw_box(1, 1, TARGET_W - 2, TARGET_H - 2, COLOR_FG);
    fill_rect(0, 0, TARGET_W, FONT_H + 8, COLOR_TITLE);
    draw_string_center(1, " Shell (embedded /bin/sh)  [Enter] sends  [Tab] completes  [Esc] back ",
                       COLOR_BG, COLOR_TITLE);
    draw_hline(FONT_H + 8, COLOR_FG);

    /* render scrollback lines with margins and word wrap */
    int out_lines = ROWS - 6;
    char *p = sh_line_buf;
    char *endcur = sh_line_buf + sh_line_used;
    int start = 0;
    if (sh_line_used >= sizeof(sh_line_buf)) start = 1;
    (void)out_lines;
    int row = SH_MARG_Y + 2;
    /* split into lines */
    char *lines[out_lines + 1];
    int nlines = 0;
    while (p < endcur && nlines < out_lines) {
        lines[nlines++] = p;
        char *nl = memchr(p, '\n', endcur - p);
        if (nl) { *nl = '\0'; p = nl + 1; }
        else { p = endcur; break; }
    }
    int lastrow = 0;
    for (int i = 0; i < nlines; i++) lastrow = sh_draw_wrap(row, lines[i]);
    (void)lastrow; (void)start;

    /* prompt line at bottom */
    char prompt[COLS * 2 + 1];
    snprintf(prompt, sizeof(prompt), "# %s", shell_line);
    sh_draw_wrap(ROWS - 3, prompt);
    draw_string(SH_MARG_X, ROWS - 1, "[Enter] run  [Tab] autocomplete  [Esc] back to menu",
                COLOR_DIM, COLOR_BG);
}

/* simple filename/command autocompletion: expand the current word using PATH
 * and the current directory; single match completes, several list matches */
static int sh_complete(void) {
    if (shell_len <= 0) return 0;
    int ws = shell_len - 1;
    while (ws > 0 && shell_line[ws] != ' ' && shell_line[ws] != '\t') ws--;
    if (shell_line[ws] == ' ' || shell_line[ws] == '\t') ws++;
    int wlen = shell_len - ws;
    if (wlen <= 0) return 0;
    char word[512];
    memcpy(word, shell_line + ws, wlen);
    word[wlen] = 0;

    const char *dirs[16];
    int ndirs = 0;
    dirs[ndirs++] = ".";
    const char *path = getenv("PATH");
    char pathbuf[1024];
    if (path) {
        snprintf(pathbuf, sizeof(pathbuf), "%s", path);
        char *tok = strtok(pathbuf, ":");
        while (tok && ndirs < 15) { if (tok[0]) dirs[ndirs++] = tok; tok = strtok(NULL, ":"); }
    }

    char matches[64][256];
    int nm = 0;
    for (int d = 0; d < ndirs && nm < 64; d++) {
        DIR *dir = opendir(dirs[d]);
        if (!dir) continue;
        struct dirent *de;
        while ((de = readdir(dir)) && nm < 64) {
            if (strncmp(de->d_name, word, wlen) == 0) {
                int n = snprintf(matches[nm], sizeof(matches[nm]), "%s/%s",
                                 (strcmp(dirs[d], ".") == 0) ? "" : dirs[d], de->d_name);
                if (de->d_type == DT_DIR) matches[nm][n] = '/';
                nm++;
            }
        }
        closedir(dir);
    }
    if (nm == 0) return 0;

    if (nm == 1) {
        char *m = matches[0];
        if (m[0] && m[1] != '/') m++;          /* strip leading "/" */
        int mlen = strlen(m);
        if (shell_len + mlen - wlen < SHELL_BUF_MAX - 1) {
            memcpy(shell_line + shell_len - wlen, m + wlen, mlen - wlen + 1);
            shell_len = shell_len - wlen + mlen;
            sh_dirty = 1;
        }
        return 1;
    }
    /* several: find common prefix and list candidates */
    int maxmatch = 0, minmatch = 256;
    for (int i = 0; i < nm; i++) {
        int l = strlen(matches[i]);
        if (l > maxmatch) maxmatch = l;
        if (l < minmatch) minmatch = l;
    }
    int pref = minmatch;
    for (int i = 0; i < pref; i++) {
        char c = matches[0][i];
        for (int j = 1; j < nm; j++)
            if (matches[j][i] != c) { pref = i; goto done; }
    }
done:
    if (pref > wlen) {
        char *m = matches[0];
        if (m[0] && m[1] != '/') m++;
        if (shell_len + pref - wlen < SHELL_BUF_MAX - 1) {
            memcpy(shell_line + shell_len - wlen, m + wlen, pref - wlen);
            shell_len = shell_len - wlen + pref;
            shell_line[shell_len] = 0;
        }
    }
    char list[1024]; int ln = 0;
    ln += snprintf(list + ln, sizeof(list) - ln, "\n");
    for (int i = 0; i < nm && nm > 1; i++)
        ln += snprintf(list + ln, sizeof(list) - ln, "  %s", matches[i]);
    sh_append(list, ln);
    sh_append("\n", 1);
    sh_dirty = 1;
    return 1;
}

static int sh_run_command(void) {
    if (sh_pid < 0) return -1;
    if (shell_len <= 0) return 0;
    char buf[SHELL_BUF_MAX + 2];
    snprintf(buf, sizeof(buf), "%s\n", shell_line);
    ssize_t w = write(sh_tty_in, buf, strlen(buf));
    if (w < 0) plog("[sh] write cmd failed: %s", strerror(errno));
    sh_append(shell_line, shell_len);
    sh_append("\n", 1);
    shell_len = 0;
    shell_line[0] = 0;
    sh_dirty = 1;
    return 0;
}

static void run_shell(void) {
    sh_reset_buffer();
    shell_len = 0;
    shell_line[0] = 0;

    int pin[2], pout[2];
    if (pipe(pin) < 0 || pipe(pout) < 0) {
        plog("[sh] pipe failed: %s", strerror(errno));
        return;
    }
    pid_t pid = fork();
    if (pid < 0) {
        plog("[sh] fork failed: %s", strerror(errno));
        return;
    }
    if (pid == 0) {
        /* child: /bin/sh with stdin/stdout/stderr wired to pipes */
        dup2(pin[0], 0);
        dup2(pout[1], 1);
        dup2(pout[1], 2);
        close(pin[0]); close(pin[1]); close(pout[0]); close(pout[1]);
        execl("/bin/sh", "/bin/sh", NULL);
        _exit(127);
    }
    close(pin[0]); close(pout[1]);
    sh_tty_out = pout[0];
    sh_tty_in  = pin[1];
    sh_pid = pid;
    fcntl(sh_tty_out, F_SETFL, fcntl(sh_tty_out, F_GETFL) | O_NONBLOCK);
    plog("[sh] shell pid %d", pid);

    time_t idle_start = time(NULL);
    int  was_alive = 1;
    while (running) {
        struct pollfd pfds[1 + MX_INPUT];
        pfds[0].fd = sh_tty_out;
        pfds[0].events = POLLIN;
        for (int i = 0; i < input_count; i++) {
            pfds[i + 1].fd = input_fds[i];
            pfds[i + 1].events = POLLIN;
        }
        int pr = poll(pfds, input_count + 1, 50);
        if (pr < 0) { if (errno == EINTR) continue; break; }

        for (int i = 0; i < input_count; i++) {
            if (!(pfds[i + 1].revents & POLLIN)) continue;
            struct input_event ev;
            ssize_t r;
            while ((r = read(input_fds[i], &ev, sizeof(ev))) == (ssize_t)sizeof(ev)) {
                if (ev.type != EV_KEY || ev.value != 1) continue;
                if (ev.code == KEY_ESC) { running = 0; break; }
                char c = key_to_ascii(ev.code);
                if (c == '\t') { sh_complete(); }
                else if (c == '\n') { sh_run_command(); }
                else if (c == '\b' || ev.code == KEY_DELETE) { if (shell_len > 0) { shell_len--; shell_line[shell_len] = 0; sh_dirty = 1; } }
                else if (c >= 0x20 && c < 0x7f) {
                    if (shell_len < SHELL_BUF_MAX - 1) {
                        shell_line[shell_len++] = c;
                        shell_line[shell_len] = 0;
                        sh_dirty = 1;
                    }
                }
            }
        }

        /* drain shell output */
        char tmp[1024];
        int got = 0;
        for (;;) {
            ssize_t r = read(sh_tty_out, tmp, sizeof(tmp));
            if (r > 0) { sh_append(tmp, (int)r); got = 1; }
            else break;
        }

        int st = 0;
        pid_t wpid = waitpid(sh_pid, &st, WNOHANG);
        int alive = (wpid == 0);

        /* trim trailing prompt line of sh while preserving partial line */
        if (!got && !alive && was_alive) {
            was_alive = 0;
            plog("[sh] shell exited (status %d)", WIFEXITED(st) ? WEXITSTATUS(st) : -1);
        }

        if (input_count == 0 && !alive) {
            if (time(NULL) - idle_start > 3)
                { sh_append("(shell exited, returning to menu)\n", 33); break; }
        }
        if (input_count > 0) idle_start = time(NULL);
        if (sh_dirty) { sh_draw(); sh_dirty = 0; }
        if (!running) break;
        usleep(20000);
    }

    if (sh_pid > 0) { kill(sh_pid, SIGKILL); waitpid(sh_pid, NULL, 0); }
    close(sh_tty_out); close(sh_tty_in);
    sh_pid = -1;
    sh_tty_out = sh_tty_in = -1;
    /* flush pending input events left over from typing in shell */
}

/* --- Native VT shell: drop DRM master and let kernel fbcon render tty1 --- */
#include <linux/vt.h>
#include <linux/fb.h>
#include <sys/kd.h>
static void vt_diag(const char *tag) {
    FILE *fp = popen("cat /proc/fb 2>&1; echo ---; ls /sys/class/vtconsole 2>&1; echo ---; cat /sys/class/graphics/fb0/blank 2>&1; echo ---; cat /proc/tty/driver/vc_sel 2>/dev/null | head -5; echo ---; cat /sys/class/graphics/fb0/modes 2>&1", "r");
    if (fp) {
        char line[128];
        plog("[vt] diagnostics [%s]:", tag);
        while (fgets(line, sizeof(line), fp))
            plog("  %s", line[strlen(line)-1] == '\n' ? (line[strlen(line)-1]=0, line) : line);
        pclose(fp);
    }
}
static void run_vt_shell(void) {
    plog("[vt] switching to native VT shell via fbcon");
    clear_screen(COLOR_BG);
    draw_string_center(ROWS / 2 - 1, "Switching to native VT console (fbcon)...",
                       COLOR_WARN, COLOR_BG);
    vt_diag("before");

    drmDropMaster(drm_fd);

    /* nudge fb0 out of any blank state so fbcon repaints immediately */
    int fb = open("/dev/fb0", O_RDWR | O_NOCTTY);
    if (fb >= 0) {
        ioctl(fb, FBIOBLANK, FB_BLANK_UNBLANK);
        close(fb);
    }

    int ttyfd = -1;
    int f0 = open("/dev/tty0", O_RDWR | O_NOCTTY);
    int f1 = open("/dev/tty1", O_RDWR | O_NOCTTY);
    if (f0 >= 0) {
        if (ioctl(f0, VT_ACTIVATE, 1) == 0) ioctl(f0, VT_WAITACTIVE, 1);
        close(f0);
    }
    if (f1 >= 0) {
        /* force text mode on the VT before handing it to the shell */
        ioctl(f1, KDSETMODE, KD_TEXT);
        ttyfd = f1;
    }

    if (ttyfd < 0) {
        plog("[vt] no /dev/tty1, falling back");
        drmSetMaster(drm_fd);
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        /* child: run a login-ish shell attached to the VT */
        setsid();
        ioctl(ttyfd, TIOCSCTTY, 0);
        dup2(ttyfd, 0);
        dup2(ttyfd, 1);
        dup2(ttyfd, 2);
        if (ttyfd > 2) close(ttyfd);
        execl("/bin/sh", "/bin/sh", NULL);
        _exit(127);
    }
    close(ttyfd);
    if (pid < 0) { plog("[vt] fork failed: %s", strerror(errno)); }

    /* give fbcon a moment to take over scanout, then verify */
    sleep(2);
    vt_diag("during");

    int st = 0;
    if (pid > 0) waitpid(pid, &st, 0);
    plog("[vt] shell ended (status %d)", WIFEXITED(st) ? WEXITSTATUS(st) : -1);

    /* hand the display back: tell the kernel console driver it no longer
     * owns the framebuffer (fbcon stops sweeping fb0), then re-acquire
     * DRM master so the menu framebuffer is scanned out again */
    int kfd = open("/dev/tty1", O_RDWR | O_NOCTTY);
    if (kfd >= 0) {
        ioctl(kfd, KDSETMODE, KD_GRAPHICS);
        close(kfd);
    }

    /* reacquire DRM + redraw menu */
    drmSetMaster(drm_fd);
    drmModeSetCrtc(drm_fd, crtc_id, fb_id, 0, 0, &conn_id, 1, &drm_mode);
}

static void diag_usb(void) {
    plog("=== input diagnosis ===");
    FILE *fp = popen("ls -la /sys/class/input/ 2>&1; echo ---; ls -la /dev/input/ 2>&1; echo ---; cat /proc/bus/input/devices 2>&1", "r");
    if (fp) {
        char line[128];
        while (fgets(line, sizeof(line), fp))
            plog("  %s", line[strlen(line)-1] == '\n' ? (line[strlen(line)-1]=0, line) : line);
        pclose(fp);
    }
    fp = popen("ls -la /sys/bus/usb/devices/ 2>/dev/null; echo ---; cat /proc/interrupts 2>/dev/null | grep -i usb; echo; echo --- framebuffer; ls -la /dev/fb* 2>&1; echo ---; ls -la /sys/class/graphics/ 2>&1", "r");
    if (fp) {
        char line[128];
        while (fgets(line, sizeof(line), fp))
            plog("  %s", line[strlen(line)-1] == '\n' ? (line[strlen(line)-1]=0, line) : line);
        pclose(fp);
    }
    plog("=== end input diagnosis ===");
}

static int open_input(void) {
    char path[64];
    DIR *d = opendir("/sys/class/input");
    if (!d) return -1;
    struct dirent *de;
    while ((de = readdir(d)) != NULL && input_count < MX_INPUT) {
        if (strncmp(de->d_name, "event", 5) != 0) continue;
        /* skip already-open devices */
        int dup = 0;
        for (int i = 0; i < input_count; i++)
            if (!strcmp(input_names[i], de->d_name)) dup = 1;
        if (dup) continue;

        snprintf(path, sizeof(path), "%s%s", INPUT_GLOB, de->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        /* grab all keyboard-like and gamepad-like devices */
        int interesting = 0;
        unsigned long bits[64] = {0};
        if (ioctl(fd, EVIOCGBIT(0, sizeof(bits)), bits) >= 0) {
            if (bits[0] & (1UL << EV_KEY)) {
                unsigned long kbits[(BTN_TRIGGER_HAPPY+63)/64] = {0};
                if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(kbits)), kbits) >= 0) {
                    for (int i = 0; i < BTN_TRIGGER_HAPPY; i++)
                        if (kbits[i >> 6] & (1UL << (i & 63)))
                            interesting = 1;
                }
            }
            if (bits[0] & (1UL << EV_ABS))
                interesting = 1;  /* gamepad analog axes (joystick) */
        }
        if (interesting) {
            log_caps(fd, path);
            snprintf(input_names[input_count], sizeof(input_names[0]), "%s",
                     de->d_name);
            input_fds[input_count] = fd;
            int has_abs = 0, has_rel = 0;
            unsigned long abits[64] = {0};
            if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abits)), abits) >= 0)
                for (int i = 0; i < (int)(sizeof(abits)*8); i++)
                    if (abits[i >> 6] & (1UL << (i & 63))) has_abs = 1;
            if (ioctl(fd, EVIOCGBIT(EV_REL, sizeof(abits)), abits) >= 0)
                for (int i = 0; i < (int)(sizeof(abits)*8); i++)
                    if (abits[i >> 6] & (1UL << (i & 63))) has_rel = 1;
            input_absaxis[input_count] = (has_abs && !has_rel) ? 1 : 0;
            plog("[input] OPEN %s (axes: %s)", path,
                 input_absaxis[input_count] ? "yes" : "no");
            input_count++;
        } else {
            plog("[input] SKIP %s (no keys/axes)", path);
            close(fd);
        }
    }
    closedir(d);
    return (input_count > 0) ? 0 : -1;
}

static int read_key(void) {
    if (input_count == 0) return -1;
    struct pollfd pfds[MX_INPUT];
    for (int i = 0; i < input_count; i++) {
        pfds[i].fd = input_fds[i];
        pfds[i].events = POLLIN;
    }
    if (poll(pfds, input_count, 50) <= 0) return -1;
    for (int i = 0; i < input_count; i++) {
        if (!(pfds[i].revents & POLLIN)) continue;
        struct input_event ev;
        ssize_t r;
        while ((r = read(input_fds[i], &ev, sizeof(ev))) == (ssize_t)sizeof(ev)) {
            /* raw logging of every key/abs event so we can map exact codes */
            if (ev.type == EV_KEY && ev.value == 1)
                plog("[input] dev%d KEY code=%d -> VK 0x%x",
                     i, ev.code, map_key(&ev, input_absaxis[i]));
            /* live status for on-screen diagnostics */
            last_ev_type = ev.type;
            last_ev_code = ev.code;
            last_ev_val = ev.value;
            int vk = map_key(&ev, input_absaxis[i]);
            if (vk > 0) {
                snprintf(input_status, sizeof(input_status),
                         "dev%d T=%d C=%d V=%d -> VK 0x%x",
                         i, ev.type, ev.code, ev.value, vk);
                plog("[input] dev%d event type=%d code=%d value=%d -> VK 0x%x",
                     i, ev.type, ev.code, ev.value, vk);
                return vk;
            }
        }
    }
    return -1;
}

/* --- Popen wrapper for command output --- */
static int run_cmd(const char *cmd, char *out, int outsize) {
    FILE *fp = popen(cmd, "r");
    if (!fp) return -1;
    int n = fread(out, 1, outsize - 1, fp);
    pclose(fp);
    if (n > 0 && out[n-1] == '\n') n--;
    out[n] = 0;
    return n;
}

/* --- Menu screens --- */
static const char *menu_items[] = {
    "System Info",
    "CPU / Memory",
    "Storage",
    "Network",
    "GPU / DRM",
    "Input Devices",
    "Boot Log",
    "Shell (embedded /bin/sh)",
    "Game Menu (RetroArch)",
    "About / Credits",
};
#define MENU_COUNT ((int)(sizeof(menu_items) / sizeof(menu_items[0])))

#define MENU_SHELL    7
#define MENU_RETRO    8
#define MENU_ABOUT    9

static void draw_menu(void) {
    clear_screen(COLOR_BG);
    /* title bar */
    fill_rect(0, 0, TARGET_W, FONT_H + 8, COLOR_TITLE);
    draw_string_center(1, " GStick 4K Lite - RK3032 Test Console (v8) ", COLOR_BG, COLOR_TITLE);
    draw_hline(FONT_H + 8, COLOR_FG);

    int start_row = 3;
    for (int i = 0; i < MENU_COUNT; i++) {
        int row = start_row + i * 2;
        char buf[COLS + 1];
        const char *label = menu_items[i];
        int sel = (i == menu_sel);
        uint16_t fg = sel ? COLOR_BG : COLOR_FG;
        uint16_t bg = sel ? COLOR_HL : COLOR_BG;

        snprintf(buf, sizeof(buf), "  %c %d. %-40s",
                 sel ? '>' : ' ', i + 1, label);
        draw_string(2, row, buf, fg, bg);
    }

    /* footer: input status + hints */
    int fy = ROWS - 3;
    draw_hline((fy - 1) * FONT_H, COLOR_DIM);
    draw_string(2, fy - 1, input_status, COLOR_OK, COLOR_BG);
    draw_string_center(fy, "[Up/Down] Move   [Enter] Select   [Esc] Back", COLOR_DIM, COLOR_BG);
}

static void show_system_info(void) {
    char buf[MAX_OUTPUT];
    clear_screen(COLOR_BG);
    draw_string(1, 0, "== System Info ==", COLOR_TITLE, COLOR_BG);
    draw_hline(FONT_H, COLOR_DIM);
    run_cmd("uname -a", buf, sizeof(buf));
    int row = 2;
    char *p = buf;
    while (*p && row < ROWS - 4) {
        char line[COLS + 1];
        int i = 0;
        while (*p && *p != '\n' && i < COLS) line[i++] = *p++;
        line[i] = 0;
        if (*p == '\n') p++;
        draw_string(1, row++, line, COLOR_FG, COLOR_BG);
    }
    run_cmd("cat /etc/os-release 2>/dev/null | head -5", buf, sizeof(buf));
    row++;
    p = buf;
    while (*p && row < ROWS - 2) {
        char line[COLS + 1];
        int i = 0;
        while (*p && *p != '\n' && i < COLS) line[i++] = *p++;
        line[i] = 0;
        if (*p == '\n') p++;
        draw_string(1, row++, line, COLOR_OK, COLOR_BG);
    }
    draw_string_center(ROWS - 1, "Press any key to return", COLOR_DIM, COLOR_BG);
}

static void show_cpu_mem(void) {
    char buf[MAX_OUTPUT];
    clear_screen(COLOR_BG);
    draw_string(1, 0, "== CPU / Memory ==", COLOR_TITLE, COLOR_BG);
    draw_hline(FONT_H, COLOR_DIM);
    run_cmd("cat /proc/cpuinfo | grep -E 'Processor|model name|BogoMIPS|Features' | head -8", buf, sizeof(buf));
    int row = 2;
    char *p = buf;
    while (*p && row < ROWS - 12) {
        char line[COLS + 1];
        int i = 0;
        while (*p && *p != '\n' && i < COLS) line[i++] = *p++;
        line[i] = 0;
        if (*p == '\n') p++;
        draw_string(1, row++, line, COLOR_FG, COLOR_BG);
    }
    row++;
    draw_string(1, row++, "-- Memory --", COLOR_TITLE, COLOR_BG);
    run_cmd("free -h", buf, sizeof(buf));
    p = buf;
    while (*p && row < ROWS - 2) {
        char line[COLS + 1];
        int i = 0;
        while (*p && *p != '\n' && i < COLS) line[i++] = *p++;
        line[i] = 0;
        if (*p == '\n') p++;
        draw_string(1, row++, line, COLOR_FG, COLOR_BG);
    }
    draw_string_center(ROWS - 1, "Press any key to return", COLOR_DIM, COLOR_BG);
}

static void show_storage(void) {
    char buf[MAX_OUTPUT];
    clear_screen(COLOR_BG);
    draw_string(1, 0, "== Storage ==", COLOR_TITLE, COLOR_BG);
    draw_hline(FONT_H, COLOR_DIM);
    run_cmd("df -h", buf, sizeof(buf));
    int row = 2;
    char *p = buf;
    while (*p && row < ROWS - 2) {
        char line[COLS + 1];
        int i = 0;
        while (*p && *p != '\n' && i < COLS) line[i++] = *p++;
        line[i] = 0;
        if (*p == '\n') p++;
        draw_string(1, row++, line, COLOR_FG, COLOR_BG);
    }
    draw_string_center(ROWS - 1, "Press any key to return", COLOR_DIM, COLOR_BG);
}

static void show_network(void) {
    char buf[MAX_OUTPUT];
    clear_screen(COLOR_BG);
    draw_string(1, 0, "== Network ==", COLOR_TITLE, COLOR_BG);
    draw_hline(FONT_H, COLOR_DIM);
    run_cmd("ip addr show 2>/dev/null || ifconfig 2>/dev/null", buf, sizeof(buf));
    int row = 2;
    char *p = buf;
    while (*p && row < ROWS - 2) {
        char line[COLS + 1];
        int i = 0;
        while (*p && *p != '\n' && i < COLS) line[i++] = *p++;
        line[i] = 0;
        if (*p == '\n') p++;
        draw_string(1, row++, line, COLOR_FG, COLOR_BG);
    }
    draw_string_center(ROWS - 1, "Press any key to return", COLOR_DIM, COLOR_BG);
}

static void show_gpu_drm(void) {
    char buf[MAX_OUTPUT];
    clear_screen(COLOR_BG);
    draw_string(1, 0, "== GPU / DRM ==", COLOR_TITLE, COLOR_BG);
    draw_hline(FONT_H, COLOR_DIM);
    run_cmd("ls /dev/dri/ 2>/dev/null", buf, sizeof(buf));
    int row = 2;
    draw_string(1, row++, "DRM devices:", COLOR_OK, COLOR_BG);
    char *p = buf;
    while (*p && row < ROWS - 14) {
        char line[COLS + 1];
        int i = 0;
        while (*p && *p != '\n' && i < COLS) line[i++] = *p++;
        line[i] = 0;
        if (*p == '\n') p++;
        draw_string(3, row++, line, COLOR_FG, COLOR_BG);
    }
    row++;
    run_cmd("cat /sys/class/drm/card0-HDMI-A-1/status 2>/dev/null", buf, sizeof(buf));
    char info[128];
    snprintf(info, sizeof(info), "HDMI status: %s", buf);
    draw_string(1, row++, info, COLOR_OK, COLOR_BG);
    snprintf(info, sizeof(info), "Resolution: %ux%u", width, height);
    draw_string(1, row++, info, COLOR_FG, COLOR_BG);
    snprintf(info, sizeof(info), "Mode: %s", drm_mode.name);
    draw_string(1, row++, info, COLOR_FG, COLOR_BG);
    /* Show Mali info */
    run_cmd("ls /usr/lib/libmali* 2>/dev/null | head -3", buf, sizeof(buf));
    if (buf[0]) {
        row++;
        draw_string(1, row++, "Mali GPU:", COLOR_OK, COLOR_BG);
        p = buf;
        while (*p && row < ROWS - 2) {
            char line[COLS + 1];
            int i = 0;
            while (*p && *p != '\n' && i < COLS) line[i++] = *p++;
            line[i] = 0;
            if (*p == '\n') p++;
            draw_string(3, row++, line, COLOR_FG, COLOR_BG);
        }
    }
    draw_string_center(ROWS - 1, "Press any key to return", COLOR_DIM, COLOR_BG);
}

static void show_input_devices(void) {
    char buf[MAX_OUTPUT];
    clear_screen(COLOR_BG);
    draw_string(1, 0, "== Input Devices ==", COLOR_TITLE, COLOR_BG);
    draw_hline(FONT_H, COLOR_DIM);
    run_cmd("ls -la /dev/input/ 2>/dev/null", buf, sizeof(buf));
    int row = 2;
    char *p = buf;
    while (*p && row < ROWS / 2 - 2) {
        char line[COLS + 1];
        int i = 0;
        while (*p && *p != '\n' && i < COLS) line[i++] = *p++;
        line[i] = 0;
        if (*p == '\n') p++;
        draw_string(1, row++, line, COLOR_FG, COLOR_BG);
    }
    row++;
    draw_string(1, row++, "-- Device Names --", COLOR_TITLE, COLOR_BG);
    row++;
    DIR *d = opendir("/sys/class/input");
    if (d) {
        struct dirent *de;
        while ((de = readdir(d)) != NULL && row < ROWS - 3) {
            if (strncmp(de->d_name, "event", 5) != 0) continue;
            char namepath[256], name[128] = "?";
            snprintf(namepath, sizeof(namepath),
                     "/sys/class/input/%s/device/name", de->d_name);
            FILE *f = fopen(namepath, "r");
            if (f) { if (fgets(name, sizeof(name), f)) name[strcspn(name, "\n")] = 0; fclose(f); }
            char line[COLS + 1];
            snprintf(line, sizeof(line), "  %s: %s", de->d_name, name);
            draw_string(1, row++, line, COLOR_FG, COLOR_BG);
        }
        closedir(d);
    }
    draw_string_center(ROWS - 1, "Press any key to return", COLOR_DIM, COLOR_BG);
}

static void show_boot_log(void) {
    char buf[MAX_OUTPUT];
    clear_screen(COLOR_BG);
    draw_string(1, 0, "== Boot Log (dmesg) ==", COLOR_TITLE, COLOR_BG);
    draw_hline(FONT_H, COLOR_DIM);
    run_cmd("dmesg | tail -38", buf, sizeof(buf));
    int row = 2;
    char *p = buf;
    while (*p && row < ROWS - 2) {
        char line[COLS + 1];
        int i = 0;
        while (*p && *p != '\n' && i < COLS) line[i++] = *p++;
        line[i] = 0;
        if (*p == '\n') p++;
        draw_string(1, row++, line, COLOR_FG, COLOR_BG);
    }
    draw_string_center(ROWS - 1, "Press any key to return", COLOR_DIM, COLOR_BG);
}

static void show_about(void) {
    clear_screen(COLOR_BG);
    draw_string(1, 0, "== About / Credits ==", COLOR_TITLE, COLOR_BG);
    draw_hline(FONT_H, COLOR_DIM);
    static const char *lines[] = {
        "GStick 4K Lite - RK3032 test console",
        "",
        "Interactive HDMI test launcher for the Rockchip RK3032",
        "development stick. Renders directly via DRM/KMS with",
        "keyboard + joystick input through evdev.",
        "",
        "Created by:  cargabsj175",
        "Year:        2023",
        "License:     MIT",
        "",
        "Built with:  libdrm (xf86drm), Linux input subsystem",
        "Toolchain:   Bootlin arm gnueabihf stable-2018.11",
        "",
        "No warranty, use at your own risk.",
    };
    int row = 2;
    for (size_t i = 0; i < sizeof(lines) / sizeof(lines[0]); i++)
        draw_string(2, row++, lines[i], COLOR_FG, COLOR_BG);
    draw_string_center(ROWS - 1, "Press any key to return", COLOR_DIM, COLOR_BG);
}

/* Launch any program under DRM: drop master, exec, wait, re-acquire.
 * argv must end with a NULL element. */
static void run_exec(char *const argv[]) {
    plog("[run] launching: %s", argv[0]);
    clear_screen(COLOR_BG);
    char splash[COLS + 1];
    snprintf(splash, sizeof(splash), "Launching %s ...", argv[0]);
    draw_string_center(ROWS / 2 - 1, splash, COLOR_WARN, COLOR_BG);
    draw_string_center(ROWS / 2 + 1, "When it exits you return to the console", COLOR_DIM, COLOR_BG);
    drmDropMaster(drm_fd);

    pid_t pid = fork();
    if (pid == 0) {
        setenv("HOME", "/sdcard", 1);
        execv(argv[0], argv);
        plog("[run] exec %s failed: %s", argv[0], strerror(errno));
        _exit(127);
    }
    int stt = 0;
    if (pid > 0) waitpid(pid, &stt, 0);
    plog("[run] %s exited (status %d)",
         argv[0], WIFEXITED(stt) ? WEXITSTATUS(stt) : -1);

    /* reacquire DRM master and restore our framebuffer */
    drmSetMaster(drm_fd);
    drmModeSetCrtc(drm_fd, crtc_id, fb_id, 0, 0, &conn_id, 1, &drm_mode);
}

/* Launch RetroArch (present in the test rootfs, config video_driver=drm). */
static void run_retroarch(const char *cfg, const char *core, const char *rom) {
    char core_path[512] = "";
    if (core && core[0]) {
        /* cores live in either /sdcard/retro_lib or the real exFAT mount */
        snprintf(core_path, sizeof(core_path), "/sdcard/retro_lib/%s", core);
        if (access(core_path, R_OK) != 0) {
            snprintf(core_path, sizeof(core_path), "/mnt/sdcard/retro_lib/%s", core);
            if (access(core_path, R_OK) != 0) {
                plog("[ra] core not found: %s", core);
                return;
            }
        }
    }
    char cfg_path[512];
    if (cfg && cfg[0] && access(cfg, R_OK) == 0)
        snprintf(cfg_path, sizeof(cfg_path), "%s", cfg);
    else
        snprintf(cfg_path, sizeof(cfg_path), "/etc/retroarch.cfg");

    plog("[ra] launching RetroArch (cfg=%s core=%s rom=%s)", cfg_path, core_path, rom ? rom : "-");
    if (core_path[0] && rom) {
        char *argv[] = {
            (char *)"/usr/bin/retroarch", (char *)"--config", cfg_path,
            (char *)"-L", core_path, (char *)rom, NULL
        };
        run_exec(argv);
    } else {
        char *argv[] = {
            (char *)"/usr/bin/retroarch", (char *)"--config", cfg_path, NULL
        };
        run_exec(argv);
    }
}

/* --- Game Menu: read simple text entries from the sdcard, GRUB-style ---
 * Config file: /mnt/sdcard/drm-text-console.cfg
 * Two accepted line formats ('#' = comment, fields separated by '|'):
 *   1) Game:     Name | core_libretro.so | /mnt/sdcard/.../rom.ext
 *   2) Command:  Name | /usr/bin/program [arg1 [arg2 ...]]
 * Example:
 *   Flappy (Intv) | freeintv_libretro.so | /mnt/sdcard/roms/INTV/flappy.int
 *   MiniGUI Demo  | /usr/bin/game
 */
#define GAME_CFG    "/mnt/sdcard/drm-text-console.cfg"
#define GAME_MAX    256
#define GAME_PAGE   8                     /* entries per page */
#define GAME_ARGMAX 16
static char game_name[GAME_MAX][128];
static char game_core[GAME_MAX][64];     /* "" for command entries */
static char game_rom[GAME_MAX][384];
static char game_cmd[GAME_MAX][512];     /* raw command line, "" for games   */
static char game_thumb[GAME_MAX][384];   /* optional 4th field: PNG thumb */
static char *game_argv[GAME_MAX][GAME_ARGMAX + 1];
static char  game_argv_buf[GAME_MAX][GAME_ARGMAX][128];
static int   game_count = 0;

/* Map a libretro core name to a generic per-system thumbnail prefix.
 * Looks for /mnt/sdcard/thumbs/<prefix>.png (fallback No_image.png). */
static const struct { const char *core; const char *sys; } core_sys_map[] = {
    { "freeintv",   "intv" },
    { "stella",     "a26" },
    { "nestopia",   "nes" },
    { "fceumm",     "nes" },
    { "quicknes",   "nes" },
    { "gambatte",   "gb" },
    { "mgba",       "gba" },
    { "snes9x",     "snes" },
    { "snes9x2010", "snes" },
    { "genplus_gx", "md" },
    { "picodrive",  "md" },
    { "fbneo",      "neogeo" },
    { "fbalpha",    "neogeo" },
    { "mame2003",   "neogeo" },
    { "mame2010",   "neogeo" },
    { "ppsspp",     "psp" },
    { NULL, NULL },
};
#define THUMB_DIR   "/mnt/sdcard/thumbs"

/* Resolve the PNG thumb for an entry. Returns the path to open (static
 * buffer reused), or NULL if none should be drawn. */
static const char *game_thumb_path(int idx) {
    if (game_thumb[idx][0] && access(game_thumb[idx], R_OK) == 0)
        return game_thumb[idx];
    const char *sys = NULL;
    if (game_core[idx][0]) {
        for (int i = 0; core_sys_map[i].core; i++) {
            if (!strncmp(game_core[idx], core_sys_map[i].core, strlen(core_sys_map[i].core))) {
                sys = core_sys_map[i].sys;
                break;
            }
        }
    }
    static char path[384];
    if (sys) {
        snprintf(path, sizeof(path), "%s/%s.png", THUMB_DIR, sys);
        if (access(path, R_OK) == 0) return path;
    }
    snprintf(path, sizeof(path), "%s/No_image.png", THUMB_DIR);
    if (access(path, R_OK) == 0) return path;
    return NULL;
}

/* One-entry PNG cache to avoid decoding on every draw pass (~50 Hz). */
static char   thumb_cache_path[384];
static uint8_t *thumb_cache_px = NULL;
static int     thumb_cache_w = 0, thumb_cache_h = 0;

static void draw_entry_thumb(int idx, int x, int y, int w, int h) {
    const char *path = game_thumb_path(idx);
    if (!path) {
        fill_rect(x, y, w, h, COLOR_BG);
        return;
    }
    if (strcmp(path, thumb_cache_path) || !thumb_cache_px) {
        if (thumb_cache_px) { free(thumb_cache_px); thumb_cache_px = NULL; }
        int pw = 0, ph = 0;
        if (png_load_rgba(path, &thumb_cache_px, &pw, &ph) == 0) {
            thumb_cache_w = pw;
            thumb_cache_h = ph;
            snprintf(thumb_cache_path, sizeof(thumb_cache_path), "%s", path);
        } else {
            thumb_cache_path[0] = 0;
        }
    }
    if (!thumb_cache_px) return;
    draw_image_scaled(x, y, w, h, thumb_cache_px, thumb_cache_w, thumb_cache_h,
                      COLOR_BG);
}

/* split command line on spaces into argv[] (up to GAME_ARGMAX tokens) */
static int cmd_split(const char *line, char *buf[GAME_ARGMAX + 1],
                     char storage[GAME_ARGMAX][128]) {
    char tmp[512];
    snprintf(tmp, sizeof(tmp), "%s", line);
    int n = 0;
    char *tok = strtok(tmp, " \t");
    while (tok && n < GAME_ARGMAX) {
        snprintf(storage[n], 128, "%s", tok);
        buf[n] = storage[n];
        n++;
        tok = strtok(NULL, " \t");
    }
    buf[n] = NULL;
    return n;
}

static int game_load(void) {
    game_count = 0;
    FILE *f = fopen(GAME_CFG, "r");
    if (!f) {
        plog("[game] cannot open %s: %s", GAME_CFG, strerror(errno));
        return -1;
    }
    char line[1024];
    while (fgets(line, sizeof(line), f) && game_count < GAME_MAX) {
        char *p = line;
        while (*p == ' ' || *p == '\t') p++;
        if (*p == '#' || *p == '\n' || *p == '\r' || *p == '\0') continue;
        line[strcspn(line, "\r\n")] = 0;   /* strip EOL */
        char *n = strtok(line, "|");
        char *c = strtok(NULL, "|");
        char *r = strtok(NULL, "|");
        if (!n) continue;
        /* trim whitespace around each field */
        while (*n == ' ' || *n == '\t') n++;
        char *t = strchr(n, '\0') - 1;
        while (t >= n && (*t == ' ' || *t == '\t')) { *t = 0; t--; }
        snprintf(game_name[game_count], 128, "%s", n);

        char *th = NULL;
        if (c && r) th = strtok(NULL, "|");   /* optional 4th field: thumb */
        game_thumb[game_count][0] = 0;
        if (th) {
            while (*th == ' ' || *th == '\t') th++;
            t = strchr(th, '\0') - 1;
            while (t >= th && (*t == ' ' || *t == '\t')) { *t = 0; t--; }
            snprintf(game_thumb[game_count], 384, "%s", th);
        }

        if (c && !r) {
            /* format 2: generic command in field c */
            while (*c == ' ' || *c == '\t') c++;
            t = strchr(c, '\0') - 1;
            while (t >= c && (*t == ' ' || *t == '\t')) { *t = 0; t--; }
            if (!c[0]) { continue; }
            snprintf(game_cmd[game_count], 512, "%s", c);
            game_core[game_count][0] = 0;
            game_rom[game_count][0] = 0;
            cmd_split(c, game_argv[game_count], game_argv_buf[game_count]);
            plog("[game] entry %d (cmd): %s | %s", game_count, n, c);
        } else if (c && r) {
            /* format 1: RetroArch core + rom */
            while (*c == ' ' || *c == '\t') c++;
            while (*r == ' ' || *r == '\t') r++;
            t = strchr(c, '\0') - 1;
            while (t >= c && (*t == ' ' || *t == '\t')) { *t = 0; t--; }
            t = strchr(r, '\0') - 1;
            while (t >= r && (*t == ' ' || *t == '\t')) { *t = 0; t--; }
            /* translate /sdcard -> /mnt/sdcard if that is where it is mounted */
            if (!strncmp(r, "/sdcard/", 8)) {
                char alt[384];
                snprintf(alt, sizeof(alt), "/mnt/sdcard/%s", r + 8);
                if (access(alt, R_OK) == 0) snprintf(r, 384, "%s", alt);
            }
            snprintf(game_core[game_count], 64, "%s", c);
            snprintf(game_rom[game_count], 384, "%s", r);
            game_cmd[game_count][0] = 0;
            plog("[game] entry %d (rom): %s | %s | %s", game_count, n, c, r);
        } else {
            continue;
        }
        game_count++;
    }
    fclose(f);
    plog("[game] %d entries loaded from %s", game_count, GAME_CFG);
    return 0;
}

static void game_menu(void) {
    if (game_count == 0) game_load();
    if (game_count <= 0) {
        clear_screen(COLOR_BG);
        draw_box(0, 0, TARGET_W, TARGET_H, COLOR_FG);
        draw_string_center(ROWS / 2 - 2, "No games found in:", COLOR_WARN, COLOR_BG);
        draw_string_center(ROWS / 2, GAME_CFG, COLOR_FG, COLOR_BG);
        draw_string_center(ROWS / 2 + 1, "Edit the file or press any key", COLOR_DIM, COLOR_BG);
        while (running) {
            if (read_key() >= 0) break;
            usleep(20000);
        }
        return;
    }

    int pages = (game_count + GAME_PAGE - 1) / GAME_PAGE;
    int page = 0, sel = 0;
    while (running) {
        clear_screen(COLOR_BG);
        fill_rect(0, 0, TARGET_W, FONT_H + 8, COLOR_TITLE);
        char title[COLS + 1];
        snprintf(title, sizeof(title), " Game Menu - %d games (page %d/%d) ",
                 game_count, page + 1, pages);
        draw_string_center(1, title, COLOR_BG, COLOR_TITLE);
        draw_hline(FONT_H + 8, COLOR_FG);

        int first = page * GAME_PAGE;
        int row = 3;
        for (int i = 0; i < GAME_PAGE && first + i < game_count; i++) {
            int idx = first + i;
            int is_sel = (idx == sel);
            char padding[COLS + 1];
            snprintf(padding, sizeof(padding), "%c %-34s",
                     is_sel ? '>' : ' ', game_name[idx]);
            draw_string(2, row++, padding,
                        is_sel ? COLOR_BG : COLOR_FG,
                        is_sel ? COLOR_HL : COLOR_BG);
            char meta[COLS + 1];
            snprintf(meta, sizeof(meta), "%s %s",
                     game_cmd[idx][0] ? "CMD" : "RA ",
                     game_cmd[idx][0] ? game_cmd[idx] : game_core[idx]);
            draw_string(3, row++, meta, COLOR_DIM, COLOR_BG);
        }

        /* thumbnail panel: generic per-system image for the selected entry */
        {
            int tx = TARGET_W - 380;
            int ty = 60;
            int tw = 340, th = 300;
            fill_rect(tx, ty, tw, th, COLOR_HL);
            draw_box(tx - 2, ty - 2, tw + 4, th + 4, COLOR_FG);
            draw_entry_thumb(sel, tx, ty, tw, th);
            char tname[COLS + 1];
            snprintf(tname, sizeof(tname), " %s ", game_name[sel]);
            draw_string(tx / FONT_W, (ty + th) / FONT_H + 1, tname,
                        COLOR_FG, COLOR_BG);
        }
        char hint[COLS + 1];
        snprintf(hint, sizeof(hint), "[Up/Down] Move  [PgUp/PgDn] Page  [Enter] Launch  [Esc] Back");
        draw_string_center(ROWS - 1, hint, COLOR_DIM, COLOR_BG);

        int k = -1;
        while (k < 0 && running) { k = read_key(); usleep(20000); }
        switch (k) {
        case VK_UP:    if (sel > 0) sel--; else sel = game_count - 1;
                       page = sel / GAME_PAGE; break;
        case VK_DOWN:  if (sel < game_count - 1) sel++; else sel = 0;
                       page = sel / GAME_PAGE; break;
        case VK_LEFT:  if (page > 0) { page--; sel = page * GAME_PAGE + GAME_PAGE - 1; } break;
        case VK_RIGHT: if (page < pages - 1) { page++; sel = page * GAME_PAGE; } break;
        case VK_ENTER:
            if (game_cmd[sel][0])
                run_exec(game_argv[sel]);             /* generic command */
            else
                run_retroarch(NULL, game_core[sel], game_rom[sel]);
            break;
        case VK_ESC:
            return;
        default:
            /* direct game access: number 1..10 selects an entry by position */
            if (k >= VK_NUM(1) && k <= VK_NUM(10)) {
                int n = k - VK_NUM(1) + page * GAME_PAGE;
                if (n < game_count) sel = n;
            }
            break;
        }
    }
}

typedef void (*show_fn)(void);
static show_fn show_fns[] = {
    show_system_info,
    show_cpu_mem,
    show_storage,
    show_network,
    show_gpu_drm,
    show_input_devices,
    show_boot_log,
    NULL,               /* MENU_SHELL: handled separately */
    NULL,               /* MENU_RETRO: handled separately */
    show_about,         /* MENU_ABOUT */
};

/* --- Main --- */
int main(int argc, char *argv[]) {
    (void)argc; (void)argv;
    signal(SIGINT, sighandler);
    signal(SIGTERM, sighandler);

    /* Open DRM */
    persist_open();
    plog("drm-text-console v%d starting", BUILD_VSN);
    drm_fd = open(DRM_DEV, O_RDWR | O_CLOEXEC);
    if (drm_fd < 0) {
        plog("Cannot open %s: %s", DRM_DEV, strerror(errno));
        return 1;
    }

    /* DRM cap */
    if (drmSetMaster(drm_fd)) {
        fprintf(stderr, "drmSetMaster: %s (non-fatal)\n", strerror(errno));
    }

    if (find_connector() < 0) { cleanup_drm(); return 1; }
    if (create_framebuffer() < 0) { cleanup_drm(); return 1; }

    /* Open input */
    diag_usb();
    open_input();
    time_t last_rescan = time(NULL);
    if (input_count == 0) {
        plog("[input] run 1: no devices");
        snprintf(input_status, sizeof(input_status), "0 devices @ boot, rescanning...");
    } else {
        snprintf(input_status, sizeof(input_status), "%d device(s):", input_count);
        for (int i = 0; i < input_count; i++) {
            snprintf(input_status + strlen(input_status),
                     sizeof(input_status) - strlen(input_status),
                     " %s", input_names[i]);
        }
        plog("[input] opened %d device(s)", input_count);
    }

    /* Main loop */
    fprintf(stderr, "[menu] Starting menu loop\n");
    draw_menu();

    while (running) {
        /* periodic rescan: pick up keyboards/gamepads hotplugged after boot */
        if (time(NULL) - last_rescan >= 2) {
            last_rescan = time(NULL);
            int before = input_count;
            open_input();
            if (input_count > before) {
                plog("[input] rescan: %d -> %d devices", before, input_count);
                snprintf(input_status, sizeof(input_status), "%d device(s):", input_count);
                for (int i = 0; i < input_count; i++) {
                    snprintf(input_status + strlen(input_status),
                             sizeof(input_status) - strlen(input_status),
                             " %s", input_names[i]);
                }
                draw_menu();
            }
        }
        int key = read_key();
        if (key < 0) { usleep(20000); continue; }

        switch (key) {
        case VK_UP:    menu_sel = (menu_sel - 1 + MENU_COUNT) % MENU_COUNT; draw_menu(); break;
        case VK_DOWN:  menu_sel = (menu_sel + 1) % MENU_COUNT; draw_menu(); break;
        case VK_LEFT:  menu_sel = (menu_sel - 1 + MENU_COUNT) % MENU_COUNT; draw_menu(); break;
        case VK_RIGHT: menu_sel = (menu_sel + 1) % MENU_COUNT; draw_menu(); break;
        case VK_ENTER:
            if (menu_sel == MENU_SHELL) {
                /* Shell: embedded via pipes, or native VT shell if the marker
                 * file /sdcard/vtshell exists (kernel fbcon renders /dev/tty1). */
                int vt_mode = 0;
                struct stat sb;
                if (stat("/sdcard/vtshell", &sb) == 0 ||
                    stat("/mnt/sdcard/vtshell", &sb) == 0)
                    vt_mode = 1;
                if (vt_mode) run_vt_shell();
                else        run_shell();
                draw_menu();
            } else if (menu_sel == MENU_RETRO) {
                game_menu();
                draw_menu();
            } else if (menu_sel < MENU_COUNT) {
                /* show info screen */
                show_fns[menu_sel]();
                /* wait for key to go back */
                while (running) {
                    int k = read_key();
                    if (k >= 0) break;
                    usleep(20000);
                }
                draw_menu();
            }
            break;
        case VK_ESC:
            running = 0;
            break;
        default:
            /* number keys VK_NUM(1..10) */
            if (key >= VK_NUM(1) && key <= VK_NUM(10)) {
                int n = key - VK_NUM(1);
                if (n < MENU_COUNT) {
                    menu_sel = n;
                    draw_menu();
                    /* dispatch through the same path as VK_ENTER */
                    if (n == MENU_SHELL) {
                        int vt_mode = 0;
                        struct stat sb;
                        if (stat("/sdcard/vtshell", &sb) == 0 ||
                            stat("/mnt/sdcard/vtshell", &sb) == 0)
                            vt_mode = 1;
                        if (vt_mode) run_vt_shell();
                        else        run_shell();
                        draw_menu();
                    } else if (n == MENU_RETRO) {
                        game_menu();
                        draw_menu();
                    } else {
                        show_fns[n]();
                        while (running) {
                            int k = read_key();
                            if (k >= 0) break;
                            usleep(20000);
                        }
                        draw_menu();
                    }
                }
            }
            break;
        }
    }

    /* Cleanup */
    clear_screen(COLOR_BG);
    fprintf(stderr, "[menu] Exiting\n");
    for (int i = 0; i < input_count; i++) close(input_fds[i]);
    cleanup_drm();
    return 0;
}
