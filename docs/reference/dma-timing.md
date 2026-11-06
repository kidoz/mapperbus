# DMA timing

The PPU, APU and expansion audio advance with CPU/DMA bus cycles. PPU access
timing uses the regional master-clock dividers: NTSC/Dendy retain two dots before
an access and one afterward; PAL advances 16 dots every five CPU cycles. See
[PPU clock scheduling](ppu-timing.md#cpuppu-clock-scheduling).
[Interrupt timing](interrupt-timing.md) describes IRQ/NMI polling and edge
preservation during DMA.

The emulator chooses a deterministic power-on phase: odd APU cycle indices are
gets, even indices are puts. Hardware can power on in either alignment.

## Scheduling and arbitration

- A `$4015` start with an empty sample buffer schedules a load halt on the get
  in the second following APU cycle, three/four CPU cycles after the write.
- The free-running DMC output unit schedules a reload on the put after it
  empties the sample buffer. Buffered output finishes after playback is stopped;
  the stop races below can still produce a halt or sample fetch.
- The CPU services a ready request before its next read, including dummy reads
  inside an instruction. Writes finish before a DMA halt can succeed.
- DMC performs halt, dummy, optional alignment, and sample get cycles. The sample
  get fills the buffer, advances/wraps the address, and updates length/loop/IRQ state.
- OAM and DMC setup can overlap OAM accesses. A DMC get wins over an OAM get;
  OAM preserves its offset and realigns before continuing. Common overlap adds
  two cycles; a halt on the penultimate/last OAM put adds one/three cycles.
- Idle DMA cycles repeat the halted read on NTSC/Dendy. PAL performs the initial
  halt read but suppresses repeats. FDS clocks advance before every bus access,
  allowing newly arriving disk bytes to be consumed during DMA.

The contracts come from the [NESdev DMA reference](https://www.nesdev.org/wiki/DMA),
[hardware DMA investigation](https://forums.nesdev.org/viewtopic.php?t=14120), and
[DMC reader/output reference](https://www.nesdev.org/wiki/APU_DMC).

## NTSC DMC stop races

The NTSC model selects the documented late-RP2A03G/RP2A03H behavior from
[NESdev's DMA bug description](https://www.nesdev.org/wiki/DMA#Bugs) and the
[AccuracyCoin diagnostic source](https://github.com/100thCoin/AccuracyCoin/blob/66e8711292d432cf009b2831f3ea1a055d75c9ba/AccuracyCoin.asm).
It does not expose a CPU-revision selector.

- Clearing `$4015` bit 4 immediately clears the remaining-byte count. The
  reader stop latch settles on the next get, after the output-byte clock. A
  sample-ending fetch similarly settles the implicit stop on the next get.
- A reload scheduled as that latch clears produces a one-cycle aborted DMA.
  It repeats the halted read once and fetches no sample. If a CPU write occupies
  that opportunity, the halt pulse expires instead of waiting for a later read.
- If the output byte clock sees an empty buffer on the same get that completes
  a load, the arriving byte can feed the shifter immediately. An overlapping
  reload retains the original fetch address. Even after the one-byte sample
  ends, that request performs another full DMA and buffers a second copy.
- The bus accepts that queued request before releasing the CPU's halted read.
  OAM accesses continue to overlap halt/setup cycles; an aborted DMA does not
  steal an OAM data transfer.

These quirks are enabled only for NTSC (including the Multi fallback). PAL and
Dendy retain their previous cancellation behavior. NESdev marks 2A07 behavior
unknown and reports different timing on affected clones; the NTSC tables do not
establish those hardware contracts.

## NTSC register decode and controller clocks

DMA keeps the halted 6502 address separate from the address driven to external
memory. APU register selection uses bits 15–5 of the former and bits 4–0 of the
latter. Consequently, an OAM transfer from page `$40` cannot read APU registers
unless the halted CPU address is in `$4000–$401F`. When enabled, the registers
also appear at DMA addresses with matching low five bits, including RAM and ROM.
These rules follow the [hardware investigator's register-read notes](https://www.nesdev.org/wiki/User:Fiskbit#APU_register_reads)
and the [DMA register-conflict reference](https://www.nesdev.org/wiki/DMA#Register_conflicts).

- `$4015` returns internal status with bit 5 from external open bus. Reading it
  acknowledges the APU frame IRQ without replacing the external data latch.
- Controller reads drive the modeled NES bits 0–4 while preserving external
  bits 5–7 by default; RF wiring additionally preserves `$4016` bits 3–4.
  A DMA alias can clock either controller, independently of the port
  on which the CPU was halted.
- OAM puts drive their transferred byte onto external open bus and the PPU data
  port. APU status read into OAM is observable on the subsequent put even though
  the internal read itself did not drive the external bus.

The default NTSC controller timing is NES/AV Famicom. Each port has a separate
output-enable line. Contiguous reads keep it asserted and return the same bit;
releasing it clocks the controller once. A DMC sample get normally breaks the
sequence, but an alias of that same controller can keep the line asserted. An
aborted one-cycle DMA followed by the CPU's resumed read therefore clocks once
when that continuous read ends.

The controller strobe request is sampled at the get-to-put boundary. This
preserves the documented difference between the two possible one-cycle RMW
strobe pulses. While effectively strobed, live buttons reload the shift register;
a connected standard controller shifts in ones after the eighth bit.

The bus owns line history and pending strobe requests; `Controller::peek` and
`clock` separate sampling from the clock edge. Timed CPU/DMA reads use this path.
Untimed debugger reads retain their immediate read side effects and do not
change the timed output-enable history. PAL/Dendy retain their previous per-read
controller clocking and DMA register behavior.

Select `rf-famicom` for the original RF Famicom's half-cycle output-enable gate:
each CPU/DMA read releases the line before the next cycle, clocking once per
read, including consecutive reads and aborted halts. This selects controller
timing only: it retains the selected data-pin wiring, NTSC APU decoder, and
late-2A03 stop behavior. It is not a full RF console or CPU-revision profile.
Changing the timing releases any held NES read once; reapplying it has no effect.

All frontends load `[input] controller_timing=nes-av-famicom|rf-famicom`. The GUI
exposes Settings → General → Hardware → Controller timing; CLI and SDL3 accept
`--controller-timing` with the same tokens. Changes apply immediately to both
ports. The setting is retained through reset. Snapshots restore their own timing
without rewriting the saved preference; opening another game in the GUI applies
the preference again. PAL/Dendy execution ignores this NTSC timing selection.

### Controller data wiring

The separate `ControllerWiring` choice models the connected built-in pads and
unattached accessory inputs. NES is the default. RF Famicom wiring follows
[NESdev's port input table](https://www.nesdev.org/wiki/Controller_port_registers)
and [standard-controller report](https://www.nesdev.org/wiki/Standard_controller):

| Read | NES floating mask | RF floating mask | RF driven inputs |
|---|---|---|---|
| `$4016` | `$E0` | `$F8` | D0 pad 1; D1 unattached expansion = 0; D2 live digital microphone |
| `$4017` | `$E0` | `$E0` | D0 pad 2; unattached D1–D4 = 0 |

Normal, untimed, and aliased DMA reads merge only floating bits from the external
bus. The built-in RF pad 2 latches zero for Select/Start; its other six buttons
and post-eight-bit ones remain normal. Host buttons remain stored, allowing a
switch back to NES without losing input. A report already being shifted remains
latched until the next strobe. Host updates while strobed and pending strobe
requests obey the same masking on the high-to-low transition. Changing wiring
does not release or re-clock the current output-enable line.

`[input] controller_wiring=nes|rf-famicom` is shared by all frontends. The GUI
adds Controller wiring beside Controller timing in General → Hardware; CLI and
SDL3 accept `--controller-wiring`. Set both choices to `rf-famicom` for RF input
hardware. Wiring is independent of CPU revision and region, while the RF timing
choice affects NTSC only. Live audio capture/mixing, expansion controllers and
other accessories remain separate work.

The APU activation diagnostic also requires the separate
[PPU I/O latch](ppu-timing.md#ppu-io-bus). Its unmodified test executes a prepared
instruction through PPU open bus; the runner does not bypass its prerequisites.

### Digital microphone

The [standard-controller reference](https://www.nesdev.org/wiki/Standard_controller)
describes a live microphone report on `$4016` D2 and games detecting transitions.
The emulator supplies deterministic activity from a bindable input action.
It models a synthetic carrier, not a captured acoustic waveform: while held,
the period is 1024 CPU cycles, high for phases 0–511 and low for 512–1023.
Activation and release reset phase to zero; reapplying the same held value leaves
phase intact. Each CPU/DMA cycle advances it before the access. Reads, strobes,
controller shifts and host polls never advance it. Reset/region changes realign
phase. Pausing or audio backpressure freezes emulated time and the carrier.

Only RF wiring exposes the level, on `$4016` D2. `$4017` D2 and NES wiring stay
zero. Normal, debugger and DMA-aliased reads share this routing; activity does
not alter pad shift reports or floating-bit masks. The waveform continues while
held even if another wiring choice currently gates its output.

GUI and SDL3 keyboard/gamepad profiles map the extra Microphone action to one
shared core input. M is the player-one keyboard default; other bindings start
unbound. Session input combines both profiles once per emulated frame and clears
activity on successful ROM open/close. The next host input sync after snapshot
load retains saved phase if the held value is unchanged, or releases/restarts it
if host input differs. No host microphone device is opened and no microphone
audio is mixed. This carrier is an emulator input policy, not a claim about a
physical Famicom's microphone frequency, sensitivity, or analog response.

## Snapshots

Version 16 appends three bus bytes: microphone activity (0/1) and a 16-bit carrier
phase (0–1023). An inactive microphone requires phase zero. Invalid flags/phases
reject with full rollback. Versions 3–15 import with activity off and phase zero;
existing wiring/timing import rules remain unchanged. Activity is valid but its
pin is gated off under NES wiring. The exact microphone phase requires v16.

Version 15 appends one controller-wiring byte to the bus section (0: NES, 1: RF
Famicom), after the timing byte. Unknown values reject with full machine rollback.
Versions 3–14 restore NES wiring while retaining any version-14 timing choice;
this preserves the behavior of previous timing-only states. The controller's
serialized button/report layout stays unchanged. Active wiring and timing restore
from snapshots without changing the saved startup preferences.

Version 14 appends one controller-timing byte to the bus section (0: NES/AV
Famicom, 1: RF Famicom). Snapshots using RF timing require idle output-enable
at the completed CPU-cycle boundary. Invalid timing values or held RF lines reject with full
rollback. Versions 3–13 have no timing choice and restore NES/AV Famicom; version
3 still requires explicit legacy import.

Version 8 introduced eleven bytes in the APU section: a 64-bit CPU cycle count,
one-byte request delay, pending flag, and silence flag. The cycle count agrees
with the CPU's count. Invalid flags, delays, pending-buffer combinations, or
inconsistent counts reject the snapshot and restore the previous machine.
Version 9 adds interrupt and frame-reset state; its layout is described in the
[interrupt reference](interrupt-timing.md#snapshots).
Version 13 appends two bytes to the bus section: asserted controller port
(0, 1, or 2) and pending strobe request (0 or 1). It also appends the PPU I/O
latch to the PPU section. Non-NTSC states require an idle output-enable field.
The controller's stored strobe flag is range-checked. Invalid fields reject with
machine rollback. A held read and a not-yet-sampled strobe survive restoration.

Versions 4–12 remain readable. Their missing controller line history starts
idle because the old reader already shifted on every access. The pending strobe
request adopts the restored effective strobe. The missing PPU I/O latch starts
at zero. Exact new I/O history requires version 13.

Version 12 introduced six bytes in the APU section:

| Field | Size |
|---|---|
| Reader stop latch | 1 byte |
| Stop delay to the next get | 1 byte, 0–2 cycles |
| Pending request is a reload | 1 byte |
| Latched request address | 2 bytes |
| Empty-buffer byte-clock marker | 1 byte |

Flags, stop-delay phase, request combinations, address range, and byte-clock
phase are validated; malformed state rolls back the machine. A rate write
clears the same-cycle marker because a CPU write cannot share a bus cycle with
a DMC fetch. Active DMA bookkeeping stays transient and finishes before the
instruction boundary. Delayed stops, aborted requests, and queued reloads
retain their state across device snapshots; machine tests also cover a delayed
stop and rejection rollback. The standalone audio-synthesis callback reads only
on gets so it retains the same stop-delay phase invariants; the emulator uses
the bus arbitration path instead.

Versions 4–11 remain readable. Missing stop latches are initialized from the
remaining-byte count and enable flag; the missing address comes from the current
reader address. A ready legacy request is treated as a reload, and a delayed
one as a load. These imports cannot reconstruct old stop races; exact replay of
the new latches requires version 12.

Versions 4–7 remain readable. Their missing DMA phase comes from CPU elapsed
cycles; pending requests and silence are reconstructed from channel state.
Older blobs cannot recover the exact original delayed request or silence phase.
An inactive legacy zero-rate timer is initialized to the regional power-on rate.
Version 3 retains the explicit legacy-import policy.

## Regressions

Generated fixtures cover load/reload alignment, halts blocked by single and RMW
writes, cancellation before a halt, full OAM contents during overlap, FDS
acknowledgements and arriving bytes, address wrapping and loop/IRQ completion,
buffered output after reader disable, and pending-request snapshot replay/rollback.
Stop-specific tests cover both write phases, one repeated abort read, write
suppression, OAM overlap, duplicate-address playback, all three regions, device
and machine replay, rate changes, regional cancellation, and invalid-state rollback.

```sh
meson test -C buildDir cpu-accuracy apu fds-system save-state --print-errorlogs
```

Two public Blargg diagnostics are optional. Keep them outside Git in a directory
named by `MAPPERBUS_CPU_DMA_DIAGNOSTICS`; tests verify SHA-256 before execution.
Both must return success through the `$6000` protocol and print `Passed`.

Source revision:
[97720008e51db15dd281a2a1e64d4c65cf1bca4c](https://github.com/christopherpow/nes-test-roms/tree/97720008e51db15dd281a2a1e64d4c65cf1bca4c/sprdma_and_dmc_dma).

| File | SHA-256 |
|---|---|
| `sprdma_and_dmc_dma.nes` | `db3199bc1b0bdc07a316b3ab999d8fd8bb361456d2154e364c132cb06a26a10f` |
| `sprdma_and_dmc_dma_512.nes` | `3789f5134b0561b4344e3f4ce08b4d2a416f67435e083917a80d87fdb9d3583c` |

```sh
MAPPERBUS_CPU_DMA_DIAGNOSTICS="$PWD/tmp/dmc-timing" \
  meson test -C buildDir dma-diagnostics --print-errorlogs
```

### AccuracyCoin stop diagnostics

The same `dma-diagnostics` target can run an unmodified AccuracyCoin ROM through
its controller menu, selecting only Explicit DMA Abort and Implicit DMA Abort.
The runner requires the ROM's DMA synchronization pretest to pass, checks result
codes 1 and 5 (later CPU behavior), and compares all 64 answer-table bytes.
Before this change both tests failed with error 2; both now pass.

Source revision: [66e8711292d432cf009b2831f3ea1a055d75c9ba](https://github.com/100thCoin/AccuracyCoin/tree/66e8711292d432cf009b2831f3ea1a055d75c9ba).
The upstream diagnostic is MIT-licensed; its ROM is an optional local asset.

| File | SHA-256 |
|---|---|
| `AccuracyCoin.nes` | `9332c3735dd7dea2f4711e670d6478d8b0085edeacdd4f9f300fb04a8bbe3f88` |

```sh
MAPPERBUS_DMC_STOP_DIAGNOSTICS="$PWD/tmp/dmc-stop/AccuracyCoin" \
MAPPERBUS_CPU_DMA_DIAGNOSTICS="$PWD/tmp/dmc-timing" \
  meson test -C buildDir dma-diagnostics --print-errorlogs
```

Unset directories skip their optional test cases. Selecting a directory requires
its pinned files. Passing these two tests does not certify the full AccuracyCoin
suite, all CPU revisions, or every DMC enable/disable sequence.

### AccuracyCoin register and controller diagnostics

`MAPPERBUS_DMA_CONFLICT_DIAGNOSTICS` selects the same pinned ROM described above,
using a separate optional case in `dma-diagnostics`. The unmodified controller
menu runs these six tests for every timing/wiring combination. The expected
results are:

| Test | Expected result |
|---|---|
| DMA + $4015 Read | 1 |
| DMA + $4016 Read | 5 with NES/AV timing; 9 with RF timing |
| DMC DMA Bus Conflicts | 1 with NES wiring; 9 with RF wiring |
| APU Register Activation | 9 |
| Controller Strobing | 1 |
| Controller Clocking | 5 with NES/AV timing; 9 with RF timing |

All four combinations pass, including the ROM's internal prerequisites. Wiring
and clocking are tested independently. Synthetic regressions additionally check
all 256 external bus values on both ports, six-button pad 2 reports, host updates
while strobed, legacy timing-only imports, corrupt wiring rollback, and
independent ports, continuous reads, both strobe phases, open-bus isolation,
aliased OAM activation/IRQ acknowledgement, cross-controller DMC reads,
post-eight-bit ones, PPU latch behavior, and device/machine snapshot replay.
Microphone checks cover both read paths, both controller timings, strobe
independence, half-period boundaries, DMA aliases, phase replay, legacy imports,
invalid-state rollback, input-profile merging, pause, rebinding and legacy
configuration conflicts. A virtual SDL gamepad drives an original synthesized
ROM which records observing both microphone levels in CPU RAM; NodalKit tests
cover keyboard/gamepad binding and disconnect behavior.

```sh
MAPPERBUS_DMA_CONFLICT_DIAGNOSTICS="$PWD/tmp/dmc-stop/AccuracyCoin" \
MAPPERBUS_DMC_STOP_DIAGNOSTICS="$PWD/tmp/dmc-stop/AccuracyCoin" \
MAPPERBUS_CPU_DMA_DIAGNOSTICS="$PWD/tmp/dmc-timing" \
  meson test -C buildDir dma-diagnostics --print-errorlogs
```

## Remaining limits

These tests verify CPU-visible conflict values and OAM transfers. They do not
measure the DMC sample latch's audio payload under electrical contention. PAL
repeated-read addresses and register conflicts, expansion
accessory behavior and live microphone audio, CPU revision selection, and
adjacent PPUDATA read quirks remain outside this coverage. Passing the eight selected AccuracyCoin tests does not certify the
whole ROM. Reset bus cycles and analog power-on alignment remain outside the
current model.

[Reference index](README.md) · [FDS checks](fds-compatibility.md)
