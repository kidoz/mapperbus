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
| TakuikaNinja FDS BIOS Dumper | v1.0 | Fast and slow dumps match the supplied 8 KiB BIOS; write protection, either corrupted CRC byte, automatic retry, and mid-write snapshot replay pass |

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
[watchdog diagnostic](https://github.com/TakuikaNinja/FDS-4030D1-Addr), and
[BIOS dumper](https://github.com/TakuikaNinja/FDS-BIOS-Dumper).
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
Its 300-second timeout includes the slow disk-write and sanitizer workflows.

The public mirroring and IRQ programs deliberately declare an absent final file
to bypass the BIOS license screen via NMI. The loader accepts complete physical
blocks without requiring the declared count to equal the available file count;
truncated block headers and payloads are still rejected.

### Disk writes and physical block CRCs

The unmodified BIOS Dumper v1.0 is pinned to source commit
`7196728970664b43016aaaf293a7006b4e84c968` and its release-image SHA-256. Its
author reports testing with a Sharp Twin Famicom and FDSKey; this supplies an
independent working disk-write program, rather than a dedicated cycle-level CRC
diagnostic. See the [upstream usage and hardware acknowledgement](https://github.com/TakuikaNinja/FDS-BIOS-Dumper/blob/7196728970664b43016aaaf293a7006b4e84c968/README.md).

The runner verifies the program's BIOS CRC32 calculation, then exercises both
dump modes: one 8 KiB file and eight 1 KiB files. It checks the program's success
state and `OK!` text, the directory count/names/addresses, preservation of its
four boot files, and byte-for-byte equality between the dumped data and the
supplied BIOS. Both modes replay a snapshot taken in write-data mode to identical
machine state, flush to an isolated disk save, and reopen with unchanged source
identity. Temporary disk saves are removed after each case; artifacts contain
screenshots, not dumped BIOS data.

Every physical block's CRC is checked before remounting the disk, using a
separate normal-polynomial bit-serial calculation. A BIOS-free case checks fixed
CRC vectors generated from [FDSKey's delayed-input CRC algorithm](https://github.com/ClusterM/fdskey/blob/f07ab82864a9f4a2e7b14b37f1837715edb0d0fe/FdsKey/Core/Src/fdsemu.c#L45-L71),
pinned at `f07ab82864a9f4a2e7b14b37f1837715edb0d0fe`. This avoids treating CRCs
regenerated from an exported `.fds` image as evidence of correct physical writes.

Additional cases write-protect the mounted disk or alter either byte of its
first physical block CRC in a runtime snapshot. The original program and BIOS
report `Err. 03` and `Err. 27`, respectively, without changing the disk data.
Clearing protection and ejecting/reinserting the disk lets the program retry and
complete without patching its code. In the CRC cases, reinsertion reconstructs
a clean track from the unchanged `.fds` data, removing the injected fault.
The expected errors follow the
[BIOS disk-I/O contract](https://www.nesdev.org/wiki/FDS_BIOS#Error_list).

These fault cases are emulator injections. No physical-drive fault capture was
made here, and drive-level copy-protection circuits, torn magnetic writes, and
cycle-exact CRC-control edge timing remain outside this validation. The `.fds`
format omits physical CRC bytes; runtime snapshots retain the live track, but
exported disks and persistent normalized disk saves reconstruct CRCs on mount.

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

CPU execution performs real bus reads for implied idle cycles, index correction,
stack/interrupt handling, and unofficial NOPs. These reads acknowledge `$4030`
and consume `$4031` just as explicit reads do. Read-modify-write instructions
perform both the old-value and new-value writes. MMC1 filters consecutive serial
data writes while still accepting a reset write.

The bus advances the FDS watchdog, timer, and disk transfer once before each
CPU/DMA access. This allows bytes arriving during DMA to be read or acknowledged
on that DMA cycle. OAM DMA starts at the next read after the last `$4014` write,
uses the last page written by a read-modify-write instruction, and performs real
halt/alignment and source reads between PPU writes. DMC loads and reloads schedule
on APU get/put phases and halt the next eligible CPU read. Halt/dummy/alignment
and sample fetch take three or four cycles; setup overlaps OAM and DMC gets take
priority. NTSC/Dendy repeat halted reads; PAL performs the initial halt read and
suppresses repeats pending separate validation.
Untimed host debugger/test accesses do not advance these clocks. The emulator
does not step the FDS drive, APU, or expansion audio a second time after an
instruction or DMA. All three advance with each bus cycle.

These contracts follow the [DMA reference](https://www.nesdev.org/wiki/DMA),
[hardware DMA tests](https://forums.nesdev.org/viewtopic.php?t=14120), and
[MMC1 register reference](https://www.nesdev.org/wiki/MMC1).
The [DMA timing reference](dma-timing.md) records the optional public Blargg
diagnostics and the pending-request snapshot format.

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

- CPU polling now follows per-cycle detector outputs, including I-flag latency,
  branches and IRQ/BRK hijacking, checked by the five public
  [interrupt diagnostics](interrupt-timing.md). The 21 FDS IRQ checks do not
  certify every CPU/PPU alignment or register race.
- `$4094` exposes the settled product rather than sequential multiplier partial
  results. `$4095`'s undocumented upper counter and the absolute alignment of
  divider/PWM phases to hardware power-on are not verified.
- Envelope timing follows the hardware-research formula
  `8 * (speed + 1) * (master + 1)`, including zero. The older “zero disables”
  description conflicts with that formula; see the
  [research and capture probe](fds-audio-research.md) for the evidence and limits.
- Audio reset transients, analog filtering/DAC nonlinearity, and rapid
  pause/resume behavior need further hardware work.
- NTSC DMC stop/abort and duplicate-fetch timing pass the two targeted
  AccuracyCoin diagnostics; see [DMA timing](dma-timing.md). PAL/clone stop
  quirks and PAL register/repeated-read behavior remain unverified. The selected
  All four combinations of NES/AV or RF timing and NES or RF data wiring pass six
  additional targeted checks each. RF built-in controller 2 omits Start/Select.
  Synthetic microphone activity has normal/DMA read and snapshot coverage;
  microphone analog circuitry, expansion accessories, and electrical
  DMC sample capture remain outside those timing checks. Reset-cycle bus activity,
  analog DRAM decay, 2C33 test mode, and mechanical disk effects are not modeled.
- CRC coverage includes the public BIOS-dumper write/readback flows, independent
  fixed vectors, and injected CRC faults described above. Physical-drive fault
  timing, copy-protection circuitry, and torn magnetic writes are not certified.

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
- All ten public-diagnostic/CRC cases passed, including fast/slow disk writes,
  write protection and retry, either CRC-byte fault, and mid-write replay.
- All ten public NTSC [PPU timing diagnostics](ppu-timing.md) passed, including
  vblank flag reads, NMI enable/disable races and odd-frame clock timing.
- With optional microphone playback, the regular build passed all 35 suite executables
  with the public diagnostics and eight licensed workflows enabled. The generated
  24 game frames match the preceding capture-only build byte-for-byte.
- The GUI-disabled ASan/UBSan build passed all 32 suite executables with public
  diagnostics, including mixing/session and SDL dummy-device queue clearing. The
  licensed workflows were not repeated under sanitizers for this host-only
  change; all eight passed on the preceding unchanged core. Leak detection was
  disabled because this macOS runtime does not support it.
- Both public Blargg OAM/DMC timing diagnostics and eight selected AccuracyCoin
  stop/register/controller checks (the six register/controller checks across all four
  timing/wiring combinations) passed in regular and sanitizer builds;
  [DMA timing](dma-timing.md) records the source pins and accuracy limits.
- The FDS static analyzer, changed-file formatting and documentation-link checks
  passed. The headless CLI ran Metroid for 1200 frames with the new audio model.
  Source BIOS and disk SHA-256 values were unchanged.

### GUI workflow validation

The permission update passed all 35 regular and 32 GUI-disabled ASan/UBSan
suite targets in an asset-free run; optional external diagnostic/game cases
were not repeated for this host-only change. Leak detection remains disabled
on this macOS runtime. Injected SDL dummy tests cover request deduplication,
pending/grant/deny/restricted states, revocation and Retry, continued frames and
bound input. Core-only configuration selects only the C++ compiler.

Native macOS checks used three temporary review identities. The user confirmed
a first-use Allow decision; capture remained closed until Retry, then reported
recording activity. The user also confirmed a fresh Don't Allow decision; Retry
showed denied-access instructions and capture stayed closed. Revocation and
re-enabling in System Settings required quitting/reopening as macOS instructed;
the denied message and subsequent recording recovery were verified. Status
instructions wrap and are exposed through accessibility text. Restricted policy
was injected, not tested on a managed Mac.

Computer Use blocks the system notification app, so the user handled the native
prompt decisions. Review apps were closed, their three microphone permission
records reset, and the original configuration restored byte-for-byte. No raw
microphone PCM was saved; the normal MapperBus permission was left enabled.

Optional microphone playback was reviewed in the rebuilt packaged GUI with
Metroid, RF wiring/timing, the MacBook microphone and FIIO output. It defaulted
off at volume 25; volume 0/100 and the switch worked. With microphone volume 100
and master volume 40, the user confirmed hearing their voice through headphones.
Pause changed capture to waiting, resume restarted it, and disabling microphone
audio left capture recording. No raw PCM was saved; the app was closed and the
original configuration restored to the hash below. These checks do not certify
Famicom analog audio fidelity; permission checks are recorded below.

Deterministic tests cover mono/stereo addition, clipping, nonfinite samples,
resampling, queue bounds/underruns, mute independent of activity, device failure,
pause and successful state loads. Mixing on/off produces byte-identical machine
snapshots while changing only the submitted playback samples. SDL dummy output
also verifies clearing a paused stream's queued samples.

Real-device live capture was checked in the packaged macOS GUI at commit
`30810dd`, with Metroid and RF Famicom wiring/timing. Both MacBook Pro Microphone
and Steinberg UR22mkII reported detected activity. Sensitivity 1 produced quiet
status, while 100 detected activity. System-default selection, retry, disabling
and re-enabling capture worked. Pause closed capture; resume restarted recording.
A paused snapshot load kept capture closed, and a subsequent save was
byte-for-byte identical to the original snapshot (SHA-256
`b925a119737f8d0cd7923d1590413ed95230ebebf86afb39931ecbd317881d9c`).

The earlier capture check used existing macOS authorization and did not change
permission settings. Unplugging the Steinberg interface reported disconnection; Retry
while absent reported the selected device unavailable without falling back.
Reconnection retained the error until Retry, which resumed recording. An auxiliary
standalone probe returned only zero-valued samples and was not counted as proof
of real capture; the packaged GUI supplied the activity observations above.
No microphone PCM was saved. Both validation apps were closed, and the original
configuration was restored byte-for-byte (SHA-256
`926d77e87c8cba149227102f9b566c3050bd43efcbcb6f2d20330406884c6b06`).
Validation snapshots use an isolated folder.

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

`MAPPERBUS_FDS_TEST_OUTPUT` is optional. Game captures go into `nes-av-famicom/`
and `rf-famicom/` subdirectories. The tests skip when `MAPPERBUS_FDS_TEST_ASSETS` is unset; inspect the
test log to distinguish that from an actual licensed-image run. With the asset
directory set, missing files or mismatched hashes fail. The suite has a
720-second base timeout for all eight workflows. Use `--timeout-multiplier=2`
for ASan/UBSan runs.

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
Bus-side-effect regressions check indexed correction reads, NOPs, RTS and accepted
IRQ prefetches against `$4030`/`$4031`, bytes arriving during OAM/DMC stalls,
write-only APU open bus, both RMW writes, and deterministic snapshot replay across
DMA. MMC1 tests run CPU RMW instructions against its serial and reset ports.
Wave frequency and envelope tests explicitly enable sound before starting a
channel. These register contracts are
based on the [NESdev FDS register reference](https://www.nesdev.org/wiki/Famicom_Disk_System)
and [FDS audio reference](https://www.nesdev.org/wiki/FDS_audio), with accumulator
and pitch details from [Loopy's hardware measurements](https://forums.nesdev.org/viewtopic.php?p=232713).
Analog reset transients and undocumented modulation-table reset behavior remain outside the
model.

Run the synthetic coverage without a BIOS or game image:

```sh
meson test -C buildDir bus fds fds-system mappers save-state --print-errorlogs
```

[Reference index](README.md) · [FDS setup](../how-to/play-fds-games.md)
