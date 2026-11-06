# Source tree

Paths are relative to the repository root.

| Path | Responsibility |
|---|---|
| `src/core/emulator.*` | Emulator facade, subsystem wiring, stepping, states, battery RAM |
| `src/core/cpu/` | MOS 6502 instructions, interrupts, CPU state |
| `src/core/ppu/` | Picture generation, scrolling, sprites, PPU state |
| `src/core/apu/` | Sound channels, resampling, filters, output buffering, audio settings |
| `src/core/bus/` | Memory routing, DMA, device and mapper register access |
| `src/core/cartridge/` | ROM reading, iNES/NES 2.0 headers, CRC32 and region database |
| `src/core/mappers/` | Mapper interface, implementations, factory registry, serialized mapper state |
| `src/core/fds/` | FDS disk and audio subsystem |
| `src/core/input/` | Emulated controller shift-register behavior |
| `src/core/state/` | Binary state reader and writer |
| `src/core/types.hpp` | Shared numeric types, frame buffers, region timings, results |
| `src/platform/` | Video, audio, input interfaces and null backends; CPU upscalers |
| `src/app/emulation_session.*` | Session lifecycle, ticks, pause, input/audio/video coordination, save slots |
| `src/app/rom_source.*` | Shared raw/ZIP/7z loading, member selection, BIOS and persistence paths |
| `src/app/configuration.*` | Shared frontend configuration parsing and persistence |
| `src/app/session_actions.*` | Shared session action definitions and dispatch |
| `src/app/app.*` | SDL3 application's session wrapper and run loop |
| `src/app/main.cpp` | SDL3 composition root and command-line parsing |
| `src/frontends/sdl3/` | SDL3 backends, GPU upscalers, embedded and compiled shaders |
| `src/frontends/nodalkit/` | NodalKit GUI entry point, controller, widgets, audio/input adapters |
| `src/frontends/cli/` | Headless CLI entry point |
| `tests/unit/` | Catch2 tests for subsystems and application behavior |
| `tests/integration/` | Generated-ROM rendering, FDS diagnostics/game workflows, and SDL3 GPU checks |
| `tests/support/` | Synthetic ROM/capture programs, archive writers, and pinned diagnostic metadata |
| `subprojects/*.wrap` | Dependency fallback declarations |
| `subprojects/packagefiles/` | Meson packaging overlays for dependencies |
| `packaging/` | Linux launcher/icon and Arch Linux packaging |
| `.github/workflows/` | Build, test, sanitizer, lint, and Linux CI workflows |
| `docs/` | Tutorials, how-to guides, reference, and explanation |

Each source layer has its own `meson.build`. The root build selects frontends
and tests through [project options](build-options.md). Build output is kept in
directories such as `buildDir`, outside `src/`.

The shared source loader accepts an optional persistence-directory argument for
embedded callers and isolated tests. It applies to FDS BIOS fallback/disk changes
and archived cartridge battery files; state-slot storage has its own settings.
Frontends use the platform data-directory default.

For how the layers interact and why they are separate, see
[Architecture](../explanation/architecture.md).
