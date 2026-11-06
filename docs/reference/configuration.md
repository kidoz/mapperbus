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
- GUI settings and per-game slot selections can be saved. The SDL3 frontend reads supported settings and
  applies process-local command-line overrides. The headless CLI reads the FDS BIOS path.

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

## Famicom Disk System

`[fds] bios_path` is an optional, quoted path to an exactly 8192-byte BIOS.
It defaults to empty, selecting application data `bios/disksys.rom`. The GUI
persists it through File → Famicom Disk System; SDL3 and CLI accept a process-local
`--fds-bios PATH` override. See [FDS setup](../how-to/play-fds-games.md).

## Save-state storage and selected slots

| `[save_states]` key | Default | Values |
|---|---|---|
| `mode` | `user-data` | `user-data` or `portable`; unknown values use `user-data` |
| `directory` | empty | Absolute state-root override; empty uses `mode`; relative paths are ignored |

The user-data root is the OS-specific data directory plus `states`: Linux uses
absolute `$XDG_DATA_HOME` or `~/.local/share`, macOS uses
`~/Library/Application Support/MapperBus`, and Windows uses
`%APPDATA%\MapperBus` (home `AppData/Roaming/MapperBus` fallback).
Portable mode uses `<ROM-folder>/mapperbus-states`. An explicit `directory`
takes priority over either mode. Roots contain sanitized display-name folders
suffixed by the full ROM SHA-256. Existing folders are matched by hash, so a
filename change does not create a new save identity.

`[state_slots]` maps lowercase, 64-digit content SHA-256 keys to selected slot
indices `0`–`9` (displayed as 1–10), shared by SDL3 and the GUI. Missing or invalid
records use slot 1. Entries are independent of the recent-games list. Both
sections are optional version-1 additions; older builds ignore them.

SDL3 `--state-dir PATH` and `--portable-states` override storage for the current
run without persisting that override. Explicit slot changes and successful
save/load targets are remembered by content hash.

The core writes version-5 snapshots with media SHA-256 in the header; directory
policy lives in the application layer. Legacy version-3 loading is enabled
only when importing a sibling `.state`/`.stateN` file. Battery `.sav` behavior
is unchanged. See [Save and restore a game](../how-to/save-and-restore.md).

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
| `turbo_a` | `22` | S |
| `turbo_b` | `4` | A |
| `start` | `40` | Return |
| `select` | `229` | Right Shift |

| `[input.gamepad]` key | Default |
|---|---|
| `enabled` | `true` |
| `index` | `0` (nonnegative integer) |
| `deadzone` | `12000` (clamped to 1–32767) |
| `up`, `down`, `left`, `right` | `dpup`, `dpdown`, `dpleft`, `dpright` |
| `a`, `b` | `east`, `south` |
| `turbo_a`, `turbo_b` | `north`, `west` |
| `start`, `select` | `start`, `back` |

See [mapping tokens](commands-and-controls.md#gamepad-mapping-tokens) for accepted
control names. These sections configure player 1. Player 2 has independent
`[input.player2.keyboard]` and `[input.player2.gamepad]` sections with the same
keys. Its keyboard defaults to unbound (`0`) for every action, and its gamepad
defaults to index `1`; the other gamepad defaults match player 1.

In GUI Settings → Input, select **Player 1** or **Player 2** before rebinding,
choosing a gamepad, or restoring defaults. Each profile saves automatically.
Both graphical frontends combine the keyboard and gamepad assigned to each
player. Turbo speed is shared by both players.

Keyboard code `0` and gamepad token `none` explicitly leave an action unbound.
Missing turbo bindings use the defaults above unless those controls already
belong to another action in the saved configuration; conflicting new defaults
start unbound.

`[input.turbo]` stores `period_frames`: `6` (Slow), `4` (Normal, default), or
`2` (Fast). Other values fall back to `4`. Half the period is pressed and half
released. This optional version-1 setting applies to keyboard and gamepad input
in both graphical frontends. See [turbo behavior](commands-and-controls.md#default-keyboard-bindings).

## Recent ROMs

`[recent]` stores up to five `rom_0`…`rom_4` path entries and matching
`opened_0`…`opened_4` Unix timestamps, newest first. Paths are quoted when saved.
Optional quoted `member_0`…`member_4` values identify a selected ROM inside a
ZIP or 7z archive. Missing values mean raw media or automatic archive selection.
Deduplication uses both the normalized filesystem path and the exact member name.
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
