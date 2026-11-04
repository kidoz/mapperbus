# Save and restore a game

Load a ROM in the SDL3 frontend before saving or loading a state. The directory
containing the ROM must be writable.

## Use a save slot

1. Press `F5` to save slot 0.
2. Continue playing.
3. Press `F7` to restore slot 0.

Use `Shift+F5` and `Shift+F7` for slot 1. On keyboards that reserve function keys
for system actions, hold Fn as well. A save replaces the previous contents of
that slot. The GUI also exposes save and load actions.

For a ROM named `game.nes`, states are stored beside it as `game.state` and
`game.state1`. The ROM extension is replaced. If an operation fails, check the
frontend error message, directory permissions, and whether the state exists.

## Keep battery-backed progress

For cartridges with battery-backed RAM, MapperBus automatically tries to load
`game.sav` when opening `game.nes`. It flushes that RAM when switching or closing
a cartridge and when the emulator is destroyed. Exit normally so the current
progress can be written.

To back up progress, close the frontend and copy the sibling `.sav` and any
`.state` or `.state1` files with the ROM. Battery saves hold the cartridge's
persistent RAM; save states hold an emulated machine snapshot. A game without
battery-backed RAM does not gain an automatic battery save.

Save-state loading validates the version, mapper, and serialized data. Keep the
corresponding ROM and build when preserving a long-lived snapshot; cross-version
or unrelated-ROM compatibility is not guaranteed.

Implementation: [EmulationSession](../../src/app/emulation_session.cpp) names
state files; [Emulator](../../src/core/emulator.cpp) handles snapshots and
battery-backed RAM.
