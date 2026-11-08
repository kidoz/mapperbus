# Load a Sunsoft DCS option cartridge

Use a base cartridge identified by **NES 2.0 mapper 68, submapper 1**, plus a
separate option-ROM file. The base can contain up to 128 KiB PRG ROM. Ordinary
mapper 68 cartridges do not accept an option ROM.

The option file can be raw PRG data of 16, 32, 64, or 128 KiB, or an iNES image
containing only its header and that PRG data. CHR data, trainers, trailing data,
and other sizes are rejected. Smaller chips mirror across the external bank
window. The option file itself must be an ordinary file; a base cartridge may
also be selected from ZIP/7z.

Launch the headless frontend:

```sh
./buildDir/src/frontends/cli/mapperbus-cli base.nes --dcs-option option.bin --frames 60
```

Launch SDL3 or the optional GUI:

```sh
./buildDir/src/app/mapperbus-sdl3 base.nes --dcs-option option.bin
./buildDir/src/frontends/nodalkit/mapperbus base.nes --dcs-option option.bin
```

For an archived base, SDL3 and CLI accept `--entry NAME`. The GUI presents its
existing member chooser when required and keeps the option selection through
that choice and load-error Retry. Pair selection is currently a launch argument;
there is no option-cartridge picker or hot swap. Paired GUI launches are not
added to Recent ROMs, whose entries cannot store a second file. Reopen the base
with another `--dcs-option` path to change the option cartridge.

The game controls option-ROM access. Writes to `$6000–$7FFF` with WRAM disabled
activate or retrigger the licensing timer. The option is readable in the lower
PRG window until expiry; absent or expired media returns CPU open bus. WRAM
writes, reads and bank changes do not retrigger it. The fixed bank stays internal.
Reset selects internal bank zero and clears the timer; game code must strobe it
again. CPU/M2 cycles, including DMA, advance the timer.

The model uses **107,520 M2 cycles**, the inferred period within the published
107,516–107,575 interval. This is not a new hardware measurement. See
[the mapper specification](https://www.nesdev.org/w/index.php?oldid=23720) and
[the original Sunsoft-4/6 investigation](https://forums.nesdev.org/viewtopic.php?t=9744).

Save-state version 20 records the remaining timer count. Its identity includes
both the exact base image and normalized option PRG bytes, so another option,
a modified option, or an empty slot cannot load that pair's snapshot. Renaming
files does not change identity, and raw/headered wrappers of the same option
PRG are equivalent. Older snapshots remain supported for an empty option slot;
older snapshots cannot describe an attached option. Rejected loads leave the
running machine intact.

Battery saves continue to belong to the base cartridge: raw images use their
existing sibling `.sav`, and archived bases use the base content hash. State
slots and per-game settings use the combined identity. Failed option preparation
does not replace the active cartridge.

Verification uses original synthetic ROMs through the mapper, CPU/bus, source
loader, CLI, and software GUI host. No commercial-game or physical-cartridge
validation is claimed.

[Mapper implementation coverage](../reference/mapper-implementation.md) ·
[Open archived ROMs](open-archived-roms.md)
