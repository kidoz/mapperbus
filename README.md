# MapperBus

[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
[![C++23](https://img.shields.io/badge/C%2B%2B-23-00599C.svg)](https://en.cppreference.com/w/cpp/23)
[![Build System](https://img.shields.io/badge/build-Meson-blueviolet.svg)](https://mesonbuild.com/)
[![Platform](https://img.shields.io/badge/platform-macOS%20%7C%20Linux-lightgrey.svg)]()

A clean, extensible NES/Famicom emulator with FDS support, written in modern C++23.

## Features

### Emulation Core

- **CPU** -- MOS 6502 with unofficial opcodes, NMI/IRQ handling
- **PPU** -- Scanline-accurate rendering, sprites, sprite 0 hit, sprite overflow
- **APU** -- 5 channels, DMC stalling, LP/HP filters, BLEP resampling, ring buffer, DRC, HP-TPDF dithering, pseudo-stereo
- **FDS** -- Famicom Disk System with wavetable audio, envelope and modulation

### Supported Mappers

| #  | Name         | Expansion Audio |
|----|--------------|-----------------|
| 0  | NROM         |                 |
| 1  | MMC1         |                 |
| 2  | UxROM        |                 |
| 3  | CNROM        |                 |
| 4  | MMC3         |                 |
| 5  | MMC5         | Yes             |
| 7  | AxROM        |                 |
| 11 | Color Dreams |                 |
| 19 | Namco 163    | Yes             |
| 24 | VRC6a        | Yes             |
| 26 | VRC6b        | Yes             |
| 69 | Sunsoft 5B   | Yes             |
| 85 | VRC7         | Yes             |

### Video Upscaling

- **xBRZ** -- CPU-based edge-smoothing upscaler (2x--6x)
- **xBRZ GPU** -- Metal compute shader accelerated xBRZ
- **FSR 1** -- CPU-based AMD FidelityFX Super Resolution
- **FSR 1 GPU** -- Metal compute shader EASU + RCAS pipeline with zero-copy presentation

### Audio Pipeline

- BlipBuffer band-limited resampling with Blackman-Nuttall windowed sinc (64-tap, 256 phases)
- Hardware-accurate or enhanced Butterworth filter chains
- NES and Famicom filter profiles
- Resistance-modeled expansion audio mixing
- Dynamic rate control for drift-free playback

### Region Support

Auto-detection via NES 2.0 header, CRC32 database, and filename heuristics. Manual override for NTSC, PAL, and Dendy.

## Building

### Requirements

- C++23 compiler (Clang 17+ or GCC 13+)
- [Meson](https://mesonbuild.com/) build system
- [just](https://github.com/casey/just) command runner (optional)
- SDL3 (fetched automatically via Meson wraps)

### Quick Start

```bash
just setup
just build
```

Or manually:

```bash
meson setup buildDir
meson compile -C buildDir
```

### Build Options

| Option         | Default | Description          |
|----------------|---------|----------------------|
| `enable_sdl3`  | `true`  | Build SDL3 frontend  |
| `enable_cli`   | `true`  | Build CLI frontend   |
| `enable_tests` | `true`  | Build test suite     |

## Usage

```bash
# Standard
just run <rom.nes>

# With GPU xBRZ upscaling (2x-6x, default 3x)
just run-gpu <rom.nes> [scale]

# With GPU FSR 1 upscaling (2x-6x, default 4x)
just run-gpu-fsr <rom.nes> [scale]

# CPU xBRZ upscaling
just run-xbrz <rom.nes> [scale]

# Headless CLI
just run-cli <rom.nes>
```

### Full Options

```
mapperbus-sdl3 [options] <rom-file>

  --scale N             Upscale factor (2-6)
  --gpu                 GPU-accelerated xBRZ upscaling
  --gpu-fsr             GPU-accelerated FSR 1 upscaling
  --fsr                 CPU FSR 1 upscaling
  --sample-rate N       Audio sample rate (default: 96000)
  --resampling MODE     blip or cubic (default: blip)
  --filter-mode MODE    accurate, enhanced, or unfiltered (default: unfiltered)
  --filter-profile P    nes or famicom (default: nes)
  --region R            ntsc, pal, or dendy (default: auto)
  --stereo              Enable pseudo-stereo output
  --dither              Enable HP-TPDF dithering
  --expansion-mixing M  simple or resistance (default: simple)
```

## Testing

```bash
just test
```

9 test suites covering bus logic, mappers, APU, audio pipeline, region detection, and render integration.

## Project Structure

```
src/
  core/         # Emulation core (zero platform dependencies)
  platform/     # Platform abstraction interfaces
  frontends/    # SDL3 and CLI implementations
  app/          # Composition root
tests/
  unit/         # Deterministic headless tests
  integration/  # ROM-based render tests
```

## License

This project is licensed under the MIT License -- see the [LICENSE](LICENSE) file for details.

## Author

Aleksandr Pavlov <ckidoz@gmail.com>
