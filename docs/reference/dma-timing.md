# DMA timing

The APU and expansion audio advance once per CPU/DMA bus cycle. The PPU remains
instruction-stepped, with its existing scanline/per-pixel renderer. IRQ/NMI
sampling remains at instruction boundaries.

The emulator chooses a deterministic power-on phase: odd APU cycle indices are
gets, even indices are puts. Hardware can power on in either alignment.

## Scheduling and arbitration

- A `$4015` start with an empty sample buffer schedules a load halt on the get
  in the second following APU cycle, three/four CPU cycles after the write.
- The free-running DMC output unit schedules a reload on the put after it
  empties the sample buffer. Disabling the reader stops further fetches while
  buffered output finishes.
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

## Snapshots

Version 8 appends eleven bytes to the APU section: a 64-bit CPU cycle count,
one-byte request delay, pending flag, and silence flag. The cycle count agrees
with the CPU's count. Invalid flags, delays, pending-buffer combinations, or
inconsistent counts reject the snapshot and restore the previous machine.
Active DMA finishes before the instruction boundary; a delayed request can
remain pending across it and is serialized.

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

## Remaining limits

The stop/abort and duplicate-fetch DMC bugs are not modeled. PAL repeated-read
addresses, internal `$4000–$401F` register conflicts, joypad output-enable edge
behavior, and adjacent PPUDATA read quirks are not certified. The public timing
passes do not establish those contracts or precise IRQ/NMI sampling. Reset bus
cycles and analog power-on alignment are outside the current model.

[Reference index](README.md) · [FDS checks](fds-compatibility.md)
