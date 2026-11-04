# Update the logo and application icons

Run commands from the repository root. Normal builds use the checked-in assets;
image tools are needed only when changing the artwork.

## Edit and regenerate the artwork

Edit [the master icon](../../assets/branding/mapperbus-icon.svg). The cartridge,
M circuit, and contacts use a 32-pixel grid inside a blue launcher tile. Keep the
pixel steps and the strong light/dark contrast. The rounded outer tile belongs
to the desktop launcher; the cartridge and lettering carry the pixel-art style.
Use [the small icon](../../assets/branding/mapperbus-icon-small.svg) for the
hand-adjusted 16-pixel version. Both sources are editable SVGs.

The palette is blue `#2563c7`, navy `#112c51`, light blue `#65b5ff`, ivory
`#fff9e9`, and gold `#f4bc59`. The original wordmark uses 5-by-7 pixel glyphs
specified in the generator; it has no font dependency. All artwork is covered
by the repository's [MIT license](../../LICENSE).

Install librsvg and ImageMagick. On macOS:

```sh
brew install librsvg imagemagick
```

Regenerate all outputs:

```sh
python3 tools/generate_branding.py
```

The command updates:

| Output | Purpose |
|---|---|
| `assets/branding/mapperbus-logo.svg` | Wordmark for light backgrounds |
| `assets/branding/mapperbus-logo-light.svg` | Wordmark for dark backgrounds |
| `assets/branding/png/mapperbus-{size}.png` | Transparent PNGs at 16, 24, 32, 48, 64, 128, 256, 512, and 1024 pixels |
| `packaging/linux/dev.mapperbus.gui.svg` | Scalable Linux launcher icon |
| `packaging/windows/mapperbus.ico` | Seven Windows icon sizes, 16–256 pixels |
| `packaging/macos/MapperBus.icns` | macOS standard and Retina representations, up to 1024 pixels |
| `src/frontends/app_icon.hpp` | Embedded 32-pixel RGBA artwork for the SDL3 window |

Review the 16-, 24-, and 32-pixel outputs at their actual size and against both
light and dark backgrounds. Commit the generated files with the sources so
builds do not need an SVG renderer, ImageMagick, or a font installation.

## Build the macOS application icon into a bundle

The NodalKit GUI needs an application bundle for Finder and Dock artwork.
Configure and build on macOS with Xcode command-line tools installed:

```sh
meson setup buildDir-gui -Denable_nodalkit_gui=true
meson compile -C buildDir-gui mapperbus-macos-app
open buildDir-gui/MapperBus.app
```

For an existing build directory, add `--reconfigure` to the setup command.
The target creates `MapperBus.app` in that build directory, copies the GUI
executable and its non-system dylibs into the bundle, rewrites their library
references, and ad-hoc signs and verifies the result. Re-run the target after
rebuilding the GUI or changing an icon. It can replace an existing bundle with
the same bundle identifier. Non-system `.framework` dependencies are rejected.
This target creates a local application bundle; Developer ID signing and
notarization for distribution are separate release work. The existing
`meson install` executable layout is unchanged.

## Use the Linux and Windows icons

On Linux, build and [install the NodalKit GUI](build-and-install.md).
`meson install --tags runtime` installs its desktop entry, scalable SVG, and
16–512 pixel PNGs in the hicolor theme. The launcher icon name and Wayland
application ID are both `dev.mapperbus.gui`.

On Windows, Meson compiles `packaging/windows/mapperbus.rc` into both graphical
executables using the toolchain's resource compiler. The NodalKit main window
loads large and small icons from that resource. SDL3 sets its window icon from
embedded RGBA data, so launching from a different working directory still
shows the artwork. The headless CLI has no graphical icon resources.
