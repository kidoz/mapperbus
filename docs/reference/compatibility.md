# Compatibility

This page describes implemented components and registered mapper numbers. A
registered mapper is not a guarantee that every game or board variant works.

## Cartridge formats and mappers

The cartridge loader parses iNES and NES 2.0 headers. The built-in registry
contains the following mapper IDs:

| Mapper ID | Implementation | Expansion audio |
|---|---|---|
| 0 | NROM | — |
| 1 | MMC1 | — |
| 2 | UxROM | — |
| 3 | CNROM | — |
| 4 | MMC3 | — |
| 118, 119 | MMC3 variants: TxSROM CIRAM routing, TQROM mixed CHR ROM/RAM | — |
| 64, 158 | Tengen RAMBO-1: CPU/M2 and filtered A12 IRQs, extended CHR banking, CIRAM variant | — |
| 5 | MMC5 | Pulse and PCM |
| 7 | AxROM | — |
| 9 | MMC2 | — |
| 11 | Color Dreams | — |
| 19 | Namco 163 | Wavetable |
| 210 | Namco 175/340, including submappers 0/1/2 and optional gated/mirrored RAM | — |
| 24, 26 | VRC6 | Pulse and sawtooth |
| 69 | Sunsoft 5B | AY-style square channels |
| 85 | VRC7 | FM |
| 10 | MMC4 | — |
| 281, 282, 295, 358, 386, 387, 388, 397 | J.Y. multicart PCB profiles: outer banks, register aliases, shared IRQs and persistence | Shared ASIC limits; unexplained PCB bits/dump instability unverified |
| 35, 90, 209, 211 | J.Y. Company ASIC: banking, CHR latches/nametables, four IRQ sources, arithmetic, WRAM and snapshots | Unknown $C007 IRQ mode and early multiplier intermediates unverified |
| 30 | UNROM 512, submappers 0–4, CHR RAM, flash saves, mirroring and LED wiring | — |
| 111 | GTROM with flash/bonus PPU RAM, or direct MMC1 with six-bit CHR-ROM banking | GTMP3 external playback unsupported |
| 157 | Datach reader: barcode input, shared and optional cartridge EEPROM, CHR RAM and M2 IRQ | — |
| 16, 153, 159 | Bandai FCG/LZ93D50: variant IRQs, Xicor EEPROM or gated WRAM, PPU-driven outer PRG banking | — |
| 18 | Jaleco SS88006 with banked PRG/CHR, protected RAM and variable-width M2 IRQ | Optional external ADPCM speech unsupported |
| 72, 92 | Jaleco JF-17/JF-19 with edge-clocked banking and bus conflicts | Optional external ADPCM speech unsupported |
| 101 | JF-10 legacy dump ordering, fixed PRG and banked CHR | — |
| 21, 22, 23, 25 | VRC2/VRC4 board variants | — |
| 73, 75 | VRC3, VRC1 | — |
| 76, 88, 95, 154, 206 | Namco 108 family | — |
| 28, 31 | Action 53, NSF-subset cartridge banking | — |
| 32, 65 | Irem G-101 (including submapper 1), H3001 with CPU timer IRQ | — |
| 77 | Irem LROG017 with mixed CHR ROM/RAM, external nametable RAM and four-screen CIRAM routing | — |
| 67, 68 | Sunsoft-3 with CPU IRQ, Sunsoft-4 with ROM nametables and DCS licensing | — |
| 33, 48 | Taito TC0190/TC0690, including mapper-48 scanline IRQ profiles | — |
| 80, 207 | Taito X1-005 with protected internal RAM and CIRAM wiring variants | — |
| 82, 552 | Taito X1-017 with protected 5 KiB RAM, CPU bus pull-downs and M2 IRQ | — |
| 13, 34, 66, 70, 71, 78, 79, 87, 89, 93, 94, 97, 113, 140, 152, 180, 184 | Discrete cartridge boards | — |
| 38, 46, 58, 61, 62, 81, 107, 145, 148, 149, 193, 200, 201, 203, 212, 213, 225, 226, 229, 231, 232, 240 | Additional latch boards and multicarts | — |

The registry contains 105 cartridge IDs. See [mapper implementation coverage](mapper-implementation.md)
for individual specifications, tested behavior, board limitations, and the full
inventory of documented IDs still awaiting implementation. The expanded set has
synthetic banking, IRQ, CPU/PPU integration and save-state coverage; this does
not establish commercial-title compatibility for every new board.

