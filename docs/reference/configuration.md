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
- GUI settings and per-game slot selections can be saved. The SDL3 frontend
  reads supported settings and applies process-local command-line overrides.
  The headless CLI reads the FDS BIOS path and controller hardware preferences.

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

## Controller timing

`[input] controller_timing` accepts `nes-av-famicom` (default) or `rf-famicom`.
Unknown or missing values select the default. All frontends apply this preference;
CLI and SDL3 accept a process-local `--controller-timing` override. In the GUI,
Settings → General → Hardware changes it immediately for both ports and saves it.
It affects NTSC read edges only, independently of region and CPU revision.

Snapshots restore the timing saved with the machine; versions before 14 select
NES/AV Famicom. Loading a snapshot does not overwrite the preference in the
configuration file. A new GUI ROM open reapplies the saved preference. Reset
retains the active machine selection. See [DMA timing](dma-timing.md).

## Controller wiring

`[input] controller_wiring` accepts `nes` (default) or `rf-famicom`. Missing or
invalid values select NES. It is independent of `controller_timing`; choose
`rf-famicom` for both to use the modeled RF Famicom input hardware.

All frontends apply this preference. CLI and SDL3 accept `--controller-wiring`
with the same tokens, without changing the saved configuration. The GUI exposes
Settings → General → Hardware → Controller wiring and saves explicit choices.
RF wiring models the built-in second controller without Start/Select and the
unattached controller-port inputs. Digital microphone activity is available
through the Microphone action; expansion accessories are not implemented. Existing host bindings are retained for switching back to
NES. Data-pin changes take effect immediately; a latched button report changes
on the next strobe. Reset retains the selection.

Snapshot version 15 stores wiring independently of timing. Older snapshots load
NES wiring, including version 14 snapshots that selected RF timing. Loading a
snapshot changes the active selection without rewriting the configuration;
a new GUI ROM open reapplies both preferences.

## Microphone bindings

Both input profiles accept `microphone` under their keyboard/gamepad sections.
Player one defaults to keyboard code `16` (M); player two is unbound. Gamepad
bindings default to `none`. Either profile activates the same single RF Famicom
microphone. GUI Settings → Input includes Microphone capture, clear, conflict
handling, defaults and input testing. SDL3 mapping options accept `microphone`
or `mic`, for example `--gamepad2-map microphone=rightshoulder`.

Hold the bound key/button with RF Famicom wiring selected to generate digital
activity. Keyboard M defaults are unbound on legacy import if another action in
either profile already uses M; explicit bindings are retained. Bindings use the
existing version-1 format. Runtime activity and carrier phase are stored in
snapshot version 16, not user configuration. Older states restore an inactive
microphone. Bound input remains available alongside optional live capture.
See [microphone timing](dma-timing.md#digital-microphone).

## Live microphone capture

GUI Settings → Input → Live microphone saves these optional version-1 keys:

```ini
[microphone]
enabled=false
device=""
sensitivity=50
audio_enabled=false
volume=25
```

Capture is disabled by default. An empty device selects the system recording
input when capture starts; a name selects the first exact matching device.
Duplicate names appear once. A missing named device reports an error without
falling back. Sensitivity ranges from 1 to 100; larger values detect quieter
sounds. Out-of-range integers clamp, and malformed values use 50.

Only unpaused RF Famicom play opens capture. Pause, stop, ROM close, wiring
changes away from RF, and successful snapshot loads close it and discard queued
sound. Resume or the next eligible frame starts fresh. Disconnects, device/default
changes, and capture failures release live activity; use Retry / Refresh Devices
or change a setting to retry. System-default capture stops if the recording-device
set or default name changes. Bound keyboard/gamepad input remains usable.

Microphone audio is separately off by default. Enable it to add captured sound
at playback, with `volume` from 0 to 100 (default 25). Both Capture input and
unpaused RF play are required; enabling playback alone never opens a device.
The volume does not affect sensitivity or the game's activity signal, and
sensitivity does not gate playback. GUI master volume/mute apply to the combined
output. Bound M/gamepad simulation produces activity only, not an audio tone.

The host mixer linearly resamples 16 kHz mono PCM to the session playback rate,
copies it to both stereo channels when needed, and clamps the sum to [-1, 1].
Its queue holds at most 2048 capture samples (128 ms); stale backlogs are replaced
and underruns add silence. Pause, stop, reset, device failure, successful snapshot
loads, device/audio-setting changes, Retry, and disabling/changing microphone
audio discard pending mic sound. Queued playback that may contain it is cleared
at those boundaries. Capture stays open when only microphone playback is muted.
This is an optional host monitor, not a model of Famicom analog mixing circuitry.

Raw PCM is used only in memory, never saved or transmitted. Capture settings and
device state are host preferences outside snapshots; version 16 still stores only the core's
processed activity and carrier phase. The SDL3 executable accepts process-local
[overrides](commands-and-controls.md#famicom-microphone). Builds without SDL audio
report capture unavailable. Real-device GUI capture has been checked on macOS
with the built-in microphone and a Steinberg UR22mkII. macOS desktop binaries
and the app bundle include a microphone purpose description. See the
[validation scope](fds-compatibility.md#gui-workflow-validation).

### macOS microphone permission

On the first eligible physical-device start, macOS asks for microphone access.
Answer the prompt and choose Retry / Refresh Devices. The request is asynchronous;
no recording stream opens while access is pending, and emulation plus bound
keyboard/gamepad microphone input remain usable. Opening Settings or enumerating
devices never requests access. Granting permission alone does not start capture
until Retry or a relevant settings change.

If access is denied, enable MapperBus under System Settings → Privacy & Security
→ Microphone. Quit and reopen the app if macOS asks, then Retry. macOS can cache
the old decision until the process exits; Retry cannot replace a required restart.
Restricted access reports the policy restriction and directs the user to their
administrator. Permission is checked again on capture reads/discards; loss of
access closes capture and clears pending microphone playback through the normal
failure path. Permission decisions stay in macOS, outside config and snapshots.

The macOS bridge uses [AVFoundation authorization status](https://developer.apple.com/documentation/avfoundation/avcapturedevice/authorizationstatus(for:))
and its [asynchronous request API](https://developer.apple.com/documentation/avfoundation/avcapturedevice/requestaccess(for:completionhandler:)).
SDL dummy/disk audio drivers bypass the native permission prompt. Native grant,
denial, and privacy-setting recovery were exercised with temporary app identities;
managed-policy restriction is covered by injected tests.

## Famicom Disk System

`[fds] bios_path` is an optional, quoted path to an exactly 8192-byte BIOS.
It defaults to empty, selecting application data `bios/disksys.rom`. The GUI
persists it through Settings → General → BIOS; SDL3 and CLI accept a process-local
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

The core writes version-7 snapshots with media SHA-256, FDS audio phase/output
latches, and DRAM watchdog state. Versions 4–6 remain readable; directory policy lives in
the application layer. Legacy version-3 loading is enabled
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
