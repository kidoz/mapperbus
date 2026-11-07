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

The NodalKit GUI saves settings in the background. Rapid edits keep only the
latest pending configuration behind the active write. Settings shows **Saving
settings…**, **Saved automatically**, or a failure with **Retry Settings Save**.
Ordinary settings take effect while their save is pending; failed writes retain
the previous file and keep newer edits available for Retry.

Controller hardware, remembered player assignments, gamepad assignment, and
validated save-folder changes wait for saving before they apply. **Saving
Settings** cannot be cancelled once writing starts. A failed proposal offers
**Retry** or **Keep Current Settings**. Discarding a proposal retains the active
settings. If a controller disconnects before assignment is applied, reconnect it
and run Assign Gamepads again.

Normal Close ROM and Quit wait for settings writes as well as game progress.
A save failure keeps the application open with **Keep Open** and **Retry**.
The file format and synchronous configuration APIs used by other frontends are
unchanged.

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
configuration file. A new GUI ROM open reapplies the resolved global or per-game
preference. Reset retains the active machine selection. See [DMA timing](dma-timing.md).

## Controller wiring

`[input] controller_wiring` accepts `nes` (default) or `rf-famicom`. Missing or
invalid values select NES. It is independent of `controller_timing`; choose
`rf-famicom` for both to use the modeled RF Famicom input hardware.

All frontends apply this preference. CLI and SDL3 accept `--controller-wiring`
with the same tokens, without changing the saved configuration. The GUI exposes
Settings → General → Hardware → Controller wiring and saves explicit choices.
RF wiring models the built-in second controller without Start/Select and the
unattached controller-port inputs. Digital microphone activity is available
through the Microphone action. External standard pads are configured separately below. Existing host bindings are retained for switching back to
NES. Data-pin changes take effect immediately; a latched button report changes
on the next strobe. Reset retains the selection.

Snapshot version 15 stores wiring independently of timing. Older snapshots load
NES wiring, including version 14 snapshots that selected RF timing. Loading a
snapshot changes the active selection without rewriting the configuration;
a new GUI ROM open reapplies the resolved global or per-game preferences.

## Controller mode

`[input] controller_mode` accepts `standard` (default), `four-score`, or `hori-4-player`.
Missing or invalid values select Standard. GUI Settings → General → Hardware
exposes **Controller mode**; CLI and SDL3 accept `--controller-mode` with the
same tokens as a process-local override.

Four Score uses the four existing input profiles as Players 1–4. Profiles 3/4
keep their `input.player3`/`input.player4` sections and default device indices
2/3; their keys start unbound. GUI profile labels become **Player 3/4** in this
mode. The same bindings return as **External 1/2** in Standard mode.

Four Score sends all four pads on D0 and suppresses external D1 reports. The
Famicom attachment switches are disabled while this mode is active; their saved
values are retained for switching back. All adapter pads include Start/Select,
including when RF built-in wiring is selected. Wiring and NTSC timing remain
independent choices; NES wiring with NES/AV timing is the normal Four Score setup.
Games must support the adapter's four-player report. Host turbo actions remain
available; Hori and adapter-specific physical turbo circuitry are outside this mode.

Reset retains mode. Version 18 snapshots restore mode and partially read reports;
older snapshots select Standard while retaining any v17 Famicom attachments.
Loading a snapshot leaves the saved startup preference unchanged. Opening another
GUI game applies its remembered hardware settings, or the global defaults. A mode
change ends any held read once and initializes a fresh adapter report; reapplying the same mode preserves its position.

### Remember hardware for a game

In **Settings → General → Hardware**, turn on **Remember for this game** while
a game is loaded. It saves the active controller mode, wiring, timing and both
Standard-mode expansion attachments. Later changes in this group update that
game's preferences. With the switch off, changes update global defaults. The
switch is disabled without a loaded game.

Turn it off to remove the game's override and immediately apply global defaults.
Button bindings, keyboard controls, deadzones, turbo and microphone preferences
remain shared. Device assignments can be remembered independently under Input. A save failure retains the previous
preferences and running hardware, and shows an error in Settings.

