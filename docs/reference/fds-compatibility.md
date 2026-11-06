# FDS compatibility checks

Compatibility coverage combines public diagnostics with two supplied game images.
These checks exercise specific workflows; they do not establish compatibility
with other titles, later levels, or custom track formats.

## Public hardware diagnostics

A second opt-in suite runs public diagnostic programs with a user-supplied BIOS.
The programs are fetched separately; they are not vendored into MapperBus.

| Diagnostic | Pinned build | Result on 2026-11-06 |
|---|---|---|
| Sour FDS IRQ tests | v7, original archive image | All 21 internal checks report `O` (success) |
| TakuikaNinja FDS Mirroring Test | v1.1 | All three failure counters are zero; screen displays three passes |
| TakuikaNinja FDS 4023 Test | v1.0 | I/O toggles work; the program observes disk removal/reinsertion while disk writes are disabled |
| TakuikaNinja FDS Audio Registers | v1.2 | Play/reset/resume controls and repeated modulation-counter decrements work |

The I/O and audio programs are interactive viewers. Their results above describe
specific automated interactions, not a claim that every displayed register matches
hardware. The IRQ runner reads the ROM's result bytes at `$0405–$0419`; it does not
infer success from a nonblank screen. The mirroring runner reads the original
program's three failure counters. No diagnostic binary is patched by this runner.

