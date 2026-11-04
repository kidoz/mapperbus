# Build and test MapperBus

In this lesson you will build the headless CLI and run a core test. You do not
need a game ROM, window system, or audio device.

## Before you start

Open a terminal in a checkout of MapperBus. Install a C++23 compiler and standard
library supporting `std::expected`, `std::format`, and `std::print`, plus Meson
and Ninja. The [build guide](../how-to/build-and-install.md) lists dependency
details. Meson can fetch Catch2 if it is not installed, so the first setup may
need network access.

## 1. Configure a headless build

Run from the repository root:

```sh
meson setup buildDir-headless -Denable_sdl3=false -Denable_nodalkit_gui=false
```

Meson checks your compiler and prepares the build. The CLI and tests remain
enabled; both graphical frontends are disabled. Use a new build directory for
this lesson. If it already exists, add `--reconfigure` to the command.

## 2. Compile

```sh
meson compile -C buildDir-headless
```

After compilation succeeds, the CLI executable is at
`buildDir-headless/src/frontends/cli/mapperbus-cli`.

## 3. Run a core test

```sh
meson test -C buildDir-headless bus --print-errorlogs
```

The `bus` test should report `OK`. It exercises emulated memory routing without
opening a window or loading a game from your collection.

## 4. Run the project suite

```sh
meson test -C buildDir-headless --suite mapperbus --print-errorlogs
```

Meson reports the result of each enabled test. The tests create their own ROM
fixtures where needed, including the render integration test named
`battle-city-render`.

You now have a compiled emulator and have exercised its core. Continue with
[Run your first game](first-game.md) to add a window and keyboard input, or read
[Architecture](../explanation/architecture.md) to connect this build to the code.
