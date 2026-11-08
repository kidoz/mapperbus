# Build and install

Run commands from the repository root. Use a separate build directory for each
configuration you want to keep. Add `--reconfigure` to a `meson setup` command
when its build directory already exists.

## Prepare dependencies

- Use a C++23 compiler and standard library with `std::expected`, `std::format`,
  and `std::print`. Check the standard library as well as the compiler version.
- Install Meson and Ninja. The bundled NodalKit GUI requires Meson 1.11 or newer.
- SDL3 3.4.0 or newer is required by the SDL3 frontend. Meson prefers an installed
  dependency and can fetch the wrap fallback. Tests similarly use Catch2 v3.
- ROM archive loading uses libarchive 3.6 or newer. Meson prefers an installed
  copy and falls back to the pinned 3.8.9 WrapDB subproject. The fallback needs
  a C compiler, zlib and liblzma development files (`zlib1g-dev liblzma-dev` on
  Debian/Ubuntu, `zlib xz` on Arch, and the macOS SDK plus `brew install xz`).
  Optional codecs depend on the libraries available when configuring.
  To force the bundled build, add `--force-fallback-for=libarchive` to setup.
  For an installed Homebrew libarchive, use
  `--pkg-config-path="$(brew --prefix libarchive)/lib/pkgconfig"`.
- Install `pkg-config` for system dependency discovery. `just` is optional.
- For Vulkan upscaling, install `glslangValidator`, a Vulkan loader, and a driver
  for your GPU. The macOS GPU path uses Metal.

Meson wraps can require network access on first setup. Dependency declarations
live in [the build files](../reference/build-options.md).

## Build the default frontends

```sh
meson setup buildDir
meson compile -C buildDir
```

This builds SDL3, the headless CLI, and tests. Launch SDL3 with:

```sh
./buildDir/src/app/mapperbus-sdl3 "/path/to/game.nes"
```

## Build the NodalKit GUI

```sh
meson setup buildDir-gui -Denable_nodalkit_gui=true
meson compile -C buildDir-gui mapperbus-gui
```

For a macOS application bundle with Finder and Dock artwork, see
[application icons and bundles](update-branding.md#build-the-macos-application-icon-into-a-bundle).
You can also launch the `mapperbus` executable directly:

```sh
./buildDir-gui/src/frontends/nodalkit/mapperbus
```

On Linux, launch `mapperbus-gui` in a Wayland session:

```sh
./buildDir-gui/src/frontends/nodalkit/mapperbus-gui
```

You can pass a ROM path as the first argument. Without one, use the GUI's open
action. The Meson target `mapperbus-gui` works on both platforms; on macOS it is
an alias for the differently named executable.

## Run GUI BIOS regression tests without a display

With the GUI build configured, run:

```sh
meson compile -C buildDir-gui test-gui-bios
meson test -C buildDir-gui gui-bios --print-errorlogs
```

These tests run the production controller with software windows and controlled
file pickers. They use synthetic ROMs and BIOS files, require no display, audio
device, gamepad, or commercial files, and isolate media, state, and configuration
I/O in temporary directories. They cover session-only and saved BIOS selection,
save failures with Retry/Keep actions, exact archive-member recovery, cancellation,
late callbacks, and Quit during a settings save. The Linux GUI CI job runs them as
part of the MapperBus suite.

## Build only the headless CLI and tests

```sh
meson setup buildDir-headless -Denable_sdl3=false -Denable_nodalkit_gui=false
meson compile -C buildDir-headless
meson test -C buildDir-headless --suite mapperbus --print-errorlogs
./buildDir-headless/src/frontends/cli/mapperbus-cli "/path/to/game.nes"
```

The CLI executes 60 frames and exits. It does not open a display or audio device.
Disabling `enable_sdl3` alone does not make an enabled NodalKit GUI headless;
keep both graphical frontend options disabled for this configuration.

## Build desktop frontends on Arch Linux

Install the dependencies used by the Linux desktop build:

```sh
sudo pacman -S --needed base-devel git meson ninja pkgconf libarchive sdl3 \
    wayland wayland-protocols libxkbcommon freetype2 fontconfig harfbuzz glib2 \
    glslang vulkan-headers vulkan-icd-loader
```

Install the Vulkan driver appropriate for your GPU, then configure:

```sh
meson setup buildDir-linux -Denable_nodalkit_gui=true -Dvulkan_shaders=enabled
meson compile -C buildDir-linux
meson test -C buildDir-linux --suite mapperbus --print-errorlogs
```

SDL3 supports Wayland and X11; the NodalKit GUI currently requires Wayland.
For CPU upscaling without embedded Vulkan shaders, set
`-Dvulkan_shaders=disabled`. The default `auto` uses `glslangValidator` when
available. A system shadercross library provides an alternative shader path.

## Install the runtime

After building the chosen configuration, install to its configured prefix:

```sh
meson install -C buildDir --tags runtime
```

Use the build directory you actually built. A system prefix may require `sudo`.
For a user-owned prefix, pass `--prefix="$HOME/.local"` during setup. The Linux
GUI installation includes a desktop launcher and icon; a bundled NodalKit build
also supplies the toolkit runtime.

For option defaults and executable locations, see
[Build options](../reference/build-options.md). For failures in GPU validation,
see [Run tests and diagnostics](test-and-diagnose.md).
