# Interrupt timing

CPU bus cycles record detector outputs and instructions poll those saved
outputs at their designated cycles. IRQ uses the preceding cycle's pin level;
NMI uses a retained edge. Servicing begins at the next instruction boundary.

## Polling and entry

- Ordinary instructions poll before their final bus access, using detector
  outputs derived from the preceding cycle. A final-cycle acknowledgement cannot
  erase an already accepted IRQ.
- CLI, SEI and PLP poll with the old I flag. RTI uses its restored I flag.
- Branches poll before the operand fetch. Taken same-page branches skip the
  third-cycle poll; page-crossing branches poll again before the PCH correction.
  Either opportunity can accept the interrupt, with NMI taking priority.
- Interrupt entry does not poll for a second entry. A handler executes at least
  one instruction before another interrupt can be serviced.
- An NMI arriving before vector selection can redirect IRQ/BRK to `$FFFA`.
  The pushed PC and B flag retain their original meaning. Later edges wait
  through the first handler instruction; the vector pair remains coherent.
- DMA clocks the detectors without advancing the CPU's polling sequence. NMI
  edges survive halted reads and later PPUSTATUS acknowledgements. IRQ is
  recognized at the CPU's next eligible polling opportunity.

These contracts follow [CPU interrupts](https://www.nesdev.org/wiki/CPU_interrupts)
and the hardware-derived [6502 interrupt recognition analysis](https://www.nesdev.org/wiki/Visual6502wiki/6502_Interrupt_Recognition_Stages_and_Tolerances).

## Source timing

For NTSC and Dendy, the bus advances two PPU dots before the access and one
afterward. PAL advances 16 dots every five CPU cycles, retaining an integer
remainder around each access; see [PPU clock scheduling](ppu-timing.md#cpuppu-clock-scheduling).
These selected alignments keep the existing per-pixel renderer and scroll rules. PPUSTATUS read at the dot before vblank suppresses that frame's
flag and NMI; the CPU's already latched edges survive later reads. The source
uses the [PPU frame-timing reference](https://www.nesdev.org/wiki/PPU_frame_timing).

APU `$4017` immediately updates mode/inhibit and acknowledges an inhibited IRQ.
Counter reset and the five-step quarter/half clocks wait for the second get
after the write: three cycles after a put, four after a get. Four-step IRQ
asserts on three consecutive cycles, with a 29830-cycle NTSC period. The
[frame-counter hardware clarification](https://forums.nesdev.org/viewtopic.php?t=26816)
resolves the older conflicting descriptions of reset delay.

## Snapshots

Version 9 introduced five bytes in addition to version 8:

| Section | Extension |
|---|---|
| CPU | Accepted decision: none, IRQ or NMI |
| PPU | Pending vblank suppression |
| APU | Pending frame-reset delay, 0–4 cycles |
| Bus | Previous NMI line and latched edge |

Flags, decision values and reset delay are checked on restore. Malformed state
rolls back the entire machine. Accepted IRQ survives even if the saved I flag
is set, as in CLI/SEI. Active DMA completes before a snapshot; pending decisions,
detector state, source delays and DMC requests persist across it.

Versions 4–8 remain readable. Missing sampling history is reconstructed from
restored source state, so exact pending-interrupt replay requires version 9.
Version 3 retains the explicit legacy-import policy.
Version 10 additionally preserves PPU frame parity and a retained skipped-dot
decision. Version 11 retains the fractional PAL clock phase; see
[PPU timing](ppu-timing.md#snapshots).

## Validation

Generated tests cover CLI/SEI/PLP latency, final-cycle acknowledgement, early/late
NMI, branch polling, IRQ/BRK takeover and stack flags, handler latency, OAM/DMC
edges, accepted-interrupt replay and malformed-state rollback. APU tests cover
the three IRQ pulses, period and delayed half clock; PPU tests cover suppression
and its snapshot state.

```sh
meson test -C buildDir cpu-accuracy apu ppu fds-system save-state --print-errorlogs
```

The optional `interrupt-diagnostics` target runs all five public Blargg
`cpu_interrupts_v2` singles from revision
[97720008e51db15dd281a2a1e64d4c65cf1bca4c](https://github.com/christopherpow/nes-test-roms/tree/97720008e51db15dd281a2a1e64d4c65cf1bca4c/cpu_interrupts_v2).
Hashes are pinned in the [runner](../../tests/integration/test_interrupt_diagnostics.cpp).
Each ROM must finish through the `$6000` protocol and print `Passed`.

```sh
MAPPERBUS_CPU_INTERRUPT_DIAGNOSTICS="$PWD/tmp/interrupt-timing" \
  meson test -C buildDir interrupt-diagnostics --print-errorlogs
```

## Limits

The diagnostic captures validate a deterministic NTSC alignment. They do not
certify every CPU/PPU power-on phase, subcycle analog propagation, PAL's fractional
PPU cadence, reset bus activity, or every PPU register race. The renderer retains
its existing approximation of fetches and mapper A12 edges. Simultaneous `$4015`
read/IRQ-flag assertion needs separate validation. The implemented NTSC DMC stop
races and the remaining internal-register conflict limits are described in
[DMA timing](dma-timing.md).
The ten separate [PPU diagnostics](ppu-timing.md) cover the implemented vblank
read races and odd-frame timing for that alignment.

[Reference index](README.md) · [FDS checks](fds-compatibility.md)
