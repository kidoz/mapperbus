# Save and restore a game

Load a ROM in the SDL3 or NodalKit frontend before saving or loading a state.
States use your per-user data directory by default, so the ROM folder can be
read-only.

## Use a save slot

Each game has ten slots, labeled 1–10. MapperBus remembers the selected slot
by ROM content, including after restarting, moving or renaming the ROM, or
clearing recent games.

1. Select a slot. In the GUI, use **Emulation → State Slot** in the native menu
   or **Choose State Slot…** in the header menu. The chooser marks slots as
   Saved or Empty. In SDL3, press `Ctrl+1`–`Ctrl+9` for slots 1–9, or `Ctrl+0`
   for slot 10.
2. Press `F5` to save to the selected slot.
3. Continue playing, then press `F7` to restore that slot.

The GUI shows the active slot in its status bar and save/load menu labels;
SDL3 logs it. Saving replaces that slot. Selecting an empty slot does not save
automatically; loading one reports an error and preserves the current machine.

`Shift+F5` and `Shift+F7` target slot 2 directly. A successful save or load also
selects and remembers that slot. On keyboards that reserve function keys for
system actions, hold Fn as well.

## Locate or change the save folder

Use **Open States Folder** in the GUI's slot menu or chooser to open the loaded
game's folder. Default locations are:

| Platform | State root |
|---|---|
| Linux | `$XDG_DATA_HOME/mapperbus/states`, or `~/.local/share/mapperbus/states` |
| macOS | `~/Library/Application Support/MapperBus/states` |
| Windows | `%APPDATA%\MapperBus\states` |

Each game uses a folder such as `Game--<SHA-256>` containing `slot-0.state`
through `slot-9.state` (displayed as slots 1–10). The hash identifies exact ROM
contents; the display name is a label. Existing folders are reused by hash
after a rename, while different revisions, regions, or hacks stay separate.

In **Choose State Slot…**, choose **User data directory** or **Portable (beside
ROM)**, then **Apply Storage Location**. Portable mode uses
`<ROM-folder>/mapperbus-states/<game-key>/`. You can instead enter an absolute
folder override; it takes priority over Location. Clear it to use the selected
mode. Storage settings apply to all games; changing them leaves existing files
where they are.

For SDL3, use `--state-dir /absolute/path` or `--portable-states`. These options
override the saved storage settings for that run. The selected slot is still
remembered. See the [configuration reference](../reference/configuration.md).

## Load an older sibling state

If the configured location has no file for a slot, MapperBus checks legacy
siblings: `game.state` for slot 1, then `game.state1` through `game.state9` for
slots 2–10. A present but invalid central file reports an error instead of
falling back. Legacy files are never moved or deleted. Pressing `F5` after an
import writes a new snapshot to the configured location.

New version-7 snapshots (and supported version-4/5/6 snapshots) validate the media
SHA-256 before changing the machine. New snapshots preserve FDS audio latches and
DRAM watchdog state. Older snapshots reconstruct missing audio state and leave
the watchdog disabled until the next `$4023` write.
The legacy sibling import also accepts version 3; those files contain no hash,
so use them only with their original ROM. Older versions remain unsupported.

For archived ROMs and FDS games, legacy sibling import is disabled because an archive can
contain several games. Content-identified state files work across raw and
archived copies of the same bytes.

## Keep battery-backed progress

For cartridges with battery-backed RAM, MapperBus automatically tries to load
`game.sav` when opening `game.nes`. It flushes that RAM when switching or closing
a cartridge and when the emulator is destroyed. Exit normally so the current
progress can be written.

To back up progress, close the frontend and copy the game's state folder and
the sibling `.sav` file with the ROM. Keep any legacy sibling states as well. Battery saves hold the cartridge's
persistent RAM; save states hold an emulated machine snapshot. A game without
battery-backed RAM does not gain an automatic battery save.

Save-state loading validates the version, mapper, media identity, and serialized data. Keep the
corresponding ROM and build when preserving a long-lived snapshot; cross-version
or unrelated-ROM compatibility is not guaranteed.

Implementation: [EmulationSession](../../src/app/emulation_session.cpp) names
state files; [Emulator](../../src/core/emulator.cpp) handles snapshots and
battery-backed RAM.

Archived cartridges store battery RAM under the MapperBus application data
folder as `battery/<ROM-SHA256>.sav`. Different games in one archive have
independent saves. Raw files retain their sibling `.sav` behavior. See
[Open archived ROMs](open-archived-roms.md) for locations and selection.

FDS disk progress uses separate content-identified disk saves. See
[Play Famicom Disk System games](play-fds-games.md#keep-disk-progress).
