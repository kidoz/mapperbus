# Configuration

MapperBus frontends share `mapperbus.conf`, an INI-style UTF-8 file. The core
does not read this file. Implementations and defaults are defined in
[configuration.hpp](../../src/app/configuration.hpp),
[configuration.cpp](../../src/app/configuration.cpp), and
[audio_settings.hpp](../../src/core/apu/audio_settings.hpp).

## File locations

| Platform | Location |
|---|---|
| macOS | `~/Library/Application Support/MapperBus/mapperbus.conf` |
| Linux | `$XDG_CONFIG_HOME/mapperbus/mapperbus.conf` when the variable is an absolute path; otherwise `~/.config/mapperbus/mapperbus.conf` |
| Windows path convention | `%APPDATA%\MapperBus\mapperbus.conf`; fallback to the home directory's `AppData/Roaming/MapperBus/mapperbus.conf` |

The Windows path is implemented in the configuration layer; it does not imply
that the Windows frontend has the same validation coverage as macOS and Linux.
When `mapperbus.conf` is absent, the loader can read a sibling legacy
`config.ini`. Saving writes `mapperbus.conf`.

## Parsing and persistence

- Sections use `[section]`; settings use `key=value`.
- Blank lines and lines starting with `#` or `;` are ignored. Unquoted values
  end at either comment character. Section and key names use the spellings below.
- Quoted values support escaped backslashes, double quotes, `\n`, and `\r`.
  A trailing comment is allowed after a closing quote. Broken quoted values are skipped.
- Unknown keys and sections are ignored when loading. Missing settings use
  source defaults. Malformed recognized values generally warn and use a fallback;
  numeric settings with bounds are clamped.
- Saving writes recognized settings through a sibling temporary file followed
  by rename. Unknown settings and comments are not preserved.
- GUI settings can be saved. The SDL3 frontend reads supported settings and
  applies process-local command-line overrides. The headless CLI does not load
  this configuration.

## Format identity

| Section | Key | Saved value |
|---|---|---|
| `[mapperbus]` | `format` | `mapperbus.conf` |
| `[mapperbus]` | `version` | `1` |

These fields identify the written format; they are not a strict parser gate.

## Frontend preferences

These preferences are used by the GUI; the SDL3 entry point does not apply them
as GUI presentation settings or as its startup region override.

| `[frontend]` key | Default | Values |
|---|---|---|
| `preview_scale` | `0` | Index 0–3 in the GUI's preview-scale choices |
| `ui_density` | `0` | Index 0–3 in the GUI's density choices |
| `audio_muted` | `false` | Boolean |
| `audio_volume` | `100` | Integer percentage, 0–100 |
| `region` | `auto` | `auto`, `ntsc`, `pal`, `dendy` |

Use the GUI to choose indexed preferences rather than treating the stored index
as a literal scale multiplier or pixel density.

## Audio

The following are canonical saved tokens; the parser also accepts some legacy
aliases. Boolean values accept `true/false`, `yes/no`, `on/off`, and `1/0`.

| `[audio]` key | Default | Values / bounds |
|---|---|---|
| `sample_rate` | `96000` | Integer Hz, 8000–192000; actual device rate may differ |
| `buffer_size` | `2048` | Integer samples, 128–32768 |
| `resampling` | `blip` | `blip`, `cubic` |
| `filter_mode` | `accurate` | `accurate`, `enhanced`, `unfiltered` |
| `filter_profile` | `nes` | `nes`, `famicom` |
| `stereo` | `mono` | `mono`, `pseudo-stereo` |
| `dithering` | `false` | Boolean |
| `expansion_mixing` | `simple` | `simple`, `resistance` |
| `drc_target_fill_ratio` | `0.5` | Decimal, 0–1 |
| `drc_deadzone` | `0.05` | Decimal, 0–1 |
| `drc_rate_adjustment` | `0.005` | Decimal, 0–0.05 |

## Keyboard and gamepad

Keyboard values are integer NodalKit / USB HID-style key codes, not characters.

| `[input.keyboard]` key | Default code | Key |
|---|---|---|
| `up` | `82` | Up arrow |
| `down` | `81` | Down arrow |
| `left` | `80` | Left arrow |
| `right` | `79` | Right arrow |
| `a` | `27` | X |
| `b` | `29` | Z |
| `start` | `40` | Return |
| `select` | `229` | Right Shift |

| `[input.gamepad]` key | Default |
|---|---|
| `enabled` | `true` |
| `index` | `0` (nonnegative integer) |
| `deadzone` | `12000` (clamped to 1–32767) |
| `up`, `down`, `left`, `right` | `dpup`, `dpdown`, `dpleft`, `dpright` |
| `a`, `b` | `east`, `south` |
| `start`, `select` | `start`, `back` |

See [mapping tokens](commands-and-controls.md#gamepad-mapping-tokens) for accepted
control names. This persisted gamepad section is shared input configuration;
SDL3 player 2 selection and remapping are supplied at runtime.

## Recent ROMs

`[recent]` stores up to five `rom_0`…`rom_4` path entries and matching
`opened_0`…`opened_4` Unix timestamps, newest first. Paths are quoted when saved.
Opening an existing entry moves it to the front. Removing a recent entry does
not delete ROM or save files.

## Minimal example

Missing keys use defaults, so a partial file is sufficient:

```ini
[mapperbus]
format=mapperbus.conf
version=1

[audio]
sample_rate=48000
filter_profile=famicom

[input.gamepad]
enabled=true
index=0
a=east
b=south
```

For settings tasks, see [Configure the SDL3 frontend](../how-to/configure-sdl3.md).