All three frontends resolve controller hardware in this order: global `[input]`
defaults, saved game overrides, then explicit CLI options for the current process.
Preferences are keyed by the same content SHA-256 as save states. Byte-identical
NES ROMs match across names, locations, raw files, ZIP and 7z members; different
ROMs in one archive remain independent. FDS uses the existing combined BIOS/disk
identity, so changing the BIOS can select a different record.

Each optional version-1 section is `[game_controllers.<64 lowercase hex digits>]`:

```ini
[game_controllers.aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa]
controller_mode=hori-4-player
controller_wiring=rf-famicom
controller_timing=rf-famicom
expansion_controllers=none
```

The hash above is an example; the GUI writes the loaded game's actual identity.
`expansion_controllers` accepts `none`, `1`, `2`, or `both`. Missing or invalid
fields inherit global defaults; unknown keys, invalid identities and empty
records are ignored. The GUI saves all four hardware fields when remembering a
game. Older versions ignore these additive sections; the file version stays 1.

Opening a game resolves its preferences before the first frame. Reset retains
active hardware. Loading a save state restores that state's hardware without
rewriting either global or game preferences. Explicit edits change the selected
hardware field; **Remember for this game** captures all active fields. Closing a
GUI game returns the hardware controls to global defaults.

### Remember player assignments for a game

Load a game, then enable **Settings → Input → Remember assignments for this
game**. This captures the current device choices for all six host profiles,
including inactive ones. Use **Assign by Button…**, the Hori index preset, or
individual device/enabled controls to change that game's player order. Button
bindings, keyboard bindings and deadzones remain shared across games.

The option is disabled without a loaded game. Turn it off to remove the game's
assignment set and immediately restore global device choices. With it off,
assignment edits update global defaults. This is independent of the hardware
preference switch in General: remember controller mode there as well when games
need different adapters.

GUI and SDL3 resolve assignments by the same content identity as hardware
preferences and save states, so matching raw, ZIP and 7z media reuse a layout.
Successful game opens apply the resolved assignments before the first frame;
failed opens retain the current layout. Closing a GUI game restores global
assignments. Reset and save-state loading retain host assignments. Automatic
discovery/reconnect updates the active game's device records while an override
exists, preserving global choices and other games.

The scope switch saves before changing the active layout; a failed save leaves
the previous scope and layout in place and shows an error. The assignment wizard
retains its atomic save/application behavior. Existing identity rules apply:
indistinguishable devices may need reassignment after reconnect, restart or
switching layouts.

Optional version-1 sections store device choices only:

```ini
[game_gamepads.aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.player1]
enabled=true
index=0
guid=""
serial=""
device_name=""
ambiguous=false
```

The hash is an example; the GUI writes the actual content identity and sections
for `player1` through `player6`. Missing fields/profiles in a valid partial record
inherit final global assignments on load, regardless of section order. Unknown
keys (including bindings/deadzones), invalid identities/player numbers, and
invalid boolean/index values are ignored. An invalid/empty GUID clears identity
metadata and retains index selection. Strings use ordinary configuration quoting
and identity size limits. The GUI saves a complete six-profile set. Older readers
ignore these additive sections; the file and save-state versions are unchanged.

SDL3's explicit `--gamepad`, `--gamepad2`, and `--gamepad3`–`--gamepad6` indices
replace the resolved identity/index for that run. Remembered enabled switches
still apply. Automatic discovery never saves those overridden profiles; process
button maps and deadzones also stay process-local as before.

### Assign gamepads by button

In **Settings → Input → Player order**, choose **Assign by Button…**. The wizard
uses the active machine's controller mode and attachments, including hardware
restored from a save state:

| Mode | Assignment order |
|---|---|
| Standard | Player 1, Player 2, then each attached External 1/2 pad |
| Four Score | Players 1–4; Standard expansion attachments are ignored |
| Hori 4 Players | Hori 1–4; the built-in profiles are retained |

Standard requires two, three or four controllers according to its attachment
switches in General → Hardware. With only External 2 attached, the third role is
**External 2**; External 1 is retained. Disabled host profiles can be assigned and
are enabled on successful completion. The Hori index preset remains available
separately as **Configure Four Gamepads**.

Press a fresh button on each controller in the displayed order. Buttons already
held when the wizard opens or a controller connects are ignored; release them
and press again. The same controller cannot fill two roles, and simultaneous
presses on different controllers require a retry. If a chosen controller
disconnects, capture returns to its step and clears later choices.

