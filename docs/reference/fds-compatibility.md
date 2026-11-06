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
| TakuikaNinja FDS Audio Registers | v1.2 | Play/reset/resume, CPU-driven debug-register reads, accumulator activity, and snapshot replay work |
| TakuikaNinja FDS 4030.D1 Address | v1.0 | Captures `$67FF` / `$6E25`, matching the published Twin Famicom capture |

The I/O and audio programs are interactive viewers. Their results above describe
specific automated interactions, not a claim that every displayed register matches
hardware. The watchdog runner checks the program's two captured return addresses;
it does not interpret raster stripes as a pass/fail result. The IRQ runner reads the ROM's result bytes at `$0405–$0419`; it does not
infer success from a nonblank screen. The mirroring runner reads the original
program's three failure counters. No diagnostic binary is patched by this runner.

Sources: [Sour's IRQ research](https://forums.nesdev.org/viewtopic.php?t=16507),
[archived IRQ image](https://github.com/christopherpow/nes-test-roms/tree/b2497f4d9427643a9044f7a91016299a698fc55f/fdsirqtests),
[mirroring test](https://github.com/TakuikaNinja/FDS-Mirroring-Test),
[I/O viewer](https://github.com/TakuikaNinja/FDS-4023-Test),
[audio viewer](https://github.com/TakuikaNinja/FDS-Audio-Registers), and
[watchdog diagnostic](https://github.com/TakuikaNinja/FDS-4030D1-Addr).
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

### Audio timing and register coverage

The wave and modulation accumulators advance every 16 CPU clocks. Wave pitch
retains the full 20-bit modulation product, including the low bits needed for
very slow detuned notes. Opening wavetable RAM for writes holds the output
sample while phase continues to advance. Volume changes latch at wave position
zero; a direct zero-gain write mutes immediately. `$4083.D6` resets and holds both
envelope timers; wave halt accelerates their rate fourfold and stops modulation
phase. `$4087.D7` clears the fractional and ghost-address bits while retaining the
32-entry table address. Its D6 test control forces a step at each unit tick.

| Register | Modeled readback |
|---|---|
| `$4090`, `$4092` | Current envelope gains |
| `$4091` | Wave accumulator bits 12–19 |
| `$4093` | Modulation accumulator bits 5–11 |
| `$4094` | Settled counter × gain product bits 4–11; intermediate multiplier phases are approximated |
| `$4095` | Decoded next modulation increment in the low nibble; unknown upper nibble is zero |
| `$4096` | Held wave sample gated by 32-CPU-clock volume PWM |
| `$4097` | Signed seven-bit modulation counter |

The audio mixer averages the ultrasonic PWM duty cycle; register reads expose
its instantaneous gate. Snapshot version 6 preserves the tick/PWM phase, held
sample, latched gain, and modulation test control. Versions 4 and 5 remain
readable with converted accumulator fractions and reconstructed output latches;
the older files do not contain the exact new latch or divider state.

The unmodified public audio viewer copies `$4090–$4097` into nametable tiles.
The integration runner checks those CPU-driven values while muted, after two
counter decrements, and during playback, then verifies deterministic replay.
These checks validate the implemented register contracts, not agreement with
a cycle-by-cycle recording from physical hardware.

### DRAM refresh watchdog

The watchdog uses a 12-bit down-counter and a seven-bit refresh-row counter.
`$4023.D0 = 0` resets them to 4095 and zero. CPU cycles outside PRG RAM
`$6000–$DFFF` advance the row counter; its 128th refresh reloads the timer to
3245 instead of decrementing it. Timer underflow wraps to 4095 and latches an IRQ
when `$4023.D7` enables it. Both counters continue while the IRQ is pending.
Reading `$4030` acknowledges the watchdog (`D1`) and timer (`D0`), while retaining
the transfer IRQ (`D7`). Clearing `$4023.D7` acknowledges the watchdog without
resetting either counter.

CPU execution reports bus addresses for explicit accesses, implied idle cycles,
index correction, stack/interrupt handling, unofficial NOPs, and read-modify-write
cycles. The observation of dummy cycles does not add memory-mapped I/O side
effects. OAM DMA includes source reads, PPU writes, and halt/alignment clocks;
DMC includes the existing three-stall-plus-one-sample approximation. Host debugger
reads do not advance these counters. Drive/audio stepping does not clock the
watchdog a second time.

Snapshot version 7 appends the timer, row, IRQ enable, and pending latch. Versions
4–6 remain readable; their missing watchdog state starts reset and disabled until
the next `$4023` write, preserving their previous interrupt behavior.

Sources: [hardware investigation](https://forums.nesdev.org/viewtopic.php?t=26359),
[register reference](https://www.nesdev.org/wiki/Famicom_Disk_System#DRAM_Refresh_Watchdog),
and the hardware investigator's
[November 2026 acknowledgement/timing clarification](https://github.com/nesdev-org/MesenCE/pull/302).
The primary diagnostic's
[published capture](https://github.com/TakuikaNinja/FDS-4030D1-Addr/blob/64161a5e09930f0f3fece6e5b43d16dcf4cdeb7d/img/FDS-4030D1-Addr_TwinFC.png)
shows `$67FF` / `$6E25`; both captured values match in MapperBus.

### Remaining accuracy limits

- The instruction-stepped CPU/bus approximates the exact cycle of register access
  and interrupt sampling. The 21 IRQ results do not certify all cycle-level cases.
- `$4094` exposes the settled product rather than sequential multiplier partial
  results. `$4095`'s undocumented upper counter and the absolute alignment of
  divider/PWM phases to hardware power-on are not verified.
- Envelope timing follows the hardware-research formula
  `8 * (speed + 1) * (master + 1)`, including zero. The older “zero disables”
  description conflicts with that formula; see the
  [research and capture probe](fds-audio-research.md) for the evidence and limits.
- Audio reset transients, analog filtering/DAC nonlinearity, and rapid
  pause/resume behavior need further hardware work.
- Watchdog address accounting does not turn the CPU into a fully cycle-stepped
  bus. Interrupt sampling, dummy I/O side effects, OAM DMA reads of I/O space,
  and exact DMC halt placement retain instruction-level approximations. Analog
  DRAM decay, 2C33 test mode, and mechanical disk effects are not modeled.
- CRC coverage is synthetic: a generated CPU program verifies `$4030.D4` after
  clean and deliberately corrupted physical CRC bytes. No third-party disk-write
  or CRC diagnostic is claimed by this pass.

## Local image workflows

| Image | Workflow |
|---|---|
| Super Mario Brothers 2 (Japan) | BIOS boot, title screen, Start into world 1-1, snapshot replay during loading and gameplay, finite/nonzero mixed audio |
| Metroid (Japan) (Rev 3) | BIOS boot, name registration, side-B gameplay, controller-two save-menu input, side-A save, fresh-machine reload of disk progress, snapshot replay during side insertion |

Each game runs in three forms through `app::load_rom_source`: a raw disk,
a ZIP member with a 16-byte FDS header, and a renamed headerless 7z member.
The runner constructs the archives temporarily using libarchive. It checks
fixture SHA-256 values before playing; no BIOS, game, or generated archive is
bundled or downloaded.

Metroid's registration and in-game save use an explicitly isolated persistence
root. Fresh emulators reopen the changed disk through all three sources, then
boot from a different source to display the registered file. Disk progress uses
one normalized-source-hash file and does not alter media identity. Source disk,
archive, and BIOS-copy hashes are checked after each workflow. Super Mario Bros.
2 runs without persistent disk writes; its later save/unlock workflows are not
covered.

Snapshot checks write an actual state file during BIOS loading, gameplay, and
Metroid side insertion. Raw, ZIP, and 7z opens resolve the same content-based
state directory and replay to identical machine state and frame pixels. The
persistence root is passed directly to the shared loader; these tests do not
change process-wide home/application-data variables or use existing user saves.

The ordinary `rom-source` suite also covers mixed NES/FDS ZIP and 7z collections:
exact case-sensitive member selection, ambiguity, invalid disk data, missing or
short BIOS files, unavailable storage, corrupt ZIP checksums, truncated 7z data,
and preservation of the active FDS state on failure. Archive member names remain
identifiers and are not extraction paths.

The runner checks visible frames, CPU liveness, deterministic snapshot replay,
audio samples, and saved disk bytes. Optional frame artifacts support visual
review. Audio checks establish finite/nonzero output, not perceptual fidelity
or a comparison with recordings from hardware.

## Validation on 2026-11-06

- All six raw/ZIP/7z game workflows passed with the supplied files; frame
  artifacts confirmed SMB2 world 1-1 and Metroid gameplay/save/reloaded-file screens.
- The regular build passed 30 suite executables, including GUI close recovery.
  Its optional image/diagnostic cases were skipped in the asset-free run and
  passed in separate explicit asset runs.
- The GUI-disabled ASan/UBSan build passed 27 suite executables, including all
  five public diagnostic cases and the original audio probe. All six licensed
  raw/ZIP/7z workflows also passed in separate sanitizer runs. Leak detection was
  disabled because this macOS runtime does not support it.
- The FDS static analyzer, changed-file formatting and documentation-link checks
  passed. The headless CLI ran Metroid for 1200 frames with the new audio model.
  Source BIOS and disk SHA-256 values were unchanged.

### GUI workflow validation

The macOS NodalKit GUI was exercised with the supplied Metroid image and BIOS
using a temporary profile; the original profile was restored with matching file
hashes afterward. Raw, headered ZIP, and headerless 7z opens used the BIOS saved
through Settings. The checks covered:

- Missing BIOS and an 8191-byte BIOS selection produce actionable errors.
- Disk Controls can eject, insert side B, and toggle write protection. Loading
  a snapshot restores side A and its original protection state.
- A registered file is written to the separate disk save and appears after a
  fresh 7z boot. Raw gameplay snapshots also restore through ZIP and 7z.
- Corrupt archives and malformed members in the archive chooser leave the
  paused machine snapshot byte-for-byte unchanged.
- Blocking the disk-save directory preserves the session on Close ROM and
  Quit, reports the error, and still permits a recovery disk export. Repairing
  storage allows saving and closing normally.
- Closing a paused game and opening an archive resumes audio and gameplay;
  keyboard movement was checked after reopening in the rebuilt GUI.

The display-free [gui-close regression](../../tests/integration/test_gui_close.cpp)
sends NodalKit's native Close event
with synthetic unsaved FDS progress. It verifies that failed persistence vetoes
the close, retains the machine state and recovery export, and permits closing
after storage is repaired. The old close-notification approach fails this test
because it hides the window despite the save error. `emulation-session` also
covers device resume after closing a paused game. Both regressions were checked
against the previous behavior before applying the fixes.
The close regression also passed with ASan/UBSan on the test and core; its
NodalKit library used the regular build.

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
720-second timeout for all six workflows, including sanitizer builds.

For one image only, run the executable with a Catch2 filter:

```sh
MAPPERBUS_FDS_TEST_ASSETS="$PWD/tmp" \
./buildDir/tests/test-fds-compatibility '*SMB2*'
```

Use `'*Metroid*'` for the other game. Each filter runs raw, ZIP, and 7z.
Use `'[raw]'`, `'[zip]'`, or `'[7z]'` to run both games in just that format.
Fixture digests are recorded in
[test_fds_compatibility.cpp](../../tests/integration/test_fds_compatibility.cpp).

## Hardware regressions without external assets

The ordinary `fds` and `fds-system` suites use generated disk/BIOS fixtures.
They cover physical CRC failures, changes in gap lengths, hidden files, timer
reload programming during disk-I/O reset, zero-reload IRQs, shared IRQ
acknowledgement, side insertion, long tracks with many small files, writable
RAM, disk persistence, and snapshot restoration.

Sound-reset coverage verifies that `$4023.D1` silences and resets the volume/wave
unit while retaining access to wavetable RAM. Audio regressions cover held samples,
continued phase during writes, wave-zero gain latching, immediate mute, fractional
pitch, debug reads, PWM duty cycle, envelope reset/rate, forced modulation steps,
legacy-state conversion, invalid audio state, and replay during a table write.
Watchdog coverage checks every opcode's elapsed clock count, dummy/stack/indexed
addresses, OAM/DMC cycles, refresh and starvation loops, shared IRQ acknowledgement,
mask/reset behavior, and malformed-state rollback.
Wave frequency and envelope tests explicitly enable sound before starting a
channel. These register contracts are
based on the [NESdev FDS register reference](https://www.nesdev.org/wiki/Famicom_Disk_System)
and [FDS audio reference](https://www.nesdev.org/wiki/FDS_audio), with accumulator
and pitch details from [Loopy's hardware measurements](https://forums.nesdev.org/viewtopic.php?p=232713).
Analog reset transients and undocumented modulation-table reset behavior remain outside the
model.

Run the synthetic coverage without a BIOS or game image:

```sh
meson test -C buildDir fds fds-system save-state --print-errorlogs
```

[Reference index](README.md) · [FDS setup](../how-to/play-fds-games.md)
