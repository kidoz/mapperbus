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
| `--gamepad2 N` | Nonnegative SDL gamepad index | Override player 2 device; default index is 1 |
| `--gamepad2-map MAP` | Same mapping format | Override named player 2 bindings |

CPU xBRZ is selected when `--scale` is supplied without a mode flag. If multiple
mode flags are supplied, precedence is `--gpu-fsr`, then `--fsr`, then `--gpu`.
Use one mode flag to make the intended selection clear.

There is no dedicated `--help` option. Launching without arguments prints usage
and exits with failure. The parser does not consistently reject unknown flags
or invalid values; use the documented spellings.

Source: [SDL3 composition root](../../src/app/main.cpp).

### Gamepad mapping tokens

Input action names are `a`, `b`, `start`, `select`, `up`, `down`, `left`, `right`,
`turbo_a`, and `turbo_b`. Use `none` as a control value to clear a binding.
SDL controls include physical face positions `south`, `east`, `west`, `north`,
aliases `a`, `b`, `x`, `y`, D-pad tokens `dpup`, `dpdown`, `dpleft`, `dpright`,
and buttons such as `start`, `back`, `leftshoulder`, and `rightshoulder`.
Axis directions use a sign suffix, for example `leftx-`, `leftx+`, `lefty-`,
and `lefty+`.

Defaults use D-pad directions, A on `east`, B on `south`, Start on `start`,
Select on `back`, Turbo A on `north`, and Turbo B on `west`. Each player combines
their keyboard and gamepad state. Player 2 defaults to the second gamepad and
unbound keyboard controls.

Source: [Gamepad configuration](../../src/platform/input/gamepad_config.hpp).

### Default keyboard bindings

| NES button | Key |
|---|---|
| A | X |
| B | Z |
| Turbo A | S |
| Turbo B | A |
| Select | Right Shift |
| Start | Enter / Return |
| D-pad | Arrow keys |

These are player 1 defaults, overridden by `[input.keyboard]`. Player 2 uses
`[input.player2.keyboard]` and starts unbound. Code `0` means unbound. Configure
either player with the selector in GUI Settings → Input.

Hold Turbo A or Turbo B to repeat that button; release to stop. Ordinary A/B
remain continuous holds, including when their turbo action is also held.
Settings → Input → Turbo → Speed selects Slow (3 frames pressed, 3 released),
Normal (2 + 2, default), or Fast (1 + 1). Rates at 60 fps are 10, 15, and 30
presses per second; PAL/Dendy rates are approximately 8.3, 12.5, and 25. Use Slow if a
game misses presses. The shared `[input.turbo]` setting is `period_frames=6`,
`4`, or `2`, respectively.

Turbo advances with emulated frames, including manual frame stepping. Pausing
and audio backpressure freeze its phase. Release, ROM changes, reset, successful
state loading, and speed changes restart the pulse with a press. Turbo phase is
session input state and is not stored in emulator save-state blobs.

In Input Settings, click a binding to capture a replacement or choose Clear to
unbind it. Conflicts use the existing swap confirmation. When reading an older
configuration, a new turbo default that conflicts with another binding starts
unbound; existing custom controls keep their values.

### SDL3 runtime shortcuts

| Key | Action |
|---|---|
| `0`, `1` | Native scale |
| `2`–`6` | Select that upscale factor |
| `F9` | Cycle CPU xBRZ → CPU FSR → GPU xBRZ → GPU FSR |
| `Ctrl+1`–`Ctrl+9`, `Ctrl+0` | Select slots 1–9, or slot 10; remembered per game |
| `F5`, `F7` | Save / load the selected slot |
| `Shift+F5`, `Shift+F7` | Save / load slot 2 directly; select it on success |
| `F11` | Toggle fullscreen |
| `V` | Toggle vsync |

`--state-dir PATH` sets an absolute save-state root for the run.
`--portable-states` uses a `mapperbus-states` folder beside the ROM.
Both override the saved storage preference without persisting that override.

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
