# PPU vblank and frame timing

The NTSC PPU timing checks cover flag read/clear races, NMI enable/disable,
interrupt timing, and the shortened odd frame. They use the existing CPU bus
phase: two PPU dots before an access and one afterward. The per-pixel renderer
and scroll rules retain their current design.

## CPU/PPU clock scheduling

The bus uses integer master-clock dividers from `RegionTiming`. The hardware
ratios follow the [NESdev cycle reference](https://www.nesdev.org/wiki/Cycle_reference_chart):

| Region | Master clocks per CPU cycle | Master clocks per PPU dot | PPU dots per CPU cycle |
|---|---|---|---|
| NTSC / Multi fallback | 12 | 4 | 3 |
| PAL | 16 | 5 | 3.2 |
| Hybrid | 15 | 5 | 3 |

A retained remainder carries incomplete PPU dots across both halves of each CPU
bus cycle. This applies equally to instructions, dummy accesses, OAM DMA, and DMC
DMA. APU, FDS, and cartridge audio continue to advance once per CPU cycle.
Untimed debugger reads and writes do not advance either clock.

The selected PAL alignment places a CPU access after eight master clocks, then
advances the remaining eight. Starting with remainder zero, the five-cycle
pattern is:

| CPU cycle | Dots before access | Dots after access | End remainder |
|---|---|---|---|
| 1 | 1 | 2 | 1 |
| 2 | 1 | 2 | 2 |
| 3 | 2 | 1 | 3 |
| 4 | 2 | 1 | 4 |
| 5 | 2 | 2 | 0 |

This gives exactly 16 dots every five CPU cycles, with no floating-point drift.
Two PAL frames contain 212784 dots and take exactly 66495 CPU cycles. NTSC and
Hybrid retain the existing two-dot/one-dot access split. Reset starts the divider
at remainder zero. Changing region also clears it; reapplying the same region
preserves it. These are deterministic emulator alignment choices; the tests do
not establish every hardware power-on divider alignment or sub-dot register race.

## Vblank and NMI

Vblank sets at scanline 241, dot 1, and clears at the pre-render line, dot 1.
For the tested NTSC CPU/PPU alignment, PPUSTATUS behaves as follows:

| Read position | Vblank returned | Subsequent flag | CPU NMI |
|---|---|---|---|
| Before scanline 241, dot 0 | Clear | Sets normally | Normal |
| Dot 0 | Clear | Suppressed for that frame | Suppressed |
| Dots 1–2 | Set | Cleared by the read | Suppressed |
| Dot 3 or later | Set | Cleared by the read | An already latched edge survives |

These contracts follow the [PPU frame-timing reference](https://www.nesdev.org/wiki/PPU_frame_timing).
Tests check mirrored status ports and the bus detector, rather than relying on
the PPU compatibility/debug edge cache. Enabling NMI during vblank raises an edge;
writing the already enabled value does not retrigger it. See
[interrupt timing](interrupt-timing.md) for CPU polling and vector takeover.

## Odd frames

Frame parity toggles even while rendering is disabled. After reset, the first
frame is even and full length. A rendered odd NTSC frame contains 89341 dots;
the even frame contains 89342. Either background or sprites enables the skip.
PAL and Hybrid retain their full 341-by-312-dot frames.

The model retains the frame-length decision when pre-render dot 338 is processed
and transitions from dot 339 to the next frame's dot 0. A later PPUMASK write
cannot change that decision. This timing gate uses the register mask independently
of the renderer's existing delayed mask, preserving rendering-toggle behavior.
It does not add background prefetch increments or change scroll ownership.

The [hardware discussion of late PPUMASK writes](https://forums.nesdev.org/viewtopic.php?p=273152)
explains why the visible register write and rendering/skip timing cannot be
treated as the same instant. The pinned even/odd diagnostic checks the model's
decision boundary for the selected CPU/PPU alignment.

## PPU I/O bus

The CPU-facing PPU data latch is separate from the buffered VRAM read. Writes to
any PPU port, including read-only ports, fill it. Write-only register reads echo
it, PPUSTATUS preserves its low five bits, and readable ports update it. Palette
reads retain its upper two bits and apply the greyscale mask; OAM attribute reads
return zero for unimplemented bits 2–4. These are digital latch rules from the
[PPU register reference](https://www.nesdev.org/wiki/PPU_registers#MMIO_registers),
without analog decay or revision-specific register restrictions.

The latch is needed by the unmodified AccuracyCoin APU register-activation test,
which executes an instruction through prepared PPU open bus. It does not change
the per-pixel renderer, scroll rules, or mapper A12 approximation.

## Snapshots

Version 13 appends the PPU I/O latch byte. Older snapshots initialize the
missing latch to zero rather than guessing unavailable bus history. Exact I/O
replay requires version 13; see [DMA/controller snapshots](dma-timing.md#snapshots)
for the two accompanying bus fields.

Version 11 introduced one byte to the bus section: elapsed master clocks since the
last PPU dot. PAL accepts remainders 0–4; NTSC/Hybrid accept zero, as snapshots are
taken between complete CPU cycles. Other values reject the entire snapshot with
rollback. Restoring the region precedes validating its clock remainder.

Versions 4–10 remain readable. They advanced three PPU dots per CPU cycle in all
regions, so their missing PAL remainder starts at zero without moving the saved
PPU position. Future PAL execution follows the corrected ratio. A legacy PAL
snapshot cannot exactly reproduce the former clock behavior; exact fractional
phase replay requires version 11.

Version 10 added two bytes to the PPU section: frame parity and the retained
skip decision. Flags must be zero or one. A pending skip requires an odd NTSC
frame at pre-render dot 338 or 339; inconsistent state is rejected with machine
rollback. Late mask changes are valid after a decision has been retained.

Versions 4–9 did not shorten frames, so elapsed CPU clocks reconstruct parity
under the original CPU/PPU alignment. Their missing
skip decision starts clear. Exact replay of a pending skipped dot requires
version 10. Explicit version-3 import retains its existing policy.

## Diagnostics and regressions

`ppu-diagnostics` runs all ten public Blargg `ppu_vbl_nmi` singles from
[revision 97720008e51db15dd281a2a1e64d4c65cf1bca4c](https://github.com/christopherpow/nes-test-roms/tree/97720008e51db15dd281a2a1e64d4c65cf1bca4c/ppu_vbl_nmi).
The [runner](../../tests/integration/test_ppu_diagnostics.cpp) pins every SHA-256
and requires `$6000` success plus `Passed` output. Missing optional assets skip
the target; selecting a directory requires every pinned ROM to be present.

```sh
MAPPERBUS_PPU_DIAGNOSTICS="$PWD/tmp/ppu-timing" \
  meson test -C buildDir ppu-diagnostics --print-errorlogs
```

Before the frame-length change, tests 01–08 passed and 09/10 failed. All ten now
pass, including the published `00 01 01 02` frame-count and `08 08 09 07`
boundary captures. Synthetic tests cover exact frame lengths, background/sprite
gates, PAL/Hybrid exclusion, reset, masked-frame parity, mirrored status races,
and replay before/after the decision and across a shortened frame.

Bus and CPU tests additionally check the PAL access pattern, exact two-frame
clock count, OAM/DMC overlap at all five divider phases, status reads at vblank,
reset and region changes, snapshot replay across PAL frames, version-10 import,
and invalid-phase rollback. These PAL checks are synthetic; the ten public ROMs
above are NTSC diagnostics.

```sh
meson test -C buildDir bus ppu cpu-accuracy save-state --print-errorlogs
```

## Limits

The upstream tests describe one well-documented NTSC clock alignment; other
power-on divider alignments may produce different captures. These passes do not
certify every PPU revision, register race, analog open-bus decay, PAL-specific
vblank/NMI race behavior, or reset warmup behavior. The renderer's existing delayed-mask,
fetch, sprite, and mapper-A12 approximations remain separate from this timing gate.

[Reference index](README.md) · [FDS checks](fds-compatibility.md)
