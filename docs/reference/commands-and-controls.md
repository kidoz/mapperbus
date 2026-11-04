# Commands and controls

Executable paths below assume a build directory named `buildDir`.

## SDL3 frontend

```text
./buildDir/src/app/mapperbus-sdl3 [options] <rom-file>
```

Fresh-install defaults are listed below. Saved audio and gamepad settings are
loaded first; command-line options override the corresponding values for this
process. Scaling and region use their own startup defaults.

| Option | Value | Default / behavior |
|---|---|---|
| `--scale N` | 2–6 | No upscaler selected at launch unless a valid factor is supplied |
| `--gpu` | Flag | Select GPU xBRZ; requires a scale factor |
| `--gpu-fsr` | Flag | Select GPU FSR 1; requires a scale factor |
| `--fsr` | Flag | Select CPU FSR 1; requires a scale factor |
| `--sample-rate N` | Integer Hz | `96000` requested; device rate can differ |
| `--resampling MODE` | `blip`, `cubic` | `blip` |
| `--filter-mode MODE` | `accurate`, `enhanced`, `unfiltered` | `accurate` |
| `--filter-profile P` | `nes`, `famicom` | `nes` |
| `--region R` | `ntsc`, `pal`, `dendy` | Automatic detection when omitted |
| `--stereo` | Flag | Enable pseudo-stereo; fresh default is mono |
| `--dither` | Flag | Enable dithering; fresh default is disabled |
| `--expansion-mixing M` | `simple`, `resistance` | `simple` |
| `--gamepad N` | Nonnegative SDL gamepad index | `0` for player 1 |
| `--gamepad-deadzone N` | 1–32767 | `12000`; command-line value is clamped |
| `--gamepad-map MAP` | Comma-separated `NES=SDL` pairs | Override named player 1 bindings |
| `--gamepad2 N` | Nonnegative SDL gamepad index | Enable player 2 with the given index |
| `--gamepad2-map MAP` | Same mapping format | Enable player 2 and override its bindings; default index is 1 |

CPU xBRZ is selected when `--scale` is supplied without a mode flag. If multiple
mode flags are supplied, precedence is `--gpu-fsr`, then `--fsr`, then `--gpu`.
Use one mode flag to make the intended selection clear.

There is no dedicated `--help` option. Launching without arguments prints usage
and exits with failure. The parser does not consistently reject unknown flags
or invalid values; use the documented spellings.

Source: [SDL3 composition root](../../src/app/main.cpp).

### Gamepad mapping tokens

NES names are `a`, `b`, `start`, `select`, `up`, `down`, `left`, and `right`.
SDL controls include physical face positions `south`, `east`, `west`, `north`,
aliases `a`, `b`, `x`, `y`, D-pad tokens `dpup`, `dpdown`, `dpleft`, `dpright`,
and buttons such as `start`, `back`, `leftshoulder`, and `rightshoulder`.
Axis directions use a sign suffix, for example `leftx-`, `leftx+`, `lefty-`,
and `lefty+`.

Defaults use D-pad directions, A on `east`, B on `south`, Start on `start`,
and Select on `back`. Player 1 combines keyboard and gamepad state. Player 2
uses the second gamepad when enabled.

Source: [Gamepad configuration](../../src/platform/input/gamepad_config.hpp).

### Default keyboard bindings

| NES button | Key |
|---|---|
| A | X |
| B | Z |
| Select | Right Shift |
| Start | Enter / Return |
| D-pad | Arrow keys |

Saved `[input.keyboard]` values can override these bindings.

### SDL3 runtime shortcuts

| Key | Action |
|---|---|
| `0`, `1` | Native scale |
| `2`–`6` | Select that upscale factor |
| `F9` | Cycle CPU xBRZ → CPU FSR → GPU xBRZ → GPU FSR |
| `F5`, `F7` | Save / load slot 0 |
| `Shift+F5`, `Shift+F7` | Save / load slot 1 |
| `F11` | Toggle fullscreen |
| `V` | Toggle vsync |

Source: [SDL3 input](../../src/frontends/sdl3/sdl3_input.cpp).

## Headless CLI

```text
./buildDir/src/frontends/cli/mapperbus-cli <rom-file>
```

Loads a cartridge, resets the emulator, executes 60 frames, logs
`Ran 60 frames successfully.`, and exits. It does not present video, output
audio, or consume the SDL3 command-line options or shared frontend settings.
Missing arguments and cartridge load failures return failure.

Source: [CLI entry point](../../src/frontends/cli/main.cpp).

## NodalKit GUI

```text
# macOS
./buildDir/src/frontends/nodalkit/mapperbus [rom-file]
# Linux
./buildDir/src/frontends/nodalkit/mapperbus-gui [rom-file]
```

The first optional argument opens an initial ROM. With no argument, the GUI
opens without a cartridge. Settings and session actions are available in the
GUI; SDL3 command-line flags are not parsed by this entry point.

Source: [GUI entry point](../../src/frontends/nodalkit/main.cpp).
