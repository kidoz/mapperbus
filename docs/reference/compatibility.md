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
| 5 | MMC5 | Pulse and PCM |
| 7 | AxROM | — |
| 9 | MMC2 | — |
| 11 | Color Dreams | — |
| 19 | Namco 163 | Wavetable |
| 24, 26 | VRC6 | Pulse and sawtooth |
| 69 | Sunsoft 5B | AY-style square channels |
| 85 | VRC7 | FM |

Unsupported IDs fail cartridge creation with an `Unsupported mapper` error.
The registry also rejects invalid or unsupported ROM geometries.

Source: [Mapper registry](../../src/core/mappers/mapper_registry.cpp) and
[header parser](../../src/core/cartridge/ines_header.cpp).

## Regions

NTSC, PAL, and Dendy affect emulation timing and audio. Region selection uses
the parsed cartridge header and, for legacy headers, CRC32 database and filename
fallbacks. A multi-region cartridge defaults to NTSC when loaded. SDL3 accepts
a `--region` override; the GUI stores a region preference.

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
| SDL3 | Window, audio, keyboard, gamepads | Command-line options, four independent host input profiles, runtime shortcuts |
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
rollback and version-18 imports. Commercial Hori game compatibility and physical
adapter electrical behavior have not been independently certified.
