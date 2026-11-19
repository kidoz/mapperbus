# SDL3 Meson build port

SDL3 upstream ships a CMake build. This directory carries the Meson build used
by the `subprojects/sdl3.wrap` fallback for SDL3 3.4.20.

- `sdl3-3.4.20-meson-port.meson-diff` is derived from the wrapdb
  `sdl3_3.4.14-2` Meson port
  (https://wrapdb.mesonbuild.com/v2/sdl3_3.4.14-2/sdl3.wrap). The SDL3 3.4.14
  and 3.4.20 source trees have the same file layout, so the build files apply
  unchanged; only the Meson project version differs.
- `enable-dummy-audio.meson-diff` enables SDL3's dummy audio driver in the
  fallback build for headless tests.

When wrapdb publishes a 3.4.20 wrap, prefer replacing this port with the
wrapdb files and re-checking the dummy audio patch.
