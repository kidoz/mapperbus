# Mapper implementation coverage

This ledger tracks the requested implementation of every documented NES/Famicom
cartridge mapper, including bootlegs and homebrew. **The full target is not yet
complete.** Updated 2026-11-09 against the working-tree registry.

There are **73 registered cartridge IDs**: the original 14, 53 added in the first
batch, and Irem 32/65, Sunsoft 67/68, and Taito 33/48 in the follow-ups below.
FDS remains a separate subsystem, conventionally numbered 20. Registration and
synthetic tests do not certify every commercial game, revision, peripheral, or
unusual ROM geometry.

## Implemented in this batch

| Implementation | New IDs | Observable behavior covered |
|---|---|---|
| MMC4 | 10 | 16 KiB PRG, both eight-byte CHR trigger ranges, old-bank trigger fetch, mirroring, RAM, persistence. |
| Irem G-101 / H3001 (follow-up) | 32, 65 | Two switchable 8 KiB PRG banks, layout swap, eight 1 KiB CHR banks, Major League submapper, mirroring, CPU-cycle down counter, RAM and snapshots. |
| Sunsoft-3 / Sunsoft-4 (follow-up) | 67, 68 | 16 KiB PRG and 2 KiB CHR banks, four mirroring modes, Sunsoft-3 live IRQ counter and acknowledgement aliases, Sunsoft-4 ROM nametables and WRAM gate, DCS options with licensing and paired snapshot identity. |
| Taito TC0190 / TC0690 (follow-up) | 33, 48 | Two 8 KiB PRG windows, full-width 2/1 KiB CHR banks, distinct mirroring decoders, mapper-48 latched IRQ and submapper timing profiles, CPU/PPU and snapshot replay. |
| Namco 108 family | 76, 88, 95, 154, 206 | Register decode, fixed and switchable PRG, 1/2 KiB CHR wiring, fixed-PRG submapper, CHR-driven CIRAM, one-screen mirroring. |
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
- Optional `nametable_address()` maps board-specific CIRAM wiring. Mapper 95 is
  tested through PPU registers, including reversed horizontal nametable pairs.
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
[Sunsoft tests](../../tests/unit/test_sunsoft_boards.cpp),
[Taito tests](../../tests/unit/test_taito.cpp),
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
- Larger remaining families include Bandai/EEPROM, Jaleco SS88006, other Taito boards,
  RAMBO-1, MMC3 derivatives and outer-bank variants,
  J.Y./Sachen/TXC ASICs, flash-saving UNROM 512/GTROM, and Rainbow.
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
| 016 | Bandai FCG / LZ93D50 | Not registered | [r3876](https://www.nesdev.org/w/index.php?oldid=3876) |
| 017 | FFE Super Magic Card | Not registered | [r18933](https://www.nesdev.org/w/index.php?oldid=18933) |
| 018 | Jaleco SS88006 | Not registered | [r3918](https://www.nesdev.org/w/index.php?oldid=3918) |
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
| 030 | UNROM 512 | Not registered | [r23183](https://www.nesdev.org/w/index.php?oldid=23183) |
| 031 | NSF-subset cartridge | New implementation: DiscreteMapper | [r19421](https://www.nesdev.org/w/index.php?oldid=19421) |
| 032 | Irem G-101 | New implementation: Irem | [r23869](https://www.nesdev.org/w/index.php?oldid=23869) |
| 033 | Taito TC0190 / TC0350 subset | New implementation: TaitoTc | [r24359](https://www.nesdev.org/w/index.php?oldid=24359) |
| 034 | BNROM / NINA-001 | New implementation: DiscreteMapper | [r24073](https://www.nesdev.org/w/index.php?oldid=24073) |
| 035 | J.Y. Company ASIC | Not registered | [r24184](https://www.nesdev.org/w/index.php?oldid=24184) |
| 036 | TXC 01-22000-400 | Not registered | [r4196](https://www.nesdev.org/w/index.php?oldid=4196) |
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
| 064 | Tengen RAMBO-1 | Not registered | [r23936](https://www.nesdev.org/w/index.php?oldid=23936) |
| 065 | Irem H3001 | New implementation: Irem | [r24119](https://www.nesdev.org/w/index.php?oldid=24119) |
| 066 | GxROM / MxROM | New implementation: DiscreteMapper | [r24003](https://www.nesdev.org/w/index.php?oldid=24003) |
| 067 | Sunsoft 3 | New implementation: Sunsoft34 | [r23676](https://www.nesdev.org/w/index.php?oldid=23676) |
| 068 | Sunsoft 4 | New implementation: Sunsoft34 | [r23720](https://www.nesdev.org/w/index.php?oldid=23720) |
| 069 | FME-7 / Sunsoft 5A/5B | Existing registration: Sunsoft5b | [r24262](https://www.nesdev.org/w/index.php?oldid=24262) |
| 070 | Bandai discrete board | New implementation: DiscreteMapper | [r24524](https://www.nesdev.org/w/index.php?oldid=24524) |
| 071 | Camerica / Codemasters | New implementation: DiscreteMapper | [r21755](https://www.nesdev.org/w/index.php?oldid=21755) |
| 072 | Jaleco JF-17 | Not registered | [r24541](https://www.nesdev.org/w/index.php?oldid=24541) |
| 073 | VRC3 | New implementation: KonamiVrc | [r21300](https://www.nesdev.org/w/index.php?oldid=21300) |
| 074 | Board-specific specification | Not registered | [r24258](https://www.nesdev.org/w/index.php?oldid=24258) |
| 075 | VRC1 | New implementation: KonamiVrc | [r12313](https://www.nesdev.org/w/index.php?oldid=12313) |
| 076 | Namco 109 variant | New implementation: Namco108 | [r21294](https://www.nesdev.org/w/index.php?oldid=21294) |
| 077 | Irem Napoleon Senki board | Not registered | [r24538](https://www.nesdev.org/w/index.php?oldid=24538) |
| 078 | Holy Diver / Cosmo Carrier | New implementation: DiscreteMapper | [r21584](https://www.nesdev.org/w/index.php?oldid=21584) |
| 079 | NINA-03 / NINA-06 | New implementation: DiscreteMapper | [r22985](https://www.nesdev.org/w/index.php?oldid=22985) |
| 080 | Taito X1-005 | Not registered | [r24449](https://www.nesdev.org/w/index.php?oldid=24449) |
| 081 | Board-specific specification | New implementation: DiscreteMapper | [r4583](https://www.nesdev.org/w/index.php?oldid=4583) |
| 082 | Taito X1-017 | Not registered | [r24125](https://www.nesdev.org/w/index.php?oldid=24125) |
| 083 | Cony / Yoko | Not registered | [r24580](https://www.nesdev.org/w/index.php?oldid=24580) |
| 084 | Board-specific specification | Not registered | [r24088](https://www.nesdev.org/w/index.php?oldid=24088) |
| 085 | VRC7 | Existing registration: Vrc7 | [r22102](https://www.nesdev.org/w/index.php?oldid=22102) |
| 086 | Jaleco JF-13 | Not registered | [r24513](https://www.nesdev.org/w/index.php?oldid=24513) |
| 087 | Jaleco / Konami J87 | New implementation: DiscreteMapper | [r23104](https://www.nesdev.org/w/index.php?oldid=23104) |
| 088 | Namco 118 variant | New implementation: Namco108 | [r23903](https://www.nesdev.org/w/index.php?oldid=23903) |
| 089 | Sunsoft 2 | New implementation: DiscreteMapper | [r24544](https://www.nesdev.org/w/index.php?oldid=24544) |
| 090 | J.Y. Company ASIC | Not registered | [r24184](https://www.nesdev.org/w/index.php?oldid=24184) |
| 091 | Board-specific specification | Not registered | [r22474](https://www.nesdev.org/w/index.php?oldid=22474) |
| 092 | Jaleco JF-19 | Not registered | [r4687](https://www.nesdev.org/w/index.php?oldid=4687) |
| 093 | Sunsoft 2 | New implementation: DiscreteMapper | [r24542](https://www.nesdev.org/w/index.php?oldid=24542) |
| 094 | UxROM bit-layout variant | New implementation: DiscreteMapper | [r24548](https://www.nesdev.org/w/index.php?oldid=24548) |
| 095 | Namco 118 nametable variant | New implementation: Namco108 | [r23892](https://www.nesdev.org/w/index.php?oldid=23892) |
| 096 | Oeka Kids | Not registered | [r24545](https://www.nesdev.org/w/index.php?oldid=24545) |
| 097 | Irem TAM-S1 | New implementation: DiscreteMapper | [r22478](https://www.nesdev.org/w/index.php?oldid=22478) |
| 098 | Reserved | Not registered | [r22148](https://www.nesdev.org/w/index.php?oldid=22148) |
| 099 | Vs. System board | Not registered | [r23485](https://www.nesdev.org/w/index.php?oldid=23485) |
| 100 | Emulator tool use | Not registered | [r23979](https://www.nesdev.org/w/index.php?oldid=23979) |
| 101 | Board-specific specification | Not registered | [r21360](https://www.nesdev.org/w/index.php?oldid=21360) |
| 102 | Board-specific specification | Not registered | [r4801](https://www.nesdev.org/w/index.php?oldid=4801) |
| 103 | Board-specific specification | Not registered | [r4802](https://www.nesdev.org/w/index.php?oldid=4802) |
| 104 | Codemasters Golden Five | Not registered | [r9567](https://www.nesdev.org/w/index.php?oldid=9567) |
| 105 | NES-EVENT / MMC1 | Not registered | [r23388](https://www.nesdev.org/w/index.php?oldid=23388) |
| 106 | Board-specific specification | Not registered | [r4815](https://www.nesdev.org/w/index.php?oldid=4815) |
| 107 | Board-specific specification | New implementation: DiscreteMapper | [r24577](https://www.nesdev.org/w/index.php?oldid=24577) |
| 108 | FDS-to-cartridge boards | Not registered | [r21237](https://www.nesdev.org/w/index.php?oldid=21237) |
| 109 | Board-specific specification | Not registered | [r4825](https://www.nesdev.org/w/index.php?oldid=4825) |
| 110 | Board-specific specification | Not registered | [r4826](https://www.nesdev.org/w/index.php?oldid=4826) |
| 111 | GTROM / earlier MMC1 variant | Not registered | [r24005](https://www.nesdev.org/w/index.php?oldid=24005) |
| 112 | Board-specific specification | Not registered | [r23867](https://www.nesdev.org/w/index.php?oldid=23867) |
| 113 | Board-specific specification | New implementation: DiscreteMapper | [r20481](https://www.nesdev.org/w/index.php?oldid=20481) |
| 114 | Board-specific specification | Not registered | [r4858](https://www.nesdev.org/w/index.php?oldid=4858) |
| 115 | Board-specific specification | Not registered | [r21614](https://www.nesdev.org/w/index.php?oldid=21614) |
| 116 | Board-specific specification | Not registered | [r22958](https://www.nesdev.org/w/index.php?oldid=22958) |
| 117 | Board-specific specification | Not registered | [r4888](https://www.nesdev.org/w/index.php?oldid=4888) |
| 118 | TxSROM / MMC3 | Not registered | [r24058](https://www.nesdev.org/w/index.php?oldid=24058) |
| 119 | TQROM / MMC3 | Not registered | [r24251](https://www.nesdev.org/w/index.php?oldid=24251) |
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
| 132 | Board-specific specification | Not registered | [r4967](https://www.nesdev.org/w/index.php?oldid=4967) |
| 133 | Board-specific specification | Not registered | [r4977](https://www.nesdev.org/w/index.php?oldid=4977) |
| 134 | Board-specific specification | Not registered | [r20085](https://www.nesdev.org/w/index.php?oldid=20085) |
| 135 | Sachen 8259 | Not registered | [r23709](https://www.nesdev.org/w/index.php?oldid=23709) |
| 136 | Board-specific specification | Not registered | [r21611](https://www.nesdev.org/w/index.php?oldid=21611) |
| 137 | Board-specific specification | Not registered | [r20417](https://www.nesdev.org/w/index.php?oldid=20417) |
| 138 | Sachen 8259 | Not registered | [r23709](https://www.nesdev.org/w/index.php?oldid=23709) |
| 139 | Sachen 8259 | Not registered | [r23709](https://www.nesdev.org/w/index.php?oldid=23709) |
| 140 | Jaleco JF-11/14 | New implementation: DiscreteMapper | [r22403](https://www.nesdev.org/w/index.php?oldid=22403) |
| 141 | Sachen 8259 | Not registered | [r23709](https://www.nesdev.org/w/index.php?oldid=23709) |
| 142 | Board-specific specification | Not registered | [r5032](https://www.nesdev.org/w/index.php?oldid=5032) |
| 143 | Board-specific specification | Not registered | [r5034](https://www.nesdev.org/w/index.php?oldid=5034) |
| 144 | Board-specific specification | Not registered | [r19702](https://www.nesdev.org/w/index.php?oldid=19702) |
| 145 | Board-specific specification | New implementation: DiscreteMapper | [r5039](https://www.nesdev.org/w/index.php?oldid=5039) |
| 146 | NINA-003-006 | Not registered | [r22985](https://www.nesdev.org/w/index.php?oldid=22985) |
| 147 | Board-specific specification | Not registered | [r5057](https://www.nesdev.org/w/index.php?oldid=5057) |
| 148 | Board-specific specification | New implementation: DiscreteMapper | [r24534](https://www.nesdev.org/w/index.php?oldid=24534) |
| 149 | Board-specific specification | New implementation: DiscreteMapper | [r24529](https://www.nesdev.org/w/index.php?oldid=24529) |
| 150 | Board-specific specification | Not registered | [r5081](https://www.nesdev.org/w/index.php?oldid=5081) |
| 151 | Vs. VRC1 duplicate | Not registered | [r21586](https://www.nesdev.org/w/index.php?oldid=21586) |
| 152 | Bandai discrete mirroring variant | New implementation: DiscreteMapper | [r21304](https://www.nesdev.org/w/index.php?oldid=21304) |
| 153 | Bandai FCG + WRAM | Not registered | [r5112](https://www.nesdev.org/w/index.php?oldid=5112) |
| 154 | Namcot-3453 | New implementation: Namco108 | [r23278](https://www.nesdev.org/w/index.php?oldid=23278) |
| 155 | MMC1A | Not registered | [r24550](https://www.nesdev.org/w/index.php?oldid=24550) |
| 156 | Board-specific specification | Not registered | [r22838](https://www.nesdev.org/w/index.php?oldid=22838) |
| 157 | Bandai Datach | Not registered | [r21310](https://www.nesdev.org/w/index.php?oldid=21310) |
| 158 | RAMBO-1 nametable variant | Not registered | [r21299](https://www.nesdev.org/w/index.php?oldid=21299) |
| 159 | Bandai X24C01 EEPROM | Not registered | [r5171](https://www.nesdev.org/w/index.php?oldid=5171) |
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
| 172 | Board-specific specification | Not registered | [r5257](https://www.nesdev.org/w/index.php?oldid=5257) |
| 173 | Board-specific specification | Not registered | [r5274](https://www.nesdev.org/w/index.php?oldid=5274) |
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
| 207 | Taito X1-005 nametable variant | Not registered | [r24116](https://www.nesdev.org/w/index.php?oldid=24116) |
| 208 | Board-specific specification | Not registered | [r5614](https://www.nesdev.org/w/index.php?oldid=5614) |
| 209 | J.Y. Company ASIC | Not registered | [r24184](https://www.nesdev.org/w/index.php?oldid=24184) |
| 210 | Namco 175/340 | Not registered | [r23707](https://www.nesdev.org/w/index.php?oldid=23707) |
| 211 | J.Y. Company ASIC | Not registered | [r24184](https://www.nesdev.org/w/index.php?oldid=24184) |
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
| 243 | Board-specific specification | Not registered | [r5861](https://www.nesdev.org/w/index.php?oldid=5861) |
| 244 | Board-specific specification | Not registered | [r5862](https://www.nesdev.org/w/index.php?oldid=5862) |
| 245 | Board-specific specification | Not registered | [r5871](https://www.nesdev.org/w/index.php?oldid=5871) |
| 246 | Board-specific specification | Not registered | [r22048](https://www.nesdev.org/w/index.php?oldid=22048) |
| 248 | Board-specific specification | Not registered | [r23288](https://www.nesdev.org/w/index.php?oldid=23288) |
| 249 | T9552 | Not registered | [r20254](https://www.nesdev.org/w/index.php?oldid=20254) |
| 250 | Board-specific specification | Not registered | [r21044](https://www.nesdev.org/w/index.php?oldid=21044) |
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
| 262 | Board-specific specification | Not registered | [r7640](https://www.nesdev.org/w/index.php?oldid=7640) |
| 263 | Board-specific specification | Not registered | [r7642](https://www.nesdev.org/w/index.php?oldid=7642) |
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
| 281 | Board-specific specification | Not registered | [r18938](https://www.nesdev.org/w/index.php?oldid=18938) |
| 282 | Board-specific specification | Not registered | [r23351](https://www.nesdev.org/w/index.php?oldid=23351) |
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
| 295 | Board-specific specification | Not registered | [r23352](https://www.nesdev.org/w/index.php?oldid=23352) |
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
| 358 | Board-specific specification | Not registered | [r18941](https://www.nesdev.org/w/index.php?oldid=18941) |
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
| 386 | Board-specific specification | Not registered | [r23354](https://www.nesdev.org/w/index.php?oldid=23354) |
| 387 | Board-specific specification | Not registered | [r23355](https://www.nesdev.org/w/index.php?oldid=23355) |
| 388 | Board-specific specification | Not registered | [r18944](https://www.nesdev.org/w/index.php?oldid=18944) |
| 389 | Board-specific specification | Not registered | [r8004](https://www.nesdev.org/w/index.php?oldid=8004) |
| 390 | Board-specific specification | Not registered | [r18875](https://www.nesdev.org/w/index.php?oldid=18875) |
| 391 | Board-specific specification | Not registered | [r8012](https://www.nesdev.org/w/index.php?oldid=8012) |
| 392 | Board-specific specification | Not registered | [r19172](https://www.nesdev.org/w/index.php?oldid=19172) |
| 393 | Board-specific specification | Not registered | [r8014](https://www.nesdev.org/w/index.php?oldid=8014) |
| 394 | Board-specific specification | Not registered | [r22930](https://www.nesdev.org/w/index.php?oldid=22930) |
| 395 | Board-specific specification | Not registered | [r8017](https://www.nesdev.org/w/index.php?oldid=8017) |
| 396 | Board-specific specification | Not registered | [r22978](https://www.nesdev.org/w/index.php?oldid=22978) |
| 397 | Board-specific specification | Not registered | [r23356](https://www.nesdev.org/w/index.php?oldid=23356) |
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
| 512 | Sachen Zhongguo Daheng | Not registered | [r8073](https://www.nesdev.org/w/index.php?oldid=8073) |
| 513 | Board-specific specification | Not registered | [r8077](https://www.nesdev.org/w/index.php?oldid=8077) |
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
| 552 | Taito X1-017 wiring variant | Not registered | [r24125](https://www.nesdev.org/w/index.php?oldid=24125) |
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