Unsupported IDs fail cartridge creation with an `Unsupported mapper` error.
Sunsoft DCS option cartridges load alongside a submapper 68:1 base using
`--dcs-option PATH`; see [DCS loading and snapshot identity](../how-to/play-dcs-games.md).
The registry also rejects invalid or unsupported ROM geometries.
Namco 175 boards requiring 2 KiB RAM need explicit NES 2.0 size metadata;
legacy battery images use an 8 KiB fallback. See the mapper ledger for limits.
MMC3 variants 118/119 inherit the current rendered-scanline IRQ approximation;
filtered A12-edge timing and every commercial title remain unverified.
RAMBO-1 uses separate A12/M2 observations and passes all six published hardware
test ROMs. The both-pattern-tables-at-`$1000` probe matches the measured single
IRQ and has an exact-count regression; see the ledger for coverage and limits.
Bandai serial saves contain 128 or 256 physical EEPROM bytes; mapper 153 saves
8 KiB WRAM. EEPROM programming uses an approximate 9000-M2 interval. Mapper
16 submappers 1/3 remain supported aliases for 159/153. Datach uses mapper 157,
a separate shared reader save, and optional 128-byte cartridge save. Barcode
injection is available through the core API and headless CLI; graphical barcode
controls and physical-reader validation remain open. See the mapper ledger.

Self-flashable boards persist a complete 512 KiB chip image in a separate `.sav`;
GTROM also saves when the battery flag is clear. Source ROM files and their
content identities stay unchanged. Programming/erase timing is approximate, and
cartridge LEDs are exposed in the core without a graphical indicator.

Source: [Mapper registry](../../src/core/mappers/mapper_registry.cpp) and
[header parser](../../src/core/cartridge/ines_header.cpp).

## Regions

NTSC, PAL, and Hybrid affect emulation timing and audio. Region selection uses
the parsed cartridge header and, for legacy headers, CRC32 database and filename
fallbacks. A multi-region cartridge defaults to NTSC when loaded. SDL3 accepts
a `--region` override; the GUI stores a region preference.

Hybrid combines PAL-length frames with three PPU dots per CPU cycle, late vblank,
and NTSC APU period tables. See [Hybrid mode](dendy-research.md) for the
hardware comparison, implementation, and color-model limitations.

Source: [Cartridge loading](../../src/core/cartridge/cartridge.cpp) and
[Emulator](../../src/core/emulator.cpp).

## Famicom Disk System

FDS launch is available in the GUI, SDL3, and CLI with an external 8 KiB BIOS.
The RAM adapter maps 32 KiB PRG RAM, 8 KiB CHR RAM, and the BIOS. Standard `.fds`
images support side controls, timer/transfer IRQs, mirroring, CRCs, disk writes,
separate persistent saves, and wavetable audio. ZIP/7z members use the same path.
See [Play Famicom Disk System games](../how-to/play-fds-games.md) for setup,
formats, storage, and compatibility limits.

Source: [FDS subsystem](../../src/core/fds/fds.cpp),
[Emulator API](../../src/core/emulator.hpp), and the
[frontend entry points](commands-and-controls.md).

## Frontend scope

| Frontend | Presentation and input | Runtime scope |
|---|---|---|
| Headless CLI | No display, audio device, or interactive controller | Load NES/FDS media and execute 60 frames, or `--frames N` |
| SDL3 | Window, audio, keyboard, gamepads | Command-line options, six independent host input profiles, runtime shortcuts |
| NodalKit GUI | Toolkit window and controls; SDL3 audio/gamepads when found | ROM opening, settings, session actions, recent ROMs |

macOS and Linux have project build workflows. SDL3 supports Wayland and X11 on
Linux; the optional NodalKit GUI currently uses Wayland there. GPU upscalers use
Metal on macOS and Vulkan on Linux when the corresponding path is available.
CPU upscalers do not need compute shaders. Windows-related code and configuration
paths do not establish the same project validation coverage.

## Persistence

Save states and battery-backed cartridge RAM are implemented. State files are
versioned and validated on load; rejected states leave the current machine
unchanged. See [Save and restore](../how-to/save-and-restore.md) for filenames and
operations, and [Configuration](configuration.md) for frontend preferences.

## External Famicom controllers

Two standard expansion pads are supported on D1, alongside the built-in D0
controllers. Enable them in Settings → General → Hardware and bind **External
1/2** under Input, or use the SDL3 options in
[Commands and controls](commands-and-controls.md). Games must read D1. Hori and Four Score
serial adapters are available as separate modes below. Serial timing, DMA behavior,
and version-17 snapshot replay have synthetic regression coverage, including
four virtual SDL gamepads read by an original diagnostic ROM.

## Four Score

