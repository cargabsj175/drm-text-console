# THIRD-PARTY NOTICES

Este proyecto se distribuye bajo MIT (ver `LICENSE`). Los siguientes componentes
de terceros se incluyen como dependencias y conservan sus propias licencias:

## libdrm (mesa/drm)

Headers usados: `xf86drm.h`, `xf86drmMode.h` (enlace dinámico con
`libdrm.so.2`).

- Licencia: MIT
- Copyright: Precision Insight, Inc., VA Linux Systems, Inc., Tungsten Graphics,
  Inc., Dave Airlie, Jakob Bornecrantz.
- Fuente: `../ra_source/ra/libdrm/` (workspace, fuera de este repo)

## libpng (libpng16, estática en `third_party/lib/libpng16.a`)

- Licencia: PNG Reference Library License (permisiva)
- Copyright: The PNG Reference Library Authors; Cosmin Truta;
  Glenn Randers-Pehrson; Andreas Dilger; Guy Eric Schalnat, Group 42, Inc.
- Texto completo está en `third_party/include/png.h`.

## zlib (estática en `third_party/lib/libz.a`)

- Licencia: zlib License (permisiva)
- Copyright: Jean-loup Gailly, Mark Adler.
- Texto completo en `third_party/include/zlib.h`.

---

Nota: ninguna de las dependencias es GPL; por ello este proyecto puede
distribuirse libremente bajo MIT. Si en el futuro se añade una dependencia
GPL, la combinación deberá relicenciarse a GPLv3.