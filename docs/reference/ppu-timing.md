# PPU vblank and frame timing

The NTSC PPU timing checks cover flag read/clear races, NMI enable/disable,
interrupt timing, and the shortened odd frame. They use the existing CPU bus
phase: two PPU dots before an access and one afterward. The per-pixel renderer
and scroll rules retain their current design.

## Vblank and NMI

Vblank sets at scanline 241, dot 1, and clears at the pre-render line, dot 1.
For the tested CPU/PPU alignment, PPUSTATUS behaves as follows:

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
PAL and Dendy retain their full 341-by-312-dot frames.

The model retains the frame-length decision when pre-render dot 338 is processed
and transitions from dot 339 to the next frame's dot 0. A later PPUMASK write
cannot change that decision. This timing gate uses the register mask independently
of the renderer's existing delayed mask, preserving rendering-toggle behavior.
It does not add background prefetch increments or change scroll ownership.

The [hardware discussion of late PPUMASK writes](https://forums.nesdev.org/viewtopic.php?p=273152)
explains why the visible register write and rendering/skip timing cannot be
treated as the same instant. The pinned even/odd diagnostic checks the model's
decision boundary for the selected CPU/PPU alignment.

## Snapshots

Version 10 appends two bytes to the PPU section: frame parity and the retained
skip decision. Flags must be zero or one. A pending skip requires an odd NTSC
frame at pre-render dot 338 or 339; inconsistent state is rejected with machine
rollback. Late mask changes are valid after a decision has been retained.

Versions 4–9 remain readable. Their PPU never shortened frames, so elapsed CPU
clocks reconstruct parity under the original CPU/PPU alignment. Their missing
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
gates, PAL/Dendy exclusion, reset, masked-frame parity, mirrored status races,
and replay before/after the decision and across a shortened frame.

```sh
meson test -C buildDir ppu cpu-accuracy save-state --print-errorlogs
```

## Limits

The upstream tests describe one well-documented NTSC clock alignment; other
power-on divider alignments may produce different captures. These passes do not
certify every PPU revision, register race, analog open-bus decay, PAL fractional
CPU/PPU cadence, or reset warmup behavior. The renderer's existing delayed-mask,
fetch, sprite, and mapper-A12 approximations remain separate from this timing gate.

[Reference index](README.md) · [FDS checks](fds-compatibility.md)