Sources: [Sour's IRQ research](https://forums.nesdev.org/viewtopic.php?t=16507),
[archived IRQ image](https://github.com/christopherpow/nes-test-roms/tree/b2497f4d9427643a9044f7a91016299a698fc55f/fdsirqtests),
[mirroring test](https://github.com/TakuikaNinja/FDS-Mirroring-Test),
[I/O viewer](https://github.com/TakuikaNinja/FDS-4023-Test), and
[audio viewer](https://github.com/TakuikaNinja/FDS-Audio-Registers).
The [fixture manifest](../../tests/support/fds_diagnostics.json) records exact
URLs, versions, and SHA-256 digests.

### Run the public diagnostics

From the repository root, after building with tests enabled:

```sh
python3 tools/fetch_fds_diagnostics.py --output tmp/fds-diagnostics

MAPPERBUS_FDS_DIAGNOSTICS="$PWD/tmp/fds-diagnostics" \
MAPPERBUS_FDS_TEST_BIOS="$PWD/tmp/[BIOS] Nintendo Famicom Disk System (Japan).bin" \
MAPPERBUS_FDS_TEST_OUTPUT="$PWD/tmp/fds-diagnostics/results" \
meson test -C buildDir fds-diagnostics --print-errorlogs
```

The downloader verifies each selected FDS image and refuses to replace an existing
file with a different digest. It downloads neither a BIOS nor commercial games.
`MAPPERBUS_FDS_TEST_BIOS` may point to your BIOS under any filename.
`MAPPERBUS_FDS_TEST_OUTPUT` is optional and captures PPM frames for visual review.
The suite skips when `MAPPERBUS_FDS_DIAGNOSTICS` is unset. Once enabled, missing
files, a missing BIOS, checksum mismatches, and failed checks are errors.

The public mirroring and IRQ programs deliberately declare an absent final file
to bypass the BIOS license screen via NMI. The loader accepts complete physical
blocks without requiring the declared count to equal the available file count;
truncated block headers and payloads are still rejected.

### Remaining accuracy limits

- The instruction-stepped CPU/bus approximates the exact cycle of register access
  and interrupt sampling. The 21 IRQ results do not certify all cycle-level cases.
- The audio viewer now reads the signed modulation counter at `$4097`, but debug
  registers `$4091` and `$4093–$4096` remain unimplemented. Sequential multiplier
  phases and PWM output readback are not represented.
- Audio reset transients, rapid pause/resume behavior, volume-latch timing, and
  continued phase advancement during wavetable writes need further hardware work.
- The DRAM refresh watchdog is not modeled; analog DRAM decay and mechanical disk
  effects remain outside the current model.
- CRC coverage is synthetic: a generated CPU program verifies `$4030.D4` after
  clean and deliberately corrupted physical CRC bytes. No third-party disk-write
  or CRC diagnostic is claimed by this pass.

## Local image workflows

| Image | Workflow |
|---|---|
| Super Mario Brothers 2 (Japan) | BIOS boot, title screen, Start into world 1-1, snapshot replay during loading and gameplay, finite/nonzero mixed audio |
| Metroid (Japan) (Rev 3) | BIOS boot, name registration, side-B gameplay, controller-two save-menu input, side-A save, fresh-machine reload of disk progress, snapshot replay during side insertion |

The integration runner checks fixture SHA-256 values before running a script.
This prevents a different revision or modified image from producing misleading
results. Images and the BIOS are never downloaded or bundled. Metroid writes
use an isolated temporary disk-save file, removed after the test; source images
and existing user saves are preserved. Super Mario Bros. 2 is exercised without
persistent writes. Its later save/unlock workflows are not covered.

The runner checks visible frames, CPU liveness, deterministic snapshot replay,
audio samples, and saved disk bytes. Optional frame artifacts support visual
review. Audio checks establish finite/nonzero output, not perceptual fidelity
or a comparison with recordings from hardware.

## Validation on 2026-11-06

- Both local-image workflows passed with the supplied files; frame artifacts
  confirmed SMB2 world 1-1 and Metroid gameplay/save/reloaded-file screens.
- The regular build passed 28 suite executables. Its optional image/diagnostic
  cases were skipped in the asset-free run and passed in separate explicit
  asset runs.
- The GUI-disabled ASan/UBSan build passed 26 suite executables, including all
  four public diagnostic cases; commercial-image cases were skipped. Leak detection
  was disabled because this macOS runtime does not support it.
- The FDS static analyzer, changed-file formatting and documentation-link checks
  passed. Source BIOS and disk SHA-256 values were unchanged.

## Reproduce the checks

Build with tests enabled. Place your existing files in a directory containing:

```text
[BIOS] Nintendo Famicom Disk System (Japan).bin
Super Mario Brothers 2 (Japan).fds
Metroid (Japan) (Rev 3).fds
```

Then run from the repository root:

```sh
MAPPERBUS_FDS_TEST_ASSETS="$PWD/tmp" \
MAPPERBUS_FDS_TEST_OUTPUT="$PWD/tmp/fds-compatibility" \
meson test -C buildDir fds-compatibility --print-errorlogs
```

`MAPPERBUS_FDS_TEST_OUTPUT` is optional. It writes PPM frames into the selected
directory. The tests skip when `MAPPERBUS_FDS_TEST_ASSETS` is unset; inspect the
test log to distinguish that from an actual licensed-image run. With the asset
directory set, missing files or mismatched hashes fail. The suite has a
240-second timeout to allow for emulated disk transfers on slower builds.

For one image only, run the executable with a Catch2 filter:

```sh
MAPPERBUS_FDS_TEST_ASSETS="$PWD/tmp" \
./buildDir/tests/test-fds-compatibility 'Local SMB2*'
```

Use `'Local Metroid*'` for the other workflow. Fixture digests are recorded in
[test_fds_compatibility.cpp](../../tests/integration/test_fds_compatibility.cpp).

## Hardware regressions without external assets

The ordinary `fds` and `fds-system` suites use generated disk/BIOS fixtures.
They cover physical CRC failures, changes in gap lengths, hidden files, timer
reload programming during disk-I/O reset, zero-reload IRQs, shared IRQ
acknowledgement, side insertion, long tracks with many small files, writable
RAM, disk persistence, and snapshot restoration.

Sound-reset coverage verifies that `$4023.D1` silences and resets the volume/wave
unit while retaining access to wavetable RAM. Wave frequency and envelope tests
explicitly enable sound before starting a channel. These register contracts are
based on the [NESdev FDS register reference](https://www.nesdev.org/wiki/Famicom_Disk_System)
and [FDS audio reference](https://www.nesdev.org/wiki/FDS_audio). Analog reset
transients and undocumented modulation-table reset behavior remain outside the
model.

Run the synthetic coverage without a BIOS or game image:

```sh
meson test -C buildDir fds fds-system save-state --print-errorlogs
```

[Reference index](README.md) · [FDS setup](../how-to/play-fds-games.md)
