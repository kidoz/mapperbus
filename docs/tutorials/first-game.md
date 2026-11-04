# Run your first game

In this lesson you will open a NES cartridge ROM in the SDL3 frontend and try
the keyboard and display controls.

## Before you start

Use a checkout with the [build tools installed](../how-to/build-and-install.md),
a desktop session with audio output, and a `.nes` ROM you can use. Choose a ROM
whose mapper appears in the [compatibility table](../reference/compatibility.md).
The repository does not include a game collection.

## 1. Build the SDL3 frontend

From the repository root, configure a separate desktop build:

```sh
meson setup buildDir -Denable_sdl3=true -Denable_nodalkit_gui=false
meson compile -C buildDir
```

If `buildDir` already exists, add `--reconfigure` to the setup command. Meson
uses a suitable installed SDL3 or downloads its wrap fallback.

## 2. Open the ROM

Replace `/path/to/game.nes` with your ROM's actual path:

```sh
./buildDir/src/app/mapperbus-sdl3 "/path/to/game.nes"
```

A game window should open. Click it to give it keyboard focus. If loading fails,
read the error in the terminal and check the path and mapper number.

## 3. Try the controller

Press Enter for Start. Use the arrow keys for the D-pad, `X` for A, `Z` for B,
and right Shift for Select. These are the fresh-install bindings; an existing
[configuration file](../reference/configuration.md) may override them.

## 4. Change the picture size

Press `3` to select a 3× upscale factor. Press `F9` once to switch from CPU xBRZ
to CPU FSR. Press `1` to return to native scale. Some keyboards require holding
Fn to send function keys to the application.

## 5. Close the window

Close the game window to end the session.

You have built and used the desktop frontend. Next, follow
[Configure the SDL3 frontend](../how-to/configure-sdl3.md) for gamepads and audio,
or [Save and restore a game](../how-to/save-and-restore.md) for save slots.
