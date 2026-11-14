# Mapper implementation coverage

This ledger tracks the requested implementation of every documented NES/Famicom
cartridge mapper, including bootlegs and homebrew. **The full target is not yet
complete.** Updated 2026-11-14 against the working-tree registry.

There are **147 registered cartridge IDs**: the original 14, 53 added in the first
batch, and Irem 32/65/77, Sunsoft 67/68, Taito 33/48/80/207/82/552, Jaleco 18/72/92/101,
Namco 210, MMC3 variants 118/119, RAMBO-1 64/158, Bandai 16/153/159, and Datach
157, UNROM 512 30, GTROM/direct MMC1 111, and J.Y. Company
35/90/209/211/281/282/295/358/386/387/388/397, plus the sixteen Sachen/TXC
IDs, Sachen MMC3 clones 262/512/513, Rex Soft's 263, Nitra 250, mappers
114/115/123, T9552 249 / 4:5, mixed CHR 74/192/194/195, MRCM 196, mapper 197,
TNROM640 198, Waixing FS309 199, multicarts 202/205, Gouder/JJ-01 208,
Sugar Softec 215, Single Chip 218, Kasheng A9461 219, NTDEC N625092 221,
address-latched multicarts/RPGs 227 and Active Enterprises 228 detailed below.
FDS remains a separate subsystem, conventionally numbered 20. Registration and
synthetic tests do not certify every commercial game, revision, peripheral, or
unusual ROM geometry.

## Implemented in this batch

| Implementation | New IDs | Observable behavior covered |
|---|---|---|
| Active Enterprises (follow-up) | 228 | Packed three-chip Action 52 and always-enabled single-chip profiles, 16/32 KiB PRG, CHR ROM, H/V CIRAM, missing-chip open bus, CPU execution and compact snapshots. |
| Address-latched multicarts / RPGs (follow-up) | 227 | Three submappers, fixed/mirrored/aligned PRG modes, solder-pad menus, CHR protection, battery WRAM, CPU execution and validated snapshots. |
| NTDEC N625092 (follow-up) | 221 | Address-latched 16/32 KiB and UNROM modes, PRG A17–A20, H/V CIRAM, protected unbanked CHR RAM, CPU execution and validated snapshots. |
| Kasheng A9461 compatibility model (follow-up) | 219 | MMC3/extended bank protocols, reversed PRG pins, staged CHR latch, shared outer banks, CIRAM/IRQs, CPU execution and compact validated snapshots. |
| Single Chip / Magic Floor (follow-up) | 218 | All four CIRAM wirings, shared pattern/nametable memory, background/sprite rendering, raw PPU aliases, palette buffering, CPU execution and validated snapshots. |
| Sugar Softec (follow-up) | 215 | Both outer-pin submappers, eight address/index scrambling modes, MMC3/NROM banking, CHR/CIRAM, IRQs, CPU execution and compact validated snapshots. |
| Gouder / JJ-01 (follow-up) | 208 | Both submappers, 32 KiB PRG control, XOR protection and executable registers, CHR/CIRAM, IRQs, CPU execution and compact validated snapshots. |
| JC-016-2 multicart (follow-up) | 205 | MMC3 block pins on both ROMs and fixed PRG banks, selectable solder pad, write-only low register, IRQs, CPU execution and compact validated snapshots. |
| 150-in-1 multicart (follow-up) | 202 | Address-latched PRG/CHR, mirrored 16 KiB and consecutive-page 32 KiB modes, H/V CIRAM, CPU execution and compact validated snapshots. |
| Waixing FS309 (follow-up) | 199 | Conventional PRG through 1 MiB, fixed CHR RAM, full-chip NVRAM persistence, IRQs, CPU execution and validated snapshots. |
| TNROM640 compatibility model (follow-up) | 198 | Two-chip 640 KiB PRG banking, banked CHR RAM, separate auxiliary/NVRAM windows, IRQs, CPU execution, snapshots and battery saves. |
| Larger MMC3 CHR windows (follow-up) | 197 | Four PPU pin-wiring profiles, 512 KiB CHR ROM, gated PRG A17 control, IRQs, CPU execution and validated snapshots. |
| MRCM boards (follow-up) | 196 | Four register-address profiles, legacy heuristics, 32 KiB PRG override, CHR/CIRAM, IRQs, CPU execution and wiring-validated snapshots. |
| Waixing FS303 (follow-up) | 195 | PPU-write-selected CHR RAM windows, shared physical RAM pages, optional $5000 RAM, MMC3 banking/IRQs, CPU execution, snapshots and saves. |
| Mixed CHR at bank zero (follow-up) | 194 | Selective ROM/RAM, RAM-only bank mirroring, legacy/NES 2.0 populations, IRQs, CPU execution, configuration-tagged snapshots and saves. |
| Waixing mixed CHR (follow-up) | 74, 192 | Exact ROM/RAM bank ranges, legacy/NES 2.0 populations, paired/inverted CHR, ROM write protection, IRQs, CPU execution, snapshots and saves. |
| Waixing T9552 (follow-up) | 249; 4:5 profile | All eight address-scrambling patterns, legacy/canonical file order, optional WRAM, IRQs, CPU execution, snapshots and saves. |
| Kasheng H2288 (follow-up) | 123 | Bank-index scrambling, $5800 PRG override, write-only open bus, ordinary CHR/CIRAM, default MMC3 IRQs, CPU execution and snapshots. |
| Scrambled MMC3 boards (follow-up) | 114 | Both submapper address/index permutations, NROM override, CHR outer bank, non-repeating zero IRQ profile, native CPU execution and snapshots. |
| Kasheng SFC boards (follow-up) | 115 | MMC3/NROM PRG modes, independent outer PRG/CHR pins, solder-pad open-bus reads, zero-latch IRQ repeats, CPU execution and snapshots. |
| Nitra MMC3 boards (follow-up) | 250 | Address-data registers, standard L401 and Queen Bee V PRG-A13 mux profiles, banking/IRQs, raw/archive selection and snapshot wiring validation. |
| Rex Soft UNL-KOF97 (follow-up) | 263 | Scrambled register addresses/data pins, standard MMC3 banking and IRQs, header-selected WRAM/NVRAM, CPU execution, persistence and snapshots. |
| Sachen Street Heroes (follow-up) | 262 | Physical-region CHR outer pins, banked ROM/RAM switching, four-screen VRAM, title jumper with partial bus drivers, shared MMC3 IRQs and snapshots. |
| Sachen SA-9602B (follow-up) | 513 | PPU-selected outer PRG banks, fixed outer-zero windows, 32 KiB battery CHR RAM, shared MMC3 IRQs, snapshots and persistence. |
| Sachen MMC3 clone (follow-up) | 512 | MMC3 PRG/CHR banking and IRQs, separate pattern/nametable RAM, four graphics modes, always-enabled WRAM, battery saves and snapshots. |
| Sachen boards (follow-up) | 133, 135, 137, 138, 139, 141, 143, 146, 150, 243 | Explicit latch/8259/SA-015 wiring, asymmetric CIRAM, readable registers and D2 pad, Q-Boy CHR RAM, protection reads and snapshots. |
| TXC/JV001 boards (follow-up) | 36, 132, 136, 147, 172, 173 | Staged input/register/output, chip-width counters and inversion, S bit, reversed data pins, immediate CHR pin, 147 latch/conflict variant, open bus and snapshots. |
| J.Y. multicart PCBs (follow-up) | 281, 282, 295, 358, 386, 387, 388, 397 | Board-specific outer PRG/CHR pin assignments, inner masks, outer-register aliases, mapper-388 nametable inhibit, shared IRQs and snapshot/persistence integration. |
| J.Y. Company ASIC (follow-up) | 35, 90, 209, 211 | PRG/CHR modes and outer banking, physical-read CHR latches, mixed CHR/CIRAM nametables, all four documented IRQ sources, math/protection ports, board jumpers, WRAM persistence and snapshots. |
| Flash boards (follow-up) | 30, 111 | UNROM 512 submappers 0–4, GTROM read-triggered open-bus latch, banked CHR/nametable/bonus RAM, SST39SF040 commands and polling, separate flash persistence; mapper 111 CHR ROM selects direct MMC1. |
| MMC4 | 10 | 16 KiB PRG, both eight-byte CHR trigger ranges, old-bank trigger fetch, mirroring, RAM, persistence. |
| Datach (follow-up) | 157 | Fixed 8 KiB CHR RAM, LZ93D50 M2 IRQ, PPU-selected external EEPROM clock, shared SDA, barcode injection, separate reader/cartridge persistence and snapshots. |
| Bandai FCG / LZ93D50 (follow-up) | 16, 153, 159 | Direct/latched M2 IRQ variants, eight 1 KiB CHR banks, four mirroring modes, original Xicor serial EEPROM protocols, gated WRAM and PPU-address-driven outer PRG banking, persistence and snapshots. |
| MMC3 variants (follow-up) | 118, 119 | TxSROM CHR-driven CIRAM, TQROM mixed ROM/RAM with 128 KiB ROM extension, common PRG banking and IRQ counter, bounded state and persistence. |
| Tengen RAMBO-1 (follow-up) | 64, 158 | Three five-bit PRG banks, K/C CHR modes with R8/R9, mapper-158 CIRAM, M2 CPU timer and filtered A12 counter, cold start, reset retention and snapshots. |
| Irem G-101 / H3001 (follow-up) | 32, 65 | Two switchable 8 KiB PRG banks, layout swap, eight 1 KiB CHR banks, Major League submapper, mirroring, CPU-cycle down counter, RAM and snapshots. |
| Irem LROG017 (follow-up) | 77 | 32 KiB PRG and 2 KiB CHR ROM banks, bus conflicts, 6 KiB pattern RAM plus 2 KiB external nametable RAM, real CIRAM routing and unmapped upper nametables. |
| Sunsoft-3 / Sunsoft-4 (follow-up) | 67, 68 | 16 KiB PRG and 2 KiB CHR banks, four mirroring modes, Sunsoft-3 live IRQ counter and acknowledgement aliases, Sunsoft-4 ROM nametables and WRAM gate, DCS options with licensing and paired snapshot identity. |
| Taito TC0190 / TC0690 (follow-up) | 33, 48 | Two 8 KiB PRG windows, full-width 2/1 KiB CHR banks, distinct mirroring decoders, mapper-48 latched IRQ and submapper timing profiles, CPU/PPU and snapshot replay. |
| Taito X1-005 (follow-up) | 80, 207 | Three 8 KiB PRG windows, aligned 2/1 KiB CHR banks, exact register aliases, protected 128-byte RAM and battery persistence, H/V and CHR-driven CIRAM routing. |
| Taito X1-017 (follow-up) | 82, 552 | Legacy/physical PRG wiring, CHR half swap, three independently protected RAM regions, 5 KiB persistence, CPU bus pull-downs and M2 IRQ timer. |
| Jaleco SS88006 (follow-up) | 18 | Nibble-written PRG/CHR banks, four CIRAM arrangements, separate RAM chip/write enables, 4/8/12/16-bit wrapping M2 IRQ, persistence and snapshots. Optional external speech playback remains unsupported. |
| Jaleco discrete boards (follow-up) | 72, 92, 101 | Independent PRG/CHR rising-edge latches after bus conflicts, opposite fixed PRG windows, fixed H/V CIRAM, mapper-101 legacy dump ordering and eight-bit CHR selection, CPU/PPU snapshot replay. Optional external speech playback remains unsupported. |
| Namco 108 family | 76, 88, 95, 154, 206 | Register decode, fixed and switchable PRG, 1/2 KiB CHR wiring, fixed-PRG submapper, CHR-driven CIRAM, one-screen mirroring. |
| Namco 175/340 (follow-up) | 210 | Explicit submappers and documented fallback, three 8 KiB PRG banks and eight 1 KiB CHR banks, fixed or switchable CIRAM, gated/mirrored optional RAM, exact NVRAM persistence and snapshots. |
| Konami VRC1–4 | 21, 22, 23, 25, 73, 75 | Submapper address-line permutations, chip-specific banking and mirroring, one-bit VRC2 latch, CPU and prescaled IRQs, enabled WRAM and NVRAM. |
| Homebrew latches | 28, 31 | Action 53 mode/size matrix, banked CHR RAM, mirroring control; mapper 31's eight 4 KiB PRG windows; power-up versus soft reset. |
| Discrete cartridge boards | 13, 34, 66, 70, 71, 78, 79, 87, 89, 93, 94, 97, 113, 140, 152, 180, 184 | Address decoding, bank-bit wiring, bus conflicts where documented, mirroring variants, CPROM RAM banks, NINA RAM/register overlay. |
| Additional latch and multicart boards | 38, 46, 58, 61, 62, 81, 107, 145, 148, 149, 193, 200, 201, 203, 212, 213, 225, 226, 229, 231, 232, 240 | Outer banking, address/data latches, register mirrors, partial CPU data-bus drivers, CHR write protection and Aladdin bank-bit permutation. |

Sources are the individual immutable NESdev revisions in the inventory below.
All implementation code is original; no external emulator source was incorporated.

## Integration boundaries

- `Mapper::clock_cpu()` advances once per CPU/M2 cycle in the bus, including DMA;
  it does not depend on the presence of expansion sound. Existing audio callbacks
  retain their original clocking.
- PRG and expansion read masks merge undriven data bits with CPU open bus. The
  default mask preserves existing mapper behavior. VRC2 and multicart status/RAM
  ports exercise the new path.
- `Mapper::cpu_open_bus_mask()` models cartridge pull-downs on undriven CPU
  bits, including console APU/controller reads. X1-017 grounds floating bits;
  the default preserves the latched bus value. Driven data remains intact.
- Optional `nametable_address()` maps board-specific CIRAM wiring. Mapper 95 is
  tested through PPU registers, including reversed horizontal nametable pairs.
- `maps_nametable()` checks raw `$2000–$3EFF` decoding before ordinary `$3000`
  folding. Mapper 77 leaves the upper range undriven: reads model the low
  address byte on the multiplexed PPU memory bus, and writes are ignored.
  The default retains existing nametable and palette behavior.
- Optional `read_nametable()` / `write_nametable()` intercept normalized
  `$2000–$2FFF` accesses before CIRAM mapping. Sunsoft-4 supplies ROM bytes and
  consumes protected writes. Defaults retain existing mapper behavior; PPU
  `$3000` mirrors and palette decoding remain distinct.
- NES 2.0 header parsing retains volatile and nonvolatile PRG/CHR memory sizes.
  This does not imply that every existing mapper already uses those fields.
- New mapper state includes registers, latches, RAM and IRQ progress. The DCS
  follow-up advances machine snapshots to version 20 for its licensing counter;
  other mapper payloads remain unchanged. Older snapshots retain the existing
  import path, including version 19 DCS snapshots with an empty option slot.

See [mapper tests](../../tests/unit/test_mapper_boards.cpp),
[Irem tests](../../tests/unit/test_irem.cpp),
[LROG017 tests](../../tests/unit/test_irem_lrog017.cpp),
[Namco 175/340 tests](../../tests/unit/test_namco175340.cpp),
[MMC3 variant tests](../../tests/unit/test_mmc3_variants.cpp),
[RAMBO-1 tests](../../tests/unit/test_rambo1.cpp),
[Bandai tests](../../tests/unit/test_bandai_fcg.cpp),
[Datach tests](../../tests/unit/test_datach.cpp),
[flash-board tests](../../tests/unit/test_flash_boards.cpp),
[Sunsoft tests](../../tests/unit/test_sunsoft_boards.cpp),
[Taito tests](../../tests/unit/test_taito.cpp),
[X1-005 tests](../../tests/unit/test_taito_x1005.cpp),
[X1-017 tests](../../tests/unit/test_taito_x1017.cpp),
[SS88006 tests](../../tests/unit/test_jaleco_ss88006.cpp),
[Jaleco discrete tests](../../tests/unit/test_jaleco_discrete.cpp),
[factory](../../src/core/mappers/mapper_registry.cpp), and
[compatibility](compatibility.md).

## Known limits and remaining work

- Sunsoft-2 disabled CHR RAM currently returns `$FF`; analog PPU open-bus decay
  is not modeled. Tests verify write gating and preserved RAM contents.
- Where published descriptions leave bus-conflict behavior unknown (for example
  46 and 107), the implementation uses the documented emulator-compatible
  no-conflict behavior. This is not a new hardware measurement.
- The Namco 108 CPU-RAM-write/execution-address erratum is not modeled.
- The inferred early-M2 latch behavior of mapper 38's `$F000` alias is not
  certified; normal `$7000–$7FFF` decoding is implemented.
- Action 53 implements its standard no-PRG-RAM configuration; the optional
  implementation-specific WRAM extension is not supplied.
- Larger remaining families include other Bandai, Jaleco and Taito boards,
  further MMC3 derivatives and outer-bank variants,
  remaining Sachen and other ASIC boards, further homebrew boards, and Rainbow.
- Jaleco's optional external ADPCM speech chips and sample-media loading remain
  unimplemented, including on some mapper-18/72/92 boards.
- Enhanced consoles and copier/educational adapters may require machine or
  peripheral support beyond a cartridge mapper. Duplicate and tool-reserved
  numbers require classification rather than fabricated hardware.

## Verification

The mapper executable includes deterministic bank sentinels, submapper pin
matrices, electrical bus-conflict cases, CPU/PPU integration, IRQ timing,
component snapshot replay and truncation rejection for every new ID. Full-machine
boot/replay/rollback coverage also uses original in-memory synthetic ROMs.
No commercial-title validation is claimed for the new IDs.

Commands used are recorded here so results can be repeated:

```sh
meson test -C buildDir --print-errorlogs --suite=mapperbus
ASAN_OPTIONS=halt_on_error=1:abort_on_error=1:detect_leaks=0 \
  meson test -C buildDir-sanitize mappers bus ppu region save-state mapper-timing --print-errorlogs
just format-check
```

Leak detection is disabled because this macOS ASan runtime rejects
`detect_leaks=1` before executing the tests. Address and undefined-behavior
sanitizers remain enabled. Static analysis uses the repository clang-tidy checks
on the four new mapper implementation files.

Validation for the first batch: `just build` passed; the regular suite reported
41 passed and zero failed; the six targeted ASan/UBSan suites passed. The final
MMC4 CHR-RAM fix was followed by another successful mapper sanitizer run and
41-suite regular run. Clang-tidy reported zero diagnostics on the four new
mapper implementation files, and formatting/whitespace checks passed.

## Irem follow-up: hardware decisions and limits

