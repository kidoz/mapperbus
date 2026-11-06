# FDS compatibility checks

The current local compatibility set contains two supplied disk images. These
checks exercise specific workflows; they do not establish compatibility with
other titles, later levels, or custom track formats.

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
- The regular build passed 27 suite executables. Its optional image cases were
  skipped in the asset-free run and passed in a separate explicit asset run.
- The GUI-disabled ASan/UBSan build passed 25 suite executables, with the optional
  image cases skipped. Leak detection was disabled because this macOS runtime
  does not support it.
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