The wizard pauses a running game and suppresses game input during capture. It
saves and applies all listed assignments together after every selected
controller's buttons are released. **Cancel**, Escape, or closing Settings
discards pending choices and preserves the prior pause state. Missing devices
or a failed save prevent partial application.

Bindings, deadzones, keyboard controls, profiles outside the wizard and hardware
preferences are retained. Assignments follow Input's **Remember assignments for
this game** switch; bindings and deadzones remain shared. General's **Remember
for this game** controls hardware independently. Identical controller models
can be assigned for the current session; their usual remembered-identity limits
apply after reconnect/restart. Physical testing is separate from automated
virtual-controller checks. Builds without gamepad support disable the button.

### Multiplayer input monitor

Choose **Settings → Input → Test All Controllers…** to view the active inputs
in one panel. Standard shows Players 1/2 and attached External 1/2 pads; Four
Score shows Players 1–4; Hori shows both built-in pads plus Hori 1–4. These six
Hori inputs do not imply six-player support in a game.

Each row shows the assigned gamepad slot and device name, connection state and
held keyboard/gamepad bindings. States distinguish connected, disconnected,
disabled, unavailable and needing reassignment. Held bindings include Turbo A/B
and Microphone; the monitor shows them before turbo pulses and hardware filters,
including RF pad 2's Start/Select restriction. It does not show live microphone
sound or the controller's serial report.

If two active roles use the same connected physical gamepad, both rows say
**Shared gamepad with…**. This is informational: some games intentionally alias
built-in and external inputs. Use **Assign by Button…** if the sharing is
unintended. Identical model names, disconnected devices and inactive roles do
not create sharing notices; live device identity determines sharing.

Opening pauses a running game; closing restores its prior pause state. Return
and Space can be tested while the panel itself has focus. Escape closes it;
Tab moves focus to **Done**, which also supports keyboard activation. Keyboard
capture stays local to the monitor and clears on focus loss. Keyboard testing
also works in builds without gamepad support. Long device names or small
windows can scroll while Done remains available.

The monitor updates about 30 times per second and changes no mappings, hardware
preferences, turbo phases or snapshots. Normal device discovery remains active.

### Hori 4 Players Adapter

Select `hori-4-player` for the Famicom adapter's four-controller serial mode.
The two built-in pads remain independent on D0; four adapter pads use D1.
GUI Input shows **Built-in 1/2** and **Hori 1–4**. These six hardware inputs do
not imply that every game supports six players.

Hori 1/2 reuse profiles 3/4 (`input.player3`/`input.player4`). Hori 3/4 use
profiles 5/6 (`input.player5.keyboard`/`input.player5.gamepad` and the corresponding
`player6` sections). New keys start unbound; new gamepad indices default to 4/5.
SDL3 accepts `--gamepad5`, `--gamepad6`, `--gamepad5-map`, and `--gamepad6-map`.
Assign available devices to the Hori roles you want to use; device indices are
independent of in-game player numbers.
GUI Input offers **Configure Four Gamepads** while Hori mode is active. It enables
Hori 1–4 and assigns Gamepads 1–4 (device indices 0–3), preserving keyboard/button
bindings, deadzones, built-in profiles, and the startup controller mode. It saves
immediately and can be used before connecting devices. Other modes retain their
usual per-profile controls. Reapplying deliberately replaces remembered identities
with the current device order.

Keyboard/gamepad settings, turbo phases and defaults remain independent for all
six profiles. Every profile can bind the same shared microphone action.

The Hori mode connects four standard adapter pads independently of the two
Standard-mode attachment preferences. Those switches remain disabled while an
adapter mode is active, retaining their saved values. To model the adapter's
two-controller pass-through setting, use Standard with External 1/2 attached.
Four Score continues using only profiles 1–4. Profiles 5/6 remain saved when
inactive. Selecting a mode with four profiles clamps the visible selection to
that range without changing bindings.

