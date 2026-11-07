# Commands and controls

Executable paths below assume a build directory named `buildDir`.

## SDL3 frontend

```text
./buildDir/src/app/mapperbus-sdl3 [options] <rom-file>
```

Fresh-install defaults are listed below. Saved audio, gamepad settings, and
controller hardware preferences are loaded first. Command-line options override
the corresponding values for this process. Scaling and region use their own startup defaults.

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
| `--controller-mode M` | `standard`, `four-score`, `hori-4-player` | Saved preference, fresh default Standard |
| `--controller-wiring W` | `nes`, `rf-famicom` | Saved preference, fresh default NES |
| `--expansion-controllers P` | `none`, `1`, `2`, `both` | Attach external Famicom pads; fresh default `none` |
| `--controller-timing T` | `nes-av-famicom`, `rf-famicom` | Saved preference, fresh default NES/AV Famicom |
| `--region R` | `ntsc`, `pal`, `dendy` | Automatic detection when omitted |
| `--stereo` | Flag | Enable pseudo-stereo; fresh default is mono |
| `--dither` | Flag | Enable dithering; fresh default is disabled |
| `--expansion-mixing M` | `simple`, `resistance` | `simple` |
| `--fds-bios PATH` | 8 KiB BIOS file | Saved `[fds] bios_path`, then application data `bios/disksys.rom` |
| `--gamepad N` | Nonnegative SDL gamepad index | `0` for player 1 |
| `--gamepad-deadzone N` | 1–32767 | `12000`; command-line value is clamped |
| `--gamepad-map MAP` | Comma-separated `NES=SDL` pairs | Override named player 1 bindings |
| `--gamepad2 N` | Nonnegative SDL gamepad index | Override player 2 device; default index is 1 |
| `--gamepad2-map MAP` | Same mapping format | Override named player 2 bindings |
| `--gamepad3 N`, `--gamepad4 N` | Nonnegative SDL gamepad index | Profiles 3/4 (external pads or Four Score); default indices 2/3 |
| `--gamepad3-map MAP`, `--gamepad4-map MAP` | Same mapping format | Override profile 3/4 bindings |
| `--gamepad5 N`, `--gamepad6 N` | Nonnegative SDL gamepad index | Hori 3/4; default device indices 4/5 |
| `--gamepad5-map MAP`, `--gamepad6-map MAP` | Same mapping format | Override profile 5/6 bindings |

CPU xBRZ is selected when `--scale` is supplied without a mode flag. If multiple
mode flags are supplied, precedence is `--gpu-fsr`, then `--fsr`, then `--gpu`.
Use one mode flag to make the intended selection clear.

There is no dedicated `--help` option. Launching without arguments prints usage
and exits with failure. The parser does not consistently reject unknown flags
or invalid values; use the documented spellings.

Source: [SDL3 composition root](../../src/app/main.cpp).

### Four Score

