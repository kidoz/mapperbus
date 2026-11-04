# Build options

The authoritative project options are in [meson_options.txt](../../meson_options.txt).
The [root build](../../meson.build) sets C++23, warning level 3, warnings as
errors, and link-time optimization by default.

## Project options

| Option | Type | Default | Effect |
|---|---|---|---|
| `enable_sdl3` | Boolean | `true` | Build the SDL3 backend and application |
| `enable_nodalkit_gui` | Boolean | `false` | Build the NodalKit GUI |
| `enable_cli` | Boolean | `true` | Build the headless CLI |
| `enable_tests` | Boolean | `true` | Build and register project tests |
| `vulkan_shaders` | Feature | `auto` | Find `glslangValidator` and embed SPIR-V upscaler shaders |

`vulkan_shaders` accepts `auto`, `enabled`, and `disabled`. `enabled` requires
`glslangValidator`; `auto` uses it if found; `disabled` omits that compilation
path. This option does not disable the Metal path or an available system
shadercross path. It is evaluated when the SDL3 backend is enabled.

## Dependencies

| Component | Dependency selection |
|---|---|
| Core and application libraries | C++23 compiler and standard library; no frontend toolkit |
| SDL3 frontend | SDL3 ≥ 3.4.0, installed dependency or Meson wrap fallback |
| NodalKit GUI | Installed NodalKit or bundled fallback; bundled build requires Meson ≥ 1.11 |
| GUI audio and gamepads | SDL3 dependency probe in the GUI build, independent of `enable_sdl3` |
| Tests | Catch2 v3 via `catch2-with-main`, installed dependency or wrap fallback |
| Vulkan shaders | `glslangValidator` at build time; compatible Vulkan runtime and driver at execution |
| Alternative shader compilation | Optional system `sdl3-shadercross` dependency outside macOS |

The GUI enables its SDL3 audio/gamepad code when that dependency is found.
See [SDL3 build definitions](../../src/frontends/sdl3/meson.build),
[GUI build definitions](../../src/frontends/nodalkit/meson.build), and
[test definitions](../../tests/meson.build).

## Targets and executable paths

Paths are relative to a build directory such as `buildDir`.

| Component | Meson target | Executable path |
|---|---|---|
| SDL3 | `mapperbus-sdl3` | `src/app/mapperbus-sdl3` |
| Headless CLI | `mapperbus-cli` | `src/frontends/cli/mapperbus-cli` |
| GUI on macOS | `mapperbus` (`mapperbus-gui` alias) | `src/frontends/nodalkit/mapperbus` |
| GUI on Linux | `mapperbus-gui` | `src/frontends/nodalkit/mapperbus-gui` |

The core, platform, and application layers are separate library targets. The
executable targets are installed when enabled. Linux GUI assets use application
ID `dev.mapperbus.gui`.

## Development commands

The [justfile](../../justfile) uses `buildDir` by default.

| Recipe | Purpose |
|---|---|
| `just build`, `just test` | Compile; compile and run tests |
| `just build-gui`, `just run-gui [rom]` | Configure/build the GUI; build and launch it |
| `just run <rom>` | Build and launch SDL3 |
| `just run-cli <rom>` | Build and run the headless CLI |
| `just run-xbrz <rom> [scale]` | CPU xBRZ, default factor 3 |
| `just run-gpu <rom> [scale]` | GPU xBRZ, default factor 3 |
| `just run-gpu-fsr <rom> [scale]` | GPU FSR, default factor 4 |
| `just format`, `just format-check` | Apply or check C++ formatting |
| `just lint`, `just analyze`, `just tidy` | Build/format checks and Clang analysis |

Setup and clean recipes can wipe or remove their build directories. For
incremental configuration, use `meson setup <directory> --reconfigure`.

For build and installation steps, see [Build and install](../how-to/build-and-install.md).