Version 19 adds Hori mode and the last two pads' state. Versions 3–18 clear those
new pads; version 18 retains Four Score mode and reports. Earlier mode defaults
are unchanged. Hori identification bits, strobe, DMA and snapshot behavior are
specified under [DMA timing](dma-timing.md#hori-4-players-adapter).

## External Famicom controllers

`[input] expansion1_attached` and `expansion2_attached` are independent booleans,
both defaulting to `false`. Missing or invalid values leave that pad detached.
GUI Settings → General → Hardware exposes **External controller 1/2** switches.
CLI and SDL3 accept `--expansion-controllers none|1|2|both` for this process.
Attachments are independent of wiring and timing; select RF wiring/timing for
an original Famicom setup.

External 1 uses `[input.player3.keyboard]` / `[input.player3.gamepad]`; External 2
uses the corresponding `player4` sections. Their keyboard bindings start unbound;
gamepad indices default to `2` and `3`. Mapping tokens and other defaults match
the built-in profiles. SDL3 also accepts `--gamepad3`, `--gamepad4`,
`--gamepad3-map`, and `--gamepad4-map`.

These are standard eight-button pads on D1 of `$4016`/`$4017`. Built-in pads
remain on D0; external pad 2 includes Start/Select even with RF wiring. Games
must read D1 to use them. Four Score and Hori serial adapters use the separate
controller modes above. See [serial behavior](dma-timing.md#external-standard-pads).

Reset retains attachments. Version 17 snapshots restore attachments and partially
shifted reports; versions 3–16 import with external pads detached. Snapshot loads
leave the saved startup preference unchanged; a new GUI ROM open reapplies it.

## Microphone bindings

All six input profiles accept `microphone` under their keyboard/gamepad sections.
Player one defaults to keyboard code `16` (M); player two is unbound. Gamepad
bindings default to `none`. Every profile activates the same single RF Famicom
microphone. GUI Settings → Input includes Microphone capture, clear, conflict
handling, defaults and input testing. SDL3 mapping options accept `microphone`
or `mic`, for example `--gamepad2-map microphone=rightshoulder`.

Hold the bound key/button with RF Famicom wiring selected to generate digital
activity. Keyboard M defaults are unbound on legacy import if another action in
any profile already uses M; explicit bindings are retained. Bindings use the
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
device state are host preferences outside snapshots; snapshots store the core's
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

External pads use `player3` and `player4` sections as described above.

In GUI Settings → Input, select the profile for the active controller mode
before rebinding, choosing a gamepad, or restoring defaults. Standard uses
**Player 1/2** and **External 1/2**; Four Score uses **Player 1–4**; Hori uses
**Built-in 1/2** and **Hori 1–4**. Each profile saves automatically.
Both graphical frontends combine the keyboard and gamepad assigned to each
player. Turbo speed is shared by all six profiles; their phases are independent.

Device indices refer to the current SDL enumeration. On opening a device, both
frontends remember its identity and save it automatically. A saved identity takes
priority over the index after reconnect or restart; an unavailable saved device
waits rather than adopting a different controller at that index. The displayed
index follows the connected device when enumeration changes. Unplugging releases
held buttons, turbo and microphone actions.

Each gamepad section can include `guid`, `serial`, `device_name`, and `ambiguous`.
These optional version-1 keys use the normal quoted-string escaping. GUIDs must
contain 32 hexadecimal digits; serial/name strings are capped at 256 bytes.
Missing identities retain index-based selection and are learned on first open.
Older builds ignore these keys. No emulator snapshot fields change.

Serial numbers are used when [SDL supplies them](https://wiki.libsdl.org/SDL3/SDL_GetGamepadSerial).
Without a serial, a unique matching model can reconnect automatically. A model
match cannot distinguish two same-model physical pads that are never connected
together. Once indistinguishable matches are observed, `ambiguous=true` is retained
and automatic reassignment is blocked, including after only one remains. A live
selected handle continues working until it disconnects; after reconnect/restart,
choose **Preferred device → Use Selected Gamepad**. This explicit action keeps
bindings and deadzones. Device connection changes refresh the chooser/status.

**Configure Four Gamepads** deliberately assigns the current first four devices.
SDL3 `--gamepad`, `--gamepad2`, and `--gamepad3` through `--gamepad6` also select
an index explicitly for that run, overriding its saved identity without replacing
the saved assignment. A different connection transport or changed device GUID
may require explicit reassignment. Physical serial-number behavior is dependent
on the controller and SDL driver.

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