Select `--controller-mode four-score` in SDL3/CLI or **Four Score** in GUI
Settings → General → Hardware → Controller mode. Input settings show Players
1–4. SDL3 `--gamepad3`/`--gamepad4` and their mapping options select the two
additional host devices. Famicom D1 attachments resume when returning to Standard.
See [controller mode](configuration.md#controller-mode) for persistence and limits.

### Hori adapter

Select `--controller-mode hori-4-player` in SDL3/CLI or **Hori 4 Players** in
GUI General → Hardware → Controller mode. Input exposes Built-in 1/2 and Hori
1–4. The last two use profile 5/6 bindings and gamepad options; existing profiles
3/4 become Hori 1/2. All six profiles are saved independently. See
[Hori configuration](configuration.md#hori-4-players-adapter).

### Gamepad mapping tokens

Input action names are `a`, `b`, `start`, `select`, `up`, `down`, `left`, `right`,
`turbo_a`, `turbo_b`, and `microphone` (alias `mic`). Use `none` as a control value to clear a binding.
SDL controls include physical face positions `south`, `east`, `west`, `north`,
aliases `a`, `b`, `x`, `y`, D-pad tokens `dpup`, `dpdown`, `dpleft`, `dpright`,
and buttons such as `start`, `back`, `leftshoulder`, and `rightshoulder`.
Axis directions use a sign suffix, for example `leftx-`, `leftx+`, `lefty-`,
and `lefty+`.

Defaults use D-pad directions, A on `east`, B on `south`, Start on `start`,
Select on `back`, Turbo A on `north`, and Turbo B on `west`. Each player combines
their keyboard and gamepad state. Player 2 defaults to the second gamepad and
unbound keyboard controls. External pads use profiles 3/4, default gamepad
indices 2/3, and unbound keys. Attach them separately with
`--expansion-controllers` or General → Hardware; games must support D1 input.

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
any of the four profiles with the selector in GUI Settings → Input.

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
| `F6` | Next FDS side |
| `Shift+F6` | Eject or reinsert FDS side A |
| `F8` | Toggle FDS write protection |
| `F11` | Toggle fullscreen |
| `V` | Toggle vsync |

`--state-dir PATH` sets an absolute save-state root for the run.
`--portable-states` uses a `mapperbus-states` folder beside the ROM.
Both override the saved storage preference without persisting that override.

Source: [SDL3 input](../../src/frontends/sdl3/sdl3_input.cpp).

## Famicom microphone

With RF Famicom controller wiring selected, hold **M** to simulate microphone
activity. GUI Settings → Input → Microphone changes the keyboard or gamepad
binding for either profile; both activate the same device. Defaults are M in
player one, unbound in player two, and no gamepad assignment. Older custom
bindings take precedence over a new conflicting default. SDL3 also accepts
`--gamepad-map mic=rightshoulder` or `--gamepad2-map microphone=rightshoulder`.
The signal is independent of controller strobing and works only through RF
wiring.

Optional live capture is off by default. In GUI Settings → Input → Live
microphone, enable Capture input, choose an input device, and raise Sensitivity
for quieter sounds. Capture runs during unpaused RF Famicom play. Pause and
snapshot loads discard capture buffers; device failures release live activity
and show an error until Retry / Refresh Devices or a setting change. Keyboard
and gamepad simulation remain available. Microphone audio optionally adds captured
sound to playback with an independent 0–100 volume control (default 25). It starts
off and requires Capture input. Sensitivity only controls game input detection.
Master audio volume/mute affect the combined sound. Use headphones to avoid
feedback. No raw microphone sound is saved.

On macOS, answer the first-use microphone prompt and select Retry / Refresh
Devices. Denial messages show the privacy-settings path; follow macOS's Quit &
Reopen request when changing access, then Retry. Restricted access needs policy
administrator help. Bound input remains available. See
[permission behavior](configuration.md#macos-microphone-permission).

The SDL3 executable accepts these process-local overrides:

```text
--live-microphone
--no-live-microphone
--microphone-device NAME
--microphone-sensitivity N
--microphone-audio
--no-microphone-audio
--microphone-volume N
```

Use `default` for the system input and an integer from 1 to 100 for sensitivity.
`--microphone-audio` requires `--live-microphone` (or saved capture enabled),
and volume accepts 0–100. The headless CLI does not open capture. See
[capture preferences](configuration.md#live-microphone-capture).

## Headless CLI

```text
./buildDir/src/frontends/cli/mapperbus-cli <rom-file|archive> [--entry NAME] [--fds-bios PATH] [--frames N] [--controller-timing T] [--controller-wiring W] [--controller-mode standard|four-score|hori-4-player] [--expansion-controllers none|1|2|both]
```

Loads NES/FDS media, resets the emulator, executes 60 frames by default, and
exits. `--frames` accepts 1–1000000. `--controller-timing` accepts
`nes-av-famicom` or `rf-famicom` and overrides the shared preference for this run.
`--controller-wiring` accepts `nes` or `rf-famicom` independently of timing.
Select `rf-famicom` for both options to use RF Famicom timing and wiring.
The CLI reads the shared FDS BIOS path;
`--fds-bios` overrides it. It presents no video, audio device, or interactive
input. Invalid arguments and load/save failures return failure.

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
