# Hybrid mode

Hybrid reproduces the UMC UA6527P CPU/APU and UA6538 PPU timing profile, commonly
called Dendy timing. Choose **Settings → Video → Display → Region → Hybrid** in
the GUI, or pass `--region hybrid` to SDL3 or the headless CLI.

```sh
./buildDir/src/frontends/cli/mapperbus-cli game.nes --region hybrid --frames 120
./buildDir/src/app/mapperbus-sdl3 --region hybrid game.nes
```

## Hardware profile

The nominal values below follow the
[NESdev cycle reference chart](https://www.nesdev.org/wiki/Cycle_reference_chart).

| Property | NTSC NES | PAL NES | Hybrid |
|---|---|---|---|
| CPU clock | 1.789773 MHz | 1.662607 MHz | 1.773448 MHz |
| CPU master-clock divider | 12 | 16 | 15 |
| PPU master-clock divider | 4 | 5 | 5 |
| PPU dots per CPU cycle | 3 | 3.2 | 3 |
| Scanlines per frame | 262 | 312 | 312 |
| Rendered odd-frame dot skip | Yes | No | No |
| Frame rate | About 60.099 Hz | About 50.007 Hz | About 50.007 Hz |
| Four-step APU sequence rate | About 60 Hz | About 50 Hz | About 59 Hz |

Hybrid has 106,392 PPU dots and 35,464 CPU cycles per frame. Its additional
50 scanlines extend the post-render interval: lines 240–290 precede vblank at
line 291, dot 1. Pre-render starts at line 311. This retains the NTSC-length
NMI-to-picture interval used by cycle-counted raster code. Noise and DMC periods
use NTSC tables. See the
[Dendy timing FAQ](https://forums.nesdev.org/viewtopic.php?t=20931) and
[TV-system detection reference](https://www.nesdev.org/wiki/Detect_TV_system).

Frame-driven gameplay and music tempo can therefore slow down relative to NTSC,
while timer-driven pitch stays close to NTSC. This is an inference from the
clocks and game timing method, not a guarantee about every game's music engine.

## Clocking and audio

[`RegionTiming`](../../src/core/types.hpp) owns the nominal crystal frequency,
CPU/PPU dividers, regional scanlines, odd-frame behavior and emphasis ordering.
APU sample generation and GUI pacing derive from that profile. Hybrid and PAL
share the nominal 26,601,712.5 Hz crystal and 19,997,209 ns rounded frame period.
Hybrid uses CPU divider 15, PAL uses 16. NTSC uses its own crystal and divider.

[`MemoryBus`](../../src/core/bus/memory_bus.cpp) applies the dividers to CPU,
PPU and DMA accesses. The [APU](../../src/core/apu/apu.cpp) uses NTSC sequencer,
noise and DMC periods with the Hybrid CPU frequency. The APU sequence remains
independent of the video frame. SDL3 advances through session audio backpressure;
GUI pacing uses the regional nominal period and accumulator. Display refresh,
vsync and host scheduling can affect presentation cadence.

## Color model

The renderer applies PPUMASK grayscale and all eight emphasis combinations.
The selected UA6538 profile swaps red and green emphasis relative to NTSC, as
PAL does. This follows the
[PPU register reference](https://www.nesdev.org/wiki/PPU_registers).

Emphasis uses a precomputed RGB approximation: each selected component leaves
that channel unchanged and attenuates the other two channels by 3/4. Combined
bits compound attenuation. Grayscale masks the palette index with `$30` before
emphasis. Zero emphasis preserves the existing base palette.

These RGB factors are a presentation approximation, not measured UA6538 voltages
or a composite PAL decoder. Physical clones can differ; see the
[Dendy emphasis investigation](https://forums.nesdev.org/viewtopic.php?t=24124).

## Detection, naming and persistence

NES 2.0 header byte 12 low bits equal to 3 select Hybrid. Legacy ROM loading
retains CRC32 and filename fallbacks. Multi-region ROMs default to NTSC.
An explicit CLI override applies after detection and survives reset. SDL3 and
headless CLI accept `ntsc`, `pal`, and `hybrid`, rejecting missing/invalid values.
The GUI persists `region=hybrid`; both CLI parsers and the settings loader accept
legacy `dendy` values. Internal `Region::Dendy` and `RegionPreference::Dendy`
names and their numeric values remain for compatibility. Save-state format is
unchanged; restored regions select the correct clock and emphasis profile.

The [NES 2.0 specification](https://www.nesdev.org/wiki/NES_2.0) describes intended
ROM timing, not every compatible console. Keep licensed NTSC game headers intact
and select Hybrid with an override. The loader does not infer Hybrid from a
Russian filename or recognize an explicit `(Hybrid)`/`(Dendy)` filename tag.

PAL-only OAM/DMA behavior is not selected merely because video is 50 Hz. Hybrid
retains the existing exclusion from NTSC DMC stop bugs; the shared audio period
tables alone do not establish those chip-specific bugs. Fixed frame length also
preserves the possibility of a repeatedly suppressed vblank in an eight-cycle
PPUSTATUS polling loop. See
[PPU power-up behavior](https://www.nesdev.org/wiki/PPU_power_up_state).

## Regression checks

Tests cover full-length odd/even frames, precise Hybrid NMI edges, all noise/DMC
rate indices, four-/five-step APU events, sample counts for both resamplers,
regional emphasis/grayscale pixels, active machine snapshot replay, legacy
configuration migration, and the GUI selector's runtime/persisted setting.

`cli-region` executes an original synthetic 6502 ROM through the shipped CLI.
It counts loop iterations between NMIs to distinguish all three regions,
including a forced Hybrid mode on an NTSC-header ROM, NES 2.0 auto-detection,
override precedence, the legacy alias and invalid arguments. No licensed ROM or
BIOS is needed. These checks do not establish physical clone accuracy or a
commercial game compatibility sweep.

Verification on 2026-11-08: `meson test -C buildDir --print-errorlogs --suite
mapperbus` passed 41 targets. The subsequently added GUI selection/persistence
case passed with `meson test -C buildDir gui-bios --print-errorlogs`. Nine focused
targets in `buildDir-sanitize` passed under ASan/UBSan: region, PPU, APU, audio
pipeline, save-state, configuration, CLI region, bus, and CPU accuracy.
`just format-check` and changed-line clang-tidy checks passed.