G-101 uses five PRG bank bits and eight CHR bank bits. Submapper 1 fixes CIRAM
to its upper page and disables the entire `$9000` register. See
[mapper 32 revision 23869](https://www.nesdev.org/w/index.php?oldid=23869) and
[G-101 pinout revision 6038](https://www.nesdev.org/w/index.php?oldid=6038).

H3001 implements the physically tested two-register PRG layout. `$9000` bit 7
swaps the first programmable bank with the fixed second-last bank; writes at
`$C000` do not select a third bank. `$9001` supports vertical, horizontal and
lower one-screen mirroring. Register mirrors use `$F007` decoding, with all
eight `$800x`/`$A00x` aliases selecting the same respective PRG register. See
[the original hardware investigation](https://forums.nesdev.org/viewtopic.php?t=19778).

The H3001 timer follows [mapper 65 revision 24119](https://www.nesdev.org/w/index.php?oldid=24119):
it asserts IRQ on the decrement to zero, holds at zero, and acknowledges on
`$9003`/`$9004` writes. Enable changes pause/resume without reloading; latch writes
leave the running count intact. Reloading zero remains idle in this model;
that boundary has no independent hardware timing measurement here. IRQ service
does not acknowledge the ASIC. CPU cycles, including OAM DMA, advance it with
rendering and expansion audio disabled.

Both implementations use deterministic reset defaults (PRG banks 0/1, layout 0,
zeroed CHR registers, header mirroring) while preserving RAM. Physical H3001
power-up values are not guaranteed by the investigation. Major League always
uses upper one-screen mirroring. Legacy Major League images require a corrected
NES 2.0 submapper; automatic title/hash detection is not supplied.

Memory bounds are 256 KiB PRG for G-101, 512 KiB for H3001, and 256 KiB CHR ROM.
CHR RAM falls back to 8 KiB; smaller declared volatile sizes mirror within that
space. Explicit NES 2.0 PRG memory maps at `$6000–$7FFF`, up to 8 KiB total,
with nonvolatile bytes following volatile bytes. Legacy battery headers provide
8 KiB NVRAM; the legacy implicit RAM default alone does not create RAM. H3001's
RAM-select output is hardware-observed; optional external RAM on G-101 is a
header-directed compatibility extension, not a claim about original boards.
Larger RAM and CHR NVRAM configurations are rejected.

The follow-up tests exercise both boards' banking, aliases and ROM write
protection; G-101 submapper 1; H3001 mirroring, pause/reload/acknowledgement and
terminal IRQ behavior; writable memory and persistence; countdown/pending
snapshot replay; corrupt-register rejection; DMA clocking; and a synthetic
6502 IRQ handler with full-machine replay/rollback. The shared 55-board boot
matrix now explicitly resets the CPU and checks the cartridge reset vector.
No commercial-title or physical-cartridge validation is claimed.

Follow-up validation: `just build` passed; the regular command above reported
41 successful suite executables and zero failures (the mapper executable passed
1,974 assertions across 82 cases). All six targeted ASan/UBSan executables
passed. Optional external-ROM and GPU cases retain their environment-dependent
skips. `just format-check`, `git diff --check`, and clang-tidy on `irem.cpp`
passed with no diagnostics from the new implementation.

## Sunsoft follow-up: hardware decisions and limits

Sunsoft-3 uses four-bit PRG and six-bit CHR bank selection, with `$F800` register
decoding. Its 16-bit CPU down counter interrupts on underflow, then stops.
`$C800` writes change the live count, high byte first; `$D800` controls counting
and resets the write phase without acknowledging. Acknowledgement uses the
`$8000` aliases decoded by mask `$8800`. The unused external IRQ input is not
exposed. See [mapper 67 revision 23676](https://www.nesdev.org/w/index.php?oldid=23676).

Sunsoft-4 uses seven-bit 2 KiB pattern banks and two 1 KiB nametable banks whose
top bit is forced high. All four mirroring modes apply to both CIRAM and ROM
nametables; ROM writes leave CIRAM intact. `$F000` bit 4 gates WRAM reads and
writes; a disabled window returns CPU open bus. See
[mapper 68 revision 23720](https://www.nesdev.org/w/index.php?oldid=23720).

Both implementations accept up to 256 KiB PRG, with fixed final 16 KiB, and
8 KiB total declared PRG RAM/NVRAM. CHR ROM limits are 128 KiB for Sunsoft-3 and
256 KiB for Sunsoft-4. Sunsoft-3 accepts up to 8 KiB volatile CHR RAM as a
compatibility extension; Sunsoft-4 requires CHR ROM. Larger RAM, CHR NVRAM and
unknown submappers fail creation. Explicit NES 2.0 memory sizes are honored;
legacy Sunsoft-4 provides 8 KiB WRAM, while legacy Sunsoft-3 only supplies PRG
memory with a battery flag. Nonvolatile bytes follow volatile bytes in the CPU
window. Reset preserves RAM and uses deterministic register defaults; physical
power-up states are not certified.

[Submapper 68:1](https://www.nesdev.org/wiki/NES_2.0_submappers#068:_Sunsoft_4)
selects the Dual Cartridge System base, limited to 128 KiB internal PRG.
The fixed bank always remains internal; absent or expired option ROM drives
no CPU data bits. Reset selects internal bank zero. Option media is attached
during preparation using `--dcs-option PATH`, with CPU/M2 licensing expiry,
retriggering and paired save-state identity. See [DCS loading](../how-to/play-dcs-games.md)
for formats, the inferred timer period, snapshot compatibility, and launch/UI
limits. Legacy hashes are not used to infer this board variant.

Tests cover register masks, bank limits, mirroring, IRQ live writes and phase,
underflow, pause, full counter range, pending/countdown snapshots, DMA clocks,
CPU IRQ handling and machine rollback. PPU tests exercise all ROM nametable
arrangements, attributes, `$3000` mirrors, palette isolation, ROM write protection
and preserved CIRAM. Both IDs join the shared 57-board boot/replay matrix.
No commercial-title or physical-cartridge validation is claimed.

Sunsoft validation: `just build` passed; the regular suite command above reported
41 successful executables and zero failures, with 2,280 assertions in 96 mapper
cases. All six targeted ASan/UBSan executables passed; final mapper and PPU
sanitizer reruns passed after the DCS snapshot assertions and unsigned PPU-mask
cleanup. Formatting and whitespace checks passed. Clang-tidy reported no
diagnostics on `sunsoft34.cpp` or the changed PPU/cartridge/mapper access-hook
lines. Optional asset-dependent cases retain their existing skips.

## DCS option follow-up

Option ROMs now load through `Cartridge::from_data/from_file` and the shared
ROM-source loader before installation. CLI, SDL3 and GUI startup accept
`--dcs-option PATH`. The mapper models licensing expiry and retriggering with
CPU open bus after expiry; snapshots bind both immutable images and retain
the remaining cycle count in version 20. Battery persistence retains the base
identity. See [the DCS guide](../how-to/play-dcs-games.md) for formats and limits.

Validation: `just build` passed; `meson test -C buildDir --print-errorlogs
--suite=mapperbus` reported 42 successful executables, zero failures, and 2,472
assertions in 102 mapper cases. This includes CPU/M2 expiry and DMA boundaries,
reactivation, bank mirroring, WRAM gating, invalid timer rollback, mismatched
options, empty-slot v19 import, raw/headered and archived source preparation,
base battery paths, CLI launches, and software GUI failure/Retry identity.

Nine targeted ASan/UBSan executables passed (`mappers bus ppu region save-state
mapper-timing rom-source emulation-session cli-dcs`), followed by successful
mapper/source reruns after the final rollback and battery assertions.
Leak detection retains the macOS runtime exception documented above. Formatting
and whitespace checks passed; changed production lines in eleven translation
units passed clang-tidy, including a final full GUI-entry-point check.
Optional asset-dependent cases retain their existing skips. No new commercial
or physical-cartridge compatibility claim is made.

## Taito follow-up: hardware decisions and timing limits

Mapper 33 implements TC0190 and the non-IRQ subset of TC0350; mapper 48 supplies
TC0690 IRQ and mirroring registers. Both use two six-bit 8 KiB PRG registers,
fixed final 16 KiB, two full eight-bit 2 KiB CHR registers and four eight-bit
1 KiB CHR registers. The 2 KiB banks retain odd indices and reach 512 KiB CHR;
the 1 KiB banks reach only the first 256 KiB. Mapper 33 decodes `$8000–$BFFF`
with `$A003`, controlling mirroring in `$8000` bit 6. Mapper 48 uses `$E003`
through `$FFFF` and mirroring at `$E000` bit 6. Sources:
[mapper 33 revision 24359](https://www.nesdev.org/w/index.php?oldid=24359) and
[mapper 48 revision 22585](https://www.nesdev.org/w/index.php?oldid=22585).

The [original TC0690 hardware investigation](https://forums.nesdev.org/viewtopic.php?t=18277)
finds an up counter clocked by read-qualified falling PPU A12 transitions,
with no M2 edge filter. `$C000` sets the raw latch; `$C001` reloads and clears
pending IRQ. `$C002` enables output and `$C003` disables it without clearing the
pending latch. Counting and pending capture continue with output disabled;
reenabling an expired latch asserts output. This deliberately follows the
measurement over the older wiki's MMC3 disable/acknowledge analogy.

The current PPU emits a single dot-260 scanline callback while rendering on
visible/pre-render lines. It does not expose the full read-qualified A12 bus.
Mapper 48 therefore uses explicit compatibility profiles: submapper 0 loads
the count immediately on `$C001`, increments to wrap, and delays output by
22 CPU/M2 cycles from the existing callback. Submapper 1 defers reload to the
next callback and uses six cycles. The latter follows the Jetsons convention
[reported by emulator/database maintainers](https://forums.nesdev.org/viewtopic.php?p=301097).
The extra reload edge and offsets are software compatibility choices, not
new physical chip measurements or guarantees for every timing revision.

In particular, these offsets must not be read as measured ASIC propagation
delays. True per-read PPU A12 edges, pattern-table-dependent edge placement,
mid-scanline interactions and CPU `$2007` clocks remain outside this renderer's
timing model. Existing PPU timing and all other mapper callbacks are unchanged.
Mislabelled mapper-33 dumps are not promoted by hash; they need a corrected
header. Mapper 33 intentionally has no IRQ.

ROM limits are 512 KiB each for PRG and CHR, with complete 8 KiB PRG and 1 KiB
CHR banks. An absent CHR ROM uses up to 8 KiB banked volatile CHR RAM as a
compatibility extension; zero declared size falls back to 8 KiB. PRG RAM is
not mapped, including the implicit legacy default. Explicit NES 2.0 PRG memory,
mixed CHR ROM/RAM, CHR NVRAM, larger memory and unknown submappers are rejected.
Reset preserves CHR RAM and restores deterministic PRG 0/1, zero CHR banks,
header H/V mirroring and cleared IRQ state; hardware power-up is not certified.
New mapper payloads retain snapshot version 20 and do not alter other IDs.

Tests cover bank reachability and aliases, ROM protection, mirroring through
the PPU, IRQ latch/enable/reload/delay rules, DMA clocking, CHR RAM aliases,
corrupt/truncated state rejection, and full CPU IRQ replay under both profiles.
Both IDs join the shared 59-board boot/replay matrix. No commercial-title or
physical-cartridge validation is claimed.

Taito validation: `just build` passed; the regular suite command above reported
42 successful executables, zero failures, and 2,728 assertions in 115 mapper
cases. All six targeted ASan/UBSan executables passed. Formatting and whitespace
checks passed, and clang-tidy reported no diagnostics on `taito_tc.cpp` or the
added registry lines. Optional external-ROM and GPU cases retain their existing
environment-dependent skips.

## X1-005 follow-up: RAM protection and CIRAM wiring

The implementation follows [X1-005 revision 24449](https://www.nesdev.org/w/index.php?oldid=24449),
[mapper 207 revision 24116](https://www.nesdev.org/w/index.php?oldid=24116), and
[pinout revision 22506](https://www.nesdev.org/w/index.php?oldid=22506).
The chip provides three six-bit 8 KiB PRG windows and a fixed final 8 KiB.
Its two 2 KiB CHR selectors ignore bit 0; the four 1 KiB selectors retain all
eight bits. Unlike TC0190, the 2 KiB selectors are aligned 1 KiB indices rather
than full 2 KiB indices.

Register writes decode only `$7EF0–$7EFF` and `$7E70–$7E7F`, because CPU A7 is
unconnected. PRG registers and permission control also have their documented
adjacent-byte aliases. `$7EF7` is not a mirroring alias and is left without an
observable effect, consistent with the
[hardware tests](https://forums.nesdev.org/viewtopic.php?p=278102).
Registers are write-only. Internal RAM is 128 bytes at `$7F00–$7F7F`, mirrored
at `$7F80–$7FFF`. Permission writes must equal `$A3` exactly; every other byte
locks reads and writes. Locked RAM and register reads use CPU open bus.

Mapper 80's `$7EF6` bit 0 selects PPU A11 when clear (horizontal mirroring),
or PPU A10 when set (vertical mirroring). These address-line definitions avoid
confusing nametable arrangement names with mirroring names. Mapper 207 ignores
that register: bit 7 of the first CHR selector routes `$2000/$2400`, and bit 7
of the second routes `$2800/$2C00`. The existing `nametable_address()` hook
preserves both one-screen modes and either physical horizontal page order.
The shared CHR A17 output also remains part of CHR selection; larger CHR images
therefore couple bank selection and CIRAM, while 128 KiB images fold that bit.

Supported geometry is up to 512 KiB PRG ROM and 256 KiB CHR ROM. An absent CHR
ROM uses up to 8 KiB volatile banked CHR RAM as a compatibility extension. The
on-chip 128-byte RAM always exists; legacy implied 8 KiB WRAM is not mapped.
NES 2.0 can leave PRG memory unspecified or declare one 128-byte volatile or
nonvolatile region. Explicit NVRAM requires the battery flag. Larger/separate
PRG memories, CHR NVRAM, mixed CHR ROM/RAM and unknown submappers are rejected.
The external WRAM-select pin is not used to fabricate an additional RAM board.

Battery-backed boards expose precisely 128 bytes for `.sav` persistence even
when CPU access is locked. Reset preserves both RAM arrays, locks internal RAM,
restores deterministic PRG banks 0/1/2 and zero CHR selectors, and retains header
H/V startup policy for mapper 80. Mapper 207 starts on CIRAM page zero. Physical
power-up values are not certified. New payloads leave snapshot version 20 and
existing mapper layouts unchanged.

Tests cover all 256 permission values, every internal RAM byte and its mirror,
register pairs and A7 aliases, CHR alignment and reachability, open bus and write
protection, all mapper-207 CIRAM combinations through the PPU, CPU register-write
programs, exact snapshot replay/rollback, malformed states, and actual 128-byte
battery files with oversized-file rejection. Both IDs join the shared 61-board
boot/replay matrix. No commercial-title or physical-cartridge validation is claimed.

X1-005 validation: `just build` passed; the regular suite command above reported
42 successful executables, zero failures, and 5,321 assertions in 125 mapper
cases. The seven targeted ASan/UBSan executables (`mappers bus ppu region
save-state mapper-timing battery-save`) all passed, using the macOS leak-detection
exception documented above. Formatting and whitespace checks passed; clang-tidy
reported no diagnostics on `taito_x1005.cpp` or the new registry lines. Optional
external-ROM and GPU cases retain their existing environment-dependent skips.

## X1-017 follow-up: PRG wiring, protected RAM and CPU pull-downs

Both IDs follow [X1-017 revision 24125](https://www.nesdev.org/w/index.php?oldid=24125),
[pinout revision 11701](https://www.nesdev.org/w/index.php?oldid=11701), and
[the original hardware tests](https://forums.nesdev.org/viewtopic.php?p=246602).
The [later correction](https://forums.nesdev.org/viewtopic.php?p=246709)
establishes that IRQ control bit 2 must be clear for M2 counting.

Mapper 82 selects PRG banks with bits 2–5 and retains historical dump order,
limited to 128 KiB. Mapper 552 reverses bits 0–5 into PRG A18–A13 and supports
512 KiB. Neither uses register bits 6–7. Three 8 KiB windows switch independently;
the last 8 KiB stays fixed. Registers decode only `$7EF0–$7EFF`, with no A7 or
adjacent-byte aliases. CHR ROM supports 256 KiB in two aligned 2 KiB and four
1 KiB windows. `$7EF6.1` swaps the pattern-table halves; bit 0 selects PPU A11
(horizontal mirroring) or A10 (vertical mirroring) for CIRAM A10.

The physical 5 KiB RAM is unbanked: `$6000–$67FF`, `$6800–$6FFF`, `$7000–$73FF`.
Their independent permission keys are exactly `$CA`, `$69`, `$84` at
`$7EF7/$7EF8/$7EF9`. Other values lock both reads and writes in that region.
`$7400–$7FFF` contains no RAM. ASIC pull-downs ground undriven CPU bits across
the bus, including disabled RAM, write-only ASIC/APU registers and floating
controller bits. PPU internal register latches and driven CPU data remain intact.

IRQ latch writes do not alter the live counter. Clearing count enable reloads
`(latch+2)*16`, or 17 when zero. Acknowledgement reloads `(latch+1)*16`, or 1
when zero, and clears the pending flag. Expiry latches pending on the decrement
to zero; output enable gates it independently, so masking/re-enabling can
reassert without acknowledgement. Every CPU/M2 cycle, including DMA, counts.
The unknown bit-2 clock mode is modeled as paused; post-expiry wrapping is not
measured and the counter saturates because acknowledgement always reloads it.

The ASIC's RAM always exists, including with unspecified header memory. NES 2.0
cannot encode 5 KiB exactly: bounded PRG RAM/NVRAM declarations up to 8 KiB total
are accepted as metadata, while storage, CPU mapping and battery files stay
exactly 5 KiB. Explicit NVRAM requires the battery flag. CHR ROM is required;
CHR RAM/NVRAM, larger memories and unknown submappers are rejected. Reset
preserves RAM, locks all three regions, clears IRQ state, and uses deterministic
PRG 0/1/2, zero CHR banks and header H/V defaults. Hardware power-up values are
not certified. New mapper payloads retain machine snapshot version 20; the
immutable bus policy adds no serialized fields or changes to existing payloads.

Tests cover each PRG register bit, exact decoding, every RAM permission byte,
region boundaries, CHR inversion through PPU registers, CIRAM routing, timed
and untimed CPU pull-downs, timer reload/pause/output/acknowledgement, DMA and
interrupt sampling, real CPU IRQ service, active/pending snapshot replay,
malformed/truncated state rejection, machine rollback and actual 5 KiB battery
files. Both IDs join the shared 63-board boot/replay matrix. No commercial-title
or physical-cartridge validation is claimed.

X1-017 validation: `just build` passed; the regular suite command above reported
42 successful executables and zero failures, including 8,899 assertions in
136 mapper cases. The seven targeted ASan/UBSan executables (`mappers bus ppu
region save-state mapper-timing battery-save`) passed with the documented macOS
leak-detection exception. Both synthetic mapper images ran two frames through
`mapperbus-cli --frames 2`. Formatting, whitespace and clang-tidy checks passed
for the new implementation and changed bus/registry lines. The isolated agent
documentation validator reported zero errors and 52 existing warnings.

## SS88006 follow-up: nibble registers and variable-width IRQ

Mapper 18 follows [specification revision 3918](https://www.nesdev.org/w/index.php?oldid=3918)
and [pinout revision 22246](https://www.nesdev.org/w/index.php?oldid=22246).
Only CPU A12–A14 and A0–A1 select registers, so writes decode with `$F003`.
The three switchable 8 KiB PRG registers have six bits, split into low four
and high two; the final 8 KiB bank is fixed. Eight 1 KiB CHR registers use
two four-bit writes each. Register data bits 4–7 are ignored by the mapper.
PRG ROM supports 512 KiB, CHR ROM 256 KiB. ROM writes do not cause bus conflicts.
`$F002` selects horizontal, vertical, lower single-screen or upper single-screen
CIRAM using its low two bits.

The optional RAM window at `$6000–$7FFF` has independent chip-enable and
write-permission bits at `$9002.0/.1`, both active high. Disabled reads leave
CPU data lines undriven. NES 2.0 sizes determine whether RAM exists; up to
8 KiB total volatile/nonvolatile memory is supported, with NVRAM following
volatile bytes. Smaller declarations mirror within the CPU window. Legacy
iNES's implied 8 KiB is retained as a compatibility default because that header
cannot reliably distinguish RAM-equipped boards; its battery flag selects
NVRAM. Only the nonvolatile portion is exposed for persistence, independent of
CPU permissions. NVRAM requires the battery flag. Unknown submappers, larger
memories, CHR RAM/NVRAM and four-screen nametable requests are rejected.

Four nibble writes at `$E000–$E003` set a full 16-bit IRQ reload latch without
changing the active counter. Any `$F000` write copies all 16 latch bits and
acknowledges. `$F001` writes acknowledge and select count enable plus 4/8/12/16-bit
width; the shortest selected width takes priority. Every M2, including CPU
stalls and DMA, decrements only the selected low bits. An IRQ latches on
underflow; upper bits survive narrow modes, and counting keeps wrapping after
expiry. Disabling counting pauses the live value. CPU interrupt service alone
does not acknowledge the mapper.

Reset retains RAM but locks it, clears IRQ state, selects PRG banks 0/1/2 and
zero CHR banks, and uses header H/V mirroring. These are deterministic emulator
defaults; physical power-up state is not certified. New mapper payloads retain
machine snapshot version 20 and do not alter existing IDs or bus timing.

Some boards add a separate µPD7755C/µPD7756C ADPCM device with its own mask-ROM
samples. `$F003` controls that device; the SS88006 ASIC has no audio generator.
The cartridge pipeline currently supplies PRG/CHR media and DCS options, but
does not attach this speech media. This implementation models the mapper
without that optional device: `$F003` has no observable effect and no substitute
samples or speech audio are generated. Shared ADPCM emulation and media loading
remain a separate implementation item.

Tests cover both halves of every bank register and address aliases, high-bit
masks, ROM protection, PPU CIRAM routing, all 256 RAM-control bytes, absent and
mirrored RAM, mixed volatile/NVRAM persistence, every IRQ size-bit combination,
repeated wrapping, preserved upper bits, full reloads, live-latch independence,
DMA/sample timing, CPU IRQ service and active/pending snapshot replay. Corrupt
fields, truncation and oversized battery files are rejected. Mapper 18 joins
the shared 64-board boot/replay/rollback matrix. No commercial-title or
physical-cartridge validation is claimed.

SS88006 validation: `just build` passed; `meson test -C buildDir --print-errorlogs
--suite=mapperbus` reported 42 successful executables, zero failures, and 10,132
assertions in 148 mapper cases. The seven targeted ASan/UBSan executables
(`mappers bus ppu region save-state mapper-timing battery-save`) passed with the
documented macOS leak-detection exception. NES 2.0 and legacy iNES versions of
the original CPU IRQ program each ran two frames through `mapperbus-cli` and
persisted an 8 KiB battery file containing the expected RAM write. Formatting,
whitespace and clang-tidy checks passed for the implementation and added
registry lines. The isolated agent documentation validator reported zero
errors and 52 existing warnings.

## Jaleco discrete follow-up: edge latches and legacy dump ordering

The implementation follows [mapper 72 revision 24541](https://www.nesdev.org/w/index.php?oldid=24541),
[mapper 92 revision 4687](https://www.nesdev.org/w/index.php?oldid=4687), and
[mapper 101 revision 21360](https://www.nesdev.org/w/index.php?oldid=21360).
JF-17 and JF-19 switch 16 KiB PRG and 8 KiB CHR windows. Mapper 72 switches
`$8000–$BFFF` and fixes the last bank at `$C000`; mapper 92 fixes bank zero
at `$8000` and switches `$C000–$FFFF`. All `$8000–$FFFF` writes share the
same data-bus decoder. PRG control bit 7 and CHR control bit 6 clock their
respective bank latches only on a zero-to-one transition. Held-high writes
do not update banks; a falling write rearms the relevant latch independently.

PRG ROM remains enabled on writes, so the model ANDs CPU data with the byte
driven by the current PRG mapping. That single pre-write sample determines both
bank data and the remembered control levels, including simultaneous PRG/CHR
updates. A conflict can therefore suppress or rearm either clock. Mapper 72
uses three PRG bank bits (128 KiB); mapper 92 uses four (256 KiB). Both use
four CHR bank bits (128 KiB). Mirroring is fixed by the header's H/V wiring.

Mapper 101 preserves the historical JF-10 dump interpretation: an ordinary
binary CHR bank number, rather than mapper 87's reversed low pair. Its
`$6000–$7FFF` write-only selector uses all eight bits, including the documented
oversize extension up to 2 MiB CHR. PRG is fixed 32 KiB, or mirrored 16 KiB;
there are no bus conflicts. No dump reordering or title/hash heuristic is used.

All three require CHR ROM and have no PRG RAM, CHR RAM, battery memory or IRQ.
Legacy implied WRAM is ignored; explicit NES 2.0 RAM, NVRAM/battery flags,
unknown submappers and non-H/V nametable wiring are rejected. Bank-aligned ROM
sizes are bounded by the decoded lines. Reset uses deterministic zero bank and
command defaults; physical power-up/reset latch behavior is not certified.
Snapshots store both banks and the previous P/C levels, preserving the next
edge after restoration. Fixed board wiring is immutable, and no existing mapper
payload or machine snapshot version 20 layout changes.

Some JF-17/JF-19 boards also connect bits 4/5 and low address bits to an external
µPD7756C speech device. Playback and sample-media loading remain unsupported,
as on optional speech-equipped mapper-18 boards. Speech control bits do not
change banking or fixed mirroring in this base-board model.

Tests cover every decoded bank bit, independent rising/held/falling strobes,
simultaneous updates using old PRG outputs, conflicts on data and control bits,
mapper-101 full-width selectors and exact address range, fixed PRG/CIRAM,
ROM write protection, open CPU bus, geometry rejection and malformed snapshots.
Original 6502 programs write registers and read CHR through PPU registers into
CPU RAM, then replay a whole-machine state saved with a strobe held high.
All three IDs join the shared 67-board boot/replay/rollback matrix.
No commercial-title or physical-cartridge validation is claimed.

Jaleco discrete validation: `just build` passed; `meson test -C buildDir
--print-errorlogs --suite=mapperbus` reported 42 successful executables, zero
failures, and 10,483 assertions in 155 mapper cases. The seven targeted
ASan/UBSan executables (`mappers bus ppu region save-state mapper-timing
battery-save`) passed with the documented macOS leak-detection exception.
NES 2.0 and legacy iNES versions of each synthetic CPU/PPU program completed
two frames through `mapperbus-cli` (six successful runs). Formatting,
whitespace and clang-tidy checks passed for the new implementation and added
registry lines. The isolated agent documentation validator reported zero
errors and 52 existing warnings.

## LROG017 follow-up: shared cartridge VRAM and actual CIRAM

Mapper 77 follows [revision 24538](https://www.nesdev.org/w/index.php?oldid=24538)
and the LROG017 identification in
[the original cartridge dump notes](https://forums.nesdev.org/viewtopic.php?t=3755).
Every `$8000–$FFFF` write selects 32 KiB PRG with the low nibble and 2 KiB CHR
ROM with the high nibble. CPU data is ANDed with the byte from the pre-write
PRG mapping before either bank changes. The four-bit selectors address up to
512 KiB PRG ROM and 32 KiB CHR ROM; an entire 32 KiB PRG window switches.

The physical 8 KiB cartridge SRAM maps pages 1/2/3 at PPU `$0800–$1FFF` and
page 0 at `$2000–$27FF`. Pattern and nametable regions do not alias. `$2800–$2FFF`
uses the console's actual 2 KiB CIRAM, routed to its physical pages 0 and 1.
The reported arrangement is always four-screen, independent of header H/V
flags. Cartridge SRAM remains in mapper state; console CIRAM remains in PPU
state and follows the existing PPU reset policy. Mapper reset restores zero
bank selectors while retaining all cartridge SRAM. These are deterministic
emulator defaults; physical power-up/reset behavior is not certified.

The board fully decodes PPU address regions and leaves `$3000–$3EFF` undriven.
The new immutable `maps_nametable()` policy runs before normal `$3000` folding.
Unmapped writes are ignored. Reads use the low byte of the raw PPU address,
following [external video-memory open-bus behavior, revision 23814](https://www.nesdev.org/w/index.php?oldid=23814).
The video-memory bus multiplexes address/data; it is distinct from both the
PPU CPU-facing I/O latch and the PPUDATA read buffer. This models the documented
usual electrical result, without analog decay or clone-specific pull-ups.
PPUDATA buffering remains intact. Internal `$3Fxx` palettes still bypass this
decode; palette reads refill PPUDATA from the underlying `$2Fxx` CIRAM path.

The mandatory SRAM exists with legacy or unspecified NES 2.0 memory metadata.
Volatile CHR-size metadata may be zero, 6 KiB, or rounded-up 8 KiB, all describing
the same physical SRAM; the NES 2.0 exponent cannot encode its 6 KiB pattern
portion exactly. No extra CPU RAM or battery memory is supplied. Explicit
NES 2.0 PRG RAM, NVRAM/battery flags, larger/different RAM declarations,
unsupported ROM geometries and nonzero submappers are rejected. Legacy implied
WRAM is ignored. CHR ROM is required. There is no IRQ or expansion audio.

Snapshots store two bank bytes and exactly 8 KiB cartridge RAM. Corrupt bank
fields and truncation fail validation; full-machine load rolls back on failure.
The PPU decode policy is immutable, and the low-address result adds no mutable
bus latch. Machine version 20 and all existing component layouts remain
unchanged, including the earlier snapshot import paths.

Tests cover all bank bits, conflicts on both selectors, every cartridge SRAM
byte, ROM protection, four independent nametables and attributes, exact CIRAM
ownership, unmapped reads/writes, palette buffering and read-buffer/I/O-latch
separation. Rendered frames verify ROM and RAM background tiles from all four
nametables. An original CPU program writes both RAM types, switches banks,
reads every mapped region and an unmapped address through PPU registers, then
replays snapshots and rejects truncated/corrupt states atomically. Mapper 77
joins the shared 68-board boot/replay/rollback matrix. No commercial-title or
physical-cartridge validation is claimed.

LROG017 validation: `just build` passed; `meson test -C buildDir --print-errorlogs
--suite=mapperbus` reported 42 successful executables, zero failures, and 20,877
assertions in 162 mapper cases. The seven targeted ASan/UBSan executables
(`mappers bus ppu region save-state mapper-timing battery-save`) passed with the
documented macOS leak-detection exception. Both NES 2.0 and legacy iNES versions
of the original CPU/PPU program ran two frames through `mapperbus-cli`.
Formatting, whitespace and clang-tidy checks passed for the new implementation
and changed PPU/registry lines. The isolated agent documentation validator
reported zero errors and 52 existing warnings.

## Namco 175/340 follow-up: submappers and exact RAM geometry

Mapper 210 follows [revision 23707](https://www.nesdev.org/w/index.php?oldid=23707)
and [the submapper convention, revision 24451](https://www.nesdev.org/w/index.php?oldid=24451#210:_Namco_175_and_340).
NES 2.0 submapper 1 selects Namco 175, submapper 2 selects Namco 340. Submapper
zero selects 175 when explicit volatile or nonvolatile PRG memory is declared,
and 340 otherwise. Legacy iNES selects 175 with a battery flag and 340 without
one; its implicit WRAM default alone does not select 175. No title/hash rule
or automatic reassignment of mislabeled mapper-19 images is used.

Both ASICs have three switchable six-bit 8 KiB PRG selectors, with the final
8 KiB fixed, and eight full-width 1 KiB CHR-ROM selectors. Register aliases are
2 KiB wide. PRG supports up to 512 KiB, CHR up to 256 KiB. All CHR values,
including `$E0–$FF`, select ROM; they never substitute CIRAM. There are no bus
conflicts. Namco 175 keeps header H/V wiring fixed and ignores the top bits
of `$E000`. Namco 340 uses those bits for lower single-screen, vertical, upper
single-screen or horizontal mirroring, independently of the six PRG bank bits.

Only Namco 175 has optional PRG RAM. `$C000–$C7FF` bit 0 gates both reads and
writes at `$6000–$7FFF`; other bits are ignored. Disabled or absent RAM leaves
the CPU bus undriven. Declared power-of-two sizes up to 8 KiB mirror throughout
the CPU window. The 2 KiB and 8 KiB cases are distinct: the former aliases
`$6000/$6800/$7000/$7800`, while the latter retains four separate 2 KiB pages.
Volatile bytes precede NVRAM when both are declared; only actual NVRAM bytes
are persisted, without mirrored copies, even with CPU access disabled.

**Legacy RAM-size limit:** the standard iNES size unit cannot distinguish the
2 KiB battery RAM in Family Circuit '91 from an 8 KiB board. Legacy battery
images retain the parser's 8 KiB fallback. The 2 KiB behavior requires corrected
NES 2.0 NVRAM metadata; no game-content heuristic is supplied. Explicit Namco
175 can also be RAM-free. Namco 340 rejects declared PRG memory and battery
storage. NVRAM requires its battery flag, and a NES 2.0 battery flag requires
declared NVRAM. Unknown submappers, oversized/non-power-of-two combined RAM,
CHR RAM/NVRAM, unsupported ROM sizes and non-H/V startup headers are rejected.

Both variants lack N163 IRQ, sound, internal RAM and ROM nametables. Reads of
the absent expansion ports use CPU open bus; writes and clocks have no effect.
`$C800–$DFFF` and `$F800–$FFFF` are ignored. Namco 340 ignores `$C000–$C7FF` as
well. This permits the documented N163 initialization and nametable-register
writes to remain harmless on the 340, while retaining the real 175 RAM gate.

Reset preserves RAM, disables its CPU gate, selects PRG banks 0/1/2, zeroes CHR
selectors and restores header H/V startup mirroring. These are deterministic
emulator defaults; physical startup values are not certified. Snapshots include
banks, mirror control, RAM enable and fixed-size RAM storage. Validation rejects
invalid banks/flags, changed fixed mirroring on 175 and a fabricated RAM-control
register on 340. New payloads leave machine version 20, mapper 19, and all
existing timing and serialization layouts unchanged.

Tests cover both explicit variants and fallback profiles, bank masks and
register-range boundaries, ROM protection, all CIRAM modes through the PPU,
every RAM-gate byte, 2/8 KiB aliases, mixed volatile/NVRAM offsets, absent N163
ports and clocks, reset retention, CPU/PPU execution and snapshot replay.
Actual 2/8 KiB battery files restore with RAM locked; oversized files and corrupt
states are rejected. Mapper 210 joins the shared 69-board boot/replay/rollback
matrix. No commercial-title or physical-cartridge validation is claimed.

Namco 175/340 validation: `just build` passed; `meson test -C buildDir
--print-errorlogs --suite=mapperbus` reported 42 successful executables, zero
failures, and 22,233 assertions in 173 mapper cases. The seven targeted
ASan/UBSan executables (`mappers bus ppu region save-state mapper-timing
battery-save`) passed with the documented macOS leak-detection exception.
Seven `mapperbus-cli --frames 2` runs covered both explicit ASICs, both automatic
profiles and legacy battery/non-battery headers; battery cases retained exactly
2 KiB or 8 KiB with the expected RAM write after the CPU gate was disabled.
Formatting, whitespace and clang-tidy checks passed for the new implementation
and added registry lines. The isolated agent documentation validator reported
zero errors and 52 existing warnings.

## MMC3 variant follow-up: raw CHR outputs and shared IRQ behavior

Mapper 118 follows [TxSROM revision 24058](https://www.nesdev.org/w/index.php?oldid=24058).
CHR A17 selects CIRAM A10 before any ROM-size wrapping. Normal mode uses
R0/R1 for the two nametable pairs; inverted mode uses R2–R5 for four independent
tables. `$A000` mirroring writes are disconnected. Pattern reads retain bit 7,
including coupled nametable/CHR selection with 256 KiB third-party CHR ROM.
Arbitrary table arrangements use `nametable_address()` rather than a single
H/V enum; PPU `$3000` mirrors use the same CIRAM routing.

Mapper 119 follows [TQROM revision 24251](https://www.nesdev.org/w/index.php?oldid=24251).
Raw CHR bit 6 chooses ROM or the mandatory 8 KiB RAM. RAM banks alias on their
lowest three bits; aligned 2 KiB banks and inversion affect both chips.
ROM writes are ignored. The documented extension permits 512 KiB PRG and
128 KiB CHR ROM: raw bit 7 supplies ROM bank bit 6, packing the two ROM halves
into selectors `$00–$3F` and `$80–$BF`. Original 16/32/64 KiB ROMs ignore that
extra address bit. Ordinary MMC3 H/V mirroring remains active.

[TxROM revision 24047](https://www.nesdev.org/w/index.php?oldid=24047) distinguishes
RAM-free TLSROM and TQROM from TKSROM's 8 KiB PRG RAM. Mapper 118 honors
explicit NES 2.0 RAM/NVRAM sizes, mirrors smaller power-of-two combined storage,
and persists only NVRAM. Legacy battery headers use 8 KiB; legacy non-battery
headers select RAM-free TLSROM despite the parser's implicit WRAM default.
Mapper 119 has no PRG RAM or battery storage; its CHR RAM remains volatile.
Both require CHR ROM, aligned PRG/CHR banks, H/V startup metadata and submapper
zero. Unsupported geometry, extra memory and inconsistent battery metadata fail
creation. NES 2.0 may omit TQROM's mandatory CHR RAM size or declare exactly 8 KiB.

The variants share MMC3 register aliases, six-bit PRG selectors, PRG mode swaps,
RAM protection and the existing MMC3B reload/decrement/acknowledge logic.
**Timing limit:** production IRQ clocks remain the renderer's dot-260 callback
on rendered scanlines. Filtered electrical A12 edges, alternate MMC3 revisions
and title-specific raster timing are not certified. No PPU timing model changes
were made. Reset retains RAM with deterministic bank defaults.

New payloads store byte flags, raw banks, RAM and IRQ progress, reconstruct
derived offsets and reject invalid fields. Mapper 4's historical payload and
machine snapshot version 20 are unchanged. Tests cover all TQROM selector bytes,
both inversion modes, CIRAM and PPUDATA through the PPU, CPU execution, IRQ
replay, exact battery files and full-machine rejection/rollback. Both IDs join
the shared 71-board snapshot matrix. Commercial-title validation remains open.

MMC3 variant validation: `just build` and all 42 regression executables passed.
The mapper executable passed 24,250 assertions in 184 cases. Seven targeted
ASan/UBSan suites passed, with leak detection disabled for the documented macOS
runtime limitation; the final expanded mapper tests also passed under sanitizers.
Nine two-frame CLI runs covered legacy and NES 2.0 headers, original and enlarged
ROMs, RAM-free boards, and exact 2/8 KiB battery files with the expected write
retained after CPU RAM access was disabled. Formatting, whitespace and scoped
clang-tidy checks passed. Agent documentation validation reported zero errors
and the existing 52 warnings.

## RAMBO-1 follow-up: measured IRQ behavior and opt-in pin observations

Mappers 64/158 share `Rambo1`. Banking follows
[RAMBO-1 revision 23936](https://www.nesdev.org/w/index.php?oldid=23936) and
[mapper 158 revision 21299](https://www.nesdev.org/w/index.php?oldid=21299), with
the corrections measured by researcher teaguecl in November 2026. The pinned
[emulator guide](https://github.com/teaguecl/nes_tengen_rambo1_research/blob/544ca1b22efe18ce233f521b5a4a9c1def12c697/docs/emulator_guide.md)
and [individual findings](https://github.com/teaguecl/nes_tengen_rambo1_research/blob/544ca1b22efe18ce233f521b5a4a9c1def12c697/docs/findings.md)
take precedence over the older wiki's IRQ formulas and eight-bit PRG claim.
Those measurements cover one RAMBO-1 chip in one NTSC console; revision and
PAL electrical differences remain unverified. Research documentation is CC BY
4.0; the implementation and in-repository synthetic tests are original.

Three five-bit PRG registers map 8 KiB windows; P swaps R6/RF, and `$E000`
selects physical bank `$1F`. K enables individual R0/R8/R1/R9 CHR windows;
otherwise R0/R1 align to 2 KiB. C swaps the CHR halves. R8/R9 retain writes
with K clear. Mapper 158 derives CIRAM A10 from raw CHR A17, including K and
R8/R9, before ROM wrapping; it ignores `$A000` mirroring writes. Mapper 64
uses ordinary switchable H/V wiring. Neither board provides RAM or audio.

The CPU timer first expires `4(L+1)+2` M2 cycles after `$C001`, then every
`4(L+1)` cycles. Switching from A12 mode with an incomplete filter advances
the first expiry by four cycles. A12 needs 16 sampled low M2 falls, supports
the full-filter swallowed edge and delayed enable check, and produces only
one latch-zero expiry per explicit reload. Cold start uses PRG `$1F`, K set,
CHR zero, vertical wiring, latch `$FF` and asserted IRQ. Console reset retains
the mapper. The uncertain power-on timer phase uses a deterministic 1018-cycle
first expiry; analog retention across short power interruptions is not modeled.

`clock_cpu_falling_edge()` runs after CPU writes and the cycle's PPU dots,
including DMA. Existing `clock_cpu()` users keep their timing. An opt-in
`observe_ppu_a12()` supplies fetch-address cadence, 8x16 tile-bank selection,
idle VRAM-address pins and PPUADDR/PPUDATA changes. It is
independent of cached pixel fetches and changes no Loopy increments or existing
dot-260 IRQ users. This remains a per-pixel PPU model, not a complete electrical
fetch pipeline. A12/filter/timer state lives in the mapper payload; machine
version 20 and existing PPU/bus/mapper payloads remain unchanged.

Factories require submapper zero, 32–256 KiB bank-aligned PRG, nonempty
bank-aligned CHR up to 256 KiB, and no declared RAM, battery or four-screen
VRAM. Legacy implicit WRAM is ignored. Invalid state flags, banks, filter
values and counter phases fail loading, with full-machine rollback.

Independent validation used the research project's six MIT-licensed
[hardware test ROMs](https://github.com/teaguecl/nes_tengen_rambo1_research/blob/544ca1b22efe18ce233f521b5a4a9c1def12c697/rom/README.txt),
kept outside the repository. All six reported status `$00`: 62 tests passed,
including G4 CPU-cycle enable/disable races, G5 manual A12 pulses, and G6
rendered raster offsets and repeat counts. G6's informational both-tables-at-
`$1000` probe now reports `01 00 00`, matching the measured console's single
IRQ. Commercial-title and physical-cartridge certification remain open.

Initial RAMBO-1 validation: `just build` passed; all 42 regression executables passed,
including 27,437 assertions in 200 mapper cases. Seven targeted ASan/UBSan
suites passed with the documented macOS leak-detection exception. Original
synthetic tests cover all 256 CPU latch values, M2 enable races, filter boundaries,
K/C layouts, reset retention, both IRQ modes through CPU execution, PPU pin and
CIRAM routing, OAM DMA, malformed fields and snapshot rollback. Both IDs join
the shared 73-board boot/replay matrix. Eight two-frame CLI profiles covered
both IDs, both IRQ modes and legacy/NES 2.0 headers, without fabricated battery
files. Scoped clang-tidy, formatting and whitespace checks passed. Isolated
agent-documentation validation reported zero errors and 52 existing warnings.

### A12 idle-address correction (2026-11-10)

The initial implementation held the final pattern-table A12 level through
vblank, producing zero IRQs in G6's informational upper-table probe. The PPU
instead presents `v` while its rendering fetch circuit idles; see
[PPU rendering revision 23300](https://www.nesdev.org/w/index.php?oldid=23300#PPU_address_bus_contents).
A CPU/PPU trace at G6's reload showed `v=$0800` with the incorrect A12 high.
Using `v` supplies the low interval needed by the 16-M2 filter. The first
post-reload rise is swallowed, then the next frame's qualified rise produces
the single latch-zero expiry. Automatic reloads do not repeat that IRQ.

The opt-in pin model now drives `v` in post-render/vblank as well as when
rendering is disabled, and drives A12 low during the final dummy nametable
fetches and idle dot. Pattern fetch cadence, pixels, scroll increments and
other mappers' dot-260 callbacks retain their behavior. The obsolete CPU-access
exception was removed. No serialized fields changed; machine version remains 20.

The original eight-frame count regression failed with `0 != 1` before the fix
and now requires exactly one IRQ for both mapper 64 and 158. Additional checks
cover the dummy-fetch interval and both polarities of idle `v`. All six external
hardware ROMs still report status `$00`, including all 62 checks and the exact
G6 `INFO 01 00 00` observation.

Correction validation: `just build` and all 42 regression executables passed,
with 27,455 assertions in 202 mapper cases. All seven targeted ASan/UBSan
executables passed across the combined run and isolated mapper retry. The first
concurrent sanitizer run reached the mapper's 30-second timeout; the isolated
retry passed in 17.87 seconds. Eight CLI profiles, scoped clang-tidy, formatting
and whitespace checks passed. The agent-documentation validator reported zero
errors and the existing 52 warnings.

## Bandai follow-up: FCG variants, Xicor serial saves and JUMP2 banking

Mappers 16, 153 and 159 share `BandaiFcg`. Register wiring follows
[mapper 16 revision 3876](https://www.nesdev.org/w/index.php?oldid=3876),
[mapper 153 revision 5112](https://www.nesdev.org/w/index.php?oldid=5112), and
[mapper 159 revision 5171](https://www.nesdev.org/w/index.php?oldid=5171).
Mapper 16 submapper 4 decodes `$6000–$7FFF` and writes the live IRQ counter;
submapper 5 decodes `$8000–$FFFF` and writes a separate latch. Every high-window
IRQ-control write copies that latch, even when disabling. An enable at zero
asserts IRQ immediately. The enabled 16-bit counter decrements every M2,
including DMA, wraps, and retains pending IRQ until a control write acknowledges
it. Submapper zero and legacy mapper 16 accept both register windows with their
respective semantics. Deprecated submappers 1/3 explicitly select 159/153;
Datach submapper 2 is rejected rather than dropping its peripherals.

CHR-ROM boards provide eight full-width 1 KiB registers, a four-bit 16 KiB PRG
register and physical fixed bank 15, plus vertical, horizontal and either
single-screen CIRAM arrangement. Mapper 153 instead provides unbanked 8 KiB CHR
RAM and 8 KiB battery WRAM gated by `$800D.D5`. Its first four CHR registers
supply PRG A18; current PPU A10/A11 selects which register drives both PRG
windows. The opt-in `observe_ppu_address()` seam supplies nametable, attribute,
pattern, sprite and idle address pins independently of cached pixel reads.
PPUADDR/PPUDATA changes also update it. Existing A12 observers retain their
cadence; no Loopy prefetch increments, shifters, serialized PPU fields or machine
version changes were introduced. Machine version remains 20. The pin model
retains the per-pixel renderer's documented timing approximations.

`SerialEeprom` models the original Xicor
[X24C01, document 9900-2002.1](https://datasheet.octopart.com/X24C01P-Intersil-datasheet-181540502.pdf)
and [X24C02, document 3838-1.2](https://docs.rs-online.com/d895/0900766b80029b1c.pdf).
Both use MSB-first bytes and four-byte page buffers. X24C01's initial byte is a
seven-bit word address plus R/W; X24C02 uses the wired `$50` slave address and a
separate word-address byte. START/STOP, slave ACK, master ACK/NACK, random/current
and sequential reads, local page wrapping and whole-device read wrapping are
covered. Register bit 7 releases the host SDA driver for reads, including ACK
polling when bit 6 is low; this matches the primary
[Kazzo dumper discussion](https://forums.nesdev.org/viewtopic.php?start=705&t=7912).
Only CPU D4 is driven; other bits retain open bus. No game-specific bit reversal
or mapper reassignment heuristic is used.

STOP begins a deterministic 9000-M2 programming interval. The device NACKs
while busy and commits the page at completion. This approximates the original
parts' typical 5 ms delay within their 10 ms maximum across supported regions;
it is not an analog EEPROM timing model. Repeated START abandons an uncommitted
page, and incomplete data bytes do not program. A file flush during programming
contains the previously committed bytes; snapshots retain pending page data,
remaining programming cycles and partial serial transfers. Soft reset retains
EEPROM progress, committed memory and WRAM, while clearing mapper banks, gates
and IRQ state. Startup register defaults are deterministic emulator policy.
Fresh WRAM is `$FF`, avoiding the documented all-zero Famicom Jump II startup
trap; this is not a measured SRAM power-on pattern.

Factories accept 32–256 KiB bank-aligned PRG and 1–256 KiB bank-aligned CHR ROM
for 16/159, or up to 512 KiB PRG and no CHR ROM for 153. NES 2.0 declares no
volatile PRG RAM, no CHR NVRAM, and exactly 128 bytes (159), optional 256 bytes
(16), or 8192 bytes (153) of PRG NVRAM with a matching battery flag. Mapper 153
accepts an omitted or explicit 8 KiB CHR-RAM declaration. Legacy implicit WRAM
metadata is ignored: battery mapper 16 uses 256-byte EEPROM, 159 always has
128-byte EEPROM, and 153 always has 8 KiB WRAM. File persistence still follows
the legacy battery flag. Unsupported submappers, four-screen VRAM and conflicting
memory declarations fail creation. Datach uses the separate mapper-157 implementation below.

Validation: `just build` and `meson test -C buildDir --print-errorlogs
--suite=mapperbus` passed all 42 executables; the mapper executable reported
28,196 assertions in 215 cases. Seven targeted ASan/UBSan suites passed with
`--num-processes 1` and the documented macOS `detect_leaks=0` exception.
Focused coverage includes both IRQ variants through CPU execution and OAM DMA,
all CHR registers and mirroring modes, PPU-driven outer banking despite cached
pixel reads, open-bus D4, serial page/address wrapping, ACK polling, partial
receive/send snapshots, programming across reset, exact battery files, invalid
metadata, malformed state and atomic full-machine rollback. All three IDs join
the shared 76-board boot/replay matrix. Eleven two-frame CLI profiles exercised
all three IDs, aliases, legacy/NES 2.0 headers and optional EEPROM; persistent
files had exactly 128, 256 or 8192 bytes and the expected programmed byte, while
RAM-free profiles produced no save file.

As a regression check for the extended PPU observer, all six external RAMBO-1
hardware ROMs again reported status zero and all 62 checks passed, including
G6 `INFO 01 00 00`. Scoped clang-tidy, `just format-check` and whitespace checks
passed. Isolated agent-documentation validation reported zero errors and the
existing 52 warnings. The initial generic component-replay fixture assumed
that its post-restore CHR read could not alter state; it now captures the
sampled PPU address before saving, consistently on both replay paths. No
existing check was removed. No Bandai commercial-title or physical-cartridge
validation is claimed.

## Datach follow-up: reader memory, dual clocks and optical barcode input

Mapper 157 uses a separate `Datach` board. Its hardware mapping follows
[NESdev revision 21310](https://www.nesdev.org/w/index.php?oldid=21310) and the
[BA-BAR circuit tracing](https://seesaawiki.jp/famicomcartridge/d/Bandai%20Datach).
PRG is 16 KiB switchable plus the last bank fixed; 8 KiB CHR RAM is unbanked.
Four mirroring modes and LZ93D50 IRQ semantics reuse the original Bandai behavior.
The extracted `BandaiIrq` helper preserves the existing six-byte serialized
counter layout in mappers 16/153/159. No existing mapper payload, PPU payload,
or machine version changes; snapshots remain version 20.

The reader always contains a 256-byte X24C02. A cartridge may additionally
contain a 128-byte X24C01; only this optional chip is described by NES 2.0 PRG
NVRAM and the battery flag. `$800D` controls the internal SCL and shared SDA.
Register bit 3 at `$8000–$8003`, selected by PPU A10/A11, supplies external SCL.
The opt-in full-address observer includes nametable/attribute/pattern and idle
pins. Direction bit 7 enables CPU D4 (both open-drain EEPROM outputs ANDed)
and D3 (scanner); the other bits retain open bus. The serial helper samples the
resolved shared bus and propagates peer-output transitions with independent
clock lines. Original Xicor page, protocol and approximate programming behavior
remain as documented in the Bandai section.

`scan_barcode(digits, module_cycles)` supplies a clean EAN-8, UPC-A or EAN-13
waveform using [GS1 General Specifications 9.0, section 5.2.2](https://ref.gs1.org/standards/genspecs/9.0.0/).
It emits guard patterns, leading-digit parity and eleven white modules at either
end, with dark bars low on D3. The supplied check digit is encoded unchanged;
checksum acceptance belongs to the game. A module lasts 1–1,000,000 M2 cycles,
default 1000; clocking includes DMA. This is a deterministic synthetic swipe,
not a measurement of a person's speed or optical sensor response. Physical
polarity, noisy scans, reverse scans and commercial-game acceptance remain
unverified. Reset retains an ongoing swipe and independent EEPROM activity.
Malformed input and overlapping scan requests fail without changing the scan.

Reader persistence is independent of the cartridge battery flag. App loaders
share `<application data>/devices/datach.sav` across raw, ZIP and 7z games;
optional cartridge bytes keep the normal sibling/hash `.sav` path. CLI
`--datach-save PATH` selects a reader file explicitly. Direct core file loading
uses sibling `datach-unit.sav`; the parsed-cartridge API accepts a separate third
path, with empty paths retaining memory only. Device memory survives switches
and unload/reload within an emulator. Canonicalized paths identify the same live
device before loading an older on-disk image. Unit files must contain exactly
256 bytes, and unit/cartridge path aliases are rejected. Outgoing save failures
prevent replacement; staged machines suppress writes. Snapshots include both
EEPROM protocols and committed bytes, pending pages, clock selection, IRQ,
CHR RAM and the partial swipe; cache/path policy is outside the machine blob.

Factories require submapper zero, 32–256 KiB PRG aligned to 16 KiB, no CHR ROM,
no CHR NVRAM and omitted or 8 KiB CHR RAM. NES 2.0 allows no volatile PRG RAM
and only 0/128 bytes of cartridge NVRAM with a matching battery flag. Legacy
battery headers select the optional chip and ignore implicit WRAM. Deprecated
mapper 16 submapper 2 still requires an explicit mapper-157 header.

The headless CLI schedules one scan using `--barcode DIGITS`, zero-based
`--barcode-frame N` (default 0) and `--barcode-cycles N` (default 1000).
Use a later frame when a game needs time to reach its scanning screen. Core
clients may submit subsequent scans after the previous one finishes. Graphical
frontends automatically persist the reader, but have no barcode-entry control yet.

Validation: `just build` and `meson test -C buildDir --print-errorlogs
--suite=mapperbus` passed all 43 tests, including the new CLI Datach test.
The mapper executable reported 28,897 assertions in 225 cases; mapper 157 joins
the shared 77-board boot/replay matrix. Eight targeted ASan/UBSan suites passed
serially (`mappers bus ppu region save-state mapper-timing battery-save rom-source`,
`--num-processes 1`) with the documented macOS `detect_leaks=0` exception.

Original test programs cover CPU IRQ execution and DMA, both EEPROMs' busy
ACK polling and reset retention, shared SDA with an active peer, resuming partial
reads, a literal EAN-13 waveform, snapshot replay mid-transfer/mid-swipe,
malformed-state rollback, optional-chip metadata and PPU-selected external SCL.
Persistence checks cover exact file sizes and bytes, game switches, unload/reload,
restart, memory-only devices, staged-write suppression, incorrect incoming unit
files, aliases and outgoing write failure. A CLI case first exposed different
normalization of macOS `/var` and `/private/var` paths when both save files did
not yet exist; both paths now use weak canonicalization, and that case passes.

CLI validation covers 18 barcode launches (EAN-8/UPC-A/EAN-13 at three module
speeds, legacy/NES 2.0, raw and archive sources), six idle launches and invalid
arguments/media. Raw/ZIP/7z app tests share a seeded reader image and preserve
optional cartridge EEPROM separately. These tests use explicit temporary storage
paths; the initial archived CLI fixtures that reached the macOS application data
folder were removed, and the revised tests reran successfully in isolation.

New/changed code passed scoped clang-tidy and formatting/whitespace checks. Full
Emulator analysis still reports the baseline destructor exception-escape and
DiskSaveSnapshot initializer diagnostics, independently reproduced against HEAD;
no new diagnostics remain. Isolated agent-documentation validation reported zero
errors and the existing 52 warnings. No physical Datach reader or commercial-game
compatibility certification is claimed.

## Flash-board follow-up: UNROM 512 and both mapper-111 circuits

Mapper 30 implements [UNROM 512 revision 23183](https://www.nesdev.org/w/index.php?oldid=23183),
including submappers 0–4, legacy battery-bit conflict selection, PRG/CHR latch
wiring, fixed H/V or switchable single-screen CIRAM, submapper-3 H/V control and
submapper-4 LED groups. Raw header nametable bits are retained to distinguish
single-screen (`(flags6 & 9) == 8`) from cartridge four-screen RAM (`== 9`).
Four-screen RAM aliases the final 8 KiB CHR-RAM bank and provides independent
scratch bytes at `$3000–$3EFF`. The existing normalized nametable hooks retain
their behavior; only opt-in boards receive raw upper nametable addresses.
Palette routing and the per-pixel renderer's scroll increments stay unchanged.

CHR-free mapper 111 implements [GTROM revision 24456](https://www.nesdev.org/w/index.php?oldid=24456).
Its 32 KiB PRG bank, two pattern RAM banks and two nametable/bonus RAM banks
follow the latch. Both `$5000–$5FFF` and `$7000–$7FFF` aliases respond to writes
and reads; reads latch the actual external open-bus value, including dummy/DMA
accesses. The new completed-read callback is a no-op for existing boards. Reads
remain undriven. Logical active LED outputs are available through the core.
Accesses use the bus's existing CPU access point, rather than modeling a separate
sub-cycle latch delay. The board's 32 KiB PPU RAM includes all independent upper
nametable bytes; palette addresses continue to use console palette RAM.

CHR-ROM mapper 111 selects the [earlier direct MMC1 circuit, revision 24005](https://www.nesdev.org/w/index.php?oldid=24005).
One write commits a register, including consecutive CPU writes; bit 7 is data,
not a serial reset command. Six-bit CHR registers address up to 256 KiB in 4/8 KiB
modes. Standard PRG modes, mirroring and optional 8 KiB PRG RAM are preserved.
Mapper 1's serial writes, consecutive-write filtering and payload layout remain
unchanged. Variant selection uses the documented CHR-ROM presence, with no ROM
hash or title heuristic. All existing mapper/PPU/bus payloads and machine version
20 are retained; only newly registered board payloads are added.

`Sst39sf040` implements the [Microchip DS20005022F](https://ww1.microchip.com/downloads/aemDocuments/documents/MPD/ProductDocuments/DataSheets/SST39SF010A-SST39SF020A-SST39SF040-Data-Sheet-DS20005022.pdf)
command sequences, A14–A0 command decoding, ID entry/exit, one-to-zero byte
programming, 4 KiB sector erase, chip erase and DQ7/DQ6 busy polling (DQ6 high
first). Writes, including F0 reset, are ignored while busy. Changes commit after
32/36,000/144,000 M2 cycles for byte/sector/chip operations, respectively. These
fixed durations fall within the datasheet maximums at supported NES clocks;
analog completion races, wear and power-loss retention are not modeled. Soft
reset retains the discrete latches, RAM and independently running flash. Startup
latch zero is a deterministic policy, not a measurement of power-up randomness.
Short power-of-two flash images initially mirror to populate the 512 KiB chip;
subsequent writes modify physical flash locations independently.

Flash persistence uses a complete 512 KiB chip image in the normal sibling or
archive-content-hash `.sav`. The battery flag enables UNROM 512 flash, while
GTROM always provides persistent flash even with that flag clear. No PRG-NVRAM
header declaration is required for flash. Source files and immutable content
hashes are preserved. Incoming short/oversized flash saves fail before cartridge
replacement, leaving the active machine intact; explicit battery-save imports
are rejection-safe too. Exports contain committed bytes only. Snapshots retain
command prefixes, ID mode, pending operation/address/data/time, polling phase,
RAM, latch and LED state. Read-only UNROM boards do not serialize their immutable
PRG ROM or create a save. Staged machines retain the existing write-suppression
policy. The direct MMC1 variant uses its ordinary RAM save rather than flash.

Factories accept power-of-two 32–512 KiB flash-board PRG images and no CHR ROM.
UNROM 512 accepts omitted or 8/16/32 KiB CHR RAM, GTROM omitted or 32 KiB total
PPU RAM; explicit PRG RAM/NVRAM and CHR NVRAM are rejected. Submapper 2 conflicts
with flash writes, and submapper 3 has no documented flash-save path, so their
battery flags are rejected. Mapper-111 CHR-ROM profiles instead allow up to
256 KiB PRG and CHR, no CHR RAM, and absent or 8 KiB PRG RAM/NVRAM with matching
battery metadata. Legacy implicit WRAM is ignored for flash boards; legacy
mapper-111 CHR-ROM profiles keep the existing MMC1 8 KiB RAM fallback.

Physical cartridge certification, analog timing and graphical LED indicators
remain open. GTMP3's external YX5200 MP3/WAV player and microSD media are not
implemented. The unpublished Famiclone solder-jumper mode is not inferred from
ordinary GTROM headers. Converted WXN files are not treated as real mapper 30;
the documented mapper-176 conversion remains separate work.

Validation: `just build` and `meson test -C buildDir --print-errorlogs
--suite=mapperbus` passed all 44 tests. The mapper executable reported 30,180
assertions in 236 cases; both IDs join the shared 79-board boot/replay matrix.
All eight targeted ASan/UBSan suites passed serially with the macOS
`detect_leaks=0` exception. After the submapper-3 correction, mapper/bus/PPU
sanitizer suites also passed again. Full-chip snapshot tests bring the mapper
sanitizer run close to the former 30-second limit, so its test timeout is now
60 seconds. No assertion or validation was removed.

Original tests cover JEDEC unlock/address decoding, ID/reset, bit clearing,
DQ6/DQ7 polling, sector/chip erasure, busy snapshots and malformed fields;
all UNROM wiring profiles, raw PPU scratch RAM and palette separation; GTROM
read aliases, DMA halt-bus effects and LED/pattern banks; direct MMC1's full CHR
width and real CPU/PPU execution. RAM-resident CPU programs self-flash both
boards and replay pending operations. Complete-machine corrupt states roll
back atomically. The submapper-3 header-ignore regression failed before the
correction and now passes all four nametable flag combinations.

CLI validation passed eight write/reload launches, eight invalid flash-save
cases and two read-only UNROM profiles, using original raw ROMs and temporary
sibling saves. Raw/ZIP/7z app tests separately verify complete chip persistence,
immutable content hashes and source files. Staged machines do not write;
truncated/oversized saves fail without replacing the active machine. All these
fixtures use temporary storage roots. Scoped clang-tidy reported zero diagnostics
for the changed code; formatting and whitespace checks passed. Isolated agent
standards validation reported zero errors and the existing 52 warnings.
No commercial-title or physical-cartridge compatibility certification is claimed.

## J.Y. Company follow-up: physical strobes and shared ASIC

Mappers 35, 90, 209 and 211 use the original `JyCompany` implementation of
[NESdev ASIC revision 24184](https://www.nesdev.org/w/index.php?oldid=24184).
Mapper 90 inhibits both ROM nametables and extended CIRAM arrangements. Mapper
211 follows 209: the historical always-ROM-nametable interpretation describes
no known PCB. Legacy 35 supplies 8 KiB WRAM; NES 2.0 declarations override that
fallback. Legacy battery-marked boards get 8 KiB persistent WRAM; other legacy
90/209/211 images have no WRAM. Explicit NES 2.0 profiles support absent or 8 KiB
volatile/persistent WRAM, with matching battery metadata.

The four PRG modes include seven-bit reversal, fixed-last-bank selection,
$6000 ROM overlay, and 512 KiB outer banks. CHR supports 8/4/2/1 KiB units,
16-bit registers, 256/512 KiB inner masks and outer banks, optional banked RAM
with the $D002 write gate, and independent MMC4-like latches. B registers supply
direct 1 KiB CHR nametable indices. Normal CIRAM modes follow electrical A10/A11
wiring; ROM/CHR-RAM nametables, mixed CIRAM and four independent CIRAM selectors
use the existing PPU routing hooks. ROM writes are consumed without modifying
CIRAM. Unsupported submappers, four-screen headers, mixed ROM/RAM, CHR NVRAM,
and out-of-range geometry are rejected explicitly.

The new default-noop `observe_cpu_write` hook sees console RAM/I/O, register
writes, and OAM DMA puts before register decoding. This lets a disable-register
write acknowledge an IRQ raised by its own strobe. PPU /RD callbacks are opt-in
and occur once per two-dot fetch, after data retrieval: 170 reads per complete
rendered scanline, plus actual PPUDATA external reads. Palette reads contribute
one hidden nametable refill. Renderer/debugger CHR lookups never clock read IRQs
or change these latches. Triggering fetches return the old bank's byte. Existing
mapper pin observers, ADR-001 scroll increments, bus/PPU payloads and machine
snapshot version 20 remain unchanged.

IRQ modes count M2 rises, unfiltered A12 rises, PPU reads, or all CPU writes.
Both directions, full/eight-step prescalers, XOR loads, sticky pending state,
disable/reset and enable-without-reload are implemented. Exact solder-pad reads
drive only bits 7:6; core clients may call `configure_board_jumpers(0..3)` on the
cartridge before installation (default zero). Arithmetic ports implement an
eight-M2-cycle multiplier, wrapping accumulator and test register, including
the documented partial address decode. No title-specific detection is used.

Known limits: $C007's optional IRQ mode has no published behavior or known game
user; its control bytes are retained and serialized, while normal counting
continues. Multiplier reads before completion return the preceding completed
product; hardware intermediate values remain unverified. The per-pixel PPU
still lacks a full fetch/shifter pipeline, so exact mid-fetch pixel effects
are not certified. Boards lacking CPU A11 wiring are not inferred from titles
or the mapper number. These limits are not claimed as hardware measurements.

Original tests cover the register/mode matrices, physical PPU and DMA strobes,
old-bank latch reads, component/machine snapshot replay and malformed-state
rollback, and exact WRAM files. CLI programs for all four IDs, both NES 2.0
and iNES, verify CPU banking, multiplication, accumulator, IRQ delivery, and
persistent success markers across sixteen launches. All four IDs also join
the shared 83-board boot/replay matrix. Validation passed all 45 regular test
targets and all eight serial ASan/UBSan suites (macOS `detect_leaks=0`). The
mapper executable passed 31,326 assertions across 248 cases. Scoped clang-tidy,
formatting, and whitespace checks passed. Isolated agent documentation validation
reported zero errors and the existing 52 warnings. No commercial-title
certification is claimed.

## J.Y. multicart follow-up: outer address pins

Eight additional NES 2.0 IDs use the existing ASIC implementation with immutable
PCB profiles. Sources are [281 r18938](https://www.nesdev.org/w/index.php?oldid=18938),
[282 r23351](https://www.nesdev.org/w/index.php?oldid=23351),
[295 r23352](https://www.nesdev.org/w/index.php?oldid=23352),
[358 r18941](https://www.nesdev.org/w/index.php?oldid=18941),
[386 r23354](https://www.nesdev.org/w/index.php?oldid=23354),
[387 r23355](https://www.nesdev.org/w/index.php?oldid=23355),
[388 r18944](https://www.nesdev.org/w/index.php?oldid=18944), and
[397 r23356](https://www.nesdev.org/w/index.php?oldid=23356).
Code is original; no external emulator implementation was incorporated.

| IDs | PRG outer selection | CHR outer selection / board distinction |
|---|---|---|
| 281 | D003 bits 1:0, 256 KiB units | Same two bits and unit size |
| 282 | Bits 2:1, 256/512 KiB units selected by bit 5 | Bit 0 selects A18 in small mode; bit 3 selects A19 only when bit 4 enables it |
| 295 | Bits 2:0, 128 KiB units | Same three bits and unit size |
| 358 | Bits 2:1 drive A19:A18; 256 KiB inner window | Bit 1 also drives CHR A19; bit 0 drives A18 in small mode |
| 386 | Bits 3/1 drive A19/A18 | Bit 2 drives CHR A19; bit 0 drives A18 in small mode |
| 387 | Bits 3/1 drive A18/A17; 128 KiB inner window | Same CHR wiring as 386 |
| 388 | Bits 3/2 drive A19/A18 | Bits 1/0 drive CHR A19/A18; inhibits ROM nametables and extended CIRAM |
| 397 | Bits 2:1 drive A19:A18; retains inner PRG A17 | All three low bits select 128 KiB CHR units |

All profiles compose with the four PRG modes (including seven-bit reversal),
fixed-last-bank selection, $6000 ROM overlay, four CHR units, high CHR register
bytes and MMC4-like latches. The 282/358/386/387/388 large-CHR mode masks to
512 KiB and ignores the 256 KiB half selector. Profiles 281/295/397 keep their
fixed inner windows. B-register nametables retain their direct CHR indices;
388 uses the same advanced-nametable inhibit as 90.

The outer register uses mask $F003 on seven profiles; 358 keeps $F803. Other
registers retain the common ASIC masks. A wider outer-register mask alone is
not treated as evidence that the chip's CPU A11 input is disconnected globally:
that would also remove its multiplier. Fully unwired-A11 boards remain a
separate unverified wiring case. Likewise, 386/387's unexplained bit 4 and
358's reported unreliable 1 KiB CHR dumping are not assigned invented behavior.
The existing $C007 and early-multiplier limitations still apply.

Geometry is bounded by the decoded pins: 387 accepts at most 512 KiB PRG,
282 at most 2 MiB across its documented large window, and the other six at most
1 MiB. These eight profiles accept up to 1 MiB CHR. The 387 article's '512 MiB'
prose is a size typo: its diagram exposes only PRG A18. Known 282 multicarts use
1 MiB; accepting its full decoded range does not certify an observed 2 MiB PCB.
NES 2.0 metadata explicitly selects absent/8 KiB WRAM or banked CHR RAM, and
contradictory/mixed memory declarations are rejected as for the shared ASIC.

No serialized fields or bus/PPU timing changed; machine snapshot version remains
20. Deterministic tests use independent expected page tables, contrasting
295/397 and small/large 282 windows, physical CIRAM/PPU reads, all four IRQ
sources, bounded metadata, and component snapshots. Eight original CPU programs
switch outer banks while executing, enter an IRQ handler, and verify full-machine
replay and atomic malformed-state rollback. All eight IDs join the shared
91-board boot/replay matrix. CLI validation covers all twelve J.Y. IDs across
32 launches, including NES 2.0 mapper high bits, PRG/CHR outer switches, math,
IRQ delivery and exact WRAM persistence; source ROMs remain unchanged.

Validation passed `meson compile -C buildDir`, all 45 regular test targets, and
all eight serial ASan/UBSan suites with the macOS `detect_leaks=0` exception. The mapper
executable passed 33,567 assertions across 254 cases. Scoped clang-tidy,
formatting and whitespace checks passed. Isolated agent documentation validation
reported zero errors and the unchanged 52 warnings. No commercial-title or
physical-cartridge certification is claimed.

## Sachen/TXC follow-up: counters, wiring and low expansion ports

Sixteen new IDs use two original implementations, `Sachen` and `Txc`. Primary
sources are [36 r4196](https://www.nesdev.org/w/index.php?oldid=4196),
[132 r4967](https://www.nesdev.org/w/index.php?oldid=4967),
[133 r4977](https://www.nesdev.org/w/index.php?oldid=4977),
[8259 r23709](https://www.nesdev.org/w/index.php?oldid=23709),
[136 r21611](https://www.nesdev.org/w/index.php?oldid=21611),
[137 r20417](https://www.nesdev.org/w/index.php?oldid=20417),
[143 r5034](https://www.nesdev.org/w/index.php?oldid=5034),
[146/NINA r22985](https://www.nesdev.org/w/index.php?oldid=22985),
[147 r5057](https://www.nesdev.org/w/index.php?oldid=5057),
[150 r5081](https://www.nesdev.org/w/index.php?oldid=5081),
[172 r5257](https://www.nesdev.org/w/index.php?oldid=5257),
[173 r5274](https://www.nesdev.org/w/index.php?oldid=5274), and
[243 r5861](https://www.nesdev.org/w/index.php?oldid=5861).
No external emulator implementation or commercial ROM was incorporated.

| IDs | Behavior |
|---|---|
| 133, 146 | Distinct PRG/CHR latch bit assignments; exact $E100 decoder; fixed header H/V |
| 135, 138, 139, 141 | 8259 A/B/C CHR rewiring, high bank register, simple-mode address leakage, three PRG register bits and asymmetric CIRAM |
| 137 | 8259D individual 1 KiB top bits, final fixed 4 KiB CHR, reversed H/V selector and simple-mode behavior |
| 141 without CHR ROM | Q-Boy's unbanked 8 KiB volatile CHR RAM; registers still control PRG/CIRAM |
| 143 | Six inverted CPU address bits with upper data bits floating |
| 150, 243 | All eight three-bit registers readable, distinct CHR wiring, four CIRAM modes; 150's D2 solder pad |
| 36, 132, 173 | TXC staged P/R/Output, two/three-bit wrapping counters, inverse load and S-XOR-V status; 36's independent CHR latch and 173's immediate inverted CHR pin |
| 136, 147, 172 | JV001 six-bit input/output, low-nibble-only increment and separate read/load inversion; shifted or reversed CPU data wiring |
| 147 up to 64 KiB PRG | Documented simple latch PCB; $4103 mask and conflicts on its $C102-$FFFE aliases |
| 172 | Output writes latch H/V from inversion state; fixed PRG |

Original CPU fixtures exposed a pre-existing bus routing defect: the 1 KiB
$4000-$43FF dispatch page treated $4100-$43FF accesses as unused console I/O.
`read_apu_io`/`write_apu_io` now forward $4020-$43FF to cartridge expansion
handlers after console and loaded FDS registers take priority. Ports below
$4020 retain their behavior. This permits actual CPU programs to use the same
low-address registers already accessible through direct cartridge calls. No
new bus state or timing callback was introduced.

State layouts for older mappers and the machine version (20) are unchanged.
The new payloads store only registers, flags and optional fixed-size Q-Boy RAM.
Loads validate field widths before use; malformed full-machine states roll back
atomically. No PRG RAM or NVRAM is supplied. Unsupported submappers, contradictory
RAM declarations, oversize images and incompatible headers are rejected.
Sachen 8259 PRG accepts the decoded three-bit/256 KiB range; the main article's
128 KiB overview is narrower than its register description. 137 ignores the
known erroneous four-screen header and routes the console's ordinary CIRAM.
Mapper 150 accepts only its 64 KiB CHR wiring; misordered Honey Peach dumps need
the correct mapper 243 rather than a title-specific mapper-150 hack.

Known limits: 133 supplies the documented 72-pin compatible latch behavior;
the original 60-pin analog feedback network and unstable oscillation are not
modeled. The published 8259 description is incomplete, and 137 simple mode is
explicitly a deduction, not measured hardware. Mapper 143 does not introduce a
title-specific uninitialized console-RAM pattern. Mapper 173's 8 KiB UVEPROM
/PGM disable returns $FF; analog PPU floating-bus behavior is unverified. The
173 pin diagram exposes CHR A15 from Output bit 1; the usual 32 KiB formula folds
that pin, while the implementation also accepts its full 64 KiB decoded range.
These registrations do not certify every cartridge revision or title.

Tests cover distinct physical PRG/CHR page tables, all CIRAM modes through the
PPU, exact aliases/open-bus masks, staged and incrementing protection states,
147 conflicts and both PCB sizes, pad readback, volatile CHR RAM, bounded
metadata, snapshots and malformed-state rejection. Sixteen original CPU
programs self-check PRG, protection and PPUDATA results, then replay full-machine
states and verify atomic rollback. All new IDs also join the shared 107-board
boot/replay matrix. CLI tests load 36 NES 2.0/iNES profiles (including Q-Boy and
the small 147 latch PCB), reject sixteen unsupported submappers, preserve source
files, and verify that these non-battery boards create no save files.

Validation passed `meson compile -C buildDir`, all 46 regular test targets and
nine serial ASan/UBSan suites (the previous eight plus `fds-system`, with macOS
`detect_leaks=0`). The mapper executable passed 36,042 assertions across 270
cases. The new real-CPU/open-bus tests failed at $4100 before the bus routing
correction and passed afterward. Scoped clang-tidy, formatting and whitespace
checks passed. Isolated agent documentation validation reported zero errors and
the unchanged 52 warnings. No commercial-title certification is claimed.

## Sachen mapper-512 follow-up: six nametables and independent RAM

Mapper 512 extends the shared `Mmc3` implementation using
[NESdev revision 8073](https://www.nesdev.org/w/index.php?oldid=8073) and the
[primary NES 2.0 VRAM-accounting discussion](https://forums.nesdev.org/viewtopic.php?t=16881).
The supported board has 8 KiB battery WRAM, 4 KiB pattern RAM and a separate
4 KiB cartridge nametable pool. The latter two are disjoint portions of the
mapper's host RAM storage; changing graphics modes never aliases their contents.
They provide four cartridge nametables in addition to the console's two CIRAM
pages. NES 2.0 counts cartridge nametable memory in the CHR-RAM size, so the
supported explicit declaration is 8 KiB total PPU RAM, not 4 KiB.

| Mode | Pattern reads/writes | Nametables |
|---|---|---|
| 0 | Banked CHR ROM, writes ignored | CIRAM with normal MMC3 H/V |
| 1 | Banked CHR ROM, writes ignored | Four independent cartridge VRAM pages |
| 2 | Banked 4 KiB CHR RAM | CIRAM with normal MMC3 H/V |
| 3 | Same as mode 2 | Same as mode 2 |

ROM and RAM pattern paths share MMC3 register selection and inversion. RAM
addresses fold to its 4 KiB physical size. Cartridge nametables use direct
1 KiB pages and ordinary $3000 mirroring; palette RAM stays internal to the PPU.
A000 changes the CIRAM arrangement even while cartridge VRAM is selected, so
that arrangement is ready when returning to a CIRAM mode.

The board-mode latch uses the description's tentative $C100 mask. It has no
readback drivers, and aliases in WRAM update the latch independently of the
RAM write path. The normal MMC3 $E001 register decoder, six-bit PRG bank pins,
fixed banks, swapping and IRQ counter are reused. A001 cannot disable WRAM;
its ordinary bit-6 write protection is retained. This follows the published
'normal MMC3' behavior except for the stated enable-bit exception, rather than
claiming a new hardware measurement.

Validation accepts NES 2.0 submapper 0, standard MMC3 ROM ranges (32..512 KiB PRG,
8..256 KiB CHR ROM), the battery flag, absent or explicit 8 KiB PRG-NVRAM, and
absent or explicit 8 KiB volatile PPU RAM. Omitted sizes use those mandatory
physical defaults. Contradictory sizes, WRAM declared volatile, CHR NVRAM,
CHR-free images and unsupported submappers are rejected. A four-screen header
is accepted but never forces cartridge VRAM on: CIRAM starts from flag-6 bit 0,
and only the mode latch selects the cartridge pool.

The new mapper payload appends one validated mode byte to the existing compact
MMC3 variant layout; its RAM snapshot already holds all three cartridge RAM
pools. Mapper 4/118/119 payloads and machine version 20 remain unchanged. Console
reset follows existing MMC3 register/IRQ reset policy and retains RAM. Only
8 KiB WRAM is persistent; PPU RAM is volatile and included in save states.
Battery files use existing MMC3 WRAM loading behavior and oversized files fail
without changing the running machine.

Known limits: the source labels both address masks as tentative, describes
mode 3 as apparently equivalent to 2, and does not establish larger CHR-RAM
populations. This implementation supports the documented minimum RAM layout and
the stated decoders; exact alias gating, clone-specific reset/IRQ differences,
and write-protect wiring need hardware confirmation. It retains the existing
MMC3 dot-260/once-per-rendered-scanline IRQ callback, not a new cycle-accurate A12
filter. ADR-001 PPU timing and all other mapper layouts are preserved.

Original tests cover every mode, PRG swaps and masked pins, ROM protection,
banked RAM and inversion, six independent nametable pages, palette separation,
write-only aliases, WRAM gates, partial IRQs, RAM retention, component snapshots,
invalid fields, header accounting and battery reloads. A CPU program self-checks
PRG and PPU data, enables split pattern tables for MMC3 A12 activity, handles an
IRQ and replays full-machine states with atomic malformed-state rollback.
The shared boot/replay matrix now includes 108 boards. Independent CLI programs
verify successful execution and NVRAM reloads over eight launches, including
omitted sizes and four-screen headers, then reject eight malformed headers.

Validation passed `meson compile -C buildDir`, all 47 regular test targets and
eight serial ASan/UBSan suites with the macOS `detect_leaks=0` exception. The
mapper executable passed 36,209 assertions across 279 cases, including a final
regular mapper run after static-analysis cleanup. Scoped clang-tidy, formatting
and whitespace checks passed. Isolated agent documentation validation reported
zero errors and the unchanged 52 warnings. No commercial-title or physical-board
certification is claimed.

## Sachen mapper-513 follow-up: PPU-selected PRG and battery CHR RAM

Mapper 513 implements Sachen SA-9602B / UNL-SA-9602B, used by the unauthorized
Princess Maker port, in the shared `Mmc3` class. Its source is
[NESdev revision 8077](https://www.nesdev.org/w/index.php?oldid=8077).
The board has 32 KiB battery-backed CHR RAM and no CHR ROM. CHR registers 0–5
use bits 0–4 for physical 1 KiB RAM pages, ignore bit 5, and put bits 6–7 on
the two outer PRG bank pins. Ordinary MMC3 paired CHR banks and inversion apply.
Switchable PRG windows use those pins in 512 KiB units; both fixed windows
always address the second-last/last 8 KiB pages of outer bank zero. PRG register
bits 6–7 remain masked, and PRG mode swaps R6 with the fixed second-last page.

The implementation follows the article's inference that changing PPU addresses
can change the outer PRG bank. The existing PPU address-pin observer selects
the active logical CHR slot during rendering and CPU PPU-register accesses.
Tracking the slot also makes CHR-register and inversion writes affect PRG
immediately. Cached per-pixel CHR lookups never drive those pins. There are no
new PPU timing fields or changes to ADR-001 scroll timing. Nametable addresses
retain their low CHR-select bits, with A13 excluded from the slot selection.

The known 1536 KiB dump has three outer banks. Selecting outer bank 3 aliases
bank 2, following the compatibility convention recorded by the source. This is
a deterministic emulation choice: the source instead describes simultaneous
chip selection on hardware, whose electrical result is unmeasured. Full 2 MiB
images address four distinct outer banks. Smaller accepted populations mirror
through their ROM size; those populations and the full 2 MiB range are pin-range
support, not confirmation of other manufactured boards.

Validation accepts NES 2.0 submapper 0, 512 KiB–2 MiB PRG in 512 KiB units, no
CHR ROM, mandatory battery flag, H/V CIRAM, and absent or explicit 32 KiB CHR
NVRAM. An omitted CHR-NVRAM size uses the physical 32 KiB default. This supported
profile has no CPU WRAM: volatile CHR RAM, PRG RAM/NVRAM, other CHR-NVRAM sizes,
four-screen headers and nonzero submappers are rejected. The source does not
establish alternate CPU-RAM populations. A001 never exposes the pattern RAM
at $6000; that CPU region stays undriven.

Battery files contain all 32 KiB in physical CHR-page order, independent of the
active registers. Existing partial-file loading policy is retained; oversized
files fail without mutating the running machine. Console reset retains NVRAM.
The new mapper payload contains a length-bounded 32 KiB pool, the compact MMC3
register/IRQ state and a validated PPU slot. Existing mapper payloads and machine
version 20 remain unchanged. Ordinary MMC3 H/V mirroring and the existing
dot-260 rendered-scanline IRQ callback are shared; clone-specific IRQ timing and
an exact A12 filter remain unverified.

Original tests cover every logical CHR slot, both inversions and PRG modes,
fixed-bank wiring, distinct four-bank and three-bank-alias layouts, all physical
CHR pages, paired banks, unused bits, open bus, actual PPU rendering/address
activity, partial IRQs, reset retention, bounded malformed states and metadata.
CPU programs self-check banks and PPU data, handle an IRQ, and verify full-machine
replay and atomic malformed-state rollback. The shared boot/replay matrix now
includes 109 boards. Independent CLI programs verify eight self-check/reload
launches across both ROM layouts and omitted/explicit RAM metadata, then reject
ten malformed headers while keeping source ROMs unchanged. No external emulator
code was incorporated; no commercial-title or physical-board certification is
claimed.

Validation passed the build, all 48 MapperBus test targets and eight serial
ASan/UBSan suites with the macOS `detect_leaks=0` exception. The mapper executable
passed 36,649 assertions across 288 cases, including a final regular run after
static-analysis cleanup. Scoped clang-tidy, formatting and whitespace checks
passed. Isolated agent-documentation validation reported zero errors and the
unchanged 52 warnings.

## Sachen mapper-262 follow-up: Street Heroes / UNL-SHERO

Mapper 262 implements Street Heroes / Shìhún in the shared `Mmc3` class from
[NESdev revision 7640](https://www.nesdev.org/w/index.php?oldid=7640).
It has CHR ROM, 8 KiB volatile pattern RAM and a separate 4 KiB four-screen VRAM
pool. The $4100 latch's bit 6 selects pattern RAM; bits 0–3 supply the high ROM
bank bit for four physical 2 KiB PPU regions. Bits 7, 5 and 4 are ignored.

| PPU region | Outer CHR-ROM bit from $4100 |
|---|---|
| $0000–$07FF | Bit 3 |
| $0800–$0FFF | Bit 2 |
| $1000–$17FF | Bit 0 |
| $1800–$1FFF | Bit 1 |

These select 256 KiB units and remain tied to the physical PPU region when
MMC3 CHR inversion changes the inner register selection. Ordinary paired-bank
rounding applies. In RAM mode, the MMC3 output's low three bits select 1 KiB
physical pages; outer ROM pins have no effect. This follows the source's normal
MMC3 description, not a new measurement of the clone's RAM wiring. ROM writes
are ignored and all eight RAM pages retain data across chip/mode changes.

All four nametables use the independent cartridge VRAM pool, with normal $3000
mirrors and internal PPU palette RAM. Pattern writes never alias nametables.
The board forces four-screen wiring even if the header's four-screen flag is
absent; A000 cannot change that wiring. This is fixed VRAM, so
[NES 2.0 revision 24464](https://www.nesdev.org/w/index.php?title=NES_2.0&oldid=24464)
excludes it from the CHR-RAM size: the explicit pattern-RAM declaration is 8 KiB.
Console reset retains both RAM pools and the configured title jumper.

The jumper API accepts 0 (Street Heroes, the default) or 1 (Shìhún) and exposes
it on D7. D0–D6 float and merge with CPU open bus. The tentative $C100 decoder
also reaches aliases in $6000–$7FFF; those use the existing masked cartridge-read
path. Low expansion aliases use the existing $4020–$5FFF routing. Mode writes
at these aliases remain independent of A001. Other $6000 reads are undriven.
The source labels this decoder unknown/probable and the normal $E001 MMC3
decoder probable; exact hardware aliases remain unverified.

Factories accept NES 2.0 submapper 0, 32–512 KiB PRG in 8 KiB units, 8–512 KiB
CHR ROM in 1 KiB units, and omitted or explicit 8 KiB volatile CHR RAM. Omission
uses the board's physical RAM default. H/V/four-screen header flags are accepted;
the board always supplies four screens. This supported profile has no CPU WRAM
or battery-backed memory: PRG RAM/NVRAM, CHR NVRAM, battery flags and conflicting
pattern-RAM sizes are rejected. The source does not establish a WRAM population.
ROM ranges describe decoded pins; non-power-of-two images wrap by physical size,
without claiming other manufactured boards. No battery file is created.

The new payload adds length-bounded 4 KiB VRAM, the masked graphics latch and a
validated jumper to the compact MMC3 variant registers, IRQ state and RAM.
Old mapper payloads and machine version 20 remain unchanged. PRG banking, six-bit
R6/R7 pins and the approximate dot-260 rendered-scanline IRQ callback are shared.
No bus/PPU state fields or ADR-001 timing changed. Clone-specific timing and exact
A12 filtering remain unverified.

Original tests cover every combination of outer bits through both inversions,
all pattern-RAM pages, paired banks, ROM protection, unused bits, four independent
nametables, palette separation, read/write aliases, jumper open bus, PRG modes,
IRQ progress, RAM retention, bounded malformed states and metadata. CPU programs
check banking, both jumper settings, nametables and an IRQ before reporting
success in internal RAM; full-machine replay and atomic rollback are verified.
The shared boot/replay matrix now includes 110 boards. CLI smoke tests boot nine
header/geometry profiles, reject eleven invalid headers, preserve source ROMs
and verify that volatile-only boards create no saves. No external emulator code
was incorporated, and no commercial-title or physical-board certification is
claimed.

Validation passed the build, all 49 MapperBus test targets and eight serial
ASan/UBSan suites with the macOS `detect_leaks=0` exception. The mapper executable
passed 37,374 assertions across 296 cases, including a final regular run after
static-analysis cleanup. Scoped clang-tidy, formatting and whitespace checks
passed. Isolated agent-documentation validation reported zero errors and the
unchanged 52 warnings.

## Rex Soft mapper-263 follow-up: UNL-KOF97

Mapper 263 implements the MMC3 clone used by Rex Soft's The King of Fighters '97
and Boogerman II: The Final Adventure, using
[NESdev revision 7642](https://www.nesdev.org/w/index.php?oldid=7642).
Its CPU A12 supplies the ordinary MMC3 register-select A0. The remaining low
address bits are ignored, so each native port has 4096 aliases:

| Native port | Ordinary MMC3 register |
|---|---|
| $8000 | $8000 bank select |
| $9000 | $8001 bank data |
| $A000 | $A000 mirroring |
| $B000 | $A001 WRAM gates |
| $C000 | $C000 IRQ latch |
| $D000 | $C001 IRQ reload |
| $E000 | $E000 IRQ disable/acknowledge |
| $F000 | $E001 IRQ enable |

The clone's written pins D0..D7 reach ordinary MMC3 pins D2,D0,D5,D3,D4,D1,D6,D7.
For example, written $01 becomes $04, $02 becomes $01, and $20 becomes $02.
This permutation applies to every mapper-register write at $8000–$FFFF. WRAM
writes at $6000–$7FFF keep their original bytes, and PRG/CHR ROM reads are not
permuted. Address and data normalization occurs before the shared MMC3 handler;
PRG modes, six-bit PRG pins, paired CHR rounding, inversion, H/V CIRAM, WRAM
protection and IRQ progress then use the existing implementation.

The supported NES 2.0 submapper-0 profile accepts 32–512 KiB PRG in 8 KiB units
and 8–256 KiB CHR ROM in 1 KiB units. CHR RAM/NVRAM, four-screen headers and
nonzero submappers are rejected. These are normal MMC3 pin-range bounds rather
than certification of every ROM population. Non-power-of-two sizes wrap by
physical ROM size. CPU WRAM is absent unless explicitly declared in the header;
volatile and nonvolatile portions may total at most 8 KiB, with a power-of-two
total. The battery flag must agree with the presence of PRG NVRAM. These optional
memory populations follow the shared MMC3 model; the source does not provide a
complete physical memory survey for both titles.

Snapshots store normalized MMC3 registers, RAM and partial IRQ state in the
existing compact variant format. No additional latch, bus/PPU state field or
machine-version change is needed. Old mapper payloads and machine version 20
remain unchanged. Battery files contain only the header-declared NVRAM suffix
of physical WRAM, without the register permutation; volatile prefixes stay out
of saves. Existing bounded loading rejects oversized battery files atomically.
Console reset follows shared MMC3 register/IRQ reset and RAM-retention policy.

Original tests check all 256 data inputs at all eight ports against the existing
compact MMC3 register/IRQ payload, using an independent pin table for expected
values. Alias tests include both A0 levels and offsets across each 4 KiB range.
Other tests verify full CHR values, paired banks, PRG masks/swaps, ROM protection,
WRAM data/gates, mixed volatile/nonvolatile storage, IRQ progression, metadata,
and malformed component states. Native CPU programs self-check banking,
PPUDATA, CIRAM and mapper IRQs before full-machine replay and atomic rollback.
The shared boot/replay matrix now includes 111 boards. CLI programs verify six
self-check/reload launches across 2/4/8 KiB NVRAM, two volatile/no-WRAM boot
profiles, ten invalid headers and unchanged source ROMs.

The article notes that many KOF97 images were converted to mapper 4 and that
some fail to inhibit APU frame IRQs. This implementation does not reinterpret
such images or change APU behavior. Original CPU/CLI fixtures explicitly inhibit
the APU frame IRQ so their handler observes the mapper. Clone-specific timing
and exact A12 filtering remain unverified: the shared MMC3 IRQ path retains its
dot-260 rendered-scanline approximation. No external emulator code was
incorporated; no commercial-title or physical-board certification is claimed.

Validation passed the build, all 50 MapperBus test targets and eight serial
ASan/UBSan suites with the macOS `detect_leaks=0` exception. The mapper executable
passed 39,607 assertions across 304 cases. Before registration, both initial
native-mapper tests failed at factory creation; they passed after the change.
Scoped clang-tidy, formatting and whitespace checks passed. Isolated agent
documentation validation reported zero errors and the unchanged 52 warnings.

## Nitra mapper-250 follow-up: address data and Queen Bee V wiring

Mapper 250 implements Nitra's address-driven MMC3 in the shared `Mmc3` class.
The sources are [NESdev revision 21044](https://www.nesdev.org/w/index.php?oldid=21044),
Krzysiobal's [L401 Time Diver Avenger PCB](http://krzysiobal.com/carts/?action=view&id=533)
and [Queen Bee V N1-PCB-015 PCB](http://krzysiobal.com/carts/?action=view&id=354&lang=en).
Their reverse-engineered schematics establish the register connections and the
Queen Bee V multiplexer. No external emulator source was incorporated.

CPU A10 selects ordinary MMC3 A0. CPU A7..A0 supply data; the CPU data byte is
discarded. Bits A12, A11, A9 and A8 do not select registers or data:

| Native base | Ordinary MMC3 register |
|---|---|
| $8000 | $8000 bank select |
| $8400 | $8001 bank data |
| $A000 | $A000 mirroring |
| $A400 | $A001 WRAM control (no RAM fitted) |
| $C000 | $C000 IRQ latch |
| $C400 | $C001 IRQ reload |
| $E000 | $E000 IRQ disable/acknowledge |
| $E400 | $E001 IRQ enable |

Any CPU-data write to $8006 selects R6, and any write to $8403 sets it to 3.
Normalization feeds shared MMC3 banking/IRQ logic. PRG modes, paired CHR banks,
inversion, H/V CIRAM and ROM protection are retained. Neither documented PCB has
WRAM or battery memory, so A001 cannot expose RAM. Expansion reads are undriven.

Queen Bee V adds a 74153 selected by CPU A14 and M2, enabled by active-low CPU
ROMSEL. During M2 high, ROM A13 comes from MMC3 in the lower 16 KiB window and
CPU A13 in the upper window. Thus $C000 forces ROM A13=0 and $E000 forces it to
1, even when R6 is swapped into $C000; an odd R6 then selects its neighboring
even physical bank there. This is combinational wiring, not a register latch.
Below $8000 ROM is unselected. M2-low mux inputs need no new event in the existing
CPU read model; analog bus transients are not modeled.

The header has no assigned submapper distinction between these PCBs. Default
wiring is `nitra-l401`. Core clients select Queen Bee V before installation with
`Cartridge::configure_board_profile("nitra-queen-bee-v")`. The shared source
loader accepts the optional profile for raw, ZIP and 7z; CLI exposes
`--board-profile nitra-queen-bee-v`. Explicit `nitra-l401` is also accepted.
Invalid names, other mappers and FDS fail before replacing the running machine.
Selection does not guess from names, hashes or sizes, and does not invent a NES
2.0 submapper. GUI selection and persisted per-game profile preferences remain
unimplemented.

Factories accept iNES and NES 2.0 submapper 0, 32–128 KiB PRG in 8 KiB units,
8–128 KiB CHR ROM in 1 KiB units, and H/V CIRAM. Connected ROM address pins in
both schematics stop at A16. Larger images, CHR RAM/NVRAM, four-screen headers,
battery flags and explicit NES 2.0 CPU RAM/NVRAM are rejected. Legacy iNES's
implicit CPU-RAM size is ignored because neither board has that chip. Smaller
accepted images wrap by physical size; alternate populations and padded dumps
are not certified. The cartridge creates no save file.

Snapshots add one validated, immutable wiring byte to the compact MMC3 variant
state. A state made with the other profile is rejected with full-machine rollback;
loading never silently changes the selected PCB. Old mapper payloads and machine
version 20 remain unchanged. No PPU/bus timing field was added. IRQs retain the
shared dot-260 rendered-scanline approximation; exact A12 filtering remains
unverified. No commercial-title or physical-board certification is claimed.

Tests cover every address-data value at every port with different CPU data,
every CPU-data value, all 16 unused-address-bit combinations, CHR/inversion,
both mux modes, metadata and malformed states. Four native CPU programs exercise
both PCB/header variants and check PRG, PPUDATA, CIRAM and mapper IRQs before
replay/atomic rollback. The shared boot/replay matrix now includes 112 boards.
Source-loader tests observe the differing physical PRG results for raw/ZIP/7z,
invalid-profile preservation and cross-profile snapshot rejection. CLI smoke
tests cover twelve raw/ZIP/header/profile combinations, nine invalid headers
and six invalid profile uses, preserving source files and creating no saves.

Validation passed the build, all 51 MapperBus test targets and eight serial
ASan/UBSan suites with the macOS `detect_leaks=0` exception. The mapper executable
passed 48,074 assertions across 311 cases; ROM-source tests passed 3,964 assertions
across 37 cases. Both initial mapper tests failed at factory creation before
registration and passed after implementation. Scoped clang-tidy, formatting and
whitespace checks passed. Isolated agent-documentation validation reported zero
errors and the unchanged 52 warnings.

## Kasheng mapper-115 follow-up: NROM override and independent controls

Mapper 115 implements the Kǎshèng SFC-02B/-03/-004 register profile in `Mmc3`
from [NESdev revision 21614](https://www.nesdev.org/w/index.php?oldid=21614).
The low registers use an $E003 decoder: $6000 writes PRG mode/bank, $6001 writes
the outer CHR bank, and $6002 reads three solder pads. Every aligned four-byte
group through $7FFF aliases these ports. The controls are independent of the
MMC3 WRAM gates; A001 disable/write-protect bits never block them.

$6000 has bits `MPN.PPPp`. M (bit 7) selects NROM override, P (bit 6) supplies
PRG A18 in every mode, N (bit 5) selects the NROM window size, and bit 4 is unused:

| Mode | PRG mapping within the selected 256 KiB outer block |
|---|---|
| M=0 | MMC3 R6/R7 windows and fixed pages 30/31; ordinary PRG swapping applies |
| M=1, N=0 | Low four register bits select one 16 KiB bank, mirrored twice |
| M=1, N=1 | Low bits 3..1 select an aligned 32 KiB window; CPU A14 replaces p |

P supplies A18 to switchable and fixed windows alike. In MMC3 mode R6/R7 retain
five physical inner-bank bits; their A18 output cannot override P. MMC3 register
writes continue during NROM override, so returning to MMC3 exposes the latest
bank values. $6001 bit 0 supplies CHR A18 in 256 KiB units. Normal paired-bank
rounding and inversion apply to the inner CHR registers; other $6001 bits are
ignored. ROM writes never alter CHR data.

`configure_board_jumpers(0..7)` selects the solder-pad value, default zero.
Only D0–D2 drive $6002 reads; upper bits merge with CPU open bus. Mode/CHR ports
are write-only, $6003 is unused, and other low reads remain undriven in this
profile. Pad writes have no effect. The jumper API is available to core clients;
no graphical or CLI pad-setting control is added.

The supported profile accepts iNES and NES 2.0 submapper 0, 32–512 KiB PRG in
8 KiB units, 8–512 KiB CHR ROM in 1 KiB units, and H/V CIRAM. ROM ranges follow
the documented pins, with physical-size wrapping for smaller populations.
CHR RAM/NVRAM, battery flags, four-screen headers and explicit NES 2.0 CPU
RAM/NVRAM are rejected. Legacy iNES's default CPU-RAM field does not create a
chip. The article establishes control-register behavior rather than a complete
WRAM population survey: alternate RAM-bearing boards are outside this supported
profile. No battery file is created.

IRQ behavior uses the shared Sharp-style profile: latch zero causes repeated
IRQs on successive rendered-scanline callbacks, including during NROM override.
This is required by the documented Chinese Yuu Yuu Hakusho Final release. It
does not reuse mapper 114's different zero-latch behavior. Exact A12 filtering
and clone timing remain unverified; the existing MMC3 dot-260 approximation
and ADR-001 PPU timing are unchanged. CPU fixtures inhibit APU frame IRQs and
require at least two mapper IRQs before reporting success.

The new snapshot payload appends masked PRG mode, CHR outer bank and solder-pad
bytes to the compact MMC3 variant state. Loads validate bit 4, CHR range and pad
range, along with ordinary bank/mirror/IRQ fields. Existing mapper payloads and
machine version 20 remain unchanged. Reset clears the two control latches and
retains configured pads under the existing MMC3 reset policy; clone-specific
reset electrical behavior has not been measured.

Tests exercise all 256 PRG control values in both MMC3 PRG modes, every CHR slot
through inversion, paired banks, outer pin independence, write-only aliases,
WRAM-gate bypass, all pad values and open bus, IRQ repeats, bounded metadata,
component snapshots and invalid fields. Four original CPU programs cover both
header formats and pad settings, verify full-window switching while executing,
PPUDATA and CIRAM, and replay full-machine state with atomic malformed-state
rollback. The shared boot/replay matrix now includes 113 boards. CLI smoke tests
boot sixteen header/geometry/mirroring profiles, reject twelve invalid/obsolete
headers, preserve ROMs and create no saves. No external emulator code was used;
no commercial-title or physical-board certification is claimed.

### Obsolete mapper 248 assignment

[Mapper 248 revision 23288](https://www.nesdev.org/w/index.php?oldid=23288)
identifies 248 as a mistaken assignment for games handled by mapper 115,
including Bao Qing Tian. Its private-use reassignment is described as a proposal,
not a separate documented cartridge circuit. The inventory therefore classifies
248 as an obsolete duplicate and leaves it unregistered. Both core and CLI tests
require an explicit unsupported-ID error; ROM headers are not rewritten or
silently treated as 115.

Validation passed the build, all 52 MapperBus test targets and eight serial
ASan/UBSan suites with the macOS `detect_leaks=0` exception. The mapper executable
passed 50,484 assertions across 318 cases. Scoped clang-tidy, formatting and
whitespace checks passed. Isolated agent-documentation validation reported zero
errors and the unchanged 52 warnings.

## Mapper-114 follow-up: both scrambled MMC3 wirings

Mapper 114 implements the SuperGame/Hosenkan MMC3-clone register profiles from
[NESdev revision 4858](https://www.nesdev.org/w/index.php?oldid=4858).
Submapper 0 covers the documented Aladdin, The Lion King, Pocohontos and Super
Donkey Kong boards. Submapper 1 selects Boogerman's different wiring. Legacy
iNES selects submapper 0; submapper 1 requires NES 2.0 metadata. No filename or
content-hash heuristic chooses the wiring.

| Native port | Submapper 0 MMC3 register | Submapper 1 MMC3 register |
|---|---|---|
| $8000 | $A001 | $A001 |
| $8001 | $A000 | $8001 |
| $A000 | $8000 | $8000 |
| $A001 | $C000 | $C001 |
| $C000 | $8001 | $A000 |
| $C001 | $C001 | $C000 |
| $E000 | $E000 | $E000 |
| $E001 | $E001 | $E001 |

Writes reaching canonical $8000 permute the low three index bits; PRG mode and
CHR inversion bits 6/7 remain unchanged. Written indices 0..7 select canonical
registers `0,3,1,5,6,7,2,4` on submapper 0 and `0,2,5,3,6,1,7,4` on submapper 1.
Other data bytes are unchanged. IRQ latch and reload writes have separate,
one-to-one effects, following the corrected source description rather than its
superseded combined-write account.

The source labels low-port decoding probably $E001. This implementation uses
even $6000–$7FFE writes for the NROM override and odd $6001–$7FFF writes for CHR
A18. Thus $6002/$6003 alias the controls and do not expose mapper 115's solder
pads. Controls bypass A001 WRAM gates; low reads are undriven in this supported
profile. Exact hardware alias gating remains unmeasured.

Override bit 7 selects NROM mapping; bit 5 selects mirrored 16 KiB versus aligned
32 KiB, with CPU A14 replacing the low bank bit in 32 KiB mode. Low bits 3..0
select 16 KiB units. Bits 6 and 4 are unused. There is no mapper-115-style PRG
outer bit: ordinary MMC3 mode retains its own six-bit PRG selection, and NROM
override addresses its 16-bank/lower-256-KiB range. MMC3 register writes continue
behind the override. CHR bit 0 selects 256 KiB units after normal paired-bank
rounding and inversion; other CHR-control bits are ignored.

The mapper article identifies an MMC3A-like IRQ and summarizes zero as disabling
IRQs. The more detailed [MMC3 revision 24445](https://www.nesdev.org/w/index.php?title=MMC3&oldid=24445)
distinguishes automatic zero reloads from a software-forced reload: the former
do not repeat, while the latter can produce one IRQ. The implementation selects
the existing non-repeating MMC3 profile, including that forced-reload case.
A positive count already in flight can still reach zero after the latch is
changed to zero. No shared counter algorithm was altered. Exact forced-reload
behavior on this clone, the $C001 reload glitch and A12 filtering remain
unmeasured. The existing dot-260 rendered-scanline approximation is retained.

Factories accept NES 2.0 submappers 0/1 and legacy submapper 0, 32–512 KiB PRG
in 8 KiB units, 8–512 KiB CHR ROM in 1 KiB units, and H/V CIRAM. This ROM-only
profile rejects RAM/NVRAM, battery flags and four-screen headers; legacy CPU-RAM
defaults do not create a chip. Alternate RAM populations are not established
by the source. Smaller/non-power-of-two accepted images wrap by physical size;
these bounds describe pins, not certification of every ROM population.

The new snapshot payload appends masked PRG mode, CHR outer bank and immutable
submapper identity to the compact MMC3 state. Loads reject unused bits, invalid
CHR values and a wiring mismatch. Full-machine failures roll back atomically.
Existing mapper payloads and machine version 20 remain unchanged. Reset clears
control latches under shared MMC3 policy and retains the header-selected wiring;
clone-specific power-on/reset electrical behavior remains unverified. No bus or
PPU timing/state fields were added, and no battery file is created.

Tests cover every byte at every register port for both wirings, with inverse
pin tables supplying expected canonical writes, plus address aliases, all PRG
control values in both MMC3 modes, CHR outer/inversion, gate bypass, no-pad/open
bus behavior, IRQ transitions, geometry and malformed states. Three original
CPU programs cover NES 2.0 submappers 0/1 and legacy 0, checking banking, PPUDATA,
CIRAM, one forced-zero IRQ, a delay without repeats, and positive-count recovery
before replay/atomic rollback. The shared boot/replay matrix now includes 114
IDs, with submapper 1 covered separately. CLI smoke tests boot twenty-four
header/geometry/mirroring combinations and reject twelve invalid/duplicate
headers, preserving source ROMs and creating no saves. No external emulator
source was incorporated; no commercial-title or physical-board certification
is claimed.

### Historical mapper 182 duplicate

Revision 4858 identifies mapper 182 as a duplicate of 114, describing Hosenkan
games that initialize the low controls to zero and then do not use them. This
does not establish another cartridge circuit to implement. The inventory marks
182 as a historical duplicate and leaves it unregistered; core and CLI tests
require an unsupported-ID error. No ROM header is silently rewritten to 114.

Validation passed the build, all 53 MapperBus test targets and eight serial
ASan/UBSan suites with the macOS `detect_leaks=0` exception. The mapper executable
passed 59,158 assertions across 327 cases. Both initial native-register tests
failed at factory creation before registration and passed after implementation.
Scoped clang-tidy, formatting and whitespace checks passed. Isolated agent
documentation validation reported zero errors and the unchanged 52 warnings.

## Mapper-123 follow-up: Kasheng H2288

Mapper 123 implements the Kǎshèng H2288 / UNL-H2288 register profile from
[NESdev revision 4940](https://www.nesdev.org/w/index.php?oldid=4940), used by
Kasheng/Hummer Team's Mortal Kombat 3 and Kasheng/Shin-Shin's Earthworm Jim 2.
These board names do not imply support for similarly named SuperGame versions.

The MMC3-compatible ports retain their ordinary `$E001` address decoding.
Written bank-select indices 0..7 select canonical registers `0,3,1,5,6,7,2,4`;
mode bits 6/7 pass through. This is mapper 114 submapper 0's index permutation,
without its register-address permutation. Normal MMC3 mode retains six-bit PRG
selection and full eight-bit CHR selection, including paired banks/inversion.

All writes at `$5800–$5FFF` reach a write-only PRG override using mask `$F800`.
The written byte is `.M31 .2S0`: bit 6 enables the override, bit 1 selects
mirrored 16 KiB versus aligned 32 KiB, and bits 5,2,4,0 supply the 16 KiB bank's
bits 3,2,1,0. Bits 7/3 are unused. In 32 KiB mode CPU A14 replaces bank bit 0.
The override addresses the lower 256 KiB; normal MMC3 mode can address 512 KiB.
MMC3 writes continue behind the override and take effect when it is disabled.
The low latch bypasses A001 RAM gates. Reads leave all CPU data bits floating.
There are no `$6000/$6001` control aliases, outer CHR latch or mapper-115 solder
pad register in this profile. CHR ROM remains write-protected; MMC3 H/V CIRAM
and IRQ registers retain ordinary routing even while PRG override is enabled.

The source does not identify a clone-specific IRQ revision. This implementation
retains the default Sharp-style MMC3 zero-latch repeats rather than mapper 114's
non-repeating profile. Exact H2288 IRQ behavior, filtered A12 edges and clone
reset electrical behavior remain unmeasured. The existing dot-260 rendered-
scanline approximation and shared counter algorithm are unchanged.

Factories accept legacy iNES and NES 2.0 submapper 0, 32–512 KiB PRG in 8 KiB
units, 8–256 KiB CHR ROM in 1 KiB units, and H/V CIRAM. The supported ROM-only
profile rejects explicit RAM/NVRAM, battery and four-screen metadata. Legacy
implied WRAM does not create a chip; the article does not establish a complete
survey of RAM populations. Accepted smaller/non-power-of-two ROMs wrap by
physical size; these bounds describe address pins, not title certification.

The new snapshot payload appends one normalized PRG-override byte to compact
MMC3 state. Unused bits, malformed common fields and truncation fail validation;
full-machine failures roll back atomically. Old mapper payloads and machine
version 20 remain unchanged. Reset clears the override under shared MMC3 policy.
No bus/PPU state, expansion audio or battery file is added.

Eight direct tests exercise every data byte on every MMC3 port, ordinary high
aliases, every override byte in both PRG modes, all 2,048 low-port aliases under
three RAM gates, CHR inversion/protection, open bus, IRQ transitions, geometry,
non-power-of-two wrapping, reset and malformed snapshots. Original iNES/NES 2.0
CPU programs verify all four PRG windows, writes behind override, ignored low
ports, PPUDATA, CIRAM, repeated zero-latch IRQs and deterministic replay/rollback.
The shared boot/replay fixture now covers 115 added IDs. CLI load/boot smoke
tests cover sixteen header/geometry/mirroring combinations and eleven invalid
headers, preserving source ROMs and creating no saves. The CPU success marker
is asserted by the direct emulator test; CLI frame completion is only smoke
coverage. No external emulator source or commercial ROM was incorporated.

Validation passed all 54 MapperBus targets and eight serial ASan/UBSan suites
with the macOS `detect_leaks=0` exception. The complete mapper executable passed
75,794 assertions across 335 cases. The two initial mapper-123 regressions failed
at unsupported factory creation before implementation, then passed. Scoped
clang-tidy, formatting and whitespace checks passed. Isolated agent-documentation
validation reported zero errors, 52 warnings and three infos, matching baseline.

## T9552 follow-up: mapper 249 and mapper 4 submapper 5

The Waixing T9552 profile follows [NESdev revision 20254](https://www.nesdev.org/w/index.php?title=T9552&oldid=20254)
and the [43-319 PCB dump measurements](https://forums.nesdev.org/viewtopic.php?t=24314).
Both headers describe the same circuit. Mapper 249 files use the bank order
observed with pattern zero; NES 2.0 mapper 4 submapper 5 uses pattern two's
canonical order. The loader preserves each file's order without rewriting its
header or guessing from filenames. Mapper 4 submapper 0 remains ordinary MMC3.

A write-only latch at `$5000–$5FFF`, mask `$F000`, selects four PRG patterns
with D0–D1 and eight CHR patterns with D0–D2. Other data bits are ignored.
Power-on selects zero, including for mapper 4:5; writing two makes a canonical
file appear in ordinary MMC3 order. Low reads float, and A001 WRAM gates do not
inhibit the latch. MMC3 register addresses and data retain their ordinary wiring.

The source's columns identify MMC3 signals at physical ROM pins. Address routing
maps the selected column into the file-order column: zero for 249, two for 4:5.
PRG A14–A17 and CHR A12–A17 are permuted. Other address bits pass through.
Apply this after MMC3 PRG-mode selection and CHR pair rounding/inversion, before
wrapping by file size. In particular, do not modulo a small image's raw bank
number before scrambling: disconnected high outputs can be routed into lower
file address pins. Derived offsets are refreshed on latch/register writes.
The published 249 example, pattern two with PRG bank two yielding file bank
eight, has its own regression. PRG A18 passes through in 512 KiB files; that
physical population is not established by the measurements.

Factories accept legacy iNES 249 or NES 2.0 249:0 / 4:5, 32–512 KiB PRG in
8 KiB units, 8–256 KiB CHR ROM in 1 KiB units, and H/V CIRAM. The supported
TKROM/TLROM-like memory profiles allow no WRAM or one 8 KiB volatile/battery
chip. NES 2.0 explicitly declares the population and requires a matching battery
flag. Legacy battery images use 8 KiB NVRAM; legacy implied nonbattery RAM does
not create a chip. CHR RAM/NVRAM, four-screen wiring, mixed/larger WRAM and
unassigned 249 submappers are rejected. Non-power-of-two files use post-routing
modulo wrapping. These are emulator geometry bounds, not certification of all
physical populations. Legacy small dumps may need overdumped ordering; 4:5 is
the documented canonical format that avoids that ambiguity.

Ordinary MMC3 H/V control, A001 enable/write protection and the current IRQ
counter remain shared. The implementation retains default Sharp-style zero-latch
repeats and the existing dot-260 rendered-scanline approximation. Exact clone
IRQ/A12 timing is unmeasured. Reset clears the T9552 latch under shared reset
policy; only its power-on value is established by the source.

Both new profiles use compact MMC3 snapshots plus a masked three-bit latch and
validated immutable file-order marker. Loads reject invalid common fields,
truncation and cross-order payloads; machine failures roll back atomically.
Other mapper layouts, ordinary mapper-4 snapshots, machine version 20 and the
4 MiB state cap are unchanged. Historical 4:5 snapshots produced by the former
ordinary-MMC3 fallback are incompatible with the new profile. No bus/PPU timing
or state fields were added. Battery files contain physical WRAM bytes without
ROM address scrambling.

Eleven direct tests cover both orders, all PRG/CHR data bytes, every pattern,
PRG modes, CHR pairs/inversion/protection, post-routing small/non-power-of-two
wrapping, all 4,096 control aliases under three WRAM gates, open bus, mirroring,
reset, optional RAM/NVRAM, IRQ transitions and malformed/cross-order states.
Five original CPU fixtures cover canonical, NES 2.0 legacy-order, iNES, and
both battery profiles; each checks all patterns, PPUDATA, CIRAM, repeated IRQs
and exact replay/rollback. The shared boot/replay matrix now contains 116 added
IDs. CLI checks cover 26 header/memory profiles and 25 invalid headers, including
12 CPU-verified 8 KiB save/reload cases. Smaller CLI cases are load/boot smoke;
the full-order battery programs write a sentinel only after CPU assertions pass.
ROM files remain unchanged. No external emulator source was incorporated.

Validation passed `meson test -C buildDir --suite=mapperbus --print-errorlogs`
(all 55 targets) and eight serial ASan/UBSan suites with the macOS
`detect_leaks=0` exception. The complete mapper executable passed 256,358
assertions across 346 cases. Before implementation, the two initial tests failed
at unsupported mapper-249 creation and mapper-4:5 power-on bank selection.
Scoped clang-tidy, formatting and whitespace checks passed. Isolated agent
validation reported zero errors, 52 warnings and three infos, matching baseline.

## Waixing mixed-CHR follow-up: mappers 74 and 192

Mapper 74 covers Waixing's 43-393/43-406/860908C boards; mapper 192 covers FS308.
The implementation follows [mapper 74 revision 24258](https://www.nesdev.org/w/index.php?oldid=24258),
[mapper 192 revision 23950](https://www.nesdev.org/w/index.php?oldid=23950), and
[the mixed-CHR rules, revision 23945](https://www.nesdev.org/w/index.php?title=MMC3_with_CHR_ROM_and_CHR_RAM&oldid=23945).
Revision 24258 adds a category without changing the mapper-74 register description.

Both profiles replace a contiguous range of 1 KiB CHR ROM bank numbers with
volatile CHR RAM, starting at bank eight. Legacy iNES 74 supplies 2 KiB at banks
8–9; legacy 192 supplies 4 KiB at banks 8–11. NES 2.0 explicitly chooses the
population, with the same behavior on either mapper ID. The supported populations
are 2 and 4 KiB; missing/other NES 2.0 RAM sizes fail rather than selecting a
legacy default. CHR RAM is not bank-mirrored above its populated range.

MMC3's paired-bank alignment and CHR inversion apply before chip selection.
Decode the raw eight-bit CHR bank before ROM-size wrapping: a high bank that
wraps to ROM offset eight still selects ROM. Only the exact RAM range accepts
writes. ROM data remains immutable, including the zero writes that broke older
all-CHR-writable implementations of China Soccer League. The selected RAM page
is the bank number minus eight, independently of which PPU slot accesses it.
A001 CPU-RAM enable and write-protect gates do not protect CHR RAM.

PRG banking, standard `$E001` high-register aliases, H/V CIRAM and IRQs reuse
MMC3. The mapper-74 article mentions one-screen mirroring as another emulator's
policy, without establishing the hardware wiring; this profile retains ordinary
D0-controlled H/V behavior, including writes of two/three. That alternate
mirroring behavior, clone-specific IRQ/reset details and exact A12 filtering
remain unverified. Default Sharp-style zero-latch repeats and the current
dot-260 rendered-scanline approximation are retained. No PPU timing changes or
new bus observations were added.

Factories accept legacy iNES or NES 2.0 submapper zero, 32–512 KiB PRG in 8 KiB
units, 8–256 KiB CHR ROM in 1 KiB units, and H/V CIRAM. Smaller/non-power-of-two
ROMs wrap by physical size after RAM chip selection. Supported CPU-memory
profiles have no WRAM or one 8 KiB volatile/battery chip. NES 2.0 explicitly
declares RAM/NVRAM with a matching battery flag. Legacy battery headers select
8 KiB NVRAM; nonbattery implied WRAM is absent. CHR NVRAM, other CHR RAM sizes,
mixed/larger CPU RAM, four-screen wiring and unassigned submappers are rejected.
These are supported emulator profiles, not a complete physical-population survey.

New snapshots serialize the existing RAM arrays and compact MMC3 registers,
then append a validated immutable 16-bit CHR RAM population marker. Different
2/4 KiB populations, invalid common fields and truncated payloads fail loading;
full-machine failures restore the prior machine atomically. Same-size 74/192
mapper payloads are identical. Existing mapper layouts, machine version 20 and
the 4 MiB state cap remain unchanged. Reset clears registers/IRQs under shared
policy while retaining RAM. Only declared PRG NVRAM enters `.sav`; pattern RAM
is volatile and is restored by snapshots rather than battery files.

Twelve direct tests cover all CHR bytes in all six bank registers and both
inversion modes, paired/single-slot aliases, range boundaries, high-bank ROM
aliases, ROM clear protection, explicit sizes on both IDs, standard PRG modes,
mirroring, open bus, RAM gates, IRQs, reset and malformed/cross-population states.
Six original CPU fixtures cover both IDs with legacy defaults and both NES 2.0
sizes, including both battery profiles. Each verifies PRG, PPUDATA, ROM/RAM
selection, CIRAM, a positive IRQ and repeated zero-latch IRQs before exact replay
and rollback. The shared boot/replay matrix now contains 118 added IDs.

CLI checks cover 50 header/memory profiles and 30 invalid headers. Twelve
battery programs write a sentinel only after CPU assertions pass; save/reload
checks preserve physical 8 KiB WRAM without storing CHR RAM. The other profiles
exercise loading/booting without an externally observed CPU success marker.
Source ROMs remain unchanged. No commercial ROM or external emulator source
was incorporated.

Validation passed all 56 MapperBus targets and eight serial ASan/UBSan suites
with the macOS `detect_leaks=0` exception. The complete mapper executable passed
298,864 assertions across 358 cases. The sanitizer mapper run took 61.73 seconds;
Meson now budgets 120 seconds for this target under ASan/UBSan and retains
60 seconds in ordinary builds. A mapper-only rerun without a manual timeout
multiplier also passed (56.11 seconds); introspection verified the ordinary
60-second and sanitizer 120-second budgets.

The two initial regressions failed at unsupported factory creation before the
implementation. An added standalone clear fixture initially omitted registration;
its setup was corrected. The first complete regular/sanitizer runs then exposed
a missing NES 2.0 CHR-RAM declaration in the separate shared boot-ROM builder.
That fixture now encodes each board's RAM size, and both complete runs passed
after correction. Scoped clang-tidy, formatting and whitespace checks passed.
Isolated agent validation reported zero errors, 52 warnings and three infos,
matching baseline.

## Mapper-194 follow-up: mixed CHR starting at bank zero

Mapper 194 follows [NESdev revision 23951](https://www.nesdev.org/w/index.php?oldid=23951)
and [the mixed-CHR memory rules, revision 23945](https://www.nesdev.org/w/index.php?title=MMC3_with_CHR_ROM_and_CHR_RAM&oldid=23945).
The documented game example is Dai-2-Ji - Super Robot Taisen (As).
Legacy iNES selects 2 KiB of volatile CHR RAM at 1 KiB banks zero/one; other
banks select CHR ROM. NES 2.0 explicitly declares the RAM population. The
supported 2/4 KiB profiles use the declared number of pages starting at zero.
These size bounds describe supported metadata, not a complete board survey.

The existing mixed-CHR decoder now derives its first RAM bank from the mapper:
zero for 194 and eight for 74/192. MMC3 pair alignment and inversion precede
chip selection. When CHR ROM exists, only the exact RAM range is writable and
RAM is not mirrored into higher bank numbers. Select RAM before ROM wrapping;
for example, raw bank $80 selects ROM even when it wraps to file offset zero.
ROM writes are ignored. With no CHR ROM, the mixed-CHR summary instead specifies
mirrored RAM: every raw MMC3 bank maps modulo the physical RAM page count, with
paired banks and inversion still active. This supports RAM-only configurations
without exposing the unused portion of the core's shared RAM buffer.

Factories accept legacy iNES and NES 2.0 submapper zero, 32–512 KiB PRG in
8 KiB units, either no CHR ROM or 8–256 KiB CHR ROM in 1 KiB units, and H/V
CIRAM. NES 2.0 requires explicit 2 or 4 KiB volatile CHR RAM; legacy images use
the 2 KiB default even when a generic iNES parser infers 8 KiB for absent ROM.
Optional CPU memory follows the existing supported mixed-CHR profiles: no WRAM
or one 8 KiB volatile/battery chip, with explicit NES 2.0 metadata and a matching
battery flag. Legacy battery images use 8 KiB NVRAM and nonbattery implied WRAM
is absent. Other pattern-memory sizes, CHR NVRAM, mixed/larger CPU RAM,
four-screen wiring and unassigned submappers are rejected. Small/non-power-of-two
accepted ROMs wrap by physical size after RAM selection.

MMC3 register aliases, PRG modes, H/V control, RAM gates and IRQs are shared.
A001 does not write-protect CHR RAM. The default repeating zero-latch counter
and dot-260 rendered-scanline approximation remain; exact clone IRQ/A12 and
reset electrical behavior are unverified. Reset clears registers/IRQs under
shared policy and retains RAM. No bus/PPU timing or audio seam changed.

The new mapper's 16-bit configuration marker combines the CHR RAM size with
bank-zero wiring (bit 15) and RAM-only mirroring (bit 14). Both bits are checked
against immutable cartridge metadata when loading. This rejects cross-origin,
cross-population and ROM-presence mismatches after bounded RAM/register reads.
The old 74/192 marker remains its unmodified size word, preserving existing
payload bytes. Other mapper layouts, machine version 20 and the 4 MiB cap remain
unchanged. Full-machine failures roll back atomically. `.sav` stores only the
physical PRG NVRAM; CHR RAM is volatile and survives through snapshots.

Eight new direct cases extend the mixed-CHR group from twelve to twenty cases.
They cover legacy defaults, all CHR data bytes in all six registers and both
inversions, mixed versus mirrored RAM, boundary/high-bank ROM protection, RAM
gates, memory declarations, cross-configuration/truncated snapshots, IRQs and
open bus. Six original mapper-194 CPU fixtures cover legacy mixed/RAM-only and
both NES 2.0 sizes with and without ROM, including both battery profiles. Each
checks banking, PPUDATA, CIRAM, positive and repeated-zero IRQs, and exact replay.
Rollback tests corrupt both an active RAM byte and the configuration marker.
The old 74/192 CPU fixtures continue to run. The shared boot/replay fixture now
covers 119 added cartridge IDs.

The expanded CLI group covers 93 header/memory profiles and 45 invalid headers
across 74/192/194, including 36 CPU-verified physical-WRAM save/reload cases.
Eighteen RAM-only mapper-194 profiles exercise three PRG geometries, H/V and all
three header/population choices. Battery programs write a sentinel only after
their CHR/PRG assertions pass; remaining nonbattery cases are load/boot smoke.
Source ROMs stay unchanged, and no commercial ROM or external emulator source
was incorporated. This does not establish every game or physical board variant.

Validation passed all 56 MapperBus targets and eight serial ASan/UBSan suites
with the macOS `detect_leaks=0` exception, using the existing 120-second sanitizer
mapper budget without a manual timeout multiplier. The complete mapper
executable passed 332,008 assertions across 366 cases; the expanded mixed-CHR
group passed 75,605 assertions across twenty cases. Both initial mapper-194
regressions failed at unsupported factory creation before implementation.
Scoped clang-tidy, formatting and whitespace checks passed. Isolated agent
validation reported zero errors, 52 warnings and three infos, matching baseline.

## Mapper-195 follow-up: Waixing FS303 dynamic CHR RAM

Mapper 195 follows [NESdev revision 23952](https://www.nesdev.org/w/index.php?oldid=23952)
and the [original FS303 hardware measurements](https://forums.nesdev.org/viewtopic.php?t=19041).
Its GAL changes the CHR RAM window on a PPU write to a raw MMC3 CHR bank with
bit 7 set. CPU register writes and CHR reads have no such side effect; PPU data
is ignored. Pair alignment and inversion precede decoding, which precedes ROM
wrapping. The canonical control mask is $DA and the power-on value is $80.

| Control | RAM bank numbers (1 KiB) | Physical RAM pages |
|---|---|---|
| $80 | $28–$2B | 0–3 |
| $82 | $00–$03 | 0–3 |
| $88 | $4C–$4F | 4–7 |
| $8A | $64–$67 | 4–7 |
| $C0 | $46–$47 | 6–7 |
| $C2 | $7C–$7D | 4–5 |
| $C8 | $0A–$0B | 2–3 |
| $CA, or bit 4 set | None | ROM only |

Only 8 KiB of the mounted 32 KiB RAM chip is addressable. RAM follows raw bank
bits 0–2, preserving shared pages across window changes. The
[header-accounting discussion](https://forums.nesdev.org/viewtopic.php?p=240416#p240416)
recommends declaring the accessible 8 KiB. Supported NES 2.0 submapper-zero
images therefore declare 8 KiB volatile CHR RAM and no CHR NVRAM; legacy images
infer this wiring. Require CHR ROM, H/V CIRAM, 32–512 KiB PRG in 8 KiB units and
8–256 KiB CHR in 1 KiB units. Smaller/non-power-of-two ROMs wrap by physical size.

NES 2.0's 4 KiB volatile PRG RAM declaration selects the documented compatible
board's independent $5000–$5FFF window. An 8 KiB declaration instead populates
ordinary gated $6000–$7FFF WRAM. Optional 8 KiB PRG NVRAM can coexist with the
4 KiB auxiliary RAM, with a matching battery flag; two 8 KiB CPU chips are
rejected. Legacy battery images expose 8 KiB NVRAM and cannot select auxiliary
RAM. `.sav` contains only the 8 KiB NVRAM, excluding auxiliary and pattern RAM.

The new payload appends a $C300 configuration tag with auxiliary/base/NVRAM
population bits, the canonical CHR mode and a bounded zero/4 KiB auxiliary RAM
run. Reject wrong populations, invalid mode bits and malformed lengths. Machine
loads roll back atomically; existing mapper payloads, machine version 20 and the
4 MiB cap are unchanged. Shared reset policy restores mode $80 and retains RAM.

Keep the shared MMC3 six-bit PRG registers, aliases, H/V control, A001 gates and
repeating zero-latch IRQ profile with its dot-260 approximation. Exact clone/A12
timing, reset electrical behavior and alternate mirroring remain unverified.
Independent auxiliary RAM access regardless of A001 is the supported policy;
its electrical gating has not been measured. No ROM-hack/CRC overrides or
historical mapper-number aliases are inferred.

Regression coverage includes every command-bank alias, every PPU data byte,
all paired/inverted register values, ROM wrapping/protection, all addressable
RAM bytes, auxiliary decoding/open bus, WRAM gates and metadata rejection.
Seven original CPU programs check CHR/PRG/CIRAM behavior, positive and repeated
zero IRQs, deterministic replay and atomic rejection after RAM corruption.
CLI coverage exercises 56 header/memory profiles, including 24 CPU-verified
8 KiB save/reload cases, and rejects 17 malformed configurations without changing
the input file or creating a save. Both initial regressions failed at factory
creation before implementation.

Validation: all 57 regular targets passed; all eight focused ASan+UBSan targets
passed serially, with the mapper target completing in 62.17 seconds within its
configured 120-second sanitizer timeout. The mapper executable passed 519,587
assertions across 378 cases in both builds. Formatting, scoped clang-tidy and
whitespace checks passed. Isolated agent-document validation reported zero
errors, 52 warnings and three infos, matching baseline.

## Mapper-196 follow-up: MRCM register wiring and PRG override

Mapper 196 follows [NESdev revision 22320](https://www.nesdev.org/w/index.php?oldid=22320).
NES 2.0 selects four profiles; legacy iNES uses profile zero. Normalize only
the MMC3 register-select pin on writes at $8000–$FFFF. CPU data is unchanged.

| Profile | MMC3 A0 input | PRG behavior |
|---|---|---|
| 0 | (CPU A1 OR A2 OR A3) AND NOT A0 | Normal MMC3 until a $6000–$7FFF write; then 32 KiB override |
| 1 | CPU A1 | Normal MMC3; low writes ignored |
| 2 | CPU A2 | Normal MMC3; low writes ignored |
| 3 | CPU A1 | 32 KiB override from reset, initially bank zero |

Profile zero is the documented compatibility heuristic, not another measured
PCB wiring. For profiles zero/three, use the
[generalized mapper-189 banking convention, revision 22901](https://www.nesdev.org/w/index.php?oldid=22901):
OR the written byte's two nibbles to select a 32 KiB bank. The low decoder is
limited to $6000–$7FFF, independent of A001 under this compatibility policy.
It does not infer mapper 189's broader expansion decoder or physical variants.
Override ignores MMC3 PRG mode and R6/R7, while all MMC3 register writes remain
active for CHR, mirroring, IRQs and latent PRG state. Profile-zero override stays
enabled until reset. Reset clears the bank and restores each profile's initial
mode; exact power/reset electrical behavior has not been measured.

The supported population is ROM-only, with H/V CIRAM, 32–512 KiB PRG in 8 KiB
units and 8–256 KiB CHR in 1 KiB units. Wrap accepted smaller/non-power-of-two
ROMs by physical size. Reject unknown NES 2.0 submappers, RAM/NVRAM declarations,
battery flags, CHR-less images and alternate nametable layouts. Legacy iNES's
implied CPU RAM does not populate a chip. RAM-capable board populations are
unverified. Low reads remain open bus and cannot activate the override; no
battery file is created. Shared six-bit PRG selection, paired/inverted CHR,
ROM write protection, H/V control and Sharp-style repeating zero IRQs remain.
Exact clone/A12 timing retains the existing dot-260 approximation.

New mapper state appends a $C400-plus-profile tag and one normalized override
byte to the compact MMC3 payload. Bit 7 enables override and bits 0–3 select
the bank. Validate the immutable wiring tag, reserved bits and profile-specific
reachable modes: profiles one/two cannot enable override, profile three cannot
disable it, and an inactive profile zero must have bank zero. Existing mapper
payloads, machine version 20 and the 4 MiB cap are unchanged. Whole-machine load
failures restore the running state atomically.

Regression tests exercise all register data bytes, all low-address truth-table
entries and ignored address pins, every low-window address under A001 gates,
all override bytes in both PRG modes, small/non-power-of-two ROMs, all paired
and inverted CHR values, header rejection, open bus, reset, IRQs and snapshots.
Every possible override-state byte is checked against each profile's reachable
modes. Five original CPU programs cover legacy plus NES 2.0 profiles zero–three,
verify positive and repeated-zero IRQs, then check replay and rollback after
corrupting a live CHR register and the appended state. CLI smoke coverage loads
40 header/geometry/mirroring profiles and rejects 13 invalid configurations,
preserving ROM bytes and creating no saves. Both initial regressions failed at
unsupported factory creation before implementation.

Validation: all 58 regular targets passed. All eight focused ASan+UBSan targets
passed serially; the mapper target took 82.34 seconds within its configured
120-second sanitizer timeout. Both builds passed 789,424 mapper assertions
across 390 cases; the MRCM group passed 269,822 assertions across twelve cases.
Formatting, scoped clang-tidy and whitespace checks passed. Isolated agent
documentation validation reported zero errors, 52 warnings and three infos,
matching baseline.

## Mapper-197 follow-up: larger CHR windows and gated PRG A17

Mapper 197 follows [NESdev revision 5527](https://www.nesdev.org/w/index.php?oldid=5527).
Legacy iNES uses profile zero; NES 2.0 submappers zero–three select the documented
wiring. Physical PPU A11 drives MMC3 PA10, while PA11 is grounded (zero/three),
tied high (one), or also driven by A11 (two). Apply this wiring before MMC3 CHR
inversion. MMC3's CHR bank output selects 2 KiB pages, and physical PPU A10
passes directly to ROM A10. Preserve the full eight-bit bank output, reaching
512 KiB of CHR ROM. The resulting non-inverted windows are:

| Profile | $0000–$07FF | $0800–$0FFF | $1000–$17FF | $1800–$1FFF |
|---|---|---|---|---|
| 0, 3 | R0 even | R0 odd | R2 | R3 |
| 1 | R1 even | R1 odd | R4 | R5 |
| 2 | R0 even | R1 odd | R2 | R5 |

Inversion swaps the two halves of each row after pin wiring. Registers excluded
by a profile remain writable, without affecting that profile's CHR output.
Pattern ROM ignores writes. Apply physical-size wrapping after address wiring.

Submapper three's $6000–$7FFF latch retains data bits 3 (S) and 0 (P). With S
clear, MMC3 drives PRG A17; with S set, P supplies A17. Writes require A001's
RAM-enable bit and a clear write-protect bit. Use the 256 KiB raw PRG aperture
with fixed pages 30/31, then apply the A17 mux to every window before wrapping.
This avoids wrapping away the source pin on small/non-power-of-two ROMs. Other
profiles retain ordinary six-bit MMC3 PRG banking and ignore this low latch.

Supported factories require ROM-only H/V boards: 32–512 KiB PRG in 8 KiB units
(up to 256 KiB for submapper three) and 8–512 KiB CHR in 2 KiB units. Reject
unassigned submappers, RAM/NVRAM, battery flags, CHR-less images and alternate
nametable layouts. Ignore legacy iNES's implied CPU RAM allocation. The low
window is write-only, reads retain CPU open bus and no battery file is created.
RAM-capable populations, exact clone/A12 timing and electrical reset behavior
are unverified. Keep the existing repeating-zero/dot-260 MMC3 IRQ policy. Shared
reset clears the mux and restores enabled/unprotected RAM-interface gates.

New state appends a $C500-plus-profile wiring tag and the normalized S/P byte.
Validate the immutable profile and unused bits; only submapper three can carry
nonzero S/P state. Retain gate flags, pending IRQ progress and all MMC3 registers.
Machine loads roll back atomically on corruption. Existing mapper payloads,
machine version 20 and the 4 MiB state cap remain unchanged.

Tests cover every CHR register value in all profiles and inversion modes, both
physical A10 halves, full-size and wrapped ROMs, PRG modes, fixed/switchable A17
selection, every gate/control byte and low-window alias, reset, IRQs, headers,
open bus and malformed/cross-profile snapshots. Five original CPU programs
cover legacy and NES 2.0 profiles zero–three, including gated PRG, CHR reads and
write protection, CIRAM, positive/repeated-zero IRQs, replay and atomic rollback
after live-register corruption. CLI smoke tests load 40 header/geometry/mirroring
profiles and reject 14 invalid configurations while preserving inputs and
creating no saves. Both initial regressions failed at factory creation before
implementation.

Validation: all 59 regular targets passed. All eight focused ASan+UBSan targets
passed serially; the mapper target took 73.65 seconds within its configured
120-second sanitizer timeout. Both builds passed 1,490,144 mapper assertions
across 402 cases; the mapper-197 group passed 700,705 assertions across twelve
cases. Formatting, scoped clang-tidy and whitespace checks passed. Isolated
agent-document validation reported zero errors, 71 warnings and three infos,
identical to the unchanged November 11 baseline. Nineteen documents crossed
their verification freshness limit since the previous day's 52-warning run.

## Mapper-198 follow-up: TNROM640 compatibility model

Mapper 198 follows [NESdev revision 23191](https://www.nesdev.org/w/index.php?oldid=23191):
512 KiB of main PRG ROM at 8 KiB banks $00–$3F and 128 KiB of additional ROM at
$40–$4F. The two fixed windows use $4E/$4F, including MMC3 PRG-mode swapping.
Preserve seven PRG-register bits and interpret bit 6 as chip select. The model
uses six low bank bits on the main chip and four on the additional chip, so
$50–$7F mirror $40–$4F and write-data bit 7 is ignored. Decode each chip before
reading the combined file; whole-file modulo would incorrectly map $50 to $00.
High-bank alias wiring is an interpretation of the two-chip model, not a
physical measurement.

Pattern memory is 8 KiB volatile CHR RAM. Apply MMC3 paired alignment and
inversion, then use the low three raw bank bits for physical 1 KiB RAM pages.
Do not use the shared core's flat, PPU-addressed CHR-RAM fallback. The CPU has
4 KiB volatile RAM at $5000–$5FFF and 8 KiB battery-backed WRAM at $6000–$7FFF.
The latter uses ordinary A001 enable/write-protect gates; auxiliary and pattern
RAM remain accessible independently under the supported compatibility policy.
Auxiliary reads drive all CPU data bits. Other expansion addresses and disabled
WRAM reads retain CPU open bus.

The factory accepts the documented fixed population: exactly 640 KiB PRG, no
CHR ROM, H/V CIRAM and a battery flag. NES 2.0 submapper zero requires 4 KiB PRG
RAM, 8 KiB PRG NVRAM and 8 KiB CHR RAM, without CHR NVRAM (bytes 10/11: $76/$07).
Legacy iNES infers that population from mapper 198 and requires the battery
flag. Reject unassigned submappers, alternate ROM/RAM sizes and nametable layouts.
`.sav` contains only the 8 KiB WRAM; the auxiliary and pattern buffers stay
volatile across cartridge recreation. Shared soft reset retains all RAM and
clears registers/IRQs. Keep the repeating-zero/dot-260 MMC3 IRQ approximation.

New state appends a $C600 board tag and bounded 4 KiB auxiliary RAM run to the
compact MMC3 payload. Accept seven-bit R6/R7 only for mapper 198, validate tag
and exact auxiliary length, and rebuild bank offsets after validation. Invalid
lengths cannot allocate beyond 4 KiB; failed component loads restore a safe
buffer size. Whole-machine failures roll back RAM and registers atomically.
Previous mapper payloads, machine version 20 and the 4 MiB cap are unchanged.

The [2024 cartridge investigation](https://forums.nesdev.org/viewtopic.php?p=297200#p297200)
suggests more complex RAM banking on the inspected PCB and distinguishes a
working dump in emulation from verified hardware behavior. This implementation
targets the published compatibility model. Actual PCB RAM banking, auxiliary
enable wiring, high-bank aliases and precise clone/A12/reset behavior remain
unverified. No CRC overrides or alternate hardware profiles are inferred.

Regression tests cover every PRG byte in both modes and both header formats,
CHR pairing/inversion and high-bank RAM aliases, every RAM byte, A001 gates,
ownership/persistence boundaries, reset, open bus, IRQ progress, metadata and
malformed snapshots. Four original CPU programs cover legacy/NES 2.0 and H/V,
all memory types, PRG/CHR/CIRAM behavior, positive and repeated-zero IRQs, replay,
and atomic rollback after corrupting both live CHR and auxiliary RAM. CLI tests
verify four CPU-completed 8 KiB save/reload profiles, preserve an untouched
NVRAM byte, require a fresh success marker after reload, and reject 17 invalid
headers without creating saves or changing ROMs. Both initial regressions
failed at unsupported factory creation before implementation.

Validation: all 60 regular targets passed. All eight focused ASan+UBSan targets
passed serially; the mapper target took 75.80 seconds within its configured
120-second sanitizer timeout. Both builds passed 1,561,316 mapper assertions
across 413 cases; mapper 198 passed 71,157 assertions across eleven cases.
Formatting, scoped clang-tidy and whitespace checks passed. Isolated agent
documentation validation reported zero errors, 71 warnings and three infos,
matching the November 11 baseline.

## Mapper-199 follow-up: Waixing FS309 fixed CHR and battery SRAM

Mapper 199 follows [NESdev revision 23199](https://www.nesdev.org/w/index.php?oldid=23199).
The FS309 clone supports conventional MMC3 PRG banking through 1 MiB, preserving
seven bits in R6/R7. Fixed windows use the final two physical 8 KiB banks;
smaller/non-power-of-two accepted images wrap by physical size. The 8 KiB CHR
RAM is unbanked: all CHR-register values, pair selections and inversion bits
leave PPU-addressed pattern memory unchanged. Register writes are still stored.

The board mounts a 32 KiB battery-backed SRAM chip, with only $5000–$7FFF
visible to the CPU. Both mapped windows are persistent. MapperBus uses an
explicit linear save-layout convention: the CPU address's low fifteen bits
index the chip buffer. The raw `.sav` is exactly 32 KiB:

| CPU window | SRAM/save offsets | Supported gate policy |
|---|---|---|
| $5000–$5FFF | $5000–$5FFF | Independently accessible |
| $6000–$7FFF | $6000–$7FFF | A001 enable/write-protect |
| Unexposed cells | $0000–$4FFF | Retained on import, save and snapshots |

The pin-level SRAM order and auxiliary gate wiring are not specified in the
source; the table defines this implementation's layout and compatibility policy,
not a measured PCB pinout or a third-party save-file format. All 32 KiB survive
save/reload, including the unexposed 20 KiB. CHR RAM remains volatile across
cartridge recreation. Disabled base WRAM and unrelated expansion addresses
retain CPU open bus; low SRAM reads drive all bits. Shared soft reset retains
both RAM buffers and clears registers/IRQs. Exact clone/A12/reset electrical
behavior remains unverified; keep the repeating-zero/dot-260 MMC3 IRQ policy.

The factory accepts 32 KiB–1 MiB PRG in 8 KiB units, no CHR ROM, H/V CIRAM and
a battery flag. NES 2.0 submapper zero requires 32 KiB PRG NVRAM, no volatile
PRG RAM and 8 KiB volatile CHR RAM, without CHR NVRAM (bytes 10/11: $90/$07).
Legacy iNES infers the fitted 32 KiB chip, superseding its generic 8 KiB parser
default. Reject unassigned submappers, alternate RAM populations and nametable
layouts. The source identifies some mapper-199-labelled dumps as mapper 176;
FS309 treats $5FF3 writes as SRAM data, with no automatic mapper reassignment.

New state serializes a bounded/exact 32 KiB SRAM run, 8 KiB CHR RAM, compact MMC3
registers and a $C700 board tag. It omits the unused shared PRG-RAM array. Validate
the tag, lengths, seven-bit PRG registers and shared flags before rebuilding
offsets. Failed component loads restore a safe 32 KiB buffer extent; whole-machine
failures roll back both memory buffers and registers atomically. Previous mapper
payloads, machine version 20 and the 4 MiB cap are unchanged.

Tests cover every PRG byte in both modes across five geometries and both header
formats, all CHR-register bytes/inversion with writes to fixed RAM, every SRAM
cell, mapped/unmapped persistence, all A001 bytes, reset, open bus, IRQ progress,
metadata and malformed snapshots. Four original CPU programs cover legacy/NES
2.0 and H/V, deliberately non-contiguous CHR registers, both SRAM windows,
positive/repeated-zero IRQs, replay and atomic rollback after SRAM/CHR corruption.
CLI tests verify 20 CPU-completed 32 KiB save/reload profiles, retaining bytes in
both mapped windows and unexposed storage, and reject 17 invalid headers without
creating saves or changing ROMs. Both initial regressions failed at unsupported
factory creation before implementation.

Validation: all 61 regular targets passed. All eight focused ASan+UBSan targets
passed serially; the mapper target took 79.77 seconds within its configured
120-second sanitizer timeout. Both builds passed 1,796,882 mapper assertions
across 423 cases; FS309 passed 235,551 assertions across ten cases. Formatting,
scoped clang-tidy and whitespace checks passed. Isolated agent documentation
validation reported zero errors, 71 warnings and three infos, with findings
identical to the pre-change November 12 baseline.

## Mapper-202 follow-up: 150-in-1 address-latched multicart

Mapper 202 follows [NESdev revision 5551](https://www.nesdev.org/w/index.php?oldid=5551),
which identifies a 150-in-1 cartridge shipped with the SP60 Famiclone.
Writes anywhere in $8000–$FFFF latch address A0–A3; CPU data and higher
address bits are ignored. A1–A3 select a shared page for 16 KiB PRG and 8 KiB
CHR. A0 selects vertical (zero) or horizontal (one) CIRAM. Reads never relatch.
Ordinary mode mirrors the selected PRG page into both CPU windows. When A3
and A0 are both set, use the 32 KiB mode described by the source.

**32 KiB bank alignment remains a compatibility interpretation.** The text
does not resolve odd page selections, and the linked schematic could not be
retrieved during this implementation. MapperBus maps the selected 16 KiB page
at $8000 and its successor at $C000, with three-bit wrap: page 5 selects 5/6,
and page 7 selects 7/0. It does not align to an even pair. These choices are
explicit regression expectations, not a hardware measurement. Power-on and
soft reset normalize the latch to zero and select vertical CIRAM; actual
electrical reset behavior also remains unverified.

The supported profile accepts legacy iNES and NES 2.0 submapper zero,
16–128 KiB PRG in 16 KiB units, 8–64 KiB CHR ROM in 8 KiB units, and H/V CIRAM.
Smaller and non-power-of-two images wrap by physical ROM size after the
three-bit page selection; this is an image compatibility policy. Reject
other submappers, battery flags, declared NES 2.0 RAM/NVRAM, CHR RAM and other
nametable layouts. The generic legacy PRG-RAM default does not create RAM.
Unmapped CPU addresses retain all open-bus bits. There is no battery storage,
IRQ or expansion audio in this profile; CHR ROM writes are ignored.

New mapper-202 state contains only a $CA00 tag and a canonical four-bit latch.
Recompute mirroring from the latch. Reject wrong tags, out-of-range latches
and truncation; failed component loads restore the zero latch. Whole-machine
loads roll back atomically. Existing mapper payloads, machine version 20 and
the 4 MiB snapshot limit are unchanged.

Tests cover every latch/data byte, every high write address, all one-through-eight
PRG/CHR page counts in both header formats, ROM protection, low-address decode,
open bus, reset and malformed state. Four original CPU programs verify both
header formats and H/V metadata, odd and wrapping PRG pairs, CHR banking,
CIRAM, snapshot replay and rollback. CLI smoke tests load twelve header/size
profiles and reject fourteen invalid headers without modifying ROMs or creating
saves; CPU completion is asserted by the unit fixtures, not by the CLI smoke.

Validation: all 62 regular targets and all eight focused ASan+UBSan targets
passed. The sanitizer mapper target took 74.50 seconds within its configured
120-second timeout. Both builds passed 2,026,081 mapper assertions across 432
cases; the mapper-202 group passed 229,184 assertions across nine cases. The
two initial regressions failed at unsupported factory creation before the
implementation. Formatting, scoped clang-tidy, whitespace and registry/ledger
checks passed. Isolated agent-document validation reported zero errors,
71 warnings and three infos, identical to the pre-change November 12 baseline.

## Mapper-204 investigation: register specification still needed

[NESdev revision 5557](https://www.nesdev.org/w/index.php?oldid=5557) identifies
4-in-1, 64-in-1, 80-in-1 and 150-in-1 multicarts, but contains no register,
banking, mirroring or reset description. The [mapper discussion](https://www.nesdev.org/wiki/Talk:Mapper)
also lists usage without supplying a decoder. [CaH4e3's original changelog](https://cah4e3.shedevr.org.ru/fceultra.php)
records a mapper-204 correction for a 64-in-1 dump in December 2003, but gives no
register specification. The inspected Kevin Horton multicart indices and
Krzysiobal mapper-204 cartridge search did not establish matching hardware wiring.
The [JY-006 64-in-1 record](http://krzysiobal.com/carts/?action=view&id=268) has no
mapper assignment; its title alone is insufficient to identify it as mapper 204.

Keep 204 unregistered. A matched PCB schematic, register truth table or documented
hardware trace is needed to establish write decoding, PRG/CHR bank equations,
CIRAM selection, memory population and startup/reset behavior. Existing-emulator
support and a shared multicart title do not establish those details. No mapping
equations, ROM-size profile or synthetic success test were invented for this ID.
The mapper-205 implementation below advances the broader mapper work using a
published register specification; it does not close the mapper-204 gap.

## Mapper-205 follow-up: JC-016-2 block pins and solder pad

Mapper 205 follows [NESdev revision 5566](https://www.nesdev.org/w/index.php?oldid=5566),
including Disch's register table and the JC-016-2 solder-pad note. All writes at
$6000–$7FFF latch data bits 0–1. They select the outer PRG/CHR block immediately,
independently of MMC3 A001 gates. The low window contains no SRAM; reads retain
all CPU open-bus bits. Ordinary MMC3 registers remain at $8000–$FFFF.

Apply the published AND/OR masks to MMC3 outputs, including fixed PRG pages,
before physical-ROM wrapping:

| Block | PRG AND / OR (8 KiB pages) | CHR AND / OR (1 KiB pages) |
|---|---|---|
| 0 | $1F / $00 | $FF / $000 |
| 1 | $1F / $10 | $FF / $080 |
| 2 | $0F / $20 | $7F / $100 |
| 3 | $0F / $30 | $7F / $180 |

Retain the OR overlap in block 1. Do not replace it with an ordinary independent
outer bank. Normalize fixed PRG outputs to $3E/$3F before applying the block;
deriving them from a smaller physical image first would change the wiring.
CHR pairing and inversion precede the block pins, including CHR address bit 18.
CHR ROM remains write-protected. Block writes preserve IRQ progress and CIRAM.

The default `jc-016-2` profile uses the table directly. An optional solder pad
redirects raw block 1 to effective block 3 for both ROMs. Select it before
installation through `configure_board_jumpers(1)` or
`configure_board_profile("jc-016-2-force-block3")`; value zero / `jc-016-2`
restores ordinary wiring. The headless CLI uses the existing `--board-profile`
option. Do not infer pad position from a filename or multicart menu title.

The supported population is 32–512 KiB PRG in 8 KiB units, 8–512 KiB CHR ROM in
1 KiB units, H/V CIRAM and legacy iNES / NES 2.0 submapper zero. Reject battery
flags, declared NES 2.0 RAM/NVRAM, CHR RAM and alternate nametable/submapper
metadata. Ignore the legacy parser's generic PRG-RAM default. Wrapping smaller
or non-power-of-two ROMs after address-pin decoding is an image compatibility
policy. Shared reset clears block/register/IRQ state, restores header mirroring
and retains the physical solder pad. Exact electrical reset and clone/A12 IRQ
behavior remain unverified; retain the shared repeating-zero/dot-260 IRQ model.

New mapper-205 state omits unused RAM arrays. Its 18-byte payload stores the
compact MMC3 registers/flags, a $CD00/$CD01 immutable pad tag and the canonical
two-bit block latch. Validate the tag, latch and shared fields before rebuilding
offsets. Failed component loads reset safely; whole-machine failures roll back
atomically. Other mapper payloads, machine version 20 and the 4 MiB cap remain
unchanged. There is no battery file or expansion audio.

Regression tests cover all inner/outer data bits, PRG modes/fixed windows, CHR
pairing/inversion/high address pins, every low-register address, A001 gates,
open bus, smaller ROMs, reset, pad/profile selection and malformed snapshots.
Eight original CPU fixtures exercise both headers, H/V metadata and pad states,
ROM/CIRAM, positive and repeating-zero IRQs, replay and atomic rejection.
CLI tests smoke-load fourteen geometry/profile combinations and reject fourteen
invalid headers/profiles without changing ROMs or creating saves. CPU completion
is asserted in unit fixtures; CLI loading is smoke coverage.

Validation: all 63 regular targets and all eight focused ASan+UBSan targets
passed. The sanitizer mapper target took 79.78 seconds within its configured
120-second timeout. Both builds passed 2,233,285 mapper assertions across 442
cases; mapper 205 passed 207,189 assertions across ten cases. The three initial
regressions failed at unsupported factory creation before implementation.
Formatting, scoped clang-tidy, whitespace and registry/ledger checks passed.
Isolated agent-document validation reported zero errors, 71 warnings and three
infos, identical to the pre-change November 12 baseline.

## Mapper-208 follow-up: Gouder protection and JJ-01 submapper

Mapper 208 follows [NESdev revision 5614](https://www.nesdev.org/w/index.php?oldid=5614).
The article explicitly bases its original-board description on an emulator
compatibility model, with additional mirroring information. This implementation
uses the published register specification and functional truth table; no source
code from another emulator was read or incorporated. Exact hardware behavior
beyond that specification remains unverified.

| Profile | Selection | PRG and mirroring control | Protection |
|---|---|---|---|
| Gouder | Legacy iNES or NES 2.0 submapper 0 | Data bits 4/0 select a 32 KiB page; bit 5 selects H/V at $4800–$4FFF or $6800–$6FFF | $5000–$57FF key index; four decoded bytes at $5800–$5FFF |
| JJ-01 | NES 2.0 submapper 1 | Whole 32 KiB PRG page from R6 >> 2; ordinary MMC3 A000 mirroring | Original-board low registers are absent |

Both profiles retain MMC3 CHR pairing/inversion and IRQ registers. MMC3 PRG
mode and R7 do not split the 32 KiB window. On Gouder, ordinary A000 writes do
not drive CIRAM. Both low bank-register aliases operate independently of A001
RAM gates. Protection reads drive all eight CPU bits; the other low ports
remain open bus. Each protection-data write stores CPU data XOR the indexed
mask in one of four registers selected by address A0/A1. Changing the index
later does not alter already-decoded bytes. Instruction fetches use those same
register reads.

Production code computes the mask with an original Boolean reduction. An
independent test fixture contains all 256 published functional values and checks
every index/data/slot combination. Tests also execute a decoded absolute JMP
from $5800, rather than verifying protection only through direct method calls.

The supported ROM-only profiles accept 32–128 KiB PRG for Gouder or 32 KiB–2 MiB
for JJ-01, in 32 KiB units, and 8–256 KiB CHR ROM in 1 KiB units. Preserve the
full R6 byte only on JJ-01 and apply the literal R6 >> 2 rule before physical-size
wrapping. The large-size bounds describe the supported decoder model, not
verified PCB populations or physical register width. Smaller/non-power-of-two
images wrap after decoding. Require ordinary H/V CIRAM; reject other submappers,
battery flags, declared NES 2.0 RAM/NVRAM and CHR-RAM populations. The generic
legacy PRG-RAM default does not create RAM. No filename or CRC override chooses
the submapper, including dumps carrying an unrelated game title.

Gouder initializes its bank/mirroring latch to the documented $11 (page 3,
vertical). Shared reset normalizes the key/protection registers to zero and
restores that latch. JJ-01 resets R6 to zero and restores header mirroring.
Those reset policies and the shared repeating-zero/dot-260 IRQ approximation
are not electrical reset or clone/A12 measurements. No expansion audio or
battery persistence is exposed in these supported profiles.

New snapshots omit unused RAM arrays. The compact MMC3 state is followed by
a $D000/$D001 immutable profile tag. Gouder adds its canonical $31-masked latch,
key and four decoded bytes: 23 bytes total versus 17 on JJ-01. Validate the tag,
common fields and Gouder latch/mirroring consistency before rebuilding maps.
Failed component loads reset safely; whole-machine rejection rolls back state
atomically, including decoded protection bytes. Previous mapper payloads,
machine version 20 and the 4 MiB allocation cap remain unchanged.

Regression coverage includes every key/data byte, four protection slots, low
address aliases and open-bus drivers, PRG modes/data bytes, CHR pairing/inversion,
physical-size wrapping, A001 independence, header rejection, reset and invalid
snapshots. Six original CPU fixtures cover legacy/NES 2.0 Gouder and NES 2.0
JJ-01 under both H/V headers, protection instruction fetches, CIRAM, positive
and repeated-zero IRQs, replay and atomic rollback. CLI smoke coverage loads
fourteen geometry/header profiles and rejects sixteen invalid headers without
modifying ROMs or creating saves; CPU completion is asserted by unit fixtures.

Validation: all 64 regular targets passed with GPU checks required, and all
eight focused ASan+UBSan targets passed serially. The sanitizer mapper target
took 78.02 seconds within its configured 120-second timeout. Both builds passed
2,948,737 mapper assertions across 451 cases; mapper 208 passed 715,437 assertions
across nine cases. Both initial regressions failed at unsupported factory creation
before implementation. Formatting, scoped clang-tidy, whitespace and registry/
ledger checks passed. Isolated agent-document validation reported zero errors,
72 warnings and three infos, unchanged from the pre-change November 13 baseline.

## Mapper-214 investigation: register specification still missing

[NESdev revision 5675](https://www.nesdev.org/w/index.php?oldid=5675)
identifies Super Gun 20-in-1 but provides no register addresses, data/address-bit
decode, PRG/CHR routing, CIRAM control or reset behavior. The
[mapper discussion](https://www.nesdev.org/wiki/Talk:Mapper) also lists this
cartridge under undocumented/unimplemented mappers. Neither identification
establishes a banking specification. Mapper 81's separately documented Super Gun
board must not be assumed to describe this multicart.

Mapper 214 remains explicitly unregistered. A matching schematic, measured
register truth table or published hardware/register description is needed before
implementation. No behavior was inferred from a ROM filename or copied from
another emulator. The next implementable documented slot in this pass is 215.

## Mapper-215 follow-up: Sugar Softec scrambling and outer banks

Mapper 215 implements the register specification in
[NESdev revision 5704](https://www.nesdev.org/w/index.php?oldid=5704).
Its numeric permutation tables are functional specification data; the code is
original and no external emulator implementation was read or incorporated.
NES 2.0 submapper 0 selects UNL-8237; submapper 1 selects UNL-8237A. Legacy
iNES selects profile 0. Neither titles nor checksums choose a different profile.

Three write-only ports use address mask $F007: $5000 controls PRG override and
bank width, $5001 selects outer banks, and $5007 selects one of eight scrambling
patterns. Other low ports float. Low writes bypass MMC3 A001 gates. Scramble
the $E001-decoded MMC3 register address first, then permute only the low three
bits of writes reaching logical $8000. Preserve its PRG/CHR mode bits; ordinary
bank data, mirroring, RAM-gate and IRQ writes retain their original data bits.
Patterns 5–7 use identity address and index mappings.

| ROM output | UNL-8237 source bits in $5001 | UNL-8237A source bits in $5001 |
|---|---|---|
| PRG A18/A19[/A20] | D0/D1 | D0/D1/D3 |
| CHR A18/A19[/A20] | D2/D3 | D1/D2/D3 |
| PRG A17 when $5000 D6 is set | D4 | D4 |
| CHR A17 when $5000 D6 is set | D5 | D5 |

With $5000 D6 clear, retain the inner A17 outputs for 256 KiB outer windows.
When set, replace them for 128 KiB windows. These pins affect switchable and
fixed MMC3 PRG banks and paired/inverted CHR banks before physical ROM wrapping.
Raw fixed PRG outputs are $3E/$3F, independent of fitted image size. D7 of $5000
overrides PRG with its low-nibble 16 KiB page in both CPU halves; D5 replaces
the page's low bit with CPU A14 for aligned 32 KiB NROM mode. MMC3 registers
continue accepting writes during override and become visible again when it
is cleared. CHR remains MMC3-controlled and write-protected in every PRG mode.
Outer/mode/scrambling writes preserve CIRAM selection and IRQ progress.

Supported profiles are H/V CIRAM, PRG 32 KiB–1 MiB (profile 0) or 2 MiB
(profile 1), in 8 KiB units; CHR ROM 8 KiB–1/2 MiB respectively, in 1 KiB
units. These maxima are decoder bounds, not verified physical chip populations.
This implementation supports ROM-only boards. Reject declared RAM/NVRAM,
battery, CHR-RAM, alternate CIRAM and unknown submappers. Ignore legacy generic
PRG-RAM metadata. Optional SRAM populations are not established by this article
and are not modeled. No speculative $6xxx control aliases or mapper-258 readable
protection ports are exposed. Smaller and non-power-of-two images wrap after pin
decoding as an explicit compatibility policy. Independent PRG/CHR wrapping also
supports the article's single-game dumps with a forced PRG A18 solder pad.

Initialize $5000/$5007 to zero and canonicalize $5001's documented $xF value to
$0F. A reset restores those defaults along with shared MMC3 registers, header
mirroring and IRQ state. The article documents outer-register reset on M2
interruption; unknown power-up bits, exact reset effects on the remaining clone
registers and clone IRQ/A12 circuitry remain unverified. Retain the shared
repeating-zero/dot-260 IRQ policy. There is no expansion audio or battery file.

New 20-byte snapshots omit unused RAM, retain compact MMC3 fields, and append
the immutable $D700/$D701 wiring tag plus three canonical control bytes.
Validate the tag, register masks and shared fields before rebuilding offsets.
Component failures reset safely; whole-machine failures roll back atomically.
Existing board payloads, machine version 20 and the 4 MiB state cap are unchanged.

Regressions cover both wirings, all eight scrambling patterns and select bytes,
PRG override combinations, fixed banks, CHR pairing/inversion, low and MMC3 port
aliases, unused bits, open bus, A001 gates, smaller/non-power-of-two ROMs,
positive/repeating-zero IRQs, reset and malformed/truncated/wrong-profile states.
Six original CPU fixtures cover both submappers, legacy/NES 2.0 and H/V headers,
all scrambling modes, banked instruction fetches, CHR/CIRAM, production IRQs,
deterministic replay and failed-load rollback. CLI tests smoke-load eighteen
profiles and reject twenty-two invalid headers without changing ROMs or creating
saves; CPU completion is asserted by the unit fixtures.

Validation: all 65 regular targets (including required GPU checks) and all eight
focused ASan+UBSan targets passed. The sanitizer mapper target took 84.32 seconds
within its configured 120-second timeout. Both builds passed 3,327,826 mapper
assertions across 462 cases; mapper 215 passed 379,074 assertions across eleven
cases. The initial power-up regression failed at unsupported factory creation
before implementation. An isolated run exposed a test-fixture registry setup
dependency; the helper now initializes its own registry. Final focused and
isolated loading/CPU cases pass in both builds. Formatting, scoped clang-tidy,
whitespace and registry/ledger checks passed. Isolated agent-document validation
reported zero errors, 72 warnings and three infos across 193 documents, unchanged
from the pre-change November 13 baseline.

## Mappers 216/217: register specifications still missing

The inspected primary articles identify
[mapper 216](https://www.nesdev.org/w/index.php?oldid=5705) games, with a possible
Dendy timing requirement, and [mapper 217](https://www.nesdev.org/w/index.php?oldid=5706)
multicarts. Neither supplies register addresses or a banking/mirroring truth table.
Both remain explicitly unregistered pending a matching hardware/register source.
This pass proceeds to the documented single-chip board, mapper 218.

## Mapper-218 follow-up: Single Chip / Magic Floor

`SingleChip` implements [NESdev revision 22660](https://www.nesdev.org/w/index.php?oldid=22660)
with original code derived from its published wiring specification. No external
emulator implementation was read or incorporated. The board has a PRG ROM and
uses the console's existing 2 KiB CIRAM for pattern and nametable data. There is
no cartridge CHR memory, WRAM, battery, bank register, IRQ or expansion audio.

| Flags 6 wiring bits (3/0) | CIRAM A10 source | Pattern layout | Nametables at $2000–$2FFF |
|---|---|---|---|
| $0 (full flags $A0) | PPU A11 | 2 KiB, mirrored | Horizontal |
| $1 (full flags $A1) | PPU A10 | 2 KiB, mirrored | Vertical |
| $8 (full flags $A8) | PPU A12 | One 1 KiB bank per pattern table | Lower single screen |
| $9 (full flags $A9) | PPU A13 | Lower 1 KiB bank in both pattern tables | Upper single screen |

CIRAM A0–A9 follow the low ten PPU address bits. Preserve raw A12/A13 before
ordinary $3000 nametable folding: configuration $8 exposes the other physical
bank at $3000–$3EFF. Pattern and nametable aliases always access the same bytes.
Internal palette reads/writes remain separate, while palette reads refill the
PPUDATA buffer through the mapped $2Fxx address.

The small optional `Mapper::ciram_address` hook maps external PPU $0000–$3EFF
addresses to console CIRAM indices. Cartridge forwards it and PPU resolves it
before normal CHR/nametable routing. Both background and sprite pattern fetches
use that same resolver. Shared-memory writes invalidate cached background tile
bytes. All other boards default to ordinary routing. CIRAM remains PPU-owned;
the mapper allocates no duplicate RAM. Direct cartridge-only CHR reads have no
backing memory: emulated CPU and renderer accesses go through the PPU.
Dot timing and ADR-001's per-pixel/prefetch rules are unchanged; this is not a
new shift-register or electrical-timing model.

Support legacy iNES and NES 2.0 submapper zero, all four raw wiring flags and
nonempty power-of-two PRG images up to 32 KiB. Mirror smaller images using their
fitted address pins; arbitrary non-power-of-two dumps are rejected. The small-ROM
decoder range is an explicit supported image policy, not proof of every physical
chip population. Reject CHR ROM, NES 2.0 cartridge RAM/NVRAM, battery flags,
unknown submappers and inconsistent raw nametable metadata. Ignore legacy
generic WRAM and its automatically inferred 8 KiB CHR RAM: neither exists on
this board. Speculative extra-memory variants discussed outside the article are
not implemented. Low CPU reads remain open bus and ROM writes have no effect.

The mapper is stateless across reset and serializes only its immutable wiring
tag ($DA00/$DA01/$DA08/$DA09), two bytes. CIRAM is saved exactly once in the
existing PPU payload. Reject truncated, damaged and wrong-wiring tags; a rejected
whole-machine load restores previously decoded CIRAM atomically. The existing
console reset clears PPU RAM under its established policy. Other mapper/state
layouts, machine version 20 and the 4 MiB allocation cap are unchanged.

Regressions check every external PPU address against an independent CIRAM
model, bidirectional aliases, untouched extra VRAM, palette buffering, ROM pins,
open bus, memory/header rejection, configuration tags and truncated states.
Rendered pixels verify both background/sprite pattern-table selections and
cache invalidation through a nametable alias. Twenty original CPU fixtures cover
all wirings, both headers and supported NTSC/PAL/Dendy combinations, PPUDATA,
ROM protection, reset, deterministic replay and rollback after decoded CIRAM
was changed by a malformed state. CLI smoke tests load twenty-four profiles
(including NES 2.0 4/8 KiB PRG) and reject forty-five invalid images, preserving
ROM bytes and creating no saves. CPU completion is asserted in unit fixtures.

Validation: all 66 regular targets (including required GPU checks) and all eight
focused ASan+UBSan targets passed. Both builds passed 3,737,431 mapper assertions
across 471 cases; mapper 218 accounts for 409,590 assertions across nine cases.
The sanitizer mapper target took 86.12 seconds within its configured 120-second
timeout. The initial shared-memory regression failed at unsupported factory
creation before implementation. Formatting, scoped clang-tidy, whitespace and
registry/ledger checks passed. Isolated agent-document validation reported zero
errors, 72 warnings and three infos across 193 documents, unchanged from the
pre-change November 13 baseline.

## Mapper-219 follow-up: Kasheng A9461 compatibility model

Mapper 219 implements the published register description in
[NESdev revision 21696](https://www.nesdev.org/w/index.php?oldid=21696).
The article identifies Toy Story and Super 1997 4-in-1 / NT-8029 and cites emulator
implementations as its sources. This is a compatibility model, with explicit
policies for unresolved transitions and reset behavior. Production code is
original; no external emulator source was read or incorporated.

The $E003-decoded $8002 mode port takes priority over the ordinary $E001 MMC3
decoder. Its D5 alone selects MMC3 or extended operation; both $8000 and $8002
select the following bank-data destination. $8000 D5 is an index bit and cannot
change modes. $8001/$8003 data aliases use the current destination. In MMC3 mode,
use normal pairing, inversion and fixed PRG slots, with PRG banks reduced to four
bits and CHR banks to seven bits before adding shared 128 KiB outer banks.
Normalize fixed inner PRG outputs to $0E/$0F before wrapping physical ROMs.

Extended mode offers four independently programmable 8 KiB PRG windows. Selectors
$26/$25/$24/$23 address CPU $8000/$A000/$C000/$E000 respectively. Data D2/D3/D4/D5
drive bank bits 3/2/1/0. Selector D3 is dotted in the published diagram, so this
decoder also accepts $2E/$2D/$2C/$2B; these aliases are a diagram-based inference,
not a measured pin result. Selector D6/D7 do not invert extended mappings.

Even CHR selectors $08–$1E update one shared three-bit latch. Odd $09/$0B/$0D/$0F
commit one 1 KiB window in the lower pattern table, combining data >> 1, the latch
shifted left four, and selector D1 shifted right one. The latter OR operation
forces odd banks for the second/fourth windows; these are not atomic 2 KiB updates.
Odd $11–$1F address upper 1 KiB windows using selector D2/D3, with D1 ignored.
Those commits combine data >> 1 and latch << 4. Latch updates leave stored CHR
banks unchanged until another commit. Unsupported selectors retain outputs.

Low ports use the published $5003 decode: $5002 D0 writes outer bit 0; $5003 D5
writes outer bit 1. Each preserves the other bit and controls both ROMs. The low
decoder includes $7002/$7003 aliases and bypasses MMC3 A001 gates. Reads float;
there is no SRAM behind these ports. Scope this decoder to CPU $4020–$7FFF;
ROM-area writes retain MMC3 register decode. The article does not establish
electrical /ROMSEL gating or high-address overlap behavior. Outer/mode/index
writes preserve IRQ progress and CIRAM selection. A000/A001/C000/C001/E000/E001
retain their shared MMC3 behavior in either bank protocol.

Mode changes select independent retained MMC3 and extended bank files. The article
does not resolve whether physical bank storage is shared; this retention/remapping
rule is an explicit interpretation requiring hardware verification. Reset uses
MMC3 mode, outer $03 as required by the published menu note, zero selectors/latch,
header H/V CIRAM and cleared shared IRQs. Initialize extended PRG to 0/0/14/15 and
CHR to 0/1/0/1/0/0/0/0, matching the initial MMC3 output layout. Exact reset values
apart from the menu's outer-bank requirement, clone IRQ circuitry and A12 filtering
remain unverified. Retain the shared repeating-zero/dot-260 IRQ policy. Tests
assert these model decisions; they do not establish unmeasured hardware behavior.

Support legacy iNES and NES 2.0 submapper zero, H/V CIRAM, 32–512 KiB PRG in
8 KiB units and 8–512 KiB CHR ROM in 1 KiB units. The maxima are decoder bounds,
not verified chip populations. Only ROM-only profiles are modeled: reject battery,
declared NES 2.0 RAM/NVRAM, CHR RAM, alternate CIRAM and unknown submappers.
Ignore legacy generic WRAM. Optional SRAM populations are not specified by this
article. Apply pin limits before wrapping smaller/non-power-of-two images as an
explicit compatibility policy. There is no expansion audio or battery file.

New 33-byte state omits unused RAM, retains the compact MMC3 fields and appends
tag $DB00, selector, mode flag, CHR latch, outer bank and both extended bank arrays.
Validate widths, flags, all register arrays and normal-mode selector agreement,
then rebuild mappings. Failed component loads reset safely; whole-machine
failures roll back decoded outer banks atomically. Existing board payloads,
machine version 20 and the 4 MiB allocation cap are unchanged.

Regressions cover every data byte, outer bank, inversion mode, extended PRG slot,
CHR latch/commit selector and neighboring window; all mode/data and low-port
aliases; mode retention, unsupported selectors, smaller ROMs, open bus, A001 gates,
IRQs, reset, geometry and malformed snapshots. Four original CPU fixtures exercise
both headers/H/V metadata, both banking protocols, both ROMs, CIRAM, production
IRQs, reset, deterministic replay and atomic rejection. CLI tests smoke-load
twelve profiles and reject eighteen invalid images without changing ROM bytes
or creating saves. CPU completion is asserted in unit fixtures.

Validation: all 67 regular targets (including required GPU checks) and all eight
focused ASan+UBSan targets passed. Both builds passed 4,210,022 mapper assertions
across 484 cases; mapper 219 accounts for 472,576 assertions across thirteen
cases. The sanitizer mapper target took 88.53 seconds within its configured
120-second timeout. The initial startup regression failed at unsupported factory
creation before implementation. Formatting, scoped clang-tidy, whitespace and
registry/ledger checks passed. Isolated agent-document validation reported zero
errors, 72 warnings and three infos across 193 documents, unchanged from the
pre-change November 13 baseline.

## Mapper 220: tool/debug assignment

[NESdev revision 23976](https://www.nesdev.org/w/index.php?oldid=23976) describes
mapper 220 as a debugging assignment used by FCEUX. It also notes that some ROMs
carry this number despite needing a different mapper. Keep this slot explicitly
unregistered; it does not identify a single cartridge register layout. A verified
hash-to-board identification record is required to correct an affected image.
This pass adds no speculative hash entries, filename-based substitutions or
blanket alias. Unsupported mapper-220 images continue to be rejected.

## Mapper-221 follow-up: NTDEC N625092

`NtdecN625092` implements the published register wiring in
[NESdev revision 5744](https://www.nesdev.org/w/index.php?oldid=5744), with
[UNIF-to-NES 2.0 mapping](https://www.nesdev.org/wiki/UNIF_to_NES_2.0_Mapping)
identifying BMC-N625092 as mapper 221 submapper zero. The implementation is original;
no external emulator source was read or incorporated. It owns the PRG image and
one 8 KiB unbanked CHR RAM array, with no unused CPU-RAM backing store.

Writes to $8000–$BFFF latch address bits A0–A4, A8 and A9; writes to $C000–$FFFF
latch A0–A3. Written data is ignored, reads never relatch, and the two registers
are independent. Outer A0 selects vertical/horizontal CIRAM. A2–A4 drive PRG
A17–A19, and A9 drives PRG A20 under the article's register model. Decode the
inner 16 KiB page before wrapping the fitted ROM size:

| Outer A1 | Outer A8 | PRG $8000–$BFFF | PRG $C000–$FFFF |
|---|---|---|---|
| 0 | Either | Inner A0–A2 | Same page |
| 1 | 0 | Inner page with bit 0 cleared | Following odd page |
| 1 | 1 | Inner A0–A2 | Page 7 within the selected 128 KiB block |

The 32 KiB mode aligns the pair; odd inner selections choose the same pair as
the preceding even selection. The article repeats the label NROM-128 in that
line, but explicitly describes a 32 KiB window selected by Inner Bank >> 1.
Follow the size/formula rather than reproducing the inconsistent label.
CHR RAM reads are always enabled. Inner A3 inhibits writes across the full
8 KiB; changing outer/mode/mirroring bits leaves both the protection bit and RAM
contents intact. Re-enabling writes preserves the previous bytes. No banked
CHR memory, low CPU registers, IRQs, expansion audio or battery storage exist
in the supported board model. Unmapped CPU reads retain open bus.

Support legacy iNES and NES 2.0 submapper zero, ordinary H/V CIRAM metadata,
16 KiB–2 MiB PRG in 16 KiB units and no CHR ROM. The 2 MiB limit follows the
published PRG-A20 decoder; it does not certify that every PCB revision exposes
that pin or fits that population. Smaller/non-power-of-two images wrap after
bank-pin decoding as an explicit image compatibility policy. NES 2.0 must
declare exactly 8 KiB volatile CHR RAM and no other RAM/NVRAM. Reject batteries,
alternate CIRAM, unknown submappers and incompatible memory geometries. Ignore
the legacy parser's generic WRAM default; its inferred CHR RAM supplies this
board's documented pattern memory. UNIF parsing itself is unchanged.

Power-on uses zeroed CHR RAM and zero latches. Mapper reset clears both latches,
restores mirrored page zero/vertical CIRAM and enables CHR writes while retaining
RAM. The register article does not specify exact electrical power-up/reset
values, so these are deterministic emulator policies pending hardware evidence.
Header mirroring does not override the reset latch or later A0 writes.

New component state is 8,197 bytes: tag $DD00, canonical two-byte outer latch,
one-byte inner latch and the 8 KiB RAM. Validate fields and read RAM into fixed-size
temporary storage before committing it. A failed component load resets latches
and retains existing RAM. Whole-machine rejection restores all latches and bytes
atomically. Existing board payloads, machine version 20 and the 4 MiB state cap
are unchanged.

Regressions cover all inner/outer/mode/mirror combinations, PRG A20, all write
data bytes, every register address and CHR cell, independent protection, smaller
ROMs, low open bus, reset, geometry and malformed states. Four original CPU
fixtures exercise both headers/H/V metadata, instruction fetches through all PRG
modes, PPUDATA protection, CIRAM, CHR retention across reset, deterministic replay
and atomic rejection after a malformed state changes RAM. CLI tests smoke-load
twelve profiles and reject eighteen invalid images, preserving ROM bytes and
creating no saves. CPU completion is asserted in unit fixtures.

Validation: all 68 regular targets (including required GPU checks) and all eight
focused ASan+UBSan targets passed. Both builds passed 4,500,466 mapper assertions
across 494 cases; mapper 221 accounts for 290,429 assertions across ten cases.
The sanitizer mapper target took 91.98 seconds within its configured 120-second
timeout. The initial banking regression failed at unsupported factory creation
before implementation. Formatting, scoped clang-tidy, whitespace and registry/
ledger checks passed. Isolated agent-document validation reported zero errors,
72 warnings and three infos across 193 documents, unchanged from the pre-change
November 13 baseline.

## Mapper-227 follow-up: address-latched multicarts and RPG boards

Original implementation in `Multicart227` follows [NESdev mapper 227 r23188](https://www.nesdev.org/w/index.php?oldid=23188)
and the [hardware-tested 32 KiB alignment clarification, talk r14159](https://www.nesdev.org/w/index.php?oldid=14159).
No external emulator implementation was consulted or copied. The supported
profiles are submappers 0, 1 and 2, plus legacy headers with battery-based
population selection. Registration now covers 146 cartridge IDs.

All writes at $8000-$FFFF replace an eleven-bit address latch; write data and
A11-A14 are ignored. A6/A5/A2 select inner PRG A16-A14; A8/A4/A3 select outer
A19-A17. S selects mirrored 16 KiB or an aligned 32 KiB pair. O=0 selects a
fixed upper inner bank, first or last according to L, on multicart populations.
A1 independently selects H/V CIRAM. Submapper 2 applies its outer A18/A17 zero
rule separately to either CPU window selecting inner bank zero, retaining A19.
The decoder precedes fitted-ROM wrapping, including non-power-of-two compatibility
images; such image sizes are not a claim about physical chip populations.

Submapper 1's m bit replaces ROM A3-A0 with four solder pads for every ROM read,
including instruction fetches. Pads default to zero, are configured through
`configure_board_jumpers(0..15)` or `--board-profile menu-0` through `menu-15`,
and survive console reset. Other submappers reject these configuration requests
and ignore m. A RAM-resident original CPU probe verifies pad reads before
clearing m and returning to ROM.

The source's submapper-zero description conflicts with its explicit no-battery
CHR-protection rule. This implementation follows the population rule: no-battery
submapper 0 is a multicart, while battery-backed submapper 0 is the RPG variant.
Submappers 1/2 are multicarts and reject battery RAM. Multicarts inhibit CHR writes
when O=1. RPG boards have writable CHR throughout and unbanked 8 KiB NVRAM at
$6000-$7FFF. The statement that RPG boards omit UNROM modes is interpreted as
NROM banking independent of O. That O=0 interpretation and the submapper-2
window-by-window rule remain compatibility assumptions pending stronger PCB
or hardware-test evidence; synthetic fixtures verify the stated model.

Accept PRG ROM from 16 KiB through 1 MiB in 16 KiB units, no CHR ROM, ordinary
H/V CIRAM and exactly 8 KiB volatile CHR RAM in NES 2.0 headers. NES 2.0 RPG
headers must declare exactly 8 KiB PRG NVRAM and no volatile CPU RAM. Other
RAM, CHR NVRAM, alternate CIRAM, larger ROMs and unknown submappers are rejected.
Ignore legacy generic PRG-RAM inference on multicarts. Only fitted WRAM is
allocated. Low CPU addresses float without that chip; there is no IRQ or audio.
Reset clears the latch to bank zero/vertical/write-enabled, retaining CHR RAM,
WRAM and solder pads. Power-on RAM zeroing is deterministic emulator policy.

Component snapshots contain a two-byte hardware/pad tag, two-byte latch,
8 KiB CHR RAM and, only for RPG boards, 8 KiB WRAM: 8196 or 16388 bytes.
Read RAM locally and validate tag, canonical latch and truncation before commit.
Component rejection resets the latch while retaining prior RAM and configured
pads; whole-machine rejection rolls back atomically. Existing board payloads,
machine version 20 and the 4 MiB allocation ceiling are unchanged.

Regression coverage includes all bank/mode/mirroring combinations, all register
addresses, data-byte independence, smaller ROM wrapping, every CHR cell,
protection, solder-pad nibbles, absent-memory open bus, WRAM import and reset,
header rejection, malformed snapshots and configuration mismatch. Eight original
CPU fixtures cover legacy/NES 2.0, every population, menu pads 0/5/15, PPUDATA,
CIRAM, reset retention and deterministic replay. CLI tests check 34 loading
profiles, six battery-save reloads and 22 rejected inputs. Battery firmware leaves
an observable completion marker and increments a persisted boot counter; CLI
frame completion alone is only loading smoke for non-battery populations.

Validation: all 69 regular targets, including required GPU checks, and all eight
focused ASan+UBSan targets passed. Both builds passed 5,059,838 mapper assertions
across 504 cases; mapper 227 contributes 559,357 assertions across ten cases.
The sanitizer mapper target took 95.81 seconds within its configured 120-second
timeout. The initial regression failed at factory creation before implementation.
Formatting, scoped clang-tidy, whitespace and registry/ledger equality checks
passed. Isolated canonical-document validation retained the November 13 baseline:
zero errors, 72 warnings and three infos across 193 documents.

### Classification of the preceding unimplemented IDs

- [Mapper 222 r5746](https://www.nesdev.org/w/index.php?oldid=5746) identifies
  CTC-31 and related ports using VRC2 plus discrete IRQ logic, but supplies no
  register or IRQ truth table and points to an emulator implementation. It stays
  unregistered until published hardware/register evidence supports original code.
- [Mapper 223 r24635](https://www.nesdev.org/w/index.php?oldid=24635) calls the
  assignment an apparent duplicate and directs use of mapper 199, already
  implemented here. It is classified as obsolete; no speculative blanket alias
  or ROM-hash correction is added.
- [Mapper 224 r5752](https://www.nesdev.org/w/index.php?oldid=5752) identifies
  Jncota KT-008 and the same chipset as mapper 268 submapper 1. Chipset identity
  alone does not establish complete wiring equivalence. Keep 224 unregistered
  pending KT-008/268 family research and a tested board-specific contract.

## Mapper-228 follow-up: Active Enterprises Action 52 / Cheetahmen II

Original `Action52` implementation follows [NESdev mapper 228 r5784](https://www.nesdev.org/w/index.php?oldid=5784),
[mark_k's 2014-01-03 physical board examination](https://forums.nesdev.org/viewtopic.php?p=123220),
and [talk r14166](https://www.nesdev.org/w/index.php?oldid=14166).
The forum was available through its indexed text while direct access returned 403.
No external emulator implementation was consulted or copied.

Writes at $8000-$FFFF latch address pins and data D1-D0. Address A13 selects
H/V CIRAM, A12-A11 select the PRG chip, A10-A6 select a 16 KiB page and A5
selects mirrored 16 KiB versus an aligned even/odd 32 KiB pair. A3-A0 combine
with the two data bits to select CHR; A4/A14 and data D7-D2 are ignored.
CHR is read-only and independent of the PRG chip enable.

For packed 1536 KiB PRG images, logical chips 0/1/3 map to consecutive 512 KiB
segments. Chip select 2 drives no CPU data bits, retaining the bus through the
existing `prg_read_mask` seam. Writes can restore another chip while reads
float. For single-chip images through 512 KiB, all chip selections leave PRG
enabled, following the examined 023-N509 REV 0 Cheetahmen II board's cut-trace
population. ROM size selects these populations because no assigned submapper
is documented. This selection is a file-format compatibility policy, not a
claim about every physical revision. Alternative enable wiring needs evidence
and a separate explicit profile.

The often-repeated four-nibble expansion RAM is absent. Low CPU reads float and
writes have no effect. The talk page corrects the older claim that software never
accesses those addresses; absence of the RAM does not depend on that claim.
No WRAM, CHR RAM, battery, IRQ or expansion audio is allocated or implemented.

Accept legacy and NES 2.0 submapper zero, ordinary H/V metadata, 8-512 KiB CHR
ROM in 8 KiB units and either packed 1536 KiB PRG or 16-512 KiB single-chip PRG
in 16 KiB units. Decode pins before wrapping smaller fitted ROMs. Small and
non-power-of-two single-chip images are compatibility inputs, not certified chip
populations. Reject incomplete/more-chip PRG layouts, CHR-less images, unknown
submappers, alternate CIRAM, battery and NES 2.0 RAM declarations. Ignore legacy
generic PRG-RAM inference. Reset models the published reset-time $8000=0 write:
PRG pages 0/1, CHR bank zero and vertical CIRAM. Exact electrical latch reset
wiring is not established; this remains deterministic console-reset policy.

Snapshots are five bytes: two-byte $E400/$E401 population tag, two-byte canonical
address latch (mask $3FEF) and one-byte two-bit data latch. Validate all fields
before commit; component rejection resets latches, while whole-machine failure
rolls back atomically. Existing board payloads, machine version 20 and the 4 MiB
state allocation ceiling are unchanged.

Tests cover all CPU register addresses, chip/page/mode combinations, address and
data aliases, every CHR bank, read-only pattern memory, smaller chips, low open
bus, missing-chip reads/reconnection, header rejection and malformed snapshots.
Eight original CPU fixtures cover both populations, legacy/NES 2.0 and H/V
metadata. Replicated firmware checks banked instruction fetches, PPUDATA,
CIRAM, reset and replay; a CPU-RAM routine selects the absent chip and checks
retained absolute-operand bytes before reconnecting ROM. CLI checks 48 loading
profiles and 26 invalid images, preserving ROM bytes and creating no saves.
CLI frame completion is loading smoke; unit fixtures assert CPU completion.

Validation: all 70 regular targets, including required GPU checks, and all eight
focused ASan+UBSan targets passed. Both builds passed 5,505,482 mapper assertions
across 513 cases; mapper 228 contributes 445,629 assertions across nine cases.
The sanitizer mapper target took 97.77 seconds within its existing 120-second
timeout. The initial regression failed at factory creation before implementation.
Formatting, scoped clang-tidy, whitespace and registry/ledger equality checks
passed. Isolated canonical-document validation retained the November 14 baseline:
zero errors, 72 warnings and three infos across 193 documents.

## Every documented registry slot

This inventory contains **533 existing article links** from
[NESdev registry revision 24625](https://www.nesdev.org/w/index.php?title=Mapper&oldid=24625).
It includes duplicate, reserved, and tool-use articles, so its row count is not a
count of physical cartridge designs. The other 235 grid slots had no article in
that survey; no hardware or assignment status is inferred for them. IDs beyond
the inspected 0–767 grid are not classified here.

“New implementation” means the tested behavior described above. “Not registered”
is an explicit remaining classification/implementation item, not a silent NROM
fallback. The registry rejects unsupported IDs.
Historical/obsolete duplicates are classified separately; they do not imply another PCB
or another registered cartridge ID.

| ID | Specification identification | Implementation status | Source revision |
|---:|---|---|---|
| 000 | NROM | Existing registration: Nrom | [r23660](https://www.nesdev.org/w/index.php?oldid=23660) |
| 001 | MMC1 / SxROM | Existing registration: Mmc1 | [r24550](https://www.nesdev.org/w/index.php?oldid=24550) |
| 002 | UxROM | Existing registration: Uxrom | [r24147](https://www.nesdev.org/w/index.php?oldid=24147) |
| 003 | CNROM | Existing registration: Cnrom | [r24346](https://www.nesdev.org/w/index.php?oldid=24346) |
| 004 | MMC3 / MMC6, T9552 submapper 5 | Existing registration: Mmc3 (T9552 profile added) | [r24445](https://www.nesdev.org/w/index.php?oldid=24445) |
| 005 | MMC5 / ExROM | Existing registration: Mmc5 | [r24329](https://www.nesdev.org/w/index.php?oldid=24329) |
| 006 | FFE Magic Card | Not registered | [r18921](https://www.nesdev.org/w/index.php?oldid=18921) |
| 007 | AxROM | Existing registration: Axrom | [r24152](https://www.nesdev.org/w/index.php?oldid=24152) |
| 008 | FFE Magic Card alias | Not registered | [r18921](https://www.nesdev.org/w/index.php?oldid=18921) |
| 009 | MMC2 / PxROM | Existing registration: Mmc2 | [r22483](https://www.nesdev.org/w/index.php?oldid=22483) |
| 010 | MMC4 / FxROM | New implementation: Mmc4 | [r6620](https://www.nesdev.org/w/index.php?oldid=6620) |
| 011 | Color Dreams | Existing registration: ColorDreams | [r24575](https://www.nesdev.org/w/index.php?oldid=24575) |
| 012 | Gouder SL-5020B / FFE 4M | Not registered | [r18954](https://www.nesdev.org/w/index.php?oldid=18954) |
| 013 | CPROM | New implementation: DiscreteMapper | [r1200](https://www.nesdev.org/w/index.php?oldid=1200) |
| 014 | Board-specific specification | Not registered | [r19503](https://www.nesdev.org/w/index.php?oldid=19503) |
| 015 | Board-specific specification | Not registered | [r19956](https://www.nesdev.org/w/index.php?oldid=19956) |
| 016 | Bandai FCG / LZ93D50 | New implementation: BandaiFcg | [r3876](https://www.nesdev.org/w/index.php?oldid=3876) |
| 017 | FFE Super Magic Card | Not registered | [r18933](https://www.nesdev.org/w/index.php?oldid=18933) |
| 018 | Jaleco SS88006 | New implementation: JalecoSs88006 (external speech unsupported) | [r3918](https://www.nesdev.org/w/index.php?oldid=3918) |
| 019 | Namco 129/163 | Existing registration: Namco163 | [r24377](https://www.nesdev.org/w/index.php?oldid=24377) |
| 020 | Famicom Disk System | Separate FDS subsystem | [r23632](https://www.nesdev.org/w/index.php?oldid=23632) |
| 021 | VRC4 | New implementation: KonamiVrc | [r24169](https://www.nesdev.org/w/index.php?oldid=24169) |
| 022 | VRC2a | New implementation: KonamiVrc | [r24169](https://www.nesdev.org/w/index.php?oldid=24169) |
| 023 | VRC2b / VRC4 | New implementation: KonamiVrc | [r24169](https://www.nesdev.org/w/index.php?oldid=24169) |
| 024 | VRC6a | Existing registration: Vrc6 | [r23687](https://www.nesdev.org/w/index.php?oldid=23687) |
| 025 | VRC2c / VRC4 | New implementation: KonamiVrc | [r24169](https://www.nesdev.org/w/index.php?oldid=24169) |
| 026 | VRC6b | Existing registration: Vrc6 | [r23687](https://www.nesdev.org/w/index.php?oldid=23687) |
| 027 | Unlicensed VRC4 variant | Not registered | [r23209](https://www.nesdev.org/w/index.php?oldid=23209) |
| 028 | Action 53 | New implementation: DiscreteMapper | [r21316](https://www.nesdev.org/w/index.php?oldid=21316) |
| 029 | RET-CUFROM | Not registered | [r4093](https://www.nesdev.org/w/index.php?oldid=4093) |
| 030 | UNROM 512 | New implementation: FlashBoards | [r23183](https://www.nesdev.org/w/index.php?oldid=23183) |
| 031 | NSF-subset cartridge | New implementation: DiscreteMapper | [r19421](https://www.nesdev.org/w/index.php?oldid=19421) |
| 032 | Irem G-101 | New implementation: Irem | [r23869](https://www.nesdev.org/w/index.php?oldid=23869) |
| 033 | Taito TC0190 / TC0350 subset | New implementation: TaitoTc | [r24359](https://www.nesdev.org/w/index.php?oldid=24359) |
| 034 | BNROM / NINA-001 | New implementation: DiscreteMapper | [r24073](https://www.nesdev.org/w/index.php?oldid=24073) |
| 035 | J.Y. Company ASIC | New implementation: JyCompany | [r24184](https://www.nesdev.org/w/index.php?oldid=24184) |
| 036 | TXC/JV001 protection board | New implementation: Txc | [r4196](https://www.nesdev.org/w/index.php?oldid=4196) |
| 037 | Board-specific specification | Not registered | [r4204](https://www.nesdev.org/w/index.php?oldid=4204) |
| 038 | Board-specific specification | New implementation: DiscreteMapper | [r23289](https://www.nesdev.org/w/index.php?oldid=23289) |
| 039 | Board-specific specification | Not registered | [r4217](https://www.nesdev.org/w/index.php?oldid=4217) |
| 040 | Board-specific specification | Not registered | [r19687](https://www.nesdev.org/w/index.php?oldid=19687) |
| 041 | Board-specific specification | Not registered | [r24572](https://www.nesdev.org/w/index.php?oldid=24572) |
| 042 | Board-specific specification | Not registered | [r4239](https://www.nesdev.org/w/index.php?oldid=4239) |
| 043 | Board-specific specification | Not registered | [r4258](https://www.nesdev.org/w/index.php?oldid=4258) |
| 044 | Board-specific specification | Not registered | [r4263](https://www.nesdev.org/w/index.php?oldid=4263) |
| 045 | Board-specific specification | Not registered | [r22818](https://www.nesdev.org/w/index.php?oldid=22818) |
| 046 | Board-specific specification | New implementation: DiscreteMapper | [r4278](https://www.nesdev.org/w/index.php?oldid=4278) |
| 047 | Board-specific specification | Not registered | [r4284](https://www.nesdev.org/w/index.php?oldid=4284) |
| 048 | Taito TC0690 | New implementation: TaitoTc | [r22585](https://www.nesdev.org/w/index.php?oldid=22585) |
| 049 | Board-specific specification | Not registered | [r4300](https://www.nesdev.org/w/index.php?oldid=4300) |
| 050 | Board-specific specification | Not registered | [r4310](https://www.nesdev.org/w/index.php?oldid=4310) |
| 051 | Board-specific specification | Not registered | [r4311](https://www.nesdev.org/w/index.php?oldid=4311) |
| 052 | Board-specific specification | Not registered | [r21408](https://www.nesdev.org/w/index.php?oldid=21408) |
| 053 | Board-specific specification | Not registered | [r4320](https://www.nesdev.org/w/index.php?oldid=4320) |
| 054 | Board-specific specification | Not registered | [r4322](https://www.nesdev.org/w/index.php?oldid=4322) |
| 055 | Board-specific specification | Not registered | [r4324](https://www.nesdev.org/w/index.php?oldid=4324) |
| 056 | Board-specific specification | Not registered | [r4330](https://www.nesdev.org/w/index.php?oldid=4330) |
| 057 | Board-specific specification | Not registered | [r4333](https://www.nesdev.org/w/index.php?oldid=4333) |
| 058 | Board-specific specification | New implementation: DiscreteMapper | [r4340](https://www.nesdev.org/w/index.php?oldid=4340) |
| 059 | Board-specific specification | Not registered | [r22300](https://www.nesdev.org/w/index.php?oldid=22300) |
| 060 | Board-specific specification | Not registered | [r22301](https://www.nesdev.org/w/index.php?oldid=22301) |
| 061 | Board-specific specification | New implementation: DiscreteMapper | [r22920](https://www.nesdev.org/w/index.php?oldid=22920) |
| 062 | Board-specific specification | New implementation: DiscreteMapper | [r4357](https://www.nesdev.org/w/index.php?oldid=4357) |
| 063 | Board-specific specification | Not registered | [r19134](https://www.nesdev.org/w/index.php?oldid=19134) |
| 064 | Tengen RAMBO-1 | New implementation: Rambo1 | [r23936](https://www.nesdev.org/w/index.php?oldid=23936) |
| 065 | Irem H3001 | New implementation: Irem | [r24119](https://www.nesdev.org/w/index.php?oldid=24119) |
| 066 | GxROM / MxROM | New implementation: DiscreteMapper | [r24003](https://www.nesdev.org/w/index.php?oldid=24003) |
| 067 | Sunsoft 3 | New implementation: Sunsoft34 | [r23676](https://www.nesdev.org/w/index.php?oldid=23676) |
| 068 | Sunsoft 4 | New implementation: Sunsoft34 | [r23720](https://www.nesdev.org/w/index.php?oldid=23720) |
| 069 | FME-7 / Sunsoft 5A/5B | Existing registration: Sunsoft5b | [r24262](https://www.nesdev.org/w/index.php?oldid=24262) |
| 070 | Bandai discrete board | New implementation: DiscreteMapper | [r24524](https://www.nesdev.org/w/index.php?oldid=24524) |
| 071 | Camerica / Codemasters | New implementation: DiscreteMapper | [r21755](https://www.nesdev.org/w/index.php?oldid=21755) |
| 072 | Jaleco JF-17 | New implementation: JalecoDiscrete (external speech unsupported) | [r24541](https://www.nesdev.org/w/index.php?oldid=24541) |
| 073 | VRC3 | New implementation: KonamiVrc | [r21300](https://www.nesdev.org/w/index.php?oldid=21300) |
| 074 | Waixing 43-393/43-406/860908C mixed CHR | New implementation: Mmc3 | [r24258](https://www.nesdev.org/w/index.php?oldid=24258) |
| 075 | VRC1 | New implementation: KonamiVrc | [r12313](https://www.nesdev.org/w/index.php?oldid=12313) |
| 076 | Namco 109 variant | New implementation: Namco108 | [r21294](https://www.nesdev.org/w/index.php?oldid=21294) |
| 077 | Irem Napoleon Senki / LROG017 | New implementation: IremLrog017 | [r24538](https://www.nesdev.org/w/index.php?oldid=24538) |
| 078 | Holy Diver / Cosmo Carrier | New implementation: DiscreteMapper | [r21584](https://www.nesdev.org/w/index.php?oldid=21584) |
| 079 | NINA-03 / NINA-06 | New implementation: DiscreteMapper | [r22985](https://www.nesdev.org/w/index.php?oldid=22985) |
| 080 | Taito X1-005 | New implementation: TaitoX1005 | [r24449](https://www.nesdev.org/w/index.php?oldid=24449) |
| 081 | Board-specific specification | New implementation: DiscreteMapper | [r4583](https://www.nesdev.org/w/index.php?oldid=4583) |
| 082 | Taito X1-017 | New implementation: TaitoX1017 | [r24125](https://www.nesdev.org/w/index.php?oldid=24125) |
| 083 | Cony / Yoko | Not registered | [r24580](https://www.nesdev.org/w/index.php?oldid=24580) |
| 084 | Board-specific specification | Not registered | [r24088](https://www.nesdev.org/w/index.php?oldid=24088) |
| 085 | VRC7 | Existing registration: Vrc7 | [r22102](https://www.nesdev.org/w/index.php?oldid=22102) |
| 086 | Jaleco JF-13 | Not registered | [r24513](https://www.nesdev.org/w/index.php?oldid=24513) |
| 087 | Jaleco / Konami J87 | New implementation: DiscreteMapper | [r23104](https://www.nesdev.org/w/index.php?oldid=23104) |
| 088 | Namco 118 variant | New implementation: Namco108 | [r23903](https://www.nesdev.org/w/index.php?oldid=23903) |
| 089 | Sunsoft 2 | New implementation: DiscreteMapper | [r24544](https://www.nesdev.org/w/index.php?oldid=24544) |
| 090 | J.Y. Company ASIC | New implementation: JyCompany | [r24184](https://www.nesdev.org/w/index.php?oldid=24184) |
| 091 | Board-specific specification | Not registered | [r22474](https://www.nesdev.org/w/index.php?oldid=22474) |
| 092 | Jaleco JF-19 | New implementation: JalecoDiscrete (external speech unsupported) | [r4687](https://www.nesdev.org/w/index.php?oldid=4687) |
| 093 | Sunsoft 2 | New implementation: DiscreteMapper | [r24542](https://www.nesdev.org/w/index.php?oldid=24542) |
| 094 | UxROM bit-layout variant | New implementation: DiscreteMapper | [r24548](https://www.nesdev.org/w/index.php?oldid=24548) |
| 095 | Namco 118 nametable variant | New implementation: Namco108 | [r23892](https://www.nesdev.org/w/index.php?oldid=23892) |
| 096 | Oeka Kids | Not registered | [r24545](https://www.nesdev.org/w/index.php?oldid=24545) |
| 097 | Irem TAM-S1 | New implementation: DiscreteMapper | [r22478](https://www.nesdev.org/w/index.php?oldid=22478) |
| 098 | Reserved | Not registered | [r22148](https://www.nesdev.org/w/index.php?oldid=22148) |
| 099 | Vs. System board | Not registered | [r23485](https://www.nesdev.org/w/index.php?oldid=23485) |
| 100 | Emulator tool use | Not registered | [r23979](https://www.nesdev.org/w/index.php?oldid=23979) |
| 101 | JF-10 legacy dump ordering | New implementation: JalecoDiscrete | [r21360](https://www.nesdev.org/w/index.php?oldid=21360) |
| 102 | Board-specific specification | Not registered | [r4801](https://www.nesdev.org/w/index.php?oldid=4801) |
| 103 | Board-specific specification | Not registered | [r4802](https://www.nesdev.org/w/index.php?oldid=4802) |
| 104 | Codemasters Golden Five | Not registered | [r9567](https://www.nesdev.org/w/index.php?oldid=9567) |
| 105 | NES-EVENT / MMC1 | Not registered | [r23388](https://www.nesdev.org/w/index.php?oldid=23388) |
| 106 | Board-specific specification | Not registered | [r4815](https://www.nesdev.org/w/index.php?oldid=4815) |
| 107 | Board-specific specification | New implementation: DiscreteMapper | [r24577](https://www.nesdev.org/w/index.php?oldid=24577) |
| 108 | FDS-to-cartridge boards | Not registered | [r21237](https://www.nesdev.org/w/index.php?oldid=21237) |
| 109 | Board-specific specification | Not registered | [r4825](https://www.nesdev.org/w/index.php?oldid=4825) |
| 110 | Board-specific specification | Not registered | [r4826](https://www.nesdev.org/w/index.php?oldid=4826) |
| 111 | GTROM / earlier MMC1 variant | New implementation: FlashBoards / Mmc1 | [r24005](https://www.nesdev.org/w/index.php?oldid=24005) |
| 112 | Board-specific specification | Not registered | [r23867](https://www.nesdev.org/w/index.php?oldid=23867) |
| 113 | Board-specific specification | New implementation: DiscreteMapper | [r20481](https://www.nesdev.org/w/index.php?oldid=20481) |
| 114 | SuperGame/Hosenkan scrambled MMC3, submappers 0/1 | New implementation: Mmc3 | [r4858](https://www.nesdev.org/w/index.php?oldid=4858) |
| 115 | Kasheng SFC-02B/-03/-004 | New implementation: Mmc3 | [r21614](https://www.nesdev.org/w/index.php?oldid=21614) |
| 116 | Board-specific specification | Not registered | [r22958](https://www.nesdev.org/w/index.php?oldid=22958) |
| 117 | Board-specific specification | Not registered | [r4888](https://www.nesdev.org/w/index.php?oldid=4888) |
| 118 | TxSROM / MMC3 | New implementation: Mmc3 variant | [r24058](https://www.nesdev.org/w/index.php?oldid=24058) |
| 119 | TQROM / MMC3 | New implementation: Mmc3 variant | [r24251](https://www.nesdev.org/w/index.php?oldid=24251) |
| 120 | Board-specific specification | Not registered | [r20571](https://www.nesdev.org/w/index.php?oldid=20571) |
| 121 | Board-specific specification | Not registered | [r23905](https://www.nesdev.org/w/index.php?oldid=23905) |
| 122 | Board-specific specification | Not registered | [r22366](https://www.nesdev.org/w/index.php?oldid=22366) |
| 123 | Kasheng H2288 / UNL-H2288 | New implementation: Mmc3 | [r4940](https://www.nesdev.org/w/index.php?oldid=4940) |
| 124 | Board-specific specification | Not registered | [r4941](https://www.nesdev.org/w/index.php?oldid=4941) |
| 125 | Board-specific specification | Not registered | [r4945](https://www.nesdev.org/w/index.php?oldid=4945) |
| 126 | Board-specific specification | Not registered | [r22468](https://www.nesdev.org/w/index.php?oldid=22468) |
| 127 | Board-specific specification | Not registered | [r4951](https://www.nesdev.org/w/index.php?oldid=4951) |
| 128 | Board-specific specification | Not registered | [r21955](https://www.nesdev.org/w/index.php?oldid=21955) |
| 129 | Board-specific specification | Not registered | [r24633](https://www.nesdev.org/w/index.php?oldid=24633) |
| 130 | Board-specific specification | Not registered | [r24051](https://www.nesdev.org/w/index.php?oldid=24051) |
| 131 | Board-specific specification | Not registered | [r24052](https://www.nesdev.org/w/index.php?oldid=24052) |
| 132 | TXC/JV001 protection board | New implementation: Txc | [r4967](https://www.nesdev.org/w/index.php?oldid=4967) |
| 133 | Sachen board | New implementation: Sachen | [r4977](https://www.nesdev.org/w/index.php?oldid=4977) |
| 134 | Board-specific specification | Not registered | [r20085](https://www.nesdev.org/w/index.php?oldid=20085) |
| 135 | Sachen 8259 | New implementation: Sachen | [r23709](https://www.nesdev.org/w/index.php?oldid=23709) |
| 136 | TXC/JV001 protection board | New implementation: Txc | [r21611](https://www.nesdev.org/w/index.php?oldid=21611) |
| 137 | Sachen 8259 | New implementation: Sachen | [r20417](https://www.nesdev.org/w/index.php?oldid=20417) |
| 138 | Sachen 8259 | New implementation: Sachen | [r23709](https://www.nesdev.org/w/index.php?oldid=23709) |
| 139 | Sachen 8259 | New implementation: Sachen | [r23709](https://www.nesdev.org/w/index.php?oldid=23709) |
| 140 | Jaleco JF-11/14 | New implementation: DiscreteMapper | [r22403](https://www.nesdev.org/w/index.php?oldid=22403) |
| 141 | Sachen 8259 | New implementation: Sachen | [r23709](https://www.nesdev.org/w/index.php?oldid=23709) |
| 142 | Board-specific specification | Not registered | [r5032](https://www.nesdev.org/w/index.php?oldid=5032) |
| 143 | Sachen board | New implementation: Sachen | [r5034](https://www.nesdev.org/w/index.php?oldid=5034) |
| 144 | Board-specific specification | Not registered | [r19702](https://www.nesdev.org/w/index.php?oldid=19702) |
| 145 | Board-specific specification | New implementation: DiscreteMapper | [r5039](https://www.nesdev.org/w/index.php?oldid=5039) |
| 146 | Sachen board | New implementation: Sachen | [r22985](https://www.nesdev.org/w/index.php?oldid=22985) |
| 147 | TXC/JV001 protection board | New implementation: Txc | [r5057](https://www.nesdev.org/w/index.php?oldid=5057) |
| 148 | Board-specific specification | New implementation: DiscreteMapper | [r24534](https://www.nesdev.org/w/index.php?oldid=24534) |
| 149 | Board-specific specification | New implementation: DiscreteMapper | [r24529](https://www.nesdev.org/w/index.php?oldid=24529) |
| 150 | Sachen board | New implementation: Sachen | [r5081](https://www.nesdev.org/w/index.php?oldid=5081) |
| 151 | Vs. VRC1 duplicate | Not registered | [r21586](https://www.nesdev.org/w/index.php?oldid=21586) |
| 152 | Bandai discrete mirroring variant | New implementation: DiscreteMapper | [r21304](https://www.nesdev.org/w/index.php?oldid=21304) |
| 153 | Bandai FCG + WRAM | New implementation: BandaiFcg | [r5112](https://www.nesdev.org/w/index.php?oldid=5112) |
| 154 | Namcot-3453 | New implementation: Namco108 | [r23278](https://www.nesdev.org/w/index.php?oldid=23278) |
| 155 | MMC1A | Not registered | [r24550](https://www.nesdev.org/w/index.php?oldid=24550) |
| 156 | Board-specific specification | Not registered | [r22838](https://www.nesdev.org/w/index.php?oldid=22838) |
| 157 | Bandai Datach | New implementation: Datach | [r21310](https://www.nesdev.org/w/index.php?oldid=21310) |
| 158 | RAMBO-1 nametable variant | New implementation: Rambo1 | [r21299](https://www.nesdev.org/w/index.php?oldid=21299) |
| 159 | Bandai X24C01 EEPROM | New implementation: BandaiFcg | [r5171](https://www.nesdev.org/w/index.php?oldid=5171) |
| 160 | Board-specific specification | Not registered | [r5173](https://www.nesdev.org/w/index.php?oldid=5173) |
| 161 | Board-specific specification | Not registered | [r5175](https://www.nesdev.org/w/index.php?oldid=5175) |
| 162 | Board-specific specification | Not registered | [r19013](https://www.nesdev.org/w/index.php?oldid=19013) |
| 163 | Board-specific specification | Not registered | [r24182](https://www.nesdev.org/w/index.php?oldid=24182) |
| 164 | Board-specific specification | Not registered | [r22865](https://www.nesdev.org/w/index.php?oldid=22865) |
| 165 | Board-specific specification | Not registered | [r5217](https://www.nesdev.org/w/index.php?oldid=5217) |
| 166 | Board-specific specification | Not registered | [r22345](https://www.nesdev.org/w/index.php?oldid=22345) |
| 167 | Board-specific specification | Not registered | [r22345](https://www.nesdev.org/w/index.php?oldid=22345) |
| 168 | Racermate | Not registered | [r22805](https://www.nesdev.org/w/index.php?oldid=22805) |
| 169 | Board-specific specification | Not registered | [r5247](https://www.nesdev.org/w/index.php?oldid=5247) |
| 170 | Board-specific specification | Not registered | [r21130](https://www.nesdev.org/w/index.php?oldid=21130) |
| 171 | Board-specific specification | Not registered | [r22367](https://www.nesdev.org/w/index.php?oldid=22367) |
| 172 | TXC/JV001 protection board | New implementation: Txc | [r5257](https://www.nesdev.org/w/index.php?oldid=5257) |
| 173 | TXC/JV001 protection board | New implementation: Txc | [r5274](https://www.nesdev.org/w/index.php?oldid=5274) |
| 174 | Board-specific specification | Not registered | [r5296](https://www.nesdev.org/w/index.php?oldid=5296) |
| 175 | Board-specific specification | Not registered | [r5297](https://www.nesdev.org/w/index.php?oldid=5297) |
| 176 | FK23C | Not registered | [r24008](https://www.nesdev.org/w/index.php?oldid=24008) |
| 177 | Board-specific specification | Not registered | [r24245](https://www.nesdev.org/w/index.php?oldid=24245) |
| 178 | Waixing / Nanjing family | Not registered | [r22866](https://www.nesdev.org/w/index.php?oldid=22866) |
| 179 | Board-specific specification | Not registered | [r5374](https://www.nesdev.org/w/index.php?oldid=5374) |
| 180 | UxROM fixed-low-window variant | New implementation: DiscreteMapper | [r19264](https://www.nesdev.org/w/index.php?oldid=19264) |
| 181 | Board-specific specification | Not registered | [r24634](https://www.nesdev.org/w/index.php?oldid=24634) |
| 182 | Historical duplicate of 114 | Duplicate; not registered | [r4858](https://www.nesdev.org/w/index.php?oldid=4858) |
| 183 | Board-specific specification | Not registered | [r24192](https://www.nesdev.org/w/index.php?oldid=24192) |
| 184 | Sunsoft CHR banking | New implementation: DiscreteMapper | [r23872](https://www.nesdev.org/w/index.php?oldid=23872) |
| 185 | CNROM CHR-disable protection | Not registered | [r24346](https://www.nesdev.org/w/index.php?oldid=24346) |
| 186 | Fukutake Study Box convention | Not registered | [r19026](https://www.nesdev.org/w/index.php?oldid=19026) |
| 187 | Board-specific specification | Not registered | [r23904](https://www.nesdev.org/w/index.php?oldid=23904) |
| 188 | Bandai Karaoke Studio | Not registered | [r22386](https://www.nesdev.org/w/index.php?oldid=22386) |
| 189 | Board-specific specification | Not registered | [r22901](https://www.nesdev.org/w/index.php?oldid=22901) |
| 190 | Board-specific specification | Not registered | [r22728](https://www.nesdev.org/w/index.php?oldid=22728) |
| 191 | Board-specific specification | Not registered | [r23949](https://www.nesdev.org/w/index.php?oldid=23949) |
| 192 | Waixing FS308 mixed CHR | New implementation: Mmc3 | [r23950](https://www.nesdev.org/w/index.php?oldid=23950) |
| 193 | Board-specific specification | New implementation: DiscreteMapper | [r21022](https://www.nesdev.org/w/index.php?oldid=21022) |
| 194 | MMC3 mixed CHR, RAM starting at bank zero | New implementation: Mmc3 | [r23951](https://www.nesdev.org/w/index.php?oldid=23951) |
| 195 | Waixing FS303, PPU-write-selected mixed CHR | New implementation: Mmc3 | [r23952](https://www.nesdev.org/w/index.php?oldid=23952) |
| 196 | MRCM MMC3 register wiring and 32 KiB PRG override | New implementation: Mmc3 | [r22320](https://www.nesdev.org/w/index.php?oldid=22320) |
| 197 | MMC3 larger CHR windows, submapper-3 PRG A17 mux | New implementation: Mmc3 | [r5527](https://www.nesdev.org/w/index.php?oldid=5527) |
| 198 | TNROM640 two-chip PRG and separate volatile/NVRAM windows | New implementation: Mmc3 | [r23191](https://www.nesdev.org/w/index.php?oldid=23191) |
| 199 | Waixing FS309, conventional oversized PRG and fixed CHR RAM | New implementation: Mmc3 | [r23199](https://www.nesdev.org/w/index.php?oldid=23199) |
| 200 | Board-specific specification | New implementation: DiscreteMapper | [r20921](https://www.nesdev.org/w/index.php?oldid=20921) |
| 201 | Board-specific specification | New implementation: DiscreteMapper | [r24516](https://www.nesdev.org/w/index.php?oldid=24516) |
| 202 | 150-in-1 address-latched multicart; 32 KiB alignment compatibility policy | New implementation: DiscreteMapper | [r5551](https://www.nesdev.org/w/index.php?oldid=5551) |
| 203 | Board-specific specification | New implementation: DiscreteMapper | [r5555](https://www.nesdev.org/w/index.php?oldid=5555) |
| 204 | Multicart usage documented; register/hardware specification still needed | Not registered | [r5557](https://www.nesdev.org/w/index.php?oldid=5557) |
| 205 | JC-016-2 MMC3 block pins with selectable block-1-to-3 solder pad | New implementation: Mmc3 | [r5566](https://www.nesdev.org/w/index.php?oldid=5566) |
| 206 | Namco 108/118 / DxROM | New implementation: Namco108 | [r23830](https://www.nesdev.org/w/index.php?oldid=23830) |
| 207 | Taito X1-005 nametable variant | New implementation: TaitoX1005 | [r24116](https://www.nesdev.org/w/index.php?oldid=24116) |
| 208 | Gouder protected registers and JJ-01 R6-controlled PRG, submappers 0/1 | New implementation: Mmc3 | [r5614](https://www.nesdev.org/w/index.php?oldid=5614) |
| 209 | J.Y. Company ASIC | New implementation: JyCompany | [r24184](https://www.nesdev.org/w/index.php?oldid=24184) |
| 210 | Namco 175/340 | New implementation: Namco175340 (submappers 0/1/2) | [r23707](https://www.nesdev.org/w/index.php?oldid=23707) |
| 211 | J.Y. Company ASIC | New implementation: JyCompany | [r24184](https://www.nesdev.org/w/index.php?oldid=24184) |
| 212 | Board-specific specification | New implementation: DiscreteMapper | [r24309](https://www.nesdev.org/w/index.php?oldid=24309) |
| 213 | Board-specific specification | New implementation: DiscreteMapper | [r5674](https://www.nesdev.org/w/index.php?oldid=5674) |
| 214 | Super Gun 20-in-1 (identification only) | Not registered: register specification missing | [r5675](https://www.nesdev.org/w/index.php?oldid=5675) |
| 215 | Sugar Softec UNL-8237/8237A | New implementation: Mmc3 (submappers 0/1) | [r5704](https://www.nesdev.org/w/index.php?oldid=5704) |
| 216 | RCM/Russian games (identification only) | Not registered: register specification missing | [r5705](https://www.nesdev.org/w/index.php?oldid=5705) |
| 217 | 500/2000-in-1 (identification only) | Not registered: register specification missing | [r5706](https://www.nesdev.org/w/index.php?oldid=5706) |
| 218 | Single Chip / Magic Floor | New implementation: SingleChip (all four CIRAM wirings) | [r22660](https://www.nesdev.org/w/index.php?oldid=22660) |
| 219 | Kasheng A9461 / NT-8029 | New implementation: Mmc3 (ROM-only compatibility model) | [r21696](https://www.nesdev.org/w/index.php?oldid=21696) |
| 220 | Tool/debug assignment | Not registered: affected images require verified board identification | [r23976](https://www.nesdev.org/w/index.php?oldid=23976) |
| 221 | NTDEC N625092 / BMC-N625092 | New implementation: NtdecN625092 | [r5744](https://www.nesdev.org/w/index.php?oldid=5744) |
| 222 | CTC-31 / VRC2 plus discrete IRQ logic | Not registered: hardware/register evidence gap | [r5746](https://www.nesdev.org/w/index.php?oldid=5746) |
| 223 | Obsolete apparent duplicate of 199 | Not registered: source directs mapper 199 | [r24635](https://www.nesdev.org/w/index.php?oldid=24635) |
| 224 | Jncota KT-008 / mapper-268 chipset | Not registered: board wiring requires research | [r5752](https://www.nesdev.org/w/index.php?oldid=5752) |
| 225 | Board-specific specification | New implementation: DiscreteMapper | [r18874](https://www.nesdev.org/w/index.php?oldid=18874) |
| 226 | Board-specific specification | New implementation: DiscreteMapper | [r22261](https://www.nesdev.org/w/index.php?oldid=22261) |
| 227 | Address-latched multicart / RPG boards | New implementation: Multicart227 | [r23188](https://www.nesdev.org/w/index.php?oldid=23188) |
| 228 | Action 52 / Active Enterprises | New implementation: Action52 | [r5784](https://www.nesdev.org/w/index.php?oldid=5784) |
| 229 | Board-specific specification | New implementation: DiscreteMapper | [r5785](https://www.nesdev.org/w/index.php?oldid=5785) |
| 230 | Board-specific specification | Not registered | [r5788](https://www.nesdev.org/w/index.php?oldid=5788) |
| 231 | Board-specific specification | New implementation: DiscreteMapper | [r5791](https://www.nesdev.org/w/index.php?oldid=5791) |
| 232 | Codemasters Quattro | New implementation: DiscreteMapper | [r5797](https://www.nesdev.org/w/index.php?oldid=5797) |
| 233 | Board-specific specification | Not registered | [r5799](https://www.nesdev.org/w/index.php?oldid=5799) |
| 234 | Board-specific specification | Not registered | [r24540](https://www.nesdev.org/w/index.php?oldid=24540) |
| 235 | Board-specific specification | Not registered | [r22514](https://www.nesdev.org/w/index.php?oldid=22514) |
| 236 | Board-specific specification | Not registered | [r18875](https://www.nesdev.org/w/index.php?oldid=18875) |
| 237 | Board-specific specification | Not registered | [r5823](https://www.nesdev.org/w/index.php?oldid=5823) |
| 238 | Board-specific specification | Not registered | [r5824](https://www.nesdev.org/w/index.php?oldid=5824) |
| 240 | Board-specific specification | New implementation: DiscreteMapper | [r21944](https://www.nesdev.org/w/index.php?oldid=21944) |
| 241 | Board-specific specification | Not registered | [r23728](https://www.nesdev.org/w/index.php?oldid=23728) |
| 242 | Board-specific specification | Not registered | [r19709](https://www.nesdev.org/w/index.php?oldid=19709) |
| 243 | Sachen board | New implementation: Sachen | [r5861](https://www.nesdev.org/w/index.php?oldid=5861) |
| 244 | Board-specific specification | Not registered | [r5862](https://www.nesdev.org/w/index.php?oldid=5862) |
| 245 | Board-specific specification | Not registered | [r5871](https://www.nesdev.org/w/index.php?oldid=5871) |
| 246 | Board-specific specification | Not registered | [r22048](https://www.nesdev.org/w/index.php?oldid=22048) |
| 248 | Obsolete duplicate of 115; private-use reassignment proposed | Obsolete; not registered | [r23288](https://www.nesdev.org/w/index.php?oldid=23288) |
| 249 | Waixing T9552, legacy file order | New implementation: Mmc3 | [r20254](https://www.nesdev.org/w/index.php?oldid=20254) |
| 250 | Nitra L401 / Queen Bee V | New implementation: Mmc3 | [r21044](https://www.nesdev.org/w/index.php?oldid=21044) |
| 251 | Board-specific specification | Not registered | [r5898](https://www.nesdev.org/w/index.php?oldid=5898) |
| 252 | Board-specific specification | Not registered | [r5906](https://www.nesdev.org/w/index.php?oldid=5906) |
| 253 | Board-specific specification | Not registered | [r5910](https://www.nesdev.org/w/index.php?oldid=5910) |
| 254 | Board-specific specification | Not registered | [r24273](https://www.nesdev.org/w/index.php?oldid=24273) |
| 255 | Board-specific specification | Not registered | [r20429](https://www.nesdev.org/w/index.php?oldid=20429) |
| 256 | OneBus | Not registered | [r23934](https://www.nesdev.org/w/index.php?oldid=23934) |
| 257 | Dongda / Hongda educational boards | Not registered | [r23118](https://www.nesdev.org/w/index.php?oldid=23118) |
| 258 | Board-specific specification | Not registered | [r7627](https://www.nesdev.org/w/index.php?oldid=7627) |
| 259 | Board-specific specification | Not registered | [r7628](https://www.nesdev.org/w/index.php?oldid=7628) |
| 260 | Board-specific specification | Not registered | [r22170](https://www.nesdev.org/w/index.php?oldid=22170) |
| 261 | Board-specific specification | Not registered | [r18984](https://www.nesdev.org/w/index.php?oldid=18984) |
| 262 | Sachen Street Heroes / UNL-SHERO | New implementation: Mmc3 | [r7640](https://www.nesdev.org/w/index.php?oldid=7640) |
| 263 | Rex Soft UNL-KOF97 | New implementation: Mmc3 | [r7642](https://www.nesdev.org/w/index.php?oldid=7642) |
| 264 | Board-specific specification | Not registered | [r24580](https://www.nesdev.org/w/index.php?oldid=24580) |
| 265 | Board-specific specification | Not registered | [r21955](https://www.nesdev.org/w/index.php?oldid=21955) |
| 266 | Board-specific specification | Not registered | [r7660](https://www.nesdev.org/w/index.php?oldid=7660) |
| 267 | Board-specific specification | Not registered | [r7663](https://www.nesdev.org/w/index.php?oldid=7663) |
| 268 | Coolboy / Mindkids | Not registered | [r24626](https://www.nesdev.org/w/index.php?oldid=24626) |
| 269 | Board-specific specification | Not registered | [r24351](https://www.nesdev.org/w/index.php?oldid=24351) |
| 270 | OneBus outer banking | Not registered | [r23373](https://www.nesdev.org/w/index.php?oldid=23373) |
| 271 | Board-specific specification | Not registered | [r24533](https://www.nesdev.org/w/index.php?oldid=24533) |
| 272 | Board-specific specification | Not registered | [r7701](https://www.nesdev.org/w/index.php?oldid=7701) |
| 273 | Board-specific specification | Not registered | [r7703](https://www.nesdev.org/w/index.php?oldid=7703) |
| 274 | Board-specific specification | Not registered | [r7708](https://www.nesdev.org/w/index.php?oldid=7708) |
| 275 | Board-specific specification | Not registered | [r7709](https://www.nesdev.org/w/index.php?oldid=7709) |
| 276 | Board-specific specification | Not registered | [r7710](https://www.nesdev.org/w/index.php?oldid=7710) |
| 277 | Board-specific specification | Not registered | [r24389](https://www.nesdev.org/w/index.php?oldid=24389) |
| 278 | Board-specific specification | Not registered | [r7712](https://www.nesdev.org/w/index.php?oldid=7712) |
| 279 | Board-specific specification | Not registered | [r23180](https://www.nesdev.org/w/index.php?oldid=23180) |
| 280 | Board-specific specification | Not registered | [r7714](https://www.nesdev.org/w/index.php?oldid=7714) |
| 281 | J.Y. Company multicart | New implementation: JyCompany | [r18938](https://www.nesdev.org/w/index.php?oldid=18938) |
| 282 | J.Y. Company multicart | New implementation: JyCompany | [r23351](https://www.nesdev.org/w/index.php?oldid=23351) |
| 283 | Board-specific specification | Not registered | [r7724](https://www.nesdev.org/w/index.php?oldid=7724) |
| 284 | Drip | Not registered | [r7727](https://www.nesdev.org/w/index.php?oldid=7727) |
| 285 | Board-specific specification | Not registered | [r22915](https://www.nesdev.org/w/index.php?oldid=22915) |
| 286 | Board-specific specification | Not registered | [r24644](https://www.nesdev.org/w/index.php?oldid=24644) |
| 287 | Board-specific specification | Not registered | [r7732](https://www.nesdev.org/w/index.php?oldid=7732) |
| 288 | Board-specific specification | Not registered | [r24622](https://www.nesdev.org/w/index.php?oldid=24622) |
| 289 | Board-specific specification | Not registered | [r20161](https://www.nesdev.org/w/index.php?oldid=20161) |
| 290 | Board-specific specification | Not registered | [r7743](https://www.nesdev.org/w/index.php?oldid=7743) |
| 291 | Board-specific specification | Not registered | [r7746](https://www.nesdev.org/w/index.php?oldid=7746) |
| 292 | Board-specific specification | Not registered | [r22336](https://www.nesdev.org/w/index.php?oldid=22336) |
| 293 | Board-specific specification | Not registered | [r7751](https://www.nesdev.org/w/index.php?oldid=7751) |
| 294 | Board-specific specification | Not registered | [r19317](https://www.nesdev.org/w/index.php?oldid=19317) |
| 295 | J.Y. Company multicart | New implementation: JyCompany | [r23352](https://www.nesdev.org/w/index.php?oldid=23352) |
| 296 | VT3x | Not registered | [r23820](https://www.nesdev.org/w/index.php?oldid=23820) |
| 297 | Board-specific specification | Not registered | [r7767](https://www.nesdev.org/w/index.php?oldid=7767) |
| 298 | Board-specific specification | Not registered | [r7772](https://www.nesdev.org/w/index.php?oldid=7772) |
| 299 | Board-specific specification | Not registered | [r22947](https://www.nesdev.org/w/index.php?oldid=22947) |
| 300 | Board-specific specification | Not registered | [r7775](https://www.nesdev.org/w/index.php?oldid=7775) |
| 301 | Board-specific specification | Not registered | [r19709](https://www.nesdev.org/w/index.php?oldid=19709) |
| 302 | Board-specific specification | Not registered | [r22779](https://www.nesdev.org/w/index.php?oldid=22779) |
| 303 | Board-specific specification | Not registered | [r7779](https://www.nesdev.org/w/index.php?oldid=7779) |
| 304 | Board-specific specification | Not registered | [r7785](https://www.nesdev.org/w/index.php?oldid=7785) |
| 305 | Board-specific specification | Not registered | [r7786](https://www.nesdev.org/w/index.php?oldid=7786) |
| 306 | Board-specific specification | Not registered | [r7788](https://www.nesdev.org/w/index.php?oldid=7788) |
| 307 | Board-specific specification | Not registered | [r7791](https://www.nesdev.org/w/index.php?oldid=7791) |
| 308 | Board-specific specification | Not registered | [r7793](https://www.nesdev.org/w/index.php?oldid=7793) |
| 309 | Board-specific specification | Not registered | [r7795](https://www.nesdev.org/w/index.php?oldid=7795) |
| 310 | Board-specific specification | Not registered | [r19316](https://www.nesdev.org/w/index.php?oldid=19316) |
| 311 | Board-specific specification | Not registered | [r7798](https://www.nesdev.org/w/index.php?oldid=7798) |
| 312 | Board-specific specification | Not registered | [r7801](https://www.nesdev.org/w/index.php?oldid=7801) |
| 313 | Board-specific specification | Not registered | [r7804](https://www.nesdev.org/w/index.php?oldid=7804) |
| 314 | Board-specific specification | Not registered | [r23247](https://www.nesdev.org/w/index.php?oldid=23247) |
| 315 | Board-specific specification | Not registered | [r7811](https://www.nesdev.org/w/index.php?oldid=7811) |
| 316 | Board-specific specification | Not registered | [r23179](https://www.nesdev.org/w/index.php?oldid=23179) |
| 317 | Board-specific specification | Not registered | [r23178](https://www.nesdev.org/w/index.php?oldid=23178) |
| 318 | Board-specific specification | Not registered | [r23177](https://www.nesdev.org/w/index.php?oldid=23177) |
| 319 | Board-specific specification | Not registered | [r22946](https://www.nesdev.org/w/index.php?oldid=22946) |
| 320 | Board-specific specification | Not registered | [r20234](https://www.nesdev.org/w/index.php?oldid=20234) |
| 321 | Board-specific specification | Not registered | [r19761](https://www.nesdev.org/w/index.php?oldid=19761) |
| 322 | Board-specific specification | Not registered | [r7827](https://www.nesdev.org/w/index.php?oldid=7827) |
| 323 | Board-specific specification | Not registered | [r7829](https://www.nesdev.org/w/index.php?oldid=7829) |
| 324 | Board-specific specification | Not registered | [r24547](https://www.nesdev.org/w/index.php?oldid=24547) |
| 325 | Board-specific specification | Not registered | [r19372](https://www.nesdev.org/w/index.php?oldid=19372) |
| 326 | Board-specific specification | Not registered | [r7837](https://www.nesdev.org/w/index.php?oldid=7837) |
| 327 | Board-specific specification | Not registered | [r7840](https://www.nesdev.org/w/index.php?oldid=7840) |
| 328 | Board-specific specification | Not registered | [r7844](https://www.nesdev.org/w/index.php?oldid=7844) |
| 329 | Board-specific specification | Not registered | [r24220](https://www.nesdev.org/w/index.php?oldid=24220) |
| 330 | Board-specific specification | Not registered | [r20367](https://www.nesdev.org/w/index.php?oldid=20367) |
| 331 | Board-specific specification | Not registered | [r7849](https://www.nesdev.org/w/index.php?oldid=7849) |
| 332 | Board-specific specification | Not registered | [r7853](https://www.nesdev.org/w/index.php?oldid=7853) |
| 333 | Board-specific specification | Not registered | [r7855](https://www.nesdev.org/w/index.php?oldid=7855) |
| 334 | Board-specific specification | Not registered | [r7857](https://www.nesdev.org/w/index.php?oldid=7857) |
| 335 | Board-specific specification | Not registered | [r24495](https://www.nesdev.org/w/index.php?oldid=24495) |
| 336 | Board-specific specification | Not registered | [r24537](https://www.nesdev.org/w/index.php?oldid=24537) |
| 337 | Board-specific specification | Not registered | [r7864](https://www.nesdev.org/w/index.php?oldid=7864) |
| 338 | Board-specific specification | Not registered | [r7867](https://www.nesdev.org/w/index.php?oldid=7867) |
| 339 | Board-specific specification | Not registered | [r22997](https://www.nesdev.org/w/index.php?oldid=22997) |
| 340 | Board-specific specification | Not registered | [r7874](https://www.nesdev.org/w/index.php?oldid=7874) |
| 341 | Board-specific specification | Not registered | [r7876](https://www.nesdev.org/w/index.php?oldid=7876) |
| 342 | Board-specific specification | Not registered | [r22755](https://www.nesdev.org/w/index.php?oldid=22755) |
| 343 | Board-specific specification | Not registered | [r7885](https://www.nesdev.org/w/index.php?oldid=7885) |
| 344 | Board-specific specification | Not registered | [r19185](https://www.nesdev.org/w/index.php?oldid=19185) |
| 345 | Board-specific specification | Not registered | [r7895](https://www.nesdev.org/w/index.php?oldid=7895) |
| 346 | Board-specific specification | Not registered | [r7898](https://www.nesdev.org/w/index.php?oldid=7898) |
| 347 | Board-specific specification | Not registered | [r7902](https://www.nesdev.org/w/index.php?oldid=7902) |
| 348 | Board-specific specification | Not registered | [r7904](https://www.nesdev.org/w/index.php?oldid=7904) |
| 349 | Board-specific specification | Not registered | [r7906](https://www.nesdev.org/w/index.php?oldid=7906) |
| 350 | Board-specific specification | Not registered | [r7909](https://www.nesdev.org/w/index.php?oldid=7909) |
| 351 | Board-specific specification | Not registered | [r19719](https://www.nesdev.org/w/index.php?oldid=19719) |
| 352 | Board-specific specification | Not registered | [r22975](https://www.nesdev.org/w/index.php?oldid=22975) |
| 353 | Board-specific specification | Not registered | [r7920](https://www.nesdev.org/w/index.php?oldid=7920) |
| 354 | Board-specific specification | Not registered | [r20572](https://www.nesdev.org/w/index.php?oldid=20572) |
| 355 | Board-specific specification | Not registered | [r7933](https://www.nesdev.org/w/index.php?oldid=7933) |
| 356 | Board-specific specification | Not registered | [r7934](https://www.nesdev.org/w/index.php?oldid=7934) |
| 357 | Board-specific specification | Not registered | [r7940](https://www.nesdev.org/w/index.php?oldid=7940) |
| 358 | J.Y. Company multicart | New implementation: JyCompany | [r18941](https://www.nesdev.org/w/index.php?oldid=18941) |
| 359 | Board-specific specification | Not registered | [r7945](https://www.nesdev.org/w/index.php?oldid=7945) |
| 360 | Board-specific specification | Not registered | [r7948](https://www.nesdev.org/w/index.php?oldid=7948) |
| 361 | Board-specific specification | Not registered | [r7951](https://www.nesdev.org/w/index.php?oldid=7951) |
| 362 | Board-specific specification | Not registered | [r7956](https://www.nesdev.org/w/index.php?oldid=7956) |
| 363 | Board-specific specification | Not registered | [r18941](https://www.nesdev.org/w/index.php?oldid=18941) |
| 364 | Board-specific specification | Not registered | [r21441](https://www.nesdev.org/w/index.php?oldid=21441) |
| 365 | Asder PC-95 | Not registered | [r7963](https://www.nesdev.org/w/index.php?oldid=7963) |
| 366 | Board-specific specification | Not registered | [r7967](https://www.nesdev.org/w/index.php?oldid=7967) |
| 367 | Board-specific specification | Not registered | [r7968](https://www.nesdev.org/w/index.php?oldid=7968) |
| 368 | Board-specific specification | Not registered | [r7973](https://www.nesdev.org/w/index.php?oldid=7973) |
| 369 | Board-specific specification | Not registered | [r7975](https://www.nesdev.org/w/index.php?oldid=7975) |
| 370 | Board-specific specification | Not registered | [r7976](https://www.nesdev.org/w/index.php?oldid=7976) |
| 371 | Board-specific specification | Not registered | [r7978](https://www.nesdev.org/w/index.php?oldid=7978) |
| 372 | Board-specific specification | Not registered | [r7980](https://www.nesdev.org/w/index.php?oldid=7980) |
| 373 | Board-specific specification | Not registered | [r7982](https://www.nesdev.org/w/index.php?oldid=7982) |
| 374 | Board-specific specification | Not registered | [r7984](https://www.nesdev.org/w/index.php?oldid=7984) |
| 375 | Board-specific specification | Not registered | [r23353](https://www.nesdev.org/w/index.php?oldid=23353) |
| 376 | Board-specific specification | Not registered | [r7986](https://www.nesdev.org/w/index.php?oldid=7986) |
| 377 | Board-specific specification | Not registered | [r7987](https://www.nesdev.org/w/index.php?oldid=7987) |
| 378 | Board-specific specification | Not registered | [r7988](https://www.nesdev.org/w/index.php?oldid=7988) |
| 379 | Board-specific specification | Not registered | [r24576](https://www.nesdev.org/w/index.php?oldid=24576) |
| 380 | Board-specific specification | Not registered | [r18847](https://www.nesdev.org/w/index.php?oldid=18847) |
| 381 | Board-specific specification | Not registered | [r24543](https://www.nesdev.org/w/index.php?oldid=24543) |
| 382 | Board-specific specification | Not registered | [r24536](https://www.nesdev.org/w/index.php?oldid=24536) |
| 383 | Board-specific specification | Not registered | [r7997](https://www.nesdev.org/w/index.php?oldid=7997) |
| 384 | Board-specific specification | Not registered | [r7998](https://www.nesdev.org/w/index.php?oldid=7998) |
| 385 | Board-specific specification | Not registered | [r7999](https://www.nesdev.org/w/index.php?oldid=7999) |
| 386 | J.Y. Company multicart | New implementation: JyCompany | [r23354](https://www.nesdev.org/w/index.php?oldid=23354) |
| 387 | J.Y. Company multicart | New implementation: JyCompany | [r23355](https://www.nesdev.org/w/index.php?oldid=23355) |
| 388 | J.Y. Company multicart | New implementation: JyCompany | [r18944](https://www.nesdev.org/w/index.php?oldid=18944) |
| 389 | Board-specific specification | Not registered | [r8004](https://www.nesdev.org/w/index.php?oldid=8004) |
| 390 | Board-specific specification | Not registered | [r18875](https://www.nesdev.org/w/index.php?oldid=18875) |
| 391 | Board-specific specification | Not registered | [r8012](https://www.nesdev.org/w/index.php?oldid=8012) |
| 392 | Board-specific specification | Not registered | [r19172](https://www.nesdev.org/w/index.php?oldid=19172) |
| 393 | Board-specific specification | Not registered | [r8014](https://www.nesdev.org/w/index.php?oldid=8014) |
| 394 | Board-specific specification | Not registered | [r22930](https://www.nesdev.org/w/index.php?oldid=22930) |
| 395 | Board-specific specification | Not registered | [r8017](https://www.nesdev.org/w/index.php?oldid=8017) |
| 396 | Board-specific specification | Not registered | [r22978](https://www.nesdev.org/w/index.php?oldid=22978) |
| 397 | J.Y. Company multicart | New implementation: JyCompany | [r23356](https://www.nesdev.org/w/index.php?oldid=23356) |
| 398 | Board-specific specification | Not registered | [r8021](https://www.nesdev.org/w/index.php?oldid=8021) |
| 399 | Board-specific specification | Not registered | [r24616](https://www.nesdev.org/w/index.php?oldid=24616) |
| 400 | 8-BIT XMAS 2017 | Not registered | [r22764](https://www.nesdev.org/w/index.php?oldid=22764) |
| 401 | Board-specific specification | Not registered | [r18958](https://www.nesdev.org/w/index.php?oldid=18958) |
| 402 | Board-specific specification | Not registered | [r18959](https://www.nesdev.org/w/index.php?oldid=18959) |
| 403 | Board-specific specification | Not registered | [r18960](https://www.nesdev.org/w/index.php?oldid=18960) |
| 404 | Board-specific specification | Not registered | [r18976](https://www.nesdev.org/w/index.php?oldid=18976) |
| 405 | Board-specific specification | Not registered | [r23367](https://www.nesdev.org/w/index.php?oldid=23367) |
| 406 | Haradius Zero | Not registered | [r22429](https://www.nesdev.org/w/index.php?oldid=22429) |
| 407 | Board-specific specification | Not registered | [r24350](https://www.nesdev.org/w/index.php?oldid=24350) |
| 408 | Board-specific specification | Not registered | [r24418](https://www.nesdev.org/w/index.php?oldid=24418) |
| 409 | Board-specific specification | Not registered | [r20235](https://www.nesdev.org/w/index.php?oldid=20235) |
| 410 | Board-specific specification | Not registered | [r18957](https://www.nesdev.org/w/index.php?oldid=18957) |
| 411 | Board-specific specification | Not registered | [r8038](https://www.nesdev.org/w/index.php?oldid=8038) |
| 412 | Board-specific specification | Not registered | [r21165](https://www.nesdev.org/w/index.php?oldid=21165) |
| 413 | Board-specific specification | Not registered | [r8040](https://www.nesdev.org/w/index.php?oldid=8040) |
| 414 | Board-specific specification | Not registered | [r24535](https://www.nesdev.org/w/index.php?oldid=24535) |
| 415 | Board-specific specification | Not registered | [r8043](https://www.nesdev.org/w/index.php?oldid=8043) |
| 416 | Board-specific specification | Not registered | [r8044](https://www.nesdev.org/w/index.php?oldid=8044) |
| 417 | Board-specific specification | Not registered | [r19973](https://www.nesdev.org/w/index.php?oldid=19973) |
| 418 | Board-specific specification | Not registered | [r8047](https://www.nesdev.org/w/index.php?oldid=8047) |
| 419 | Board-specific specification | Not registered | [r23377](https://www.nesdev.org/w/index.php?oldid=23377) |
| 420 | Board-specific specification | Not registered | [r8052](https://www.nesdev.org/w/index.php?oldid=8052) |
| 421 | Board-specific specification | Not registered | [r8053](https://www.nesdev.org/w/index.php?oldid=8053) |
| 422 | Board-specific specification | Not registered | [r22468](https://www.nesdev.org/w/index.php?oldid=22468) |
| 423 | Board-specific specification | Not registered | [r23378](https://www.nesdev.org/w/index.php?oldid=23378) |
| 424 | Board-specific specification | Not registered | [r23379](https://www.nesdev.org/w/index.php?oldid=23379) |
| 425 | Board-specific specification | Not registered | [r23380](https://www.nesdev.org/w/index.php?oldid=23380) |
| 426 | Board-specific specification | Not registered | [r23381](https://www.nesdev.org/w/index.php?oldid=23381) |
| 427 | Board-specific specification | Not registered | [r23382](https://www.nesdev.org/w/index.php?oldid=23382) |
| 428 | Board-specific specification | Not registered | [r8060](https://www.nesdev.org/w/index.php?oldid=8060) |
| 429 | Board-specific specification | Not registered | [r23470](https://www.nesdev.org/w/index.php?oldid=23470) |
| 430 | Board-specific specification | Not registered | [r18979](https://www.nesdev.org/w/index.php?oldid=18979) |
| 431 | Board-specific specification | Not registered | [r23621](https://www.nesdev.org/w/index.php?oldid=23621) |
| 432 | Board-specific specification | Not registered | [r23472](https://www.nesdev.org/w/index.php?oldid=23472) |
| 433 | Board-specific specification | Not registered | [r8063](https://www.nesdev.org/w/index.php?oldid=8063) |
| 434 | Board-specific specification | Not registered | [r24539](https://www.nesdev.org/w/index.php?oldid=24539) |
| 435 | Board-specific specification | Not registered | [r19228](https://www.nesdev.org/w/index.php?oldid=19228) |
| 436 | Board-specific specification | Not registered | [r23383](https://www.nesdev.org/w/index.php?oldid=23383) |
| 437 | Board-specific specification | Not registered | [r19230](https://www.nesdev.org/w/index.php?oldid=19230) |
| 438 | Board-specific specification | Not registered | [r19303](https://www.nesdev.org/w/index.php?oldid=19303) |
| 439 | Board-specific specification | Not registered | [r21163](https://www.nesdev.org/w/index.php?oldid=21163) |
| 440 | Board-specific specification | Not registered | [r19242](https://www.nesdev.org/w/index.php?oldid=19242) |
| 441 | Board-specific specification | Not registered | [r19277](https://www.nesdev.org/w/index.php?oldid=19277) |
| 442 | Board-specific specification | Not registered | [r19244](https://www.nesdev.org/w/index.php?oldid=19244) |
| 443 | Board-specific specification | Not registered | [r8065](https://www.nesdev.org/w/index.php?oldid=8065) |
| 444 | Board-specific specification | Not registered | [r22463](https://www.nesdev.org/w/index.php?oldid=22463) |
| 445 | Board-specific specification | Not registered | [r19271](https://www.nesdev.org/w/index.php?oldid=19271) |
| 446 | Mindkids SMD172B FPGA | Not registered | [r22849](https://www.nesdev.org/w/index.php?oldid=22849) |
| 447 | Board-specific specification | Not registered | [r19301](https://www.nesdev.org/w/index.php?oldid=19301) |
| 448 | Board-specific specification | Not registered | [r19305](https://www.nesdev.org/w/index.php?oldid=19305) |
| 449 | Board-specific specification | Not registered | [r19306](https://www.nesdev.org/w/index.php?oldid=19306) |
| 450 | Board-specific specification | Not registered | [r19307](https://www.nesdev.org/w/index.php?oldid=19307) |
| 451 | Haratyler HG/MP | Not registered | [r24353](https://www.nesdev.org/w/index.php?oldid=24353) |
| 452 | Board-specific specification | Not registered | [r19632](https://www.nesdev.org/w/index.php?oldid=19632) |
| 453 | Board-specific specification | Not registered | [r19315](https://www.nesdev.org/w/index.php?oldid=19315) |
| 454 | Board-specific specification | Not registered | [r21017](https://www.nesdev.org/w/index.php?oldid=21017) |
| 455 | Board-specific specification | Not registered | [r21018](https://www.nesdev.org/w/index.php?oldid=21018) |
| 456 | Board-specific specification | Not registered | [r21021](https://www.nesdev.org/w/index.php?oldid=21021) |
| 457 | Board-specific specification | Not registered | [r21023](https://www.nesdev.org/w/index.php?oldid=21023) |
| 458 | Board-specific specification | Not registered | [r21024](https://www.nesdev.org/w/index.php?oldid=21024) |
| 459 | Board-specific specification | Not registered | [r22668](https://www.nesdev.org/w/index.php?oldid=22668) |
| 460 | Board-specific specification | Not registered | [r21050](https://www.nesdev.org/w/index.php?oldid=21050) |
| 461 | Board-specific specification | Not registered | [r21169](https://www.nesdev.org/w/index.php?oldid=21169) |
| 462 | Board-specific specification | Not registered | [r22669](https://www.nesdev.org/w/index.php?oldid=22669) |
| 463 | Board-specific specification | Not registered | [r22670](https://www.nesdev.org/w/index.php?oldid=22670) |
| 467 | Board-specific specification | Not registered | [r19326](https://www.nesdev.org/w/index.php?oldid=19326) |
| 468 | Board-specific specification | Not registered | [r20144](https://www.nesdev.org/w/index.php?oldid=20144) |
| 469 | Board-specific specification | Not registered | [r21014](https://www.nesdev.org/w/index.php?oldid=21014) |
| 470 | Board-specific specification | Not registered | [r20148](https://www.nesdev.org/w/index.php?oldid=20148) |
| 471 | Board-specific specification | Not registered | [r23357](https://www.nesdev.org/w/index.php?oldid=23357) |
| 472 | Board-specific specification | Not registered | [r23358](https://www.nesdev.org/w/index.php?oldid=23358) |
| 473 | Board-specific specification | Not registered | [r23359](https://www.nesdev.org/w/index.php?oldid=23359) |
| 474 | Board-specific specification | Not registered | [r23360](https://www.nesdev.org/w/index.php?oldid=23360) |
| 476 | Board-specific specification | Not registered | [r22133](https://www.nesdev.org/w/index.php?oldid=22133) |
| 479 | Board-specific specification | Not registered | [r21982](https://www.nesdev.org/w/index.php?oldid=21982) |
| 481 | Board-specific specification | Not registered | [r22323](https://www.nesdev.org/w/index.php?oldid=22323) |
| 487 | Board-specific specification | Not registered | [r23361](https://www.nesdev.org/w/index.php?oldid=23361) |
| 493 | Board-specific specification | Not registered | [r24546](https://www.nesdev.org/w/index.php?oldid=24546) |
| 495 | Board-specific specification | Not registered | [r22800](https://www.nesdev.org/w/index.php?oldid=22800) |
| 497 | Board-specific specification | Not registered | [r22873](https://www.nesdev.org/w/index.php?oldid=22873) |
| 512 | Sachen Zhongguo Daheng | New implementation: Mmc3 | [r8073](https://www.nesdev.org/w/index.php?oldid=8073) |
| 513 | Sachen SA-9602B / Princess Maker | New implementation: Mmc3 | [r8077](https://www.nesdev.org/w/index.php?oldid=8077) |
| 514 | Board-specific specification | Not registered | [r22322](https://www.nesdev.org/w/index.php?oldid=22322) |
| 515 | Board-specific specification | Not registered | [r22771](https://www.nesdev.org/w/index.php?oldid=22771) |
| 516 | Board-specific specification | Not registered | [r8088](https://www.nesdev.org/w/index.php?oldid=8088) |
| 517 | Board-specific specification | Not registered | [r8090](https://www.nesdev.org/w/index.php?oldid=8090) |
| 518 | Subor / Dance 2000 | Not registered | [r22909](https://www.nesdev.org/w/index.php?oldid=22909) |
| 519 | Board-specific specification | Not registered | [r8101](https://www.nesdev.org/w/index.php?oldid=8101) |
| 520 | Board-specific specification | Not registered | [r8107](https://www.nesdev.org/w/index.php?oldid=8107) |
| 521 | Board-specific specification | Not registered | [r22137](https://www.nesdev.org/w/index.php?oldid=22137) |
| 522 | Board-specific specification | Not registered | [r8112](https://www.nesdev.org/w/index.php?oldid=8112) |
| 523 | Board-specific specification | Not registered | [r23888](https://www.nesdev.org/w/index.php?oldid=23888) |
| 524 | Board-specific specification | Not registered | [r8116](https://www.nesdev.org/w/index.php?oldid=8116) |
| 525 | Board-specific specification | Not registered | [r8120](https://www.nesdev.org/w/index.php?oldid=8120) |
| 526 | Board-specific specification | Not registered | [r20369](https://www.nesdev.org/w/index.php?oldid=20369) |
| 527 | Board-specific specification | Not registered | [r8124](https://www.nesdev.org/w/index.php?oldid=8124) |
| 528 | Board-specific specification | Not registered | [r8126](https://www.nesdev.org/w/index.php?oldid=8126) |
| 529 | Board-specific specification | Not registered | [r19170](https://www.nesdev.org/w/index.php?oldid=19170) |
| 530 | Board-specific specification | Not registered | [r8130](https://www.nesdev.org/w/index.php?oldid=8130) |
| 531 | Board-specific specification | Not registered | [r8131](https://www.nesdev.org/w/index.php?oldid=8131) |
| 532 | Board-specific specification | Not registered | [r8133](https://www.nesdev.org/w/index.php?oldid=8133) |
| 533 | Board-specific specification | Not registered | [r8137](https://www.nesdev.org/w/index.php?oldid=8137) |
| 534 | Board-specific specification | Not registered | [r22468](https://www.nesdev.org/w/index.php?oldid=22468) |
| 535 | Board-specific specification | Not registered | [r8147](https://www.nesdev.org/w/index.php?oldid=8147) |
| 536 | Board-specific specification | Not registered | [r23952](https://www.nesdev.org/w/index.php?oldid=23952) |
| 537 | Board-specific specification | Not registered | [r23952](https://www.nesdev.org/w/index.php?oldid=23952) |
| 538 | Board-specific specification | Not registered | [r8156](https://www.nesdev.org/w/index.php?oldid=8156) |
| 539 | Board-specific specification | Not registered | [r8157](https://www.nesdev.org/w/index.php?oldid=8157) |
| 540 | Board-specific specification | Not registered | [r8158](https://www.nesdev.org/w/index.php?oldid=8158) |
| 541 | Board-specific specification | Not registered | [r8159](https://www.nesdev.org/w/index.php?oldid=8159) |
| 542 | Board-specific specification | Not registered | [r8160](https://www.nesdev.org/w/index.php?oldid=8160) |
| 543 | Board-specific specification | Not registered | [r8162](https://www.nesdev.org/w/index.php?oldid=8162) |
| 544 | Board-specific specification | Not registered | [r24263](https://www.nesdev.org/w/index.php?oldid=24263) |
| 545 | Board-specific specification | Not registered | [r23190](https://www.nesdev.org/w/index.php?oldid=23190) |
| 546 | Board-specific specification | Not registered | [r8169](https://www.nesdev.org/w/index.php?oldid=8169) |
| 547 | Konami VRC5 / QTa | Not registered | [r23763](https://www.nesdev.org/w/index.php?oldid=23763) |
| 548 | Board-specific specification | Not registered | [r8186](https://www.nesdev.org/w/index.php?oldid=8186) |
| 549 | Board-specific specification | Not registered | [r21240](https://www.nesdev.org/w/index.php?oldid=21240) |
| 550 | Board-specific specification | Not registered | [r8188](https://www.nesdev.org/w/index.php?oldid=8188) |
| 551 | Board-specific specification | Not registered | [r8189](https://www.nesdev.org/w/index.php?oldid=8189) |
| 552 | Taito X1-017 wiring variant | New implementation: TaitoX1017 | [r24125](https://www.nesdev.org/w/index.php?oldid=24125) |
| 553 | Board-specific specification | Not registered | [r8192](https://www.nesdev.org/w/index.php?oldid=8192) |
| 554 | Board-specific specification | Not registered | [r8195](https://www.nesdev.org/w/index.php?oldid=8195) |
| 555 | Board-specific specification | Not registered | [r8198](https://www.nesdev.org/w/index.php?oldid=8198) |
| 556 | Board-specific specification | Not registered | [r22948](https://www.nesdev.org/w/index.php?oldid=22948) |
| 557 | Board-specific specification | Not registered | [r21239](https://www.nesdev.org/w/index.php?oldid=21239) |
| 558 | Yancheng YC-03-09 | Not registered | [r23362](https://www.nesdev.org/w/index.php?oldid=23362) |
| 560 | Board-specific specification | Not registered | [r23154](https://www.nesdev.org/w/index.php?oldid=23154) |
| 561 | Bung Super Game Doctor | Not registered | [r18925](https://www.nesdev.org/w/index.php?oldid=18925) |
| 562 | Venus Turbo Game Doctor | Not registered | [r19971](https://www.nesdev.org/w/index.php?oldid=19971) |
| 594 | Rinco Talkman | Not registered | [r22961](https://www.nesdev.org/w/index.php?oldid=22961) |
| 595 | NES-4MROM-512 | Not registered | [r22979](https://www.nesdev.org/w/index.php?oldid=22979) |
| 600 | ABL UM6578 extended banking | Not registered | [r23398](https://www.nesdev.org/w/index.php?oldid=23398) |
| 601 | dreamGEAR / Senario UM6578 | Not registered | [r23369](https://www.nesdev.org/w/index.php?oldid=23369) |
| 602 | Subor SB-2000 UM6576 | Not registered | [r23370](https://www.nesdev.org/w/index.php?oldid=23370) |
| 682 | Rainbow | Not registered | [r24195](https://www.nesdev.org/w/index.php?oldid=24195) |
