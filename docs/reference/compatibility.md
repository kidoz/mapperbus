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

The core includes FDS audio, disk image loading, and disk-drive/register logic.
`Emulator::load_disk()` exposes disk mounting at the core API. The current
frontend entry points load cartridges and do not expose a complete FDS BIOS
and disk-launch workflow. FDS subsystem presence should not be read as a promise
that arbitrary `.fds` games can be launched through the documented commands.

Source: [FDS subsystem](../../src/core/fds/fds.cpp),
[Emulator API](../../src/core/emulator.hpp), and the
[frontend entry points](commands-and-controls.md).

## Frontend scope

| Frontend | Presentation and input | Runtime scope |
|---|---|---|
| Headless CLI | No display, audio device, or interactive controller | Load a cartridge and execute 60 frames |
| SDL3 | Window, audio, keyboard, gamepads | Command-line options, two-player gamepad input, runtime shortcuts |
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
