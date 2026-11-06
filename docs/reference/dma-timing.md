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

## Snapshots

Version 8 introduced eleven bytes in the APU section: a 64-bit CPU cycle count,
one-byte request delay, pending flag, and silence flag. The cycle count agrees
with the CPU's count. Invalid flags, delays, pending-buffer combinations, or
inconsistent counts reject the snapshot and restore the previous machine.
Version 9 adds interrupt and frame-reset state; its layout is described in the
[interrupt reference](interrupt-timing.md#snapshots).
Version 12 appends six bytes to the APU section:

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

## Remaining limits

PAL repeated-read addresses, internal `$4000–$401F` register conflicts,
joypad output-enable edge behavior, and adjacent PPUDATA read quirks are not certified. The public timing
passes do not establish those contracts. The separate interrupt diagnostics
cover the implemented IRQ/NMI polling contracts. Reset bus
cycles and analog power-on alignment are outside the current model.

[Reference index](README.md) · [FDS checks](fds-compatibility.md)
