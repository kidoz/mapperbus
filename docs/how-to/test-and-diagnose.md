# Run tests and diagnostics

These commands assume a configured build with `enable_tests=true`. Compile
before running tests so results reflect the current source.

## Run the project suite

```sh
meson compile -C buildDir
meson test -C buildDir --suite mapperbus --print-errorlogs
```

Use `--suite mapperbus` to select the project's tests when a build includes
dependency tests. Results and failure output are written under
`buildDir/meson-logs/`.

## Run a focused check

List available tests, then select names:

```sh
meson test -C buildDir --list --suite mapperbus
meson test -C buildDir configuration emulation-session --print-errorlogs
meson test -C buildDir save-state battery-save --print-errorlogs
```

The suites cover CPU, PPU, bus, mappers and timing, FDS, audio, configuration,
sessions, input, saves, and rendering. The `battle-city-render` integration
test uses a generated NROM fixture; no commercial ROM is required.

With the NodalKit frontend enabled, `gui-close` checks failed FDS saves, recovery
export, and native window-close veto/retry using synthetic media and the
toolkit's software window model. It needs no display or supplied BIOS:

```sh
meson test -C buildDir gui-close emulation-session --print-errorlogs
```

## Exercise local FDS images

The optional `fds-compatibility` suite uses BIOS/game files supplied locally
and runs both games through raw, ZIP, and 7z loading with isolated saves.
The `fds-diagnostics` suite uses separately fetched public diagnostics and your
BIOS; its commands and pinned sources are documented on the same reference page.
It includes the public BIOS dumper's fast/slow disk writes, write protection,
CRC-error retry, and snapshots during writes. Fixed physical-CRC vectors run
without external files even when the BIOS-driven cases are skipped.
See [FDS compatibility checks](../reference/fds-compatibility.md) for exact
fixtures, commands, frame artifacts, and the limits of those checks. The
ordinary `fds` and `fds-system` suites need no external assets.

## Validate a GPU backend

An SDL3 build adds `sdl3-input` and `sdl3-gpu`. Run:

```sh
meson test -C buildDir sdl3-gpu --print-errorlogs
```

Cases requiring a missing display or GPU backend can skip. To require a
compatible GPU device:

```sh
MAPPERBUS_REQUIRE_GPU=1 meson test -C buildDir sdl3-gpu --print-errorlogs
```

For Vulkan coverage, build with `-Dvulkan_shaders=enabled` and provide a working
Vulkan driver. A passing headless suite does not establish GPU coverage.

## Run without a display

Use the [headless configuration](build-and-install.md#build-only-the-headless-cli-and-tests)
and substitute its build directory in test commands. This omits the SDL3 tests
and the optional GUI input-table test.

## Use address and undefined-behavior sanitizers

```sh
meson setup buildDir-sanitize -Db_sanitize=address,undefined \
    -Db_lto=false -Db_lundef=false -Denable_nodalkit_gui=false
meson compile -C buildDir-sanitize
meson test -C buildDir-sanitize --suite mapperbus --print-errorlogs
```

Add `--reconfigure` for an existing sanitizer build directory. For a sanitizer
run without SDL3, also pass `-Denable_sdl3=false` during setup.

## Check formatting and static analysis

If `just` and the Clang tooling are installed:

```sh
just format-check
just lint
just analyze
just tidy
```

`lint` checks formatting and builds; `analyze` runs the Clang static analyzer;
`tidy` uses the compilation database. The current analysis recipes exclude
NodalKit frontend sources. See the [justfile](../../justfile) for exact scope.
