# Mapper implementation coverage

This ledger tracks the requested implementation of every documented NES/Famicom
cartridge mapper, including bootlegs and homebrew. **The full target is not yet
complete.** Updated 2026-11-10 against the working-tree registry.

There are **126 registered cartridge IDs**: the original 14, 53 added in the first
batch, and Irem 32/65/77, Sunsoft 67/68, Taito 33/48/80/207/82/552, Jaleco 18/72/92/101,
Namco 210, MMC3 variants 118/119, RAMBO-1 64/158, Bandai 16/153/159, and Datach
157, UNROM 512 30, GTROM/direct MMC1 111, and J.Y. Company
35/90/209/211/281/282/295/358/386/387/388/397, plus the sixteen Sachen/TXC
IDs, Sachen MMC3 clones 262/512/513, Rex Soft's 263, and Nitra 250 detailed below.
FDS remains a separate subsystem, conventionally numbered 20. Registration and
synthetic tests do not certify every commercial game, revision, peripheral, or
unusual ROM geometry.

## Implemented in this batch

| Implementation | New IDs | Observable behavior covered |
|---|---|---|
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

| ID | Specification identification | Implementation status | Source revision |
|---:|---|---|---|
| 000 | NROM | Existing registration: Nrom | [r23660](https://www.nesdev.org/w/index.php?oldid=23660) |
| 001 | MMC1 / SxROM | Existing registration: Mmc1 | [r24550](https://www.nesdev.org/w/index.php?oldid=24550) |
| 002 | UxROM | Existing registration: Uxrom | [r24147](https://www.nesdev.org/w/index.php?oldid=24147) |
| 003 | CNROM | Existing registration: Cnrom | [r24346](https://www.nesdev.org/w/index.php?oldid=24346) |
| 004 | MMC3 / MMC6 | Existing registration: Mmc3 | [r24445](https://www.nesdev.org/w/index.php?oldid=24445) |
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
| 074 | Board-specific specification | Not registered | [r24258](https://www.nesdev.org/w/index.php?oldid=24258) |
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
| 114 | Board-specific specification | Not registered | [r4858](https://www.nesdev.org/w/index.php?oldid=4858) |
| 115 | Board-specific specification | Not registered | [r21614](https://www.nesdev.org/w/index.php?oldid=21614) |
| 116 | Board-specific specification | Not registered | [r22958](https://www.nesdev.org/w/index.php?oldid=22958) |
| 117 | Board-specific specification | Not registered | [r4888](https://www.nesdev.org/w/index.php?oldid=4888) |
| 118 | TxSROM / MMC3 | New implementation: Mmc3 variant | [r24058](https://www.nesdev.org/w/index.php?oldid=24058) |
| 119 | TQROM / MMC3 | New implementation: Mmc3 variant | [r24251](https://www.nesdev.org/w/index.php?oldid=24251) |
| 120 | Board-specific specification | Not registered | [r20571](https://www.nesdev.org/w/index.php?oldid=20571) |
| 121 | Board-specific specification | Not registered | [r23905](https://www.nesdev.org/w/index.php?oldid=23905) |
| 122 | Board-specific specification | Not registered | [r22366](https://www.nesdev.org/w/index.php?oldid=22366) |
| 123 | Board-specific specification | Not registered | [r4940](https://www.nesdev.org/w/index.php?oldid=4940) |
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
| 182 | Board-specific specification | Not registered | [r4858](https://www.nesdev.org/w/index.php?oldid=4858) |
| 183 | Board-specific specification | Not registered | [r24192](https://www.nesdev.org/w/index.php?oldid=24192) |
| 184 | Sunsoft CHR banking | New implementation: DiscreteMapper | [r23872](https://www.nesdev.org/w/index.php?oldid=23872) |
| 185 | CNROM CHR-disable protection | Not registered | [r24346](https://www.nesdev.org/w/index.php?oldid=24346) |
| 186 | Fukutake Study Box convention | Not registered | [r19026](https://www.nesdev.org/w/index.php?oldid=19026) |
| 187 | Board-specific specification | Not registered | [r23904](https://www.nesdev.org/w/index.php?oldid=23904) |
| 188 | Bandai Karaoke Studio | Not registered | [r22386](https://www.nesdev.org/w/index.php?oldid=22386) |
| 189 | Board-specific specification | Not registered | [r22901](https://www.nesdev.org/w/index.php?oldid=22901) |
| 190 | Board-specific specification | Not registered | [r22728](https://www.nesdev.org/w/index.php?oldid=22728) |
| 191 | Board-specific specification | Not registered | [r23949](https://www.nesdev.org/w/index.php?oldid=23949) |
| 192 | Board-specific specification | Not registered | [r23950](https://www.nesdev.org/w/index.php?oldid=23950) |
| 193 | Board-specific specification | New implementation: DiscreteMapper | [r21022](https://www.nesdev.org/w/index.php?oldid=21022) |
| 194 | Board-specific specification | Not registered | [r23951](https://www.nesdev.org/w/index.php?oldid=23951) |
| 195 | Board-specific specification | Not registered | [r23952](https://www.nesdev.org/w/index.php?oldid=23952) |
| 196 | Board-specific specification | Not registered | [r22320](https://www.nesdev.org/w/index.php?oldid=22320) |
| 197 | Board-specific specification | Not registered | [r5527](https://www.nesdev.org/w/index.php?oldid=5527) |
| 198 | Board-specific specification | Not registered | [r23191](https://www.nesdev.org/w/index.php?oldid=23191) |
| 199 | Board-specific specification | Not registered | [r23199](https://www.nesdev.org/w/index.php?oldid=23199) |
| 200 | Board-specific specification | New implementation: DiscreteMapper | [r20921](https://www.nesdev.org/w/index.php?oldid=20921) |
| 201 | Board-specific specification | New implementation: DiscreteMapper | [r24516](https://www.nesdev.org/w/index.php?oldid=24516) |
| 202 | Board-specific specification | Not registered | [r5551](https://www.nesdev.org/w/index.php?oldid=5551) |
| 203 | Board-specific specification | New implementation: DiscreteMapper | [r5555](https://www.nesdev.org/w/index.php?oldid=5555) |
| 204 | Board-specific specification | Not registered | [r5557](https://www.nesdev.org/w/index.php?oldid=5557) |
| 205 | Board-specific specification | Not registered | [r5566](https://www.nesdev.org/w/index.php?oldid=5566) |
| 206 | Namco 108/118 / DxROM | New implementation: Namco108 | [r23830](https://www.nesdev.org/w/index.php?oldid=23830) |
| 207 | Taito X1-005 nametable variant | New implementation: TaitoX1005 | [r24116](https://www.nesdev.org/w/index.php?oldid=24116) |
| 208 | Board-specific specification | Not registered | [r5614](https://www.nesdev.org/w/index.php?oldid=5614) |
| 209 | J.Y. Company ASIC | New implementation: JyCompany | [r24184](https://www.nesdev.org/w/index.php?oldid=24184) |
| 210 | Namco 175/340 | New implementation: Namco175340 (submappers 0/1/2) | [r23707](https://www.nesdev.org/w/index.php?oldid=23707) |
| 211 | J.Y. Company ASIC | New implementation: JyCompany | [r24184](https://www.nesdev.org/w/index.php?oldid=24184) |
| 212 | Board-specific specification | New implementation: DiscreteMapper | [r24309](https://www.nesdev.org/w/index.php?oldid=24309) |
| 213 | Board-specific specification | New implementation: DiscreteMapper | [r5674](https://www.nesdev.org/w/index.php?oldid=5674) |
| 214 | Board-specific specification | Not registered | [r5675](https://www.nesdev.org/w/index.php?oldid=5675) |
| 215 | Board-specific specification | Not registered | [r5704](https://www.nesdev.org/w/index.php?oldid=5704) |
| 216 | Board-specific specification | Not registered | [r5705](https://www.nesdev.org/w/index.php?oldid=5705) |
| 217 | Board-specific specification | Not registered | [r5706](https://www.nesdev.org/w/index.php?oldid=5706) |
| 218 | Magic Floor | Not registered | [r22660](https://www.nesdev.org/w/index.php?oldid=22660) |
| 219 | Board-specific specification | Not registered | [r21696](https://www.nesdev.org/w/index.php?oldid=21696) |
| 220 | Emulator tool use | Not registered | [r23976](https://www.nesdev.org/w/index.php?oldid=23976) |
| 221 | Board-specific specification | Not registered | [r5744](https://www.nesdev.org/w/index.php?oldid=5744) |
| 222 | Board-specific specification | Not registered | [r5746](https://www.nesdev.org/w/index.php?oldid=5746) |
| 223 | Board-specific specification | Not registered | [r24635](https://www.nesdev.org/w/index.php?oldid=24635) |
| 224 | Board-specific specification | Not registered | [r5752](https://www.nesdev.org/w/index.php?oldid=5752) |
| 225 | Board-specific specification | New implementation: DiscreteMapper | [r18874](https://www.nesdev.org/w/index.php?oldid=18874) |
| 226 | Board-specific specification | New implementation: DiscreteMapper | [r22261](https://www.nesdev.org/w/index.php?oldid=22261) |
| 227 | Board-specific specification | Not registered | [r23188](https://www.nesdev.org/w/index.php?oldid=23188) |
| 228 | Action 52 / Active Enterprises | Not registered | [r5784](https://www.nesdev.org/w/index.php?oldid=5784) |
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
| 248 | Board-specific specification | Not registered | [r23288](https://www.nesdev.org/w/index.php?oldid=23288) |
| 249 | T9552 | Not registered | [r20254](https://www.nesdev.org/w/index.php?oldid=20254) |
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
