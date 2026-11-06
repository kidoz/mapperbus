# FDS audio research and capture probe

Research date: 2026-11-06. These findings separate published hardware observations,
MapperBus's selected model, and measurements that are still needed. No physical
RAM-adapter capture was available during this work.

## Findings

| Question | Primary evidence | Decision |
|---|---|---|
| `$4094` multiplier phases | Loopy reports a sequential multiplier with observable intermediate results, and identifies output bits 4–11. The report does not give a per-cycle trace or pipeline schedule. | Retain the explicit settled-product approximation. Do not invent a shift/add schedule or use another emulator as a hardware oracle. |
| `$4095` upper nibble | Loopy identifies an unexplained counter. TakuikaNinja observes upper values mainly 0–2 during playback and a reset sequence ending in `$1F`. Neither report gives a complete counter transition rule. | Keep the upper nibble's zero placeholder documented. Reset transients and their duration remain unresolved; one observed sequence is insufficient to generalize them. |
| `$408A = 0` | Loopy's hardware investigation corrects the envelope period to `8 * (e + 1) * (m + 1)`. The wiki reproduces that formula but also retains an older “zero disables” description. | Prefer the explicit measured formula. Its application at zero implies the fastest master divider, rather than disabling envelopes. This is an inference from the formula, not a new independent zero-speed hardware measurement. |

Sources: [Loopy's 2019 hardware investigation](https://forums.nesdev.org/viewtopic.php?p=232713),
[TakuikaNinja's 2026 reset observations](https://forums.nesdev.org/viewtopic.php?p=308384),
and the [FDS audio register reference](https://www.nesdev.org/wiki/FDS_audio).
Nintendo's [sound-synthesizer patent](https://patents.google.com/patent/US4783812A/en)
provides architectural context, but does not establish CPU-visible debug-register
phases or resolve the upper-nibble counter.

Regression tests cover master speeds 0, 1, and 255; local speeds 0 and 63; both
envelopes; normal/fourfold wave-halt rates; exact tick boundaries; and independent
volume-divider restart. They test the selected model against the published
formula. They do not certify the still-unknown multiplier/counter phases.

## Build the original probe

The probe is a one-side, read-only homebrew capture program. It contains original
6502 code and a small original font, with no BIOS, game, or Nintendo license-screen
data. Python's standard library is sufficient; no assembler is needed.

```sh
python3 tools/build_fds_audio_probe.py \
  --output tmp/fds-audio-probe/audio-probe.fds \
  --manifest tmp/fds-audio-probe/audio-probe.json
```

A test-enabled Meson build also generates `buildDir/tests/fds-audio-probe.fds`
and its JSON manifest when running `meson test -C buildDir fds-audio-probe`.
The manifest records the ROM SHA-256, page parameters, CPU addresses, and sample
timing. Pair captures with that manifest; do not compare different probe hashes.

Boot the image with an existing FDS BIOS in MapperBus or on a real FDS RAM adapter
using compatible disk-image hardware. An FPGA implementation of the audio channel
is a separate comparison target, not a physical 2C33 measurement.

The probe uses the documented
[NMI boot technique](https://www.nesdev.org/wiki/Programming_the_FDS#License_screen_bypass),
with a real non-boot padding file to keep the loader busy. It disables watchdog
IRQs while measuring, accesses memory outside PRG RAM frequently to refresh DRAM,
and stops the disk motor before capture. It does not write disk data.

## Capture pages

The initial run collects every page before displaying page `00`. Left/Right
changes pages, wrapping between `00` and `0F`. A reruns all measurements. Each
page shows both register addresses and two groups of 32 raw bytes, read left to
right across four rows of eight bytes. The screen deliberately makes no pass/fail
judgment about unknown hardware behavior.

| Page | Setup | First / second register |
|---|---|---|
| `00` | Counter +1, modulation gain 63 | `$4094` / `$4095` |
| `01` | Counter +50, gain 62 | `$4094` / `$4095` |
| `02` | Counter −1, gain 63 | `$4094` / `$4095` |
| `03` | Counter −64, gain 63 | `$4094` / `$4095` |
| `04` | Mod table filled with entry 1, forced stepping | `$4095` / `$4097` |
| `05` | Mod table filled with entry 7, forced stepping | `$4095` / `$4097` |
| `06` | Mod table filled with reset entry 4, forced stepping | `$4095` / `$4097` |
| `07` | Short sound-reset assertion, table entry 7 | `$4095` / `$4097` |
| `08` | Sound reset held about 74,000 clocks, then kept asserted | `$4095` / `$4097` |
| `09` | Resume after the same long reset | `$4095` / `$4097` |
| `0A`, `0B` | Master envelope speed 0; wave running / halted | `$4090` / `$4092` |
| `0C`, `0D` | Master speed 1; wave running / halted | `$4090` / `$4092` |
| `0E`, `0F` | Master speed 255; wave running / halted | `$4090` / `$4092` |

Each sample repeats its setup. Each envelope page starts with volume gain 32,
modulation gain 16, decreasing direction, and local speed zero. Product pages
hold modulation phase. Increment pages use `$4087.D6` to force stepping.

Sample index `i` uses `i + 2` padding clocks, covering delays 2–33. Relative to the
last cycle of the trigger write, the first absolute read occurs at clocks 6–37;
the second occurs eight clocks later, at 14–45. Trigger registers are `$4085`
(product), `$4083` (increment/envelopes), and `$4023` (reset/resume).

These are **bus-cycle positions in the emitted 6502 program**, not a claim that
the chip's internal divider has a known initial phase. Repeated writes and reset
history may affect that phase. MapperBus's instruction-stepped bus approximates
access timing, so its output is a software baseline, not the expected physical
phase trace. Retake captures after A and after cold/warm boots to expose variation.

## Validate and export a software baseline

```sh
MAPPERBUS_FDS_TEST_BIOS="$PWD/tmp/[BIOS] Nintendo Famicom Disk System (Japan).bin" \
MAPPERBUS_FDS_TEST_OUTPUT="$PWD/tmp/fds-audio-probe" \
meson test -C buildDir fds fds-audio-probe --print-errorlogs
```

The asset-free case loads the original program into a synthetic RAM adapter and
executes all measurements through the CPU/bus. The BIOS case also verifies actual
disk boot, displayed hex digits, page navigation, and A recapture. It exports a PPM
screen and `audio-probe-emulator.bin`, a 1024-byte software capture: 64 bytes per page,
first-register samples followed by second-register samples. The same data lives
at CPU `$D000–$D3FF`; `$05FF = $A5` marks completed capture. A companion JSON
labels the source as `mapperbus-emulator` and records probe/capture SHA-256 values.

Validation on 2026-11-06: all capture pages executed without licensed assets,
and the supplied BIOS booted the probe and displayed the captured values. This
establishes probe operation in MapperBus, not physical-hardware correctness.
All 29 regular and 27 GUI-disabled ASan/UBSan suite executables passed, including
the public diagnostics and probe BIOS case. Metroid and SMB2 workflows passed
separately. Leak detection was disabled for this macOS sanitizer runtime; supplied
BIOS and game hashes were unchanged.

For a physical capture, record the probe hash, adapter/chip revision, console,
BIOS hash, cold/warm boot state, repeat number, and all page values (screenshots
or a RAM dump). Preserve the raw upper nibble. A timing model can then be fitted
against repeated physical observations and tested separately from the current
software baseline. No new multiplier pipeline or counter rule is claimed yet.

[FDS compatibility checks](fds-compatibility.md) · [Reference index](README.md)