Four Score four-player serial mode is supported using the existing four host
profiles. Select it in General → Hardware → Controller mode or with
`--controller-mode four-score`. It provides 24-bit reports with the two adapter
signatures and post-report ones. Synthetic coverage includes all regional clock
paths, NES/AV and RF timing, both wiring choices, strobe behavior, CPU/DMC/OAM
reads, snapshot replay/rollback and v17 import. An original 6502 diagnostic reads
all four virtual SDL gamepads and both signatures into CPU RAM. The pinned public
[allpads diagnostic](dma-timing.md#optional-public-controller-diagnostic) also
identifies Four Score using its original detection routine in both timing modes. This coverage
does not certify compatibility with every commercial Four Score game.

## Hori 4 Players Adapter

Hori four-controller serial mode is supported through `hori-4-player`, with two
independent built-in pads and four expansion pads. Input exposes six profiles.
The adapter reports on D1 with identification bits at read 18/19, retaining D0
built-ins and RF microphone behavior. The original six-gamepad ROM test reads
all reports and identification bits through the emulated CPU. Bus tests cover
strobe, both read timings, wiring masks, microphone/open bus, NTSC DMC/OAM and
post-report ones; state tests cover independent partial reports, held reads,
rollback and version-18 imports. The Wit's regression below exercises a commercial
game's Hori detection and all four players. This does not establish compatibility
with every Hori title or certify physical-adapter electrical behavior.

## Bomberman II (Japan)

The original Japanese game supports **three players** in Battle Mode. Use
**Standard** controller mode with **External controller 1** attached. Bind
**Player 1**, **Player 2**, and **External 1**; the third player uses `$4016 D1`.
For an original Famicom setup, select RF Famicom wiring and timing. The equivalent
SDL3 options are:

```sh
./buildDir/src/app/mapperbus-sdl3 'tmp/Bomberman II (Japan).zip' \
  --controller-mode standard --controller-wiring rf-famicom \
  --controller-timing rf-famicom --expansion-controllers 1
```

The opt-in regression loads the ZIP through libarchive, selects a three-player
match with host inputs, and verifies independent movement and snapshot replay.
Both controller timing options are covered. Hori mode also carries the third
player on its first pad; this game does not validate the adapter's longer report
or identification signature. Extra profiles do not add a fourth player.

To run with your own matching Japanese ROM archive:

```sh
MAPPERBUS_CONTROLLER_TEST_ASSETS="$PWD/tmp" \
  meson test -C buildDir controller-compatibility --print-errorlogs
```

The archive must contain `Bomberman II (Japan).nes` with SHA-256
`9bc9051b174ec4743df78a3086df6577c55258d705c9c9c451be63152c9badf1`.
No licensed ROM data is included in the repository; ordinary CI skips this test
without the asset environment variable.

## Wit's (Japan)

Select **Hori 4 Players** and the game's **MULTI VS MODE**. Give **Hori 1–4**
separate keyboard bindings or gamepad assignments. For four host gamepads, assign
devices with **Input → Configure Four Gamepads**. This assigns indices **0–3**
to those roles and enables them while keeping bindings. The fresh defaults
before applying the preset are **2, 3, 4, 5**.
Alternatively, use **Input → Assign by Button…** and press a button on each
controller in Hori 1–4 order. This also works with identical controller models.
Distinguishable controllers are remembered across reordered reconnections and
restarts. If identical controllers require reassignment, choose **Preferred
device → Use Selected Gamepad**; see [device identities](configuration.md).
Wit's also accepts the built-in pads as aliases of its first two players.

```sh
./buildDir/src/app/mapperbus-sdl3 "tmp/Wit's (Japan).zip" \
  --controller-mode hori-4-player --controller-wiring rf-famicom \
  --controller-timing rf-famicom \
  --gamepad3 0 --gamepad4 1 --gamepad5 2 --gamepad6 3
```

The opt-in controller regression loads the ZIP through libarchive and verifies
the unmodified game's adapter detection: Standard selects no serial adapter,
Four Score selects its D0 path, and Hori selects its D1 path with both correct
signatures. In a four-player round, each Hori pad changes only its own decoded
input and rider's board quadrant. Built-in aliases and simultaneous distinct
four-pad input survive snapshot replay. Both controller timing options are
covered. This is automated core/game validation, rather than a physical
four-controller hardware certification.

Use the same `MAPPERBUS_CONTROLLER_TEST_ASSETS` command above. The archive must
contain `Wit's (Japan).nes` with SHA-256
`34352cb9090fd00d1d677308dd3adbdfd4e76622de21bb48dffbffe19df9f195`.
To run only this game, add `--test-args='[wits]'` to the Meson command. Licensed
game bytes remain outside the repository.
