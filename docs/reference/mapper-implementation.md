# Mapper implementation coverage

This ledger tracks the requested implementation of every documented NES/Famicom
cartridge mapper, including bootlegs and homebrew. **The full target is not yet
complete.** Updated 2026-11-15 against the working-tree registry.

There are **162 registered cartridge IDs**: the original 14, 53 added in the first
batch, and Irem 32/65/77, Sunsoft 67/68, Taito 33/48/80/207/82/552, Jaleco 18/72/92/101,
Namco 210, MMC3 variants 118/119, RAMBO-1 64/158, Bandai 16/153/159, and Datach
157, UNROM 512 30, GTROM/direct MMC1 111, and J.Y. Company
35/90/209/211/281/282/295/358/386/387/388/397, plus the sixteen Sachen/TXC
IDs, Sachen MMC3 clones 262/512/513, Rex Soft's 263, Nitra 250, mappers
114/115/123, T9552 249 / 4:5, mixed CHR 74/192/194/195, MRCM 196, mapper 197,
TNROM640 198, Waixing FS309 199, multicarts 202/205, Gouder/JJ-01 208,
Sugar Softec 215, Single Chip 218, Kasheng A9461 219, NTDEC N625092 221,
address-latched multicarts/RPGs 227, Active Enterprises 228, reset-driven 22-in-1 230 and
profiled 42 Games 233, Maxi 15 234, Golden Game 235, Realtec 236, Teletubbies 237, Contra Fighter 238, BxROM/WRAM 241, 43272/ET-113 242, C&E Decathlon 244, Waixing F003 245, G0151-1 246, Waixing VRC4 252/253 and Pikachu Y2K 254
detailed below.
FDS remains a separate subsystem, conventionally numbered 20. Registration and
synthetic tests do not certify every commercial game, revision, peripheral, or
unusual ROM geometry.

## Implemented in this batch

| Implementation | New IDs | Observable behavior covered |
|---|---|---|
| Pikachu Y2K (follow-up) | 254 | MMC3 PRG/CHR/CIRAM/IRQs, masked WRAM reads until bank-select unlock, raw RAM persistence, reset policy and atomic snapshots. |
| Waixing VRC4 fixed/relocated CHR (follow-up) | 253 | Default CHR4/5 RAM and explicit f009s PPU-write relocation, shared 2 KiB RAM, VRC4e banking/CIRAM/IRQ and profile/geometry-bound atomic snapshots. |
| Waixing VRC4 mixed CHR (follow-up) | 252 | Exact VRC4e A2/A3 decoding, PRG swap, four CIRAM arrangements, WRAM gate, two writable CHR pages and CPU-driven IRQs with atomic snapshots. |
| G0151-1 / Feng Shen Bang (follow-up) | 246 | Independent 8 KiB PRG / 2 KiB CHR banks, narrow GAL register decode, transient A17 vector-read override, fixed H/V, 2 KiB battery RAM and atomic snapshots. |
| Waixing F003 (follow-up) | 245 | Live PPU A10/A11 select the outer PRG half through MMC3 CHR registers; fixed 8 KiB CHR RAM, battery WRAM, scanline IRQ compatibility and atomic snapshots. |
| C&E Decathlon (follow-up) | 244 | Scrambled independent 32 KiB PRG / 8 KiB CHR latches, old-bank bus conflicts, fixed H/V, retained soft-reset banks and four-byte atomic snapshots. |
| 43272 / Waixing RPG / ET-113 (follow-up) | 242 | Address-latched PRG modes, asymmetric ROM chips, five menu pads, CHR protection, battery WRAM and validated snapshots. |
| Standard BxROM with WRAM (follow-up) | 241 | Full-byte 32 KiB PRG banking, fixed H/V, 8 KiB WRAM with optional battery, fixed CHR ROM/RAM and atomic snapshots. Optional LPC speech is available through the explicit `lpc` profile. |
| Contra Fighter compatibility model (follow-up) | 238 | Shared protection latch, MMC3 PRG/CHR/CIRAM and IRQs, CPU/DMA execution, explicit reset policy and compact atomic snapshots. |
| Teletubbies 420-in-1 (follow-up) | 237 | Four PRG modes, configuration lock, forced ROM A1 type detection, H/V CIRAM, CHR RAM, CPU execution and validated snapshots. |
| Realtec multicarts (follow-up) | 236 | Three ROM/RAM populations, four PRG modes, address-substituting solder pads, H/V CIRAM, CHR ROM/RAM, CPU execution and validated snapshots. |
| Golden Game (follow-up) | 235 | Eight ROM socket profiles, 16/32 KiB PRG, empty-socket open bus, H/V/single CIRAM, CHR RAM, CPU execution and wiring-bound snapshots. |
| Maxi 15 (follow-up) | 234 | CNROM/NINA inner/outer banking, read-triggered latches, bus-conflicting writes, one-way lock, ROM gates, Australian fixed CHR, CPU execution and compact snapshots. |
| Profiled 42 Games (follow-up) | 233 | Reset-selected/software-controlled A19, separate Disch and tentative vector-read profiles, PRG modes, custom CIRAM, CHR gates, CPU execution and profile-bound snapshots. |
| Reset-driven 22-in-1 (follow-up) | 230 | Contra/multicart chip selection, soft-reset alternation, explicit cold start, 16/32 KiB PRG, H/V CIRAM, retained CHR RAM, CPU execution and snapshots. |
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
| J.Y. Company ASIC (follow-up) | 35, 90, 160, 209, 211 | PRG/CHR modes and outer banking, physical-read CHR latches, mixed CHR/CIRAM nametables, all four documented IRQ sources, math/protection ports, board jumpers, WRAM persistence and snapshots. Mapper 160 is an obsolete mapper-90 alias with advanced nametables inhibited. |
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
- Standard iNES trainers seed the backing RAM at `$7000–$71FF` on boards
  that expose this window to the cartridge loader. Cold power reseeds volatile
  trainer RAM. Soft reset retains it, and imported battery RAM takes precedence.
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
[mapper-160 tests](../../tests/unit/test_mapper160.cpp),
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
[Mapper 160](https://www.nesdev.org/wiki/INES_Mapper_160) is registered as an
obsolete mapper-90 alias; no separate SA009 circuit is inferred from historical
emulator claims. Mappers 90 and 160 inhibit both ROM nametables and extended CIRAM
arrangements. Mapper
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

## Mapper-230 follow-up: reset-driven 22-in-1

Original `Multicart230` implementation follows [NESdev r5788](https://www.nesdev.org/w/index.php?oldid=5788).
No external emulator implementation was consulted or copied. This is a distinct
reset/chip-selection contract; it is not registered as a blanket mapper-226 alias.

The image packs 128 KiB Contra PRG followed by a separate 512 KiB multicart chip.
Contra selects a lower 16 KiB page with D2-D0, fixes the upper page at seven and
uses vertical CIRAM. Multicart D4-D0 select the page, D5 chooses mirrored 16 KiB
or an aligned even/odd 32 KiB pair, and D6 selects H/V (one is vertical).
All $8000-$FFFF addresses latch the supplied data. Ignore upper data bits per
mode; CPU reads do not relatch. Decode each chip separately, without applying
whole-image modulo across the 640 KiB package.

Every soft reset changes the active chip. The brief source does not establish
cold electrical defaults, reset-time register retention or pattern-memory gates.
The explicit compatibility model starts in Contra, clears the bank register on
each reset and uses writable, unbanked 8 KiB CHR RAM in both modes. Soft resets
retain CHR bytes; power-on zeroes them deterministically. No speculative CHR
protection, IRQ, expansion audio, WRAM or battery storage is added. PCB-level
reset and write-protection evidence remains a validation limit.

`Mapper::power_on()` and the cartridge/emulator forwarding distinguish startup
from `reset()`. The default mapper hook invokes the existing reset method, so
previous boards preserve their policy. Mapper 230 overrides it to select Contra;
soft reset toggles the mode without an extra first-reset flag. CLI startup,
synchronous session loading, background preparation and session power cycling
use power-on. Session reset remains soft; adopting prepared media adds no reset.
This hook controls cartridge initialization and the existing console device reset
sequence; it does not introduce a complete transistor-level power-cycle model.

Accept legacy and NES 2.0 submapper zero, exactly 640 KiB PRG, no CHR ROM, ordinary
H/V metadata, and exactly 8 KiB volatile CHR RAM in NES 2.0. Reject other ROM
populations, battery, additional RAM/NVRAM, alternate CIRAM and unknown submappers.
Ignore legacy generic PRG-RAM inference. Low CPU memory remains undriven.

Component snapshots are 8196 bytes: $E600 tag, one-byte mode, one-byte canonical
bank register and 8 KiB CHR. Validate the mode (0/1), mode-specific bank mask,
tag and truncation before committing local RAM. Failed component loads leave
all state unchanged; rejection never toggles the selected game. Whole-machine
failure rolls back atomically. Mode and RAM restoration determine the next
soft-reset result, without a hidden boot counter. Existing board payloads,
machine version 20 and the 4 MiB state allocation ceiling are unchanged.

Tests cover every data byte and register address in both modes, chip boundaries,
mirroring, ignored bits, every CHR RAM cell, repeated reset/power-on, open bus,
geometry, malformed states and failed-load retention. Four original CPU images
exercise legacy/NES 2.0 and both H/V metadata values, with separate firmware on
each physical chip. They check banked instruction fetches, PPUDATA, CIRAM,
CHR retention, both reset phases and replay. Shared original firmware also checks
raw session startup/reload, archive background preparation, adoption, soft reset
and power cycling. CLI tests check six startup profiles and 24 invalid images,
preserve ROM bytes and create no saves. CLI frame completion is loading smoke;
CPU mode-completion markers are asserted in the unit and application fixtures.

Validation: all 71 regular targets, including required GPU checks, and all nine
focused ASan+UBSan targets passed. Both builds passed 5,906,309 mapper assertions
across 522 cases; mapper 230 contributes 400,812 assertions across nine cases.
The loading-path regression passed 60 assertions; the complete ROM-source and
session targets passed in both builds. The sanitizer mapper target took 101.07
seconds within its existing 120-second timeout. The initial regression failed
at factory creation before implementation. Formatting, scoped clang-tidy,
whitespace and registry/ledger equality checks passed. Isolated canonical-document
validation retained the November 14 baseline: zero errors, 72 warnings and three
infos across 193 documents.

## Mapper-233 follow-up: 42 Games and conflicting historical models

Original `Multicart233` code uses [NESdev/Disch r5799](https://www.nesdev.org/w/index.php?oldid=5799),
[the original Everynes 226/233 descriptions](https://problemkaputt.de/everynes.htm#mapper233xin1plusreset),
and the [2020 board identification](https://forums.nesdev.org/viewtopic.php?t=19679)
plus [2025 A19 discussion](https://forums.nesdev.org/viewtopic.php?start=15&t=19679).
Direct forum access returned 403; indexed post text was available. No external
implementation code or commercial ROM data was used.

The sources conflict. Disch labels his register account untested and questions
its assignment. Everynes describes a different reset-vector effect. The later
board thread identifies the reset-selected 1 MiB version as mapper 233 and
reports software control of A19. Keep these models separate; do not silently
combine their incompatible mirroring and write protocols or alias mapper 226.
Select profiles before installing the cartridge, through `configure_board_profile`
or CLI `--board-profile NAME`. No private NES 2.0 submapper numbers are assigned.

| Profile | Register/CIRAM model | Reset model |
|---|---|---|
| `reset-42` (default) | 226-family even control/odd auxiliary ports; control D7 selects A19, D4-D0 inner page, D5 mirrored 16 KiB vs aligned 32 KiB, D6 H/V | Toggle the current A19; clear inner, mirror and auxiliary bits. |
| `disch-233` | One byte at every $8000-$FFFF address; D7-D6 select AAAB, vertical, horizontal or upper one-screen; no A19 pin | Clear the control latch. Maximum 512 KiB PRG. |
| `everynes-vector` (tentative, opt-in) | Same fitted 226-family register/CIRAM model as `reset-42` | Clear latches, then any completed CPU read of exactly $FFFD sets control to $FF after the old mapping supplies its byte. |

The default's reset action is an explicit compatibility policy derived from the
A19 report: software writes can change the same bit that the next reset toggles.
There is no independent hidden reset counter. Port aliases and auxiliary D1 CHR
protection follow the documented 226-family description; their inheritance on
all 233 PCBs is not established. Ignore auxiliary A20 selection because supported
populations stop at 1 MiB. The tentative profile implements Everynes's conjectured
$FF value and assumes a read-completion transition; neither value nor timing is
hardware-certified. Direct mapper ROM reads are side-effect-free; CPU/dummy/DMA
reads reach the existing completed-read observer. Other profiles do not react
to $FFFD. No speculative sticky outer-bank lock is added.

The Disch profile uses the existing nametable-address seam for the unusual AAAB
map (physical CIRAM pages 0/0/0/1); ordinary `MirrorMode` cannot express it.
All pattern RAM remains readable. The family profiles inhibit CHR writes when
auxiliary D1 is set; Disch's CHR RAM is always writable. RAM is unbanked and
retained across soft reset. Power-on clears latches and zeroes CHR deterministically.
Those cold defaults, write gates and reset-time register clearing remain explicit
model assumptions pending stronger board evidence.

Accept legacy/NES 2.0 submapper zero, ordinary H/V metadata, no CHR ROM, and PRG
16 KiB-1 MiB in 16 KiB units. The Disch profile rejects PRG above 512 KiB.
Native headers must declare exactly 8 KiB volatile CHR RAM and no other RAM/NVRAM.
Reject battery, alternate CIRAM, unknown submappers and larger/partial ROM banks.
Decode bank pins before fitted-ROM wrapping; small/non-power-of-two images are
compatibility inputs, not claimed physical populations. Ignore legacy generic
PRG-RAM inference. No unused WRAM, IRQ or expansion audio is added; low CPU
windows float. The unidentified CHR-ROM-based "Unknown Multicart 1" stays
unsupported, with no guessed game-specific CHR/mirroring table or hash correction.

Component state is 8196 bytes: profile tag $E900/$E901/$E902, one-byte control,
one-byte auxiliary (0/2, and zero only for Disch), then 8 KiB CHR RAM. Read RAM
locally and validate tags/gates/truncation before commit. Rejected component
loads change nothing and never reset the game. Whole-machine failure rolls back
atomically, including when identical ROM bytes are configured with another
profile. Existing board payloads, machine version 20 and the 4 MiB state cap
remain unchanged.

Tests cover every register address and data byte, both PRG modes, A19/software
selection, reset/power-on, smaller ROM wrapping, all CIRAM layouts and external
nametable aliases, CHR gates, low open bus, profiles and malformed snapshots.
The full PPU path verifies AAAB mapping at every $2000-$3EFF address and writes
through its aliases. Twelve original CPU fixtures cover profiles, legacy/NES 2.0
and H/V metadata, banked fetches, PPUDATA, vector-read effects and replay.
The bus fixture proves the pre-change vector byte is returned. Default-profile
application fixtures check raw/reloaded media, archive preparation/adoption,
retained reset phase and power cycling. CLI checks 48 loading profiles and
27 rejected inputs without modifying ROMs or creating saves. CLI frame completion
is loading smoke; unit/application fixtures assert CPU completion markers.

Validation: all 72 regular targets, including required GPU checks, and all nine
focused ASan+UBSan targets passed. Both builds passed 6,624,232 mapper assertions
across 533 cases; mapper 233 contributes 717,908 assertions across eleven cases.
The new application loading/reset regression passed 58 assertions in both builds.
The sanitizer mapper target took 107.76 seconds within its existing 120-second
timeout. The initial regression failed at factory creation before implementation.
Formatting, scoped clang-tidy, whitespace and registry/ledger equality checks
passed. Isolated canonical-document validation retained the November 14 baseline:
zero errors, 72 warnings and three infos across 193 documents.

## Mapper-234 follow-up: Maxi 15

Original `Maxi15` code follows [NESdev r24540](https://www.nesdev.org/w/index.php?oldid=24540),
[Mark's preliminary hardware description](https://www.nesdev.org/maxi15.txt),
and [krzysiobal's three-chip board analysis and the normalized dump discussion](https://forums.nesdev.org/viewtopic.php?t=24627).
Direct forum access returned 403; indexed post text was available. No external
implementation code or commercial ROM data was used.

Outer control is at $FF80-$FF9F, the CIC latch at $FFC0-$FFDF, and inner control
at $FFE8-$FFF7. CPU reads latch the byte supplied by the old mapping through the
existing completed-read observer; opcode, dummy and DMA reads use that same seam.
Direct mapper ROM inspection has no side effects. Writes use CPU data AND the
old driven PRG byte; disabled ROM produces no write conflict or extra read strobe.
CNROM mode combines an outer 32 KiB block with two inner CHR bits. NINA mode
uses an outer 64 KiB block, one inner PRG bit and three inner CHR bits. Mirroring
is outer-controlled H/V. CHR ROM is read-only throughout.

Follow the corrected r24540 rules: any outer Q/q/block bit locks outer and CIC
updates until reset. Mode/mirroring bits alone do not lock; inner updates remain
available. Preserve all four meaningful inner bits even while some are masked
by CNROM mode. Reset clears all three registers, including CIC. This deliberately
supersedes the preliminary account's different CIC gating/reset statements.
The two-bit CIC latch is serialized; analog charge-pump/lockout defeat is not
modeled, and there is no mapper IRQ, audio, RAM or battery storage.

ROM geometry selects the wiring: 512/512 KiB uses the original two-chip model;
1024/512 KiB uses the Australian three-chip model, whose CHR chip is permanently
enabled; 1024/1024 KiB models the documented four-chip layout. The latter is not
claimed to be a released, hardware-certified population. Q selects the second
PRG/CHR pair, while q inhibits that pair. Unpopulated selections float. The
Australian CHR ignores both gates and the extra chip-select address bit, as its
/CE is tied low. Its canonical 1 MiB PRG image contains a 64 KiB chip repeated
across the first 512 KiB, followed by the main 512 KiB chip. Loading neither
repairs nor reorders a dump; incomplete 576 KiB images are rejected.

CPU floating uses `prg_read_mask=0`. Disabled CHR reads return the low external
PPU address byte, matching the project's undriven nametable-bus policy; this is
an explicit emulator policy, not a new analog PPU-bus model. CIRAM remains active
when cartridge ROM output is disabled. Alternative two-chip CHR-enable wiring
and other physical revisions are not certified by the synthetic fixtures.

Accept legacy and NES 2.0 submapper zero, ordinary H/V metadata and exactly the
three ROM-size pairs above, with no CHR RAM, CPU RAM or NVRAM. Ignore legacy
generic PRG-RAM inference. Reject other geometries, battery, alternate CIRAM
and unknown submappers; no speculative 1.5 MiB bank or implicit ROM wrapping.

Snapshots are five bytes: $EA00/$EA01/$EA03 population tag, outer byte, inner
byte masked to $71, and two-bit CIC byte. Validate all fields before commit;
rejected component loads change nothing. Whole-machine rejection rolls back
atomically. Existing board payloads, machine version 20 and the 4 MiB state cap
are unchanged. No unused CHR/WRAM buffers are allocated.

Tests cover all outer/inner pin combinations, exact register ranges across the
full CPU address space, write conflicts, old-map read ordering, latent inner
bits, lock conditions, ROM gates, fixed CHR, PPUDATA/CIRAM, geometry and states.
Thirty original CPU fixtures cover both bank modes, read/write selection,
legacy/NES 2.0 and all populations, including six RAM-resident ROM-disabled
programs. Two further fixtures prove register effects on opcode fetches and a
page-crossing dummy read whose actual operand comes from console RAM. They also
check reset, power-on, replay and atomic invalid-state rollback. Application
fixtures check raw/reloaded files, archive preparation/adoption, disabled-ROM
execution and reset/power cycling. CLI checks 54 loading profiles and 28 invalid
inputs, retaining ROM bytes and creating no saves. CLI frame completion is loading
smoke; unit/application fixtures assert CPU completion markers.

Validation: all 73 regular targets, including required GPU checks, and all nine
focused ASan+UBSan targets passed. Both builds passed 7,124,732 mapper assertions
across 545 cases; mapper 234 contributes 500,485 assertions across twelve cases.
The new application loading/reset regression passed 162 assertions in both builds.
The sanitizer mapper target took 116.43 seconds within its existing 120-second
timeout. The initial regression failed at factory creation before implementation.
Formatting, scoped clang-tidy, whitespace and registry/ledger equality checks
passed. Isolated canonical-document validation retained the November 14 baseline:
zero errors, 72 warnings and three infos across 193 documents.

## Mapper-235 follow-up: Golden Game

Original `GoldenGame` code follows [NESdev r22514](https://www.nesdev.org/w/index.php?oldid=22514),
[Kevin Horton's board description](http://kevtris.org/mappers/bmc_nes/BTL_150in1A.html),
[the populated-socket hardware discussion](https://forums.nesdev.org/viewtopic.php?start=45&t=16551),
and [the corrected 2025 dumping report](https://forums.nesdev.org/viewtopic.php?p=299736).
The newer report corrects the original description's reversed PRG mode bit.
No external emulator implementation or commercial ROM bytes were used.

Every write at $8000-$FFFF latches address pins; data is ignored. A4-A0 select
one of 32 aligned 32 KiB blocks inside a 1 MiB chip, A9-A8 select its physical
socket, A11 selects mirrored 16 KiB instead of 32 KiB, and A12 selects the
16 KiB half in that mode. A13 selects vertical instead of horizontal CIRAM;
A10 overrides it with single-screen lower. A14 and A7-A5 are unconnected.
The canonical latch mask is $3F1F. There is 8 KiB unbanked writable CHR RAM,
with no CPU RAM, IRQ, expansion audio or battery storage.

Four physical sockets allow eight populations with socket zero present. The
CLI `--board-profile` / core `configure_board_profile` names are:

| Populated sockets | Profile | Packed PRG |
|---|---|---|
| 0 | `slots-0` | 1 MiB |
| 0, 1 | `slots-0-1` | 2 MiB |
| 0, 2 | `slots-0-2` | 2 MiB |
| 0, 1, 2 | `slots-0-1-2` | 3 MiB |
| 0, 3 | `slots-0-3` | 2 MiB |
| 0, 1, 3 | `slots-0-1-3` | 3 MiB |
| 0, 2, 3 | `slots-0-2-3` | 3 MiB |
| 0, 1, 2, 3 | `slots-0-1-2-3` | 4 MiB |

Files pack full 1 MiB chip blocks in ascending physical socket order. Defaults
are sockets 0 for 1 MiB, 0/2 for the documented 2 MiB 150-in-1, 0/1/2 for the
newer 3 MiB dump report, and all four for 4 MiB. Other layouts require explicit
selection; ROM length cannot distinguish equal-size socket populations. Profiles
validate the chip count, and snapshots bind to the selected wiring. These eight
decoder models do not certify every possible physical board. Smaller EPROMs,
other packing orders and incomplete dumps need separate evidence/normalization;
loading performs no title/hash detection, chip replication or automatic repair.
The old 4 MiB dump with a duplicated fourth block is not proof of four chips.

Absent sockets leave all CPU data bits floating via `prg_read_mask=0`, without
wrapping into another chip. RAM-resident code can reconnect ROM with another
write. CHR RAM and CIRAM remain accessible while CPU ROM floats. Reset clears
the address latch and retains CHR RAM; cold power-on additionally clears CHR
RAM as a deterministic initialization policy. The hardware report only says
reset appears to clear the latches; analog power-up contents are not certified.

Accept legacy iNES and NES 2.0 submapper zero with exact 1/2/3/4 MiB PRG, no CHR
ROM, ordinary H/V metadata and 8 KiB volatile CHR RAM. The 4 MiB file uses NES
2.0 extended PRG size. Legacy generic PRG-RAM inference is ignored. Reject
battery, CPU RAM/NVRAM, alternate CHR populations, partial chips, other CIRAM
metadata and unknown submappers.

The mapper payload is 8,196 bytes: two-byte $EB00-or-socket-mask wiring tag,
two-byte canonical address latch, and 8 KiB CHR RAM. Validate before commit;
failed component restores leave the board unchanged, and machine restores roll
back atomically. Existing mapper payloads, machine version 20 and the 4 MiB
state cap remain unchanged. Only actual CHR RAM is allocated beside ROM.

The dedicated `mapper235` target checks all socket/bank/mode/mirroring
combinations, every register address, ignored data/pins, all CHR cells, geometry,
profiles and malformed states. Twenty-nine original CPU images cover the eight
layouts and both header formats where representable, including fourteen
RAM-resident empty-socket programs. They assert banking, PPUDATA/CIRAM, cold and
soft reset, deterministic replay and atomic rejection. Bus fixtures check all
256 floating values, CIRAM access and ROM reconnection; equal-ROM machine
fixtures reject different wiring. Shared mapper tests retain registration,
CPU boot and machine-state coverage. Application fixtures check raw/archive
preparation, adoption, reload, reset and power cycling. CLI checks 51 loading
profiles and 33 rejected inputs, preserving source bytes and creating no saves;
CPU completion markers are asserted in unit/application fixtures.

Validation: all 75 regular targets, including required GPU checks, and all ten
focused ASan+UBSan targets passed. Both builds passed 726,721 assertions across
ten cases in the dedicated mapper-235 target, and 7,124,747 assertions across
545 cases in the shared mapper target. The new application regression passed
203 assertions. The shared sanitizer mapper target took 117.99 seconds within
its existing 120-second timeout; mapper 235 took 17.92 seconds in its own target.
The initial regression failed at factory creation before implementation.
Formatting, scoped clang-tidy, whitespace and exact 151-ID registry/ledger
checks passed. Isolated canonical-document validation preserved the baseline:
zero errors, 72 warnings and three infos across 193 documents.

## Mapper-236 follow-up: Realtec multicarts

Original `RealtecMulticart` follows the register specification in
[NESdev r18875](https://www.nesdev.org/w/index.php?oldid=18875).
It covers the 8031/8155 128/64 KiB PRG/CHR-ROM population, 8099's doubled
256/128 KiB population, and the documented 8106 512 KiB PRG / 8 KiB CHR-RAM
compatibility model. No external emulator implementation or commercial ROM data
was used. Board schematics and electrical reset behavior were not verified.

Writes latch address pins, ignoring data: $8000-$BFFF controls mirroring and
CHR banking on ROM boards, or outer PRG bits on the RAM board; $C000-$FFFF
controls inner PRG and four modes. UNROM fixes only the upper window's low
three bank bits, preserving the selected 128 KiB block. Pad-read mode adds
replacement of ROM A0-A3 by four solder-pad bits in both CPU windows, returning
ROM data at that substituted address. NROM-256 aligns a pair; NROM-128 mirrors
one page. Lower-latch A5 selects vertical/horizontal CIRAM. Unconnected chip
address pins alias by physical power-of-two size. CHR ROM is banked and
read-only; CHR RAM is always writable and unbanked.

Pads default to zero as a compatibility policy. Core
`configure_board_jumpers(0..15)` and CLI/core profiles `menu-0` through `menu-15`
select the physical menu wiring; selection does not write the latches or RAM.
Other profiles/pad values fail without mutation. Pad mode drives ROM bytes,
not a constant pad value or open bus. CPU space below $8000 remains unmapped.
No WRAM, battery, IRQ or expansion audio is added.

Only the three size pairs above are accepted, with legacy iNES or NES 2.0
submapper zero and H/V metadata. Native RAM metadata must match the selected
population exactly; legacy generic WRAM inference is ignored. Battery, NVRAM,
other CHR RAM sizes, unsupported ROM pairs, partial chips, alternate CIRAM
and unknown submappers are rejected. Larger theoretical decoders and other PCB
revisions need evidence before implementation.

Reset values are not specified by the source. This implementation explicitly
clears both latches on soft reset, retains CHR RAM and solder pads, and clears
CHR RAM additionally on cold power-on. Zero latches select UNROM page zero / fixed
page seven, CHR page zero and vertical CIRAM, regardless of H/V file metadata.
These are deterministic emulator policies, not claims about analog power-up.

Snapshots contain a two-byte $EC00-or-(pads<<4)-or-population tag, followed by
canonical lower/upper bytes; only the RAM board adds its 8 KiB pattern memory.
Population codes are 0/1/2 in the order above; payload sizes are 4/4/8,196 bytes.
ROM-board latch masks are $2F/$3F; RAM-board masks are $27/$37. Tags bind pad
configuration and geometry, including equal-ROM machines with different pads.
Validation precedes commit, and whole-machine rejection rolls back atomically.
Existing mapper payloads, machine version 20 and its 4 MiB cap remain unchanged.
ROM populations allocate no unused CHR RAM or WRAM.

The dedicated `mapper236` target covers all latch combinations, all 65,536 CPU
write addresses, ignored data/pins, all pad values, ROM address substitution,
CHR protection/RAM, CIRAM, geometry and malformed states. Ninety-six original
RAM-resident CPU programs cover all populations, all pads and both header
formats; odd pad values finish in pad-read mode for machine replay tests.
They assert completion, banking, PPUDATA/CIRAM, reset, cold start, deterministic
replay and invalid-state rollback. The shared mapper matrix retains registration,
CPU boot and machine-state checks. Application tests check raw/archive loading,
preparation/adoption, reload, reset and power cycling. CLI checks 60 loading
profiles and 67 invalid inputs, preserving ROM bytes and creating no saves.
CLI frame completion is loading smoke; core/application tests assert CPU markers.

Validation: all 77 regular targets, including required GPU checks, and all eleven
focused ASan+UBSan targets passed. Both builds passed 1,859,768 assertions across
ten cases in the dedicated mapper-236 target, plus 7,124,762 assertions across
545 cases in the shared mapper target. The new application regression passed
174 assertions. The shared sanitizer mapper target took 118.27 seconds within
its existing 120-second timeout; mapper 236 took 17.63 seconds in its own target.
The initial regression failed at factory creation before implementation.
Formatting, scoped clang-tidy, whitespace and exact 152-ID registry/ledger
checks passed. Isolated canonical-document validation preserved the baseline:
zero errors, 72 warnings and three infos across 193 documents.

## Mapper-237 follow-up: Teletubbies 420-in-1

Original `Teletubbies` follows [NESdev r5823](https://www.nesdev.org/w/index.php?oldid=5823)
and [BootGod's 2010 hardware report](https://forums.nesdev.org/viewtopic.php?p=55608).
Direct downloads returned 403/DNS errors; indexed article/report text was available.
No external emulator implementation or commercial ROM data was used. The examined
board has 1 MiB PRG and 8 KiB unbanked CHR RAM. Other type-detection wiring is
suggested by the source, but remains unverified and has no implemented profile.
The old description's "CNROM" name does not denote hardware CHR banking.

Writes at $8000-$FFFF capture A2-A0 and all eight data bits without conflicts.
A2 and D4-D3 choose a 128 KiB block; D2-D0 choose the inner page. D7-D6 select
UNROM, UNROM with its low window forced even, mirrored 16 KiB NROM, or aligned
32 KiB NROM. D5 selects horizontal/vertical CIRAM. A0 enables type mode, forcing
ROM A1 high in both CPU windows, including opcode/operand fetches. This returns
actual ROM bytes at substituted addresses rather than a constant menu number.
A1 locks every configuration bit except inner D2-D0 until reset. The write that
sets the lock captures all fields; later address pins and D7-D3 are ignored.

Only the documented 1 MiB / 8 KiB population is accepted: legacy iNES or NES 2.0
submapper zero, no CHR ROM, H/V metadata, and no CPU RAM, NVRAM or battery.
Native memory declarations must match; legacy generic WRAM inference is ignored.
Reject partial/alternate ROM sizes, other CHR populations, alternate CIRAM,
unknown submappers and speculative menu/type profiles. No IRQ or expansion audio
is added; CPU space below $8000 remains unmapped. CHR RAM stays writable in every
bank, type and lock mode. No unused WRAM is allocated.

The source confirms reset clears lock, type, NROM mode and outer-bank bits,
but is uncertain about transparency, mirroring and inner bits. This implementation
explicitly clears all address/data bits on reset: UNROM 0/7, horizontal CIRAM,
type off and unlocked. CHR RAM is retained on soft reset, and cleared additionally
on cold power-on as a deterministic initialization policy. Electrical retention
of the uncertain latch bits and analog RAM power-up are not certified.

State is 8,196 bytes: two-byte $ED00 wiring tag, three-bit address latch,
unmasked data byte and 8 KiB CHR RAM. Preserve latent D0 even in modes that ignore
it. Validate tag, address pins and length before commit; component rejection
changes nothing, and machine rejection rolls back atomically. Existing mapper
payloads, machine version 20 and the 4 MiB state cap remain unchanged.

The dedicated `mapper237` target checks every initial latch combination, lock
updates, all 65,536 CPU write addresses both locked/unlocked, all mode/type
paths, forced-address ROM data, CHR cells, reset, geometry and invalid states.
Sixty-four original RAM-resident CPU images cover four modes, normal/type reads,
outer-block boundaries and both header formats. They assert completion, actual
menu probing, inner writes after lock, PPUDATA/CIRAM, cold/soft reset, snapshots
with type and lock active, replay and rejection rollback. An additional CPU
fixture proves forced A1 applies to ROM opcode/operand fetches using aliased
instruction pairs. Bus fixtures prove type reads still drive real ROM data while
CHR/CIRAM remain accessible. Shared mapper tests retain registration, boot and
machine-state coverage. Application tests check raw/archive preparation,
adoption, reload, reset and power cycling in all four modes with/without type.
CLI checks 64 loading profiles and 30 rejected inputs, retaining ROM bytes and
creating no saves; CPU completion markers are asserted in unit/application tests.

Validation: all 80 regular targets, including required GPU checks, passed.
Thirteen focused ASan+UBSan targets passed across the initial run and affected
checks rerun. The first sanitizer run exposed an opcode fixture's dependency
on prior registry setup and a 30-second aggregate ROM-loading timeout. The
fixture now registers its own factories (also verified in isolation); the new
loading matrix runs as `rom-source-mapper237`, with the shared target excluding
that tag. Every case still runs, and no timeout was increased. All three affected
targets passed after these test changes; ten unchanged targets had already passed.

Both builds passed 936,196 assertions across ten dedicated mapper-237 cases.
The regular shared mapper target passed 7,124,777 assertions across 545 cases.
The application matrix passed 464 assertions in both builds; shared and dedicated
loading targets together retain all 44 cases and 21,565 assertions. The shared
sanitizer mapper target took 117.68 seconds within its existing 120-second budget.
The mapper-237 retry took 20.30 seconds; shared/dedicated loading took 22.59/7.29
seconds within their unchanged 30-second budgets. The initial regression failed
at factory creation before implementation. Formatting, scoped clang-tidy,
whitespace and exact 153-ID registry/ledger checks passed. Isolated canonical
document validation preserved zero errors, 72 warnings and three infos across
193 documents.

## Mapper-238 follow-up: Contra Fighter compatibility model

`Mmc3` now supports Contra Fighter / UNL-603-5052. The
[NESdev r5824 entry](https://www.nesdev.org/w/index.php?oldid=5824) is a title-only
stub, with no register specification. [CaH4e3's original 2006 dump report](https://cah4e3.shedevr.org.ru/dumping_2006.php)
identifies the protected MMC3 board. Its functional compatibility model was
checked against [CaH4e3's published reference](https://github.com/TASEmulators/fceux/blob/master/src/boards/603-5052.cpp)
and [Mesen's reference at revision 152abf6](https://github.com/SourMesen/Mesen2/blob/152abf690bb0568cd36de34f1744462d6be66f45/Core/NES/Mappers/Mmc3Variants/MMC3_238.h).
The implementation here uses the existing MapperBus MMC3 and an independently
expressed Boolean decoder; external implementation code is not incorporated.
No commercial ROM data was downloaded or used.

All CPU reads/writes at $4020-$7FFF address one protection latch. Input bits
D1-D0 produce responses 0, 2, 2, 3 for values 0, 1, 2, 3; higher input bits and
address bits within the window are ignored. Output D1 is the OR of the inputs,
and D0 their AND. Reads drive a complete byte in this compatibility model;
physical bus-drive masks have not been measured. The $4020-$5FFF expansion
route and $6000-$7FFF cartridge route share the same latch. MMC3's $A001 WRAM
gates do not block it, and writes never modify PRG/CHR banking or IRQ state.
Console I/O through $401F stays separate, including OAM DMA control at $4014.

Support the reference's 128 KiB PRG / 256 KiB CHR-ROM population, legacy iNES or
NES 2.0 submapper zero and ordinary H/V metadata. [MAME's software metadata](https://github.com/mamedev/mame/blob/master/hash/nes.xml)
also lists this size pair, while marking its dump bad; that is not hardware
certification or a reason to repair images automatically. Reject other size
pairs, partial chips, battery, native CPU RAM/NVRAM, CHR RAM/NVRAM, alternate
CIRAM and unknown submappers. Ignore legacy generic WRAM inference. There is no
accessible WRAM, CHR RAM or expansion audio. Existing MMC3 banking, CHR write
protection, CIRAM and scanline IRQ behavior are reused.

Cold power clears protection to zero; soft reset retains it while applying
MapperBus's existing MMC3 core-reset policy. These are explicit compatibility
policies. Exact PCB reset behavior, analog bus drive, clone A12 filtering and
zero-latch IRQ behavior are unverified. The existing repeating-zero MMC3 IRQ
model is retained, with no PPU timing changes or speculative Huang-1 supervisor
modes from other boards added.

State is 18 bytes: fifteen existing compact MMC3 register/IRQ/control bytes,
a two-byte $EE00 tag and one protection-response byte. Only 0/2/3 are reachable
latch values; other values and malformed common fields are rejected. An 18-byte
backup restores the component atomically on failure, without copying ROM or
large RAM buffers. Whole-machine rejection remains atomic. The existing MMC3
class's fixed backing arrays remain allocated but inaccessible; they are not
serialized for this ROM-only board. Existing mapper payloads, machine version
20 and its 4 MiB state cap remain unchanged.

The dedicated `mapper238` target checks all CPU register addresses, every input
byte, protection aliases, fully driven reads, absent-WRAM gates, all PRG/CHR
register values and mode bits, ROM protection, IRQs, reset and malformed states.
Sixteen original CPU images span both header formats, H/V metadata and all four
protection inputs, asserting completion, PPUDATA/CIRAM, PPU-driven IRQs, replay
and atomic rejection. Bus fixtures verify protection reads during OAM DMA and
separation from console I/O. The shared mapper matrix retains registration,
CPU boot and state checks. `rom-source-mapper238` covers raw/archive loading,
preparation/adoption, reload, soft reset and cold power without growing the
shared loading timeout. CLI checks sixteen loading profiles and 27 rejected
inputs, preserving source bytes and creating no saves; C++ fixtures assert CPU
completion markers rather than treating CLI frame completion as execution proof.

Validation: all 84 regular targets, including required GPU checks, passed.
Sixteen focused ASan+UBSan targets passed across the initial run and affected
checks rerun. The initial shared mapper run hit its existing 120-second limit;
`test_flash_boards.cpp` was moved unchanged into `flash-boards`. Every check is
retained and no timeout was increased. Fourteen other sanitizer targets passed
initially; both reorganized targets passed afterward.

Both builds passed 398,995 dedicated mapper-238 assertions across nine cases,
plus the new application's 124 assertions. Shared mapper and flash targets
retain exactly the previous combined 7,124,792 assertions across 545 cases:
7,123,539/534 in `mappers` and 1,253/11 in `flash-boards`. The sanitizer retry took
117.56 and 3.98 seconds respectively, within each existing 120-second budget.
All 45 loading cases remain registered across the shared and two dedicated
loading targets. Factory creation was unsupported before implementation.
Formatting, scoped clang-tidy, whitespace and exact 154-ID registry/ledger
checks passed. Isolated canonical-document validation preserved zero errors,
72 warnings and three infos across 193 documents.

## Mapper-241 follow-up: standard BxROM with WRAM

`BxromWram` implements the standard population described by
[NESdev mapper 241 revision 23728](https://www.nesdev.org/w/index.php?oldid=23728):
32 KiB PRG banking, fixed horizontal/vertical mirroring and 8 KiB WRAM at
$6000-$7FFF, optionally battery-backed. All $8000-$FFFF writes latch the data
byte without bus conflicts. $7FFD-$7FFF remain ordinary WRAM, with no NINA-001
register overlay. There are no mapper-controlled mirroring bits or IRQs.

The full eight-bit PRG latch agrees with
[Mesen's published reference at b9fa69d](https://github.com/SourMesen/Mesen2/blob/b9fa69ddc6d0a331fb103fdb5eef6904305703c2/Core/NES/Mappers/Unlicensed/Mapper241.h).
The implementation is original; external emulator source is a behavioral
reference. Accept power-of-two PRG chips from 32 KiB through 8 MiB; unconnected
upper address outputs alias the smaller population. The 8 MiB ceiling follows
that compatibility model, rather than certifying every PCB's physical latch
width. No FCEUX-style D7-to-bank-8 overdump repair is applied: proper dumps of
the relevant Subor board use mapper 481. Fixed 8 KiB CHR ROM or 8 KiB volatile
CHR RAM is supported, without CHR banking.

Native NES 2.0 submapper zero must declare exactly 8 KiB PRG RAM or NVRAM,
matching its battery flag, and exactly 8 KiB volatile CHR RAM when CHR ROM is
absent. Reject unknown submappers, partial/non-power-of-two PRG, other CHR
sizes, mixed or extra RAM/NVRAM and non-H/V mirroring. Legacy iNES always has
the physical 8 KiB WRAM, ignoring generic RAM-size inference. Native CHR-NVRAM
and incomplete memory descriptions are unsupported.

Soft reset selects bank zero and preserves WRAM/CHR RAM. Cold power also
selects zero, clears volatile WRAM and CHR RAM, and preserves battery WRAM,
including saves loaded before power-on. These are explicit deterministic
compatibility policies; exact electrical reset state and individual PCB pin
populations remain unverified. Battery APIs expose only the 8 KiB persistent
WRAM; short save imports update the supplied prefix and oversized direct
imports stop at chip capacity, following ordinary RAM policy. File imports
retain the existing 8 KiB cap and atomic file writes.

The initial implementation covered the base board only. The optional LPC
speech device is now available through the explicit `lpc` profile described in
the following section. The default/`standard` profile still has no device at
$4020-$5FFF: reads float and writes are ignored. No keyboard/printer repair for
hacked educational images or undocumented graphics submapper is supplied.

A dedicated class owns only its actual 8 KiB WRAM and one 8 KiB CHR ROM/RAM
buffer, with no unused large generic mapper RAM arrays. Snapshots contain a
2-byte immutable-configuration tag, the complete raw bank latch, WRAM and CHR
RAM only when present: 8,195 bytes with CHR ROM or 16,387 with CHR RAM. Tags bind
PRG geometry, mirroring, battery and CHR population; fixed-size staging rejects
malformed/truncated component states atomically. Whole-machine rollback,
other mapper payloads, state version 20 and the 4 MiB cap are unchanged.

The dedicated `mapper241` target covers nine PRG sizes, both header formats,
H/V, volatile/persistent WRAM and CHR ROM/RAM; every bank value and CPU write
address; WRAM/NINA separation, fixed CHR protection, open-bus speech addresses,
OAM DMA, reset, geometry rejection and malformed snapshots. Original 6502
firmware replicated into each PRG bank checks completion, PPUDATA/CIRAM,
soft/cold RAM policy and deterministic replay, including an 8 MiB image.
`rom-source-mapper241` checks raw, ZIP and 7z loading, session close/reload,
prepare/adopt, battery persistence and failed-load rollback. CLI probes cover
24 valid profiles and 16 rejected inputs, asserting actual battery bytes,
no volatile saves and unchanged ROM files. No commercial ROM data is used.

Validation on 2026-11-14 passed all 87 regular Meson targets with GPU checks
required and eighteen focused ASan+UBSan targets (leak detection disabled on
macOS). Both builds passed 424,369 assertions in seven dedicated mapper cases
and 672 loading/persistence assertions; the shared mapper target passed
7,123,554 assertions in 534 cases. Its sanitizer run took 117.17 seconds within
the unchanged 120-second budget. The first regular run exposed an incorrect
new DMA assertion; accounting for the PPU's absent OAM attribute bits fixed the
test, and the complete rerun passed. Formatting, scoped clang-tidy, whitespace,
and exact 155-ID registry/ledger checks passed. Isolated strict-consumer
standards validation preserved zero errors, 72 warnings and three infos across
193 documents. Existing user changes were preserved.

## Mapper-241 LPC follow-up: optional speech profile

Select `--board-profile lpc` for the optional nibble-stream speech device;
omitting the option or selecting `standard` retains the original base board.
Both profiles use the same documented ROM/WRAM/CHR geometry. No submapper or
automatic ROM-title detection is invented. The profile also flows through
`EmulationSession::open_rom` and `RomLoadRequest`/`prepare_rom` before cartridge
installation, including raw, ZIP and 7z images. Snapshots require the same profile.

The register interface comes from
[NESdev mapper 241 r23728](https://www.nesdev.org/w/index.php?oldid=23728).
The speech profile maps $5000-$5FFF; D6 reports readiness and other data-bus
bits float. Writes with D6 and D4 both high submit D0-D3; both low reset the
chip. Mixed control levels are undefined in the specification and ignored
here; D7/D5 do not participate in decoding. D6-only drive and mixed-level
handling are compatibility policies; actual clone pin masks are unmeasured.
Other expansion addresses stay
unmapped, and speech accesses cannot modify PRG/CHR banking or WRAM.
Open-bus D6 can already appear high on an unpopulated board, so a ready poll
alone cannot reliably detect the presence of speech hardware.

Unspecified board-level behavior uses the published
[NintendulatorNRS-DBG reference at b685625](https://github.com/gmestanley/NintendulatorNRS-DBG/blob/b685625310ec026afa844340d94503f9767b72df/src-mappers/src/iNES/mapper241.cpp)
and its [LPC reference](https://github.com/gmestanley/NintendulatorNRS-DBG/blob/b685625310ec026afa844340d94503f9767b72df/src-mappers/src/Hardware/Sound/s_LPC10.cpp):
low-bit-first nibbles, a bounded 128-bit FIFO, readiness below 64 queued bits,
768 kHz oscillator and 16,000 oscillator clocks of reset latency. The decoder
accepts complete nibbles until capacity, then drops overflowing nibbles without
changing queued data. Partial fields wait for more bits. Between incomplete
frames output is muted while filter/parameter history is retained. Stop energy
15 clears the speech output/filter and discards queued padding to the next
byte boundary; following queued messages can resume. Reset clears FIFO,
parameters, noise, filter, output and clock phase, and restarts the busy delay.
Both soft reset and cold power reset the speech device; existing WRAM/CHR
reset and persistence rules remain intact.

`LpcSpeech` is original implementation code with fixed, bounded storage. It
parses 4-bit energy, repeat, 6-bit pitch and ten reflection coefficients with
widths 5/5/4/4/4/4/4/3/3/3. Unvoiced frames decode K1-K4; repeat frames reuse
prior coefficients, preserving raw K5-K10 for subsequent voiced frames
while suppressing their unvoiced contribution. Energy zero denotes silence.
There is no TMS5220 parallel-command/SPEAK EXTERNAL byte protocol on this
custom interface, and no LPC-D6 or multi-rate format has been added.

Parameter and excitation tables record factual TMS5220 ROM contents published
in [MAME's decapping research at 484b993](https://github.com/mamedev/mame/blob/484b993578e6b7155e5a039c4e4962a5d1ad78e8/src/devices/sound/tms5110r.hxx),
with the [TI TMS5220 manual](https://cdn.hackaday.io/files/21000898464000/tms5220.pdf)
and patents linked by NESdev as supporting references. No external emulator
implementation code or commercial speech/ROM recordings are incorporated.
Voiced excitation uses the PE chirp table; unvoiced excitation uses signed
noise from a deterministic 13-bit LFSR advanced twenty steps per sample.
A ten-stage fixed-point lattice uses Q9 reflection coefficients, bounded
15-bit arithmetic and signed eight-bit output quantization. Unlike the
reference's approximate D6 excitation for PE, the PE chirp is used here.

One sample advances every eighty speech clocks: nominally 9.6 kHz. A fractional
integer clock accumulator derives speech clocks from the selected NTSC, PAL
or Dendy CPU clock, rounded to the nearest Hz. `Mapper::set_region` is a no-op
by default; the cartridge/emulator forward region changes to the speech mapper,
including overrides and state loading. Region changes preserve phase modulo
the new CPU-clock denominator. Mixing uses the existing per-CPU-cycle expansion
audio seam; `expansion_gains::kLpc` supplies a nominal normalized gain of 0.15.
No analog board resistance or reconstruction filter has been measured.

This is a **functional frame/sample compatibility model**, not a cycle-exact
TMS5220 or a measured clone-netlist implementation. Each frame is 200 samples;
eight 25-sample interpolation updates occur simultaneously per period.
Voicing/zero-energy transitions apply the new parameters immediately when
interpolation is inhibited. The TI chip staggers parameter updates through
its arithmetic cycles, and exact clone startup, underflow, stop/ramp and analog
waveform behavior remain unverified. The published reference's separate
experimental 1bpp/CHR-split submapper is outside this documented speech profile.
These limits require hardware evidence before claiming exact recording matches.

Speech adds a fixed 129-byte payload behind a distinct mapper profile tag.
It includes all FIFO bits, read cursor/count, fractional clock phase, reset and
sample counters, partial-frame parser, three parameter frames, interpolation,
pitch/noise state, filter delays and output. Fixed local staging rejects
truncation, malformed fields and impossible bounds before committing any mapper
state. Standard mapper-241 payloads, other mappers, machine version 20 and the
4 MiB cap remain unchanged. A speech device is allocated only for `lpc`.

Original synthetic streams check analytic chirp samples, voiced/unvoiced repeat
equivalence, silence/stop, FIFO threshold/overflow, partial-frame refill, every
coefficient index, interpolation transitions and deterministic replay. CPU
firmware polls readiness before writing each nibble and produces a WRAM
completion marker. Tests verify changing finite audio at the APU mixer across
regions, profile separation, state rollback and raw/archive preparation/adoption
with real battery persistence. CLI checks 35 valid profiles and 15 rejected
inputs, including speech firmware completion through persisted WRAM and region
overrides. Only synthetic parameters and original 6502 code are used.

Validation on 2026-11-14 passed all 87 regular targets with GPU checks required
and nineteen focused ASan+UBSan targets, including mixer/audio, region, session,
loading and state paths (leak detection disabled on macOS). Both builds passed
627,371 mapper-241 assertions in fifteen cases and 808 loading assertions in
two cases. The shared mapper target retained 7,123,554 assertions in 534 cases;
its sanitizer run took 114.72 seconds within the unchanged 120-second budget.
The complete build, including SDL3 and NodalKit applications, formatting,
scoped clang-tidy and whitespace checks passed. The exact registry/ledger set
remains 155 IDs. Isolated strict-consumer standards validation retained zero
errors, 72 warnings and three infos across 193 documents. Initial integration
test failures corrected assumptions about open-bus ready reads and the caller's
save flush before session adoption; the full subsequent run passed.

## Mapper-242 follow-up: 43272, Waixing RPG and ET-113

`Multicart242` implements the three populations described by
[NESdev mapper 242 r19709](https://www.nesdev.org/w/index.php?oldid=19709):

| Population | PRG ROM | CPU RAM | Reset mapping |
|---|---|---|---|
| 43272 multicart | 512 KiB | None | Bank 0 in both 16 KiB windows |
| Waixing RPG | 512 KiB | 8 KiB battery WRAM | Bank 0 in both windows |
| ET-113 multicart | 512 KiB followed by a 128 KiB replacement chip | None | Replacement-chip bank 0 in both windows |

CPU writes at $8000-$FFFF latch A0-A10; data and A11-A14 do not affect the
register. S selects mirrored 16 KiB versus consecutive 32 KiB mapping; multicart
O=0 overrides the upper window with inner bank 0 or 7 selected by L. Outer
bank pins select the four 128 KiB blocks. ET-113 C=0 selects only the trailing
128 KiB chip, ignoring outer pins, and C=1 the leading 512 KiB chip. Its two
chips are addressed separately, avoiding modulo across a non-power-of-two
aggregate. M controls H/V CIRAM; O protects multicart CHR RAM in NROM modes.
The RPG bypasses the UNROM override and CHR protection, with ordinary WRAM
at $6000-$7FFF. All boards have unbanked 8 KiB volatile CHR RAM.

When m is set, five solder pads replace PRG A4-A0, on both ROM chips and in
both CPU windows. Select them with `--board-profile menu-0` through `menu-31`
or `configure_board_jumpers(0..31)`. The default is zero, an explicit menu
selection policy rather than identification of a specific cartridge assembly.
Physical pads survive soft reset and cold power. The address latch resets to
zero; soft reset preserves CHR RAM and WRAM, while cold power clears CHR RAM
and preserves battery WRAM, including preloaded saves. Cold clearing is a
stable emulator policy, not an assertion about analog RAM power-up contents.

The primary description says the RPG lacks UNROM-like modes. Accordingly,
O does not override its NROM selection even when clear; exact inactive-bit
wiring on every RPG PCB remains unverified. Published emulator references
are not uniform: [NintendulatorNRS-DBG at b685625](https://github.com/gmestanley/NintendulatorNRS-DBG/blob/b685625310ec026afa844340d94503f9767b72df/src-mappers/src/iNES/multicart%20address%20latch/mapper242.cpp)
uses C-or-L for replacement-chip selection, ORs menu-pad bits into the read
address, and gates CHR protection by nonzero submapper. This implementation
follows the primary article's independent C, low-pin replacement, and
multicart/RPG distinction. No external emulator implementation code is copied.
Hardware traces would be needed to resolve those disagreements for alternate
assemblies; passing synthetic tests does not certify all game dumps.

Native NES 2.0 requires submapper zero, no CHR ROM, exactly 8 KiB CHR RAM,
no CHR NVRAM and ordinary H/V metadata. Only the RPG battery flag permits
8 KiB PRG NVRAM, with no volatile or mixed CPU RAM. ET-113 plus battery is
rejected as an undocumented combination. Reject smaller/larger/partial PRG
chips, alternate CHR sizes, unknown submappers and four/single-screen metadata.
Legacy generic WRAM inference is ignored for multicarts. Other sizes and the
incompatible mapper-380/449 variants are outside this registration; no title
patches or guessed submappers are supplied. Expansion registers float; there
are no mapper IRQs or expansion audio.

The class owns only its PRG image, 8 KiB CHR RAM, and an 8 KiB WRAM allocation
on the RPG board. State is 8,196 bytes for multicarts or 16,388 for the RPG:
a two-byte population/pad tag, the eleven-bit latch, CHR RAM and optional WRAM.
Fixed local staging validates the tag, latch and complete payload before any
component mutation. Profile/population mismatch and truncation leave the mapper
unchanged; whole-machine rejection remains atomic. Existing mapper payloads,
machine version 20 and the 4 MiB allocation cap are unchanged.

The dedicated `mapper242` target checks every CPU write address, all data
values, bank modes, ignored pins, thirty-two pad configurations, ET-113 chip
boundaries and reset, CHR protection, every RAM cell, floating reads, geometry
rejection and malformed snapshots. An independent pin-level ROM-address
oracle drives the bank assertions. Eighteen original 6502 images copy their
program into console RAM before switching banks or replacing address pins;
they check PRG, PPUDATA/CIRAM, CHR protection, soft/cold reset and replay.
`rom-source-mapper242` checks raw, ZIP and 7z loading, session close/reload,
prepare/adopt, battery persistence, menu profiles and failed-load rollback.
Its loading matrix is isolated from the existing shared timeout. CLI checks
44 valid profiles and 27 rejected inputs, including actual RPG save bytes and
unchanged ROM files. Fixtures use only synthetic data and original programs.

Validation on 2026-11-14 passed all 91 regular Meson targets with GPU checks
required and twenty-two focused ASan+UBSan targets (leak detection disabled
on macOS). Both builds passed 3,285,268 dedicated mapper-242 assertions in
eight cases, plus 620 loading/persistence assertions. The complete build,
including SDL3 and NodalKit, formatting, scoped clang-tidy, whitespace and
exact 156-ID registry/ledger checks passed. Isolated strict-consumer standards
validation preserved zero errors, 72 warnings and three infos across 193 docs.

The first sanitizer run reached the shared mapper target's unchanged
120-second timeout after twenty other targets passed. Measured separately,
J.Y. Company's eighteen cases took 9.14 seconds. Its source now belongs to a
separate `jy-company` executable, with no test-logic changes. An exact comparison
of case names and tags proved that all 534 cases remain: 516 shared plus 18
J.Y. Company. Their combined 7,123,569 assertions are unchanged by the split.
The complete rerun passed: shared mappers took 111.26 seconds and J.Y. Company
11.04 seconds, each within the original budget. Include `jy-company` alongside
`mappers` in focused checks; full Meson suite runs already include both.

## Mapper-244 follow-up: C&E Decathlon

`Decathlon` implements the ROM-only bank-switching board identified by the
[NESdev r5862 stub](https://www.nesdev.org/w/index.php?oldid=5862). Register
behavior comes from the original author's [Everynes specification](https://problemkaputt.de/everynes.htm#mapper244ceprg32kvrom8k),
with published emulator behavior used where that description is incomplete.
The core is original Boolean bit-routing code; no external implementation
code or commercial ROM contents are incorporated.

All CPU writes at $8000-$FFFF share two independent latches. Before decoding,
the written byte is ANDed with the byte driven by the **current** PRG bank.
The resulting D3 selects PRG or CHR, so a conflict can change both the intended
destination and its bank bits. PRG selection uses D1-D0, complemented by D4
and exchanged by D5; D2/D6/D7 have no PRG-selection effect. CHR uses D2-D0,
with ordered exchanges of pins 0/1, 1/2 and 2/0 controlled by D4/D5/D6.
Mode seven complements the three bits instead of exchanging them.

Everynes leaves CHR mode six unspecified. Both
[Mesen at b9fa69d](https://github.com/SourMesen/Mesen2/blob/b9fa69ddc6d0a331fb103fdb5eef6904305703c2/Core/NES/Mappers/Unlicensed/Mapper244.h)
and [NintendulatorNRS-DBG at b685625](https://github.com/gmestanley/NintendulatorNRS-DBG/blob/b685625310ec026afa844340d94503f9767b72df/src-mappers/src/iNES/mapper244.cpp)
map it to the same complement as mode seven; that is the explicit compatibility
policy here. Those references do not apply the bus conflict in their register
handlers. This implementation follows Everynes's conflict claim, using the
ordinary wired-AND model. Actual PCB drive strength, mode-six electrical
behavior and exact reset circuitry remain unmeasured. `no-conflicts` is not
an alternate profile silently selected by title or hash.

Support the 128 KiB PRG / 64 KiB CHR-ROM population recorded in
[MAME software metadata at 484b993](https://github.com/mamedev/mame/blob/484b993578e6b7155e5a039c4e4962a5d1ad78e8/hash/nes.xml).
Its Decathlon entry marks both chips as bad dumps and reports graphics
problems; that metadata is a geometry reference, not game-library or physical
board certification. No dump was downloaded or patched. Native NES 2.0 requires
submapper zero, no CPU/CHR RAM or NVRAM, no battery and ordinary H/V mirroring.
Legacy generic WRAM inference is ignored. Smaller, larger, partial, RAM-based
or otherwise ambiguous populations are rejected explicitly. Mislabelled MMC3
translations carrying mapper 244 are not automatically repaired.

The selected PRG bank maps the full 32 KiB CPU window, and CHR the full 8 KiB
pattern window. CHR is always read-only. H/V mirroring is fixed from the header;
metadata describes vertical mirroring on the recorded title, while horizontal
metadata is supported as a fixed wiring model. There is no WRAM, expansion
register, IRQ or expansion audio. Low cartridge addresses float on the CPU bus.
The class owns only its immutable PRG and CHR images and two bank bytes, with
no unused generic mapper RAM buffers.

Cold power selects PRG/CHR bank zero. Soft reset retains both latches, agreeing
with the published reference policy; exact electrical reset behavior is not
claimed. State is four bytes: a two-byte fixed-mirroring tag and two bank bytes.
PRG must be 0..3 and CHR 0..7. Fixed local staging rejects wrong tags, invalid
banks and truncation before mutation, and whole-machine rejection stays atomic.
Other mapper payloads, machine state version 20 and the 4 MiB cap are unchanged.

`mapper244` checks every command byte against independent truth tables, every
CPU write address, all four initial PRG banks across all 256-by-256 ROM/data
conflicts, every PRG/CHR cell, ROM protection, open bus, reset, unsupported
metadata and malformed states. Four original 6502 images span both headers
and H/V. Identical code/vectors in each PRG bank allow live switching; they
exercise every permutation, conflict-driven destination changes, PPUDATA/CIRAM,
and a real INC instruction whose dummy write changes the bank before its final
write. They verify a fresh completion marker, retained soft-reset banks,
cold defaults and deterministic replay.

`rom-source-mapper244` covers raw, ZIP and 7z loading, preparation/adoption,
reload, reset/power, state replay and failed-load rollback without save-file
creation. The existing loader timeout is preserved by keeping the matrix in
its own target. CLI loading checks twenty valid profiles and twenty-six
rejected inputs, unchanged source files and absent battery saves; CPU execution
completion is asserted in C++ tests. All fixtures contain original programs
and synthetic bank data.

Validation on 2026-11-14 passed all 96 regular Meson targets with GPU checks
required and twenty-six focused ASan+UBSan targets with four workers (leak
detection disabled on macOS). Both builds passed 1,246,933 mapper-244 assertions
in ten cases and 220 loading assertions. The complete build, including SDL3 and
NodalKit, formatting, scoped clang-tidy, whitespace and exact 157-ID registry/
ledger checks passed. Isolated strict-consumer standards validation retained
zero errors, 72 warnings and three infos across 193 documents.

Earlier sanitizer attempts timed out in the shared mapper/loading targets at
120/30 seconds; reducing parallelism alone was insufficient. The older
multicart matrices now run as `mapper-multicarts` and `rom-source-multicarts`.
Exact case-name/tag comparisons preserve all cases: 516 mapper cases become
465 + 51, and 43 shared loading cases become 38 + 5. Their assertion totals
remain 7,120,377 and 21,101 respectively. Isolation exposed older mapper-233/234
fixtures relying on another test to register built-in mappers; three explicit
setup calls fix that dependency without changing assertions or emulator code.
The final complete sanitizer run passed: shared mapper 81.62 seconds, multicart
mapper 26.88, shared loading 9.12 and multicart loading 13.93. All original
timeout limits remain unchanged. Include both new matrix targets, `jy-company`
and the existing per-board targets in focused checks; full suite runs already
discover them automatically.

## Mapper-245 follow-up: Waixing F003

The existing `Mmc3` implements F003 using the pin connections in
[NESdev revision 5871](https://www.nesdev.org/w/index.php?oldid=5871).
CHR-selector A12 on the clone is grounded, while PPU A10/A11 remain active.
CHR output A11 drives PRG A19. With normal CHR order, R0/R1 bit 1 selects
one of two 512 KiB PRG halves according to PPU A11. With inverted CHR order,
R2–R5 bit 1 selects the half according to PPU A11/A10. This applies to every
CPU slot, including the fixed last two banks. R6/R7 retain six-bit inner
bank selection; changing CHR registers or inversion immediately affects the
PRG output at the currently driven PPU address. Pattern memory uses raw
PPU addresses and remains flat 8 KiB CHR RAM in either inversion mode.

The physical-address observer already used by Sachen 513 carries PPUDATA,
PPUADDR, rendering and idle address pins. Cached renderer/debugger CHR reads
cannot steer the outer bank. Nametable addresses still drive A10/A11, and
PPU A12 does not change this board's CHR selector. This is a pin-routing
interpretation of the documented wiring; no real PCB trace was measured.
Games are expected to program the active CHR registers consistently, as the
specification explains. Original test firmware instead replicates its code,
IRQ handler and vectors in every PRG page to exercise inconsistent registers
safely without commercial ROM contents.

Published emulator behavior was compared as authorized by the user:
[Mesen b9fa69d](https://github.com/SourMesen/Mesen2/blob/b9fa69ddc6d0a331fb103fdb5eef6904305703c2/Core/NES/Mappers/Mmc3Variants/MMC3_245.h)
uses only R0 for the extension, while
[NintendulatorNRS-DBG b685625](https://github.com/gmestanley/NintendulatorNRS-DBG/blob/b685625310ec026afa844340d94503f9767b72df/src-mappers/src/iNES/MMC3-based/mapper245.cpp)
tracks the selected CHR read slot. The implementation here is original and
uses the existing full physical-address seam rather than copying either
shortcut. No external implementation code or ROM dump is incorporated.

There is unresolved evidence about IRQ wiring: the article grounds ASIC A12
but also lists scanline IRQs. MapperBus retains its existing MMC3 scanline
counter clocked at dot 260 on rendered visible/pre-render lines, including
its repeating-zero-latch policy. This is a compatibility model; actual clone
A12 filtering, zero-latch behavior, startup RAM protection and exact reset
circuitry remain unverified. The grounded-A12 rule is applied to CHR selection
without inventing IRQ pin behavior or changing PPU timing. No expansion audio
or alternate board profile is claimed.

Accept power-of-two PRG sizes from 32 KiB through 1 MiB, no CHR ROM, and
ordinary H/V CIRAM. Smaller PRG images alias unconnected high address pins;
this compatibility envelope is not certification of every chip population.
The documented battery flag is required. Native NES 2.0 requires submapper
zero, exactly 8 KiB PRG NVRAM and 8 KiB volatile CHR RAM, no extra PRG RAM or
CHR NVRAM. Legacy generic RAM inference does not add memory. Standard MMC3
$A000 controls H/V mirroring and $A001 gates/protects the 8 KiB WRAM window.

Soft reset restores MMC3 registers, selector, IRQ and header mirroring while
retaining both RAMs. Cold power additionally clears volatile CHR RAM and
retains battery WRAM, including a preloaded save. These are explicit emulator
reset policies. The existing two 8 KiB arrays are reused: no ROM duplication,
large optional buffers or new backing RAM allocation is added. The state
payload is 16,402 bytes: both RAMs, common MMC3 registers, a tag binding PRG
geometry and reset H/V wiring, and the live two-bit PPU selector. Truncation,
invalid fields and tag mismatch reject atomically using a bounded payload
backup. Existing mapper payloads, machine version 20 and the 4 MiB ceiling
remain unchanged.

`mapper245` covers all six PRG sizes, every CHR-register byte, both inversions
and PRG modes, all four live selectors, fixed-bank extension, ignored upper
PPU pins, six-bit R6/R7 masking, cached-read isolation, real PPU sprite fetches
and PPUDATA, every CHR/WRAM cell, protection, reset, partial IRQs, malformed
states and geometry/mirroring mismatch. Twelve original CPU firmware images
cover both headers, H/V and representative small/large PRG sizes, IRQ delivery,
PPUDATA, battery retention and deterministic machine replay. Generic mapper
snapshot/boot matrices also include 245.

`rom-source-mapper245` checks raw/ZIP/7z loading, prepare/adopt without extra
reset, save-file contents and reload, soft/cold startup, failed-load rollback
and unchanged ROM bytes. The CLI separately proves completion through the
battery signature in 24 execution profiles and rejects 22 unsupported inputs.
Dedicated targets preserve the existing shared mapper/loading timeout limits.
All fixtures are original synthetic programs and bank data.

Validation on 2026-11-14 passed all 99 regular Meson targets with GPU checks
required, and 28 focused ASan+UBSan targets with four workers (leak detection
disabled on macOS). Both builds passed 427,091 mapper-245 assertions in nine
cases and 224 loading assertions. Shared mapper/multicart targets retain all
465/51 cases; shared loading/multicart targets retain all 38/5 cases. The full
SDL3/NodalKit build, formatting, scoped clang-tidy, whitespace and exact
158-ID registry/ledger comparison passed. Isolated strict-consumer standards
validation retained zero errors, 72 warnings and three infos across 193
documents. Actual PCB IRQ/reset circuitry and commercial title compatibility
remain unverified.

## Mapper-246 follow-up: G0151-1 / Feng Shen Bang

`G0151` implements the discrete G0151-1 board using
[NESdev revision 22048](https://www.nesdev.org/w/index.php?oldid=22048)
and the original [board/GAL tracing report](https://forums.nesdev.org/viewtopic.php?start=60&t=13969).
Four six-bit registers select independent 8 KiB PRG windows; four full-byte
registers select independent 2 KiB CHR-ROM windows. Writes decode only
$6000–$601F. A2 selects PRG/CHR, A1/A0 select the window, and A4/A3 alias the
registers. $6020–$67FF and $7000–$7FFF do not select registers or RAM. Register
reads float on the CPU bus. CPU ROM writes and all CHR writes are ignored;
there is no register bus conflict.

At $FFE4–$FFE7, $FFEC–$FFEF, $FFF4–$FFF7 and $FFFC–$FFFF, reads force
PRG A17 high while preserving the selected A18. The bank register is unchanged,
so the next ordinary read returns to its selected page. This applies to actual
CPU reset/interrupt vectors as well as ordinary reads. It does not redirect
NMI vectors at $FFFA/$FFFB. Fixed H/V mirroring comes from the header. The
board has no IRQ, expansion registers, PPU pin observer or expansion audio.

The supported ROM population is 512 KiB PRG and 512 KiB CHR, as recorded in
[MAME metadata at 484b993](https://github.com/mamedev/mame/blob/484b993578e6b7155e5a039c4e4962a5d1ad78e8/hash/nes.xml).
That older entry lists 8 KiB battery RAM and marks its dumps bad. The hardware
specification and newer published implementation identify a 2 KiB window at
$6800–$6FFF, which is the memory exposed and persisted here. No dump was
downloaded, patched or certified. Other ROM populations, CHR RAM, extra WRAM,
non-H/V mirroring and native nonzero submappers are rejected. The battery flag
is required; native NES 2.0 declares exactly 2 KiB PRG NVRAM and no volatile
PRG RAM or CHR RAM/NVRAM. Legacy generic 8 KiB RAM inference is ignored.

Published behavior was compared at pinned revisions:
[Mesen b9fa69d](https://github.com/SourMesen/Mesen2/blob/b9fa69ddc6d0a331fb103fdb5eef6904305703c2/Core/NES/Mappers/Unlicensed/Mapper246.h)
decodes the broader $6000–$67FF range and omits the vector override in its
mapper handler;
[NintendulatorNRS-DBG b685625](https://github.com/gmestanley/NintendulatorNRS-DBG/blob/b685625310ec026afa844340d94503f9767b72df/src-mappers/src/iNES/mapper246.cpp)
implements the narrow decode, vector override, 2 KiB save size and retained
soft-reset latches. This implementation is original, following the measured
register decode and documented pin behavior. Analog A18 floating/charge effects
from the hardware discussion are not simulated; the documented digital rule
preserves A18. Physical timing and commercial title compatibility are unverified.

Soft reset retains registers and battery RAM. Cold power selects PRG page 63
and CHR page 255 in every slot while retaining saved RAM. R3=$FF on power-on
is specified; extending the all-high 74'670 startup to the other cells follows
the published reference as an explicit deterministic policy. It is not a
power-up guarantee for every manufactured chip. Unconnected PRG D7/D6 are
normalized away. The class owns only its immutable ROM vectors, 2 KiB RAM,
eight bank bytes and fixed mirroring; no unused CHR RAM or generic 8 KiB WRAM
buffer is allocated.

State is 2,058 bytes: a two-byte fixed-wiring tag, four PRG banks, four CHR
banks and 2 KiB RAM. Fixed local staging checks the tag, PRG range 0–63 and
complete payload before mutation. Every CHR byte is valid. Truncation,
invalid PRG banks and wrong wiring reject atomically without a ROM copy.
Machine version 20, the 4 MiB ceiling and all older mapper payloads remain
unchanged. Explicit battery imports use the actual 2 KiB cap; the existing
startup best-effort import policy is unchanged.

`mapper246` covers every CPU write address, every bank byte and all register
aliases, independent windows, patterned PRG/CHR cells, every transient-read
address, CHR protection, open bus, all RAM cells, reset policies, every snapshot
truncation length, invalid bank fields and fixed-wiring mismatches. Four original
6502 firmware images span native/legacy headers and H/V mirroring. Replicated
code permits live switching; deliberately different IRQ vectors in A17-low
pages prove BRK fetches the forced bank. The firmware also checks PPUDATA,
CIRAM, RAM retention, cold/soft startup and deterministic machine replay.

`rom-source-mapper246` covers raw/ZIP/7z loading, prepare/adopt without an
extra reset, battery signatures and reload, explicit oversized-import rejection,
failed profile/header loads and unchanged ROM bytes. The CLI independently
checks completion through a 2 KiB battery signature in twelve execution
profiles and rejects twenty-six unsupported inputs. Dedicated mapper/loading
targets keep existing timeout budgets. All ROM contents and programs are
original synthetic fixtures.

Validation completed on 2026-11-15: all 102 regular Meson targets passed with
GPU checks required, alongside 30 focused ASan+UBSan targets with four workers
(leak detection disabled on macOS). Both builds passed 4,931,409 mapper-246
assertions in ten cases and 264 loading assertions. Shared mapper/multicart
matrices retain 465/51 cases and shared loading/multicart matrices 38/5 cases.
The full SDL3/NodalKit build, formatting, scoped clang-tidy, whitespace and
exact 159-ID registry/ledger comparison passed. Isolated strict-consumer
standards validation retained zero errors, 72 warnings and three infos across
193 documents. No commercial ROM or physical PCB was used for validation.

## Mapper-251 classification: obsolete duplicate

[NESdev revision 5898](https://www.nesdev.org/w/index.php?oldid=5898)
describes 251 as a Nestopia assignment duplicating mapper 45 and directs use
of 45. It does not identify a distinct cartridge circuit. The catalog now
classifies it as obsolete; it remains unregistered, following the existing
182/223/248 duplicate policy. Core and native/legacy CLI probes require an
explicit unsupported-ID error. No header rewrite or silent alias is added.
Mapper 45 remains a separate implementation task.

## Mapper-252 follow-up: Waixing VRC4 mixed CHR

The existing `KonamiVrc` implements the digital model specified by
[mapper 252 revision 5906](https://www.nesdev.org/w/index.php?oldid=5906),
[VRC2/VRC4 revision 24169](https://www.nesdev.org/w/index.php?oldid=24169),
and [VRC IRQ revision 22720](https://www.nesdev.org/w/index.php?oldid=22720).
Chip A0/A1 connect to CPU A2/A3: ports are $x000/$x004/$x008/$x00C,
with all other low address pins ignored. Two five-bit registers select 8 KiB
PRG windows; $9008 swaps the $8000/$C000 mapping and gates 8 KiB WRAM.
$9000 selects vertical, horizontal, single-lower or single-upper CIRAM.
Writes to ordinary ROM are mapper commands without bus conflicts. Expansion
addresses float and there is no expansion audio or PPU-pin IRQ observer.

Eight VRC4 CHR registers combine low four/high five bits. Exact raw banks
6 and 7 select two physical 1 KiB volatile RAM pages in any PPU slot. Other
values select read-only CHR ROM, wrapping only after RAM selection. Slots
selecting the same RAM page alias it. This follows the literal documented
VRC4/banks-6-and-7 model: raw $106/$107 wrap to ROM pages 6/7 rather than
selecting RAM. Clone bit-8 wiring and decoder aliases are not hardware verified.
The ninth CHR bit is inherited from the VRC4 specification, not a measured
property of this particular clone.

References differ materially. Published
[Mesen b9fa69d](https://github.com/SourMesen/Mesen2/blob/b9fa69ddc6d0a331fb103fdb5eef6904305703c2/Core/NES/Mappers/Waixing/Waixing252.h)
uses eight-bit CHR registers and marks the default pattern mappings writable,
while its mapper handler omits VRC4 swap/mirroring writes.
[NintendulatorNRS-DBG b685625](https://github.com/gmestanley/NintendulatorNRS-DBG/blob/b685625310ec026afa844340d94503f9767b72df/src-mappers/src/iNES/mapper252.cpp)
uses a shared VRC4 core plus RAM decoder aliases, PPU-write-triggered relocation
and a delayed IRQ path. The [hardware discussion](https://forums.nesdev.org/viewtopic.php?p=240304)
reports mapper-252 games do not use the CHR-RAM relocation method discussed
for other Waixing boards. Those additional clone behaviors are not claimed
here without a mapper-252-specific hardware definition. The implementation
is original; no external source code or commercial ROM bytes are incorporated.

CPU cycles clock IRQs even with rendering off and during vblank. Cycle mode
ticks every M2 cycle; scanline mode divides by 114, 114, 113 cyclically, retaining
the fractional prescaler across acknowledge writes. Overflow at $FF reloads
the latch and asserts IRQ. Control acknowledges, resets the divider and reloads
when enabled; acknowledge copies the repeat-enable bit without resetting the
counter/divider. Mapper 252 follows the IRQ register description's divider
reset on every control write, including disable; existing VRC IDs keep their
prior behavior. Clone propagation delay and exact electrical startup remain
unverified. No region-dependent rescaling or PPU timing changes are added.

Accept the 256 KiB PRG / 128 KiB CHR population recorded for Waixing Sangokushi
in [MAME metadata at 484b993](https://github.com/mamedev/mame/blob/484b993578e6b7155e5a039c4e4962a5d1ad78e8/hash/nes.xml).
That older metadata allocates generic 8 KiB VRAM and marks its dumps bad;
the mapper specification identifies only two writable 1 KiB pages. Native
NES 2.0 requires submapper zero, exactly 2 KiB CHR RAM, no CHR NVRAM, and
8 KiB PRG RAM or NVRAM according to the battery flag. Both battery settings
are supported as persistence policies. Legacy headers get the same physical
memories without relying on absent mixed-CHR declarations. Other ROM/RAM
geometries and non-H/V startup wiring reject explicitly; no alternate profile,
automatic mapper correction or dump patch is introduced.

Soft reset restores the existing VRC4 register/IRQ/header-mirroring defaults
and disables WRAM while retaining both RAMs. Cold power also clears CHR RAM
and non-battery WRAM, retaining preloaded battery data. These are explicit
compatibility reset policies. The existing WRAM vector and pattern-RAM array
are reused; only 2 KiB of that array is mapped and serialized for 252. No extra
backing buffer or ROM copy is added. New 10,269-byte snapshots bind battery
population and startup mirroring, include both RAMs and partial IRQ state,
and validate fixed local arrays before applying anything. PRG/CHR bounds,
mirroring, control flags, prescaler and pending/enabled consistency are checked.
Older VRC payloads, machine version 20 and the 4 MiB ceiling stay unchanged.

`mapper252` covers exact register aliases, every PRG data byte, patterned PRG
cells in both swap modes, all 512 raw CHR banks and patterned/protected ROM
cells, shared RAM pages, nibble inputs, WRAM gates/open bus, reset, all IRQ
latches, 114/114/113 periods, partial-counter replay, malformed fields,
truncation, wiring/battery mismatch and atomic machine-state rejection.
Eight original CPU images span both headers, battery settings and H/V startup.
They check all four CIRAM arrangements, mixed CHR, live PRG swap, PPUDATA,
CPU IRQ delivery in both modes with rendering disabled, RAM reset and replay.

`rom-source-mapper252` covers raw/ZIP/7z loading, prepare/adopt, battery save
contents/reload, volatile/battery cold starts, explicit oversized-import
rejection, failed profile/header loads and unchanged source ROMs. The CLI
checks sixteen loading profiles (eight battery signatures prove firmware
completion) and twenty-six rejected inputs, including obsolete 251. Non-battery
firmware completion is checked in C++. Dedicated targets retain existing
mapper/loading timeout budgets. All test programs and bank data are original.

Validation on 2026-11-15 passed all 105 regular Meson targets with GPU checks
required and 32 focused ASan+UBSan targets with four workers (leak detection
disabled on macOS). Both builds passed 6,760,319 mapper-252 assertions in ten
cases and 464 loading assertions. Existing shared mapper/multicart cases remain
465/51, and shared loading/multicart cases 38/5. The full SDL3/NodalKit build,
formatting, scoped clang-tidy, whitespace and exact 160-ID registry/ledger
comparison passed. Isolated strict-consumer standards validation retained zero
errors, 72 warnings and three infos across 193 documents. Clone-specific analog,
bit-8/decoder, dynamic-relocation, startup and IRQ-delay behavior remain outside
this verified digital model; no physical board or commercial ROM was tested.

## Mapper-253 follow-up: fixed CHR4/5 and F009S relocation

`KonamiVrc` extends the VRC4e digital model using
[NESdev mapper 253 revision 5910](https://www.nesdev.org/w/index.php?oldid=5910),
[VRC4 revision 24169](https://www.nesdev.org/w/index.php?oldid=24169) and
[VRC IRQ revision 22720](https://www.nesdev.org/w/index.php?oldid=22720).
The default `fixed` profile maps exact raw 1 KiB CHR banks 4 and 5 to two
writable RAM pages; other banks select protected ROM. Raw $104/$105 select
ROM before wrapping in this literal nine-bit VRC4 model. CPU A2/A3 select
register ports, with the same PRG swap, WRAM gate, four CIRAM arrangements,
CPU-cycle/114–114–113 IRQ modes and acknowledge/repeat behavior as mapper 252.
Clone-specific ninth-bit wiring, propagation delays and electrical startup
remain unmeasured.

The [2019 hardware/software investigation](https://forums.nesdev.org/viewtopic.php?p=240304)
also identifies later mapper-253 games that move RAM by selecting a CHR bank,
then issuing a PPU write to that bank. This is exposed explicitly through
`--board-profile f009s`; no title/hash heuristic selects it. The fixed profile
ignores such writes to ROM. The f009s compatibility model supports these
raw-bank commands in any of the eight PPU slots:

| Selected CHR bank on PPU write | RAM banks selected afterward | Physical RAM |
|---|---|---|
| $088 | $04C–$04F and $14C–$14F | Same 2 KiB, mirrored across four banks |
| $0C2 | $07C–$07D and $17C–$17D | Same 2 KiB |
| $0C8 | $004–$005 and $104–$105 | Same 2 KiB |

Only the PPU write strobe changes the decoder. CPU bank-register writes and
CHR reads leave it unchanged; the write byte does not select the mode.
RAM uses the bank's low bit and raw PPU A9–A0, so all configurations preserve
the same data. These command writes themselves address ROM and leave ROM
contents unchanged. RAM decoder masks ignore CHR bit 8, while command matching
requires the exact raw values above, following the published NRS handler.
Writes to $188/$1C2/$1C8 and other unverified commands do not switch the mode.

Published references disagree. The pinned
[Mesen b9fa69d implementation](https://github.com/SourMesen/Mesen2/blob/b9fa69ddc6d0a331fb103fdb5eef6904305703c2/Core/NES/Mappers/Waixing/Mapper253.h)
uses CPU-side CHR-register writes to toggle a ROM-only latch, a 114-cycle IRQ
divider and narrower CPU register handlers.
[NintendulatorNRS-DBG b685625](https://github.com/gmestanley/NintendulatorNRS-DBG/blob/b685625310ec026afa844340d94503f9767b72df/src-mappers/src/iNES/mapper252.cpp)
shares VRC4 banking, intercepts PPU writes and records the three relocation
commands. The original code here follows the documented VRC4 controls and
PPU-write evidence. $88/$C8 have mapper-253 game evidence; $C2 follows the
shared published reference and related-board investigation. No external
implementation code or commercial ROM bytes are incorporated.

Cold f009s startup selects $028–$02B/$128–$12B as RAM, using the $80 startup
setting reported for the related GAL board and noted in NRS's later-board
comment. Four logical pages mirror the same 2 KiB. This is an explicit startup
inference, not a measured guarantee for every F009S board. There is no $80
write command in this profile. The larger GAL command table on
[mapper 544 / FS306](https://www.nesdev.org/wiki/NES_2.0_Mapper_544) is not
silently generalized to 253: its A10/A11 ports, third PRG bank and independent
nametable selectors remain a separate mapper. Unobserved decoder commands,
bit-8 command aliases and clone IRQ delays remain unsupported/unverified.

Accept power-of-two PRG sizes 32–256 KiB and CHR-ROM sizes 8–512 KiB. This
is the VRC4 address-width compatibility envelope; smaller images alias
unconnected high ROM pins. It is not certification of every physical chip
population or every dump bearing mapper 253. No ROM-size correction or
extended PRG banking is inferred from unrelated Waixing title metadata.
Native NES 2.0 requires submapper zero, 8 KiB PRG RAM/NVRAM according to the
battery flag, exactly 2 KiB volatile CHR RAM, no CHR NVRAM and H/V startup
mirroring. Legacy missing mixed-RAM declarations are inferred. Both profiles
use the same memory sizes; no new submapper assignment is invented.

Soft reset restores the VRC4 registers/IRQ/header mirroring and retains WRAM,
CHR RAM and the f009s decoder setting. Cold power additionally clears volatile
RAM and restores the selected profile's decoder default, retaining battery
WRAM. These are deterministic compatibility policies. Existing memory buffers
are reused without a ROM copy or extra RAM buffer. A 10,270-byte payload adds
one decoder byte to the compact Waixing state. Its tag binds PRG/CHR geometry,
battery population, H/V startup and fixed/f009s selection. Fixed local staging
rejects truncation, bad fields, unknown decoder modes and incompatible tags
before mutation. Mapper 252's 10,269-byte payload, all older VRC states,
machine version 20 and the 4 MiB ceiling stay unchanged.

`mapper253` checks every VRC4e register alias, PRG data bytes and patterned
cells, all 512 raw CHR banks, fixed RAM/ROM protection, every command bank in
all eight slots, low-byte RAM decoder aliases, shared RAM across relocation,
all 28 supported geometry combinations, WRAM gating, reset, CPU IRQ modes,
partial-counter replay, profile/geometry mismatch and malformed-state rollback.
Sixteen original CPU firmware images cover both headers, battery settings,
H/V startup and both profiles. Real PPUDATA writes switch RAM windows; the
program verifies retained data and mirrors, CIRAM modes, IRQ delivery with
rendering disabled, cold/soft reset and deterministic machine replay.

`rom-source-mapper253` covers raw/ZIP/7z loading, prepare/adopt, explicit board
profiles, save-file contents/reload, volatile/battery reset, rejected imports
and failed-load rollback without changing ROM bytes. The CLI checks 32 loading
profiles and 26 rejected inputs; 16 battery signatures prove completion, while
C++ verifies all non-battery firmware. Dedicated targets preserve the existing
timeout limits. All fixture code and bank data are original synthetic content.

Validation on 2026-11-15 passed all 108 regular Meson targets with GPU checks
required and 34 focused ASan+UBSan targets with four workers (leak detection
disabled on macOS). Both builds passed 6,785,172 mapper-253 assertions in
fourteen cases and 928 loading assertions. Mapper 252 retained all 6,760,319
assertions in ten cases. Shared mapper/multicart cases remain 465/51 and
shared loading/multicart cases 38/5. The full SDL3/NodalKit build, formatting,
scoped clang-tidy, whitespace and exact 161-ID registry/ledger comparison
passed. Isolated strict-consumer standards validation retained zero errors,
72 warnings and three infos across 193 documents. Physical startup, undocumented
GAL commands, clone wiring/delays and commercial titles remain unverified.

## Mapper-254 follow-up: Pikachu Y2K WRAM protection

The original implementation extends `Mmc3` using
[NESdev mapper 254 revision 24273](https://www.nesdev.org/w/index.php?oldid=24273).
While protection is armed, enabled $6000–$7FFF reads return raw WRAM XOR the
full byte last written to $A001–$BFFF odd. Any $8000–$9FFE even write unlocks
raw reads until console reset. These overlays run in parallel with ordinary
MMC3 registers: bank select/mode still change, and $A001 still controls WRAM
enable and write protection. WRAM writes always store raw bytes subject to
those gates. Disabled RAM leaves CPU open bus untouched; the XOR mask never
transforms an undriven bus. Low WRAM writes cannot unlock protection. Later
$A001 writes do not rearm it. PRG uses conventional switchable 8 KiB banks,
CHR uses paired/inverted MMC3 ROM banks, and $A000 controls H/V CIRAM.

This is a published compatibility model, not verified protection circuitry.
The wiki explicitly leaves original hardware and reset detection unknown.
[MesenCE at ffde0fc](https://github.com/nesdev-org/MesenCE/blob/ffde0fc646b070ef6c221b2766fbbee006396486/Core/NES/Mappers/Mmc3Variants/MMC3_254.h)
uses the XOR/unlock model but only traps literal $8000/$A001 and does not
explicitly rearm protection on reset. MapperBus follows the wiki's full
MMC3 register ranges. In contrast,
[NintendulatorNRS-DBG at b685625](https://github.com/gmestanley/NintendulatorNRS-DBG/blob/b685625310ec026afa844340d94503f9767b72df/src-mappers/src/iNES/MMC3-based/mapper254.cpp)
XORs $80, clears protection on odd-valued low WRAM writes, resets it armed,
and uses a different WRAM policy. These incompatible models are recorded as
uncertainty rather than combined into speculative board profiles. Code and
fixtures are original; reference emulator code is not incorporated.

Soft reset and cold power restore MMC3 register/IRQ/header-mirror defaults,
rearm protection and clear the XOR mask to zero. Soft reset retains WRAM;
cold power clears volatile WRAM while preserving imported battery bytes.
These reset defaults are explicit compatibility policy. The existing MMC3
rendered-scanline/dot-260 IRQ approximation, including repeating zero latches,
is retained. Filtered A12 timing, clone IRQ wiring and electrical reset
remain unverified; this change does not alter PPU timing.

The accepted population is 128 KiB PRG-ROM, 128 KiB CHR-ROM and 8 KiB WRAM.
[MAME metadata at 484b993](https://github.com/mamedev/mame/blob/484b993578e6b7155e5a039c4e4962a5d1ad78e8/hash/nes.xml)
records `pikay2k` / BTL-PIKACHUY2K with those sizes, but labels its decrypted
ROMs bad dumps. Metadata is not physical-board certification. Optional battery
persistence is a loader policy, not evidence of another population. Native
NES 2.0 requires submapper zero, exactly 8 KiB PRG RAM or NVRAM according to
the battery flag, no CHR RAM/NVRAM and H/V startup. Legacy headers infer the
physical 8 KiB WRAM even without a battery. Other geometries, four-screen
headers and invented profiles reject. There is no ROM decryption, hash-based
repair, expansion audio or speculative alternate protection mode.

The 8,211-byte mapper state contains raw WRAM, the existing 15-byte MMC3
register/IRQ/RAM state, a battery/startup-mirror tag, a validated unlock byte
and full XOR mask. It excludes unused CHR backing. Existing MMC3 storage is
reused; no new ROM-sized allocations are introduced. A bounded state backup
makes malformed/truncated imports atomic without cloning either ROM. Valid
imports restore derived banks, protection and partial IRQ state. Other mapper
payloads, machine version 20 and the 4 MiB ceiling remain unchanged. Battery
files contain exactly the raw 8 KiB WRAM, never XOR-transformed read values.

`mapper254` covers all raw byte/mask pairs, every CPU write address, alias
forwarding, every PRG data byte, patterned PRG cells, every CHR data byte in
both inversions, ROM write protection, WRAM gates/open bus, reset, IRQ replay,
malformed-state rollback and battery/mirror identity rejection. Eight original
CPU firmware images exercise both header formats, battery options and H/V
startup, PPUDATA, CIRAM, actual IRQ delivery, rearmed reset and deterministic
machine replay. `rom-source-mapper254` covers raw/ZIP/7z, session reset/power,
prepare/adopt, raw save contents/reload, capped imports, failed-load rollback
and immutable input ROMs. `cli-mapper254` runs 16 loading combinations and
25 rejected inputs; eight raw battery signatures prove firmware completion.
C++ asserts completion for volatile boards too. Dedicated targets preserve
existing timeout limits.

Validation on 2026-11-15 passed all 111 regular Meson targets with GPU checks
required and 36 focused ASan+UBSan targets with four workers (leak detection
disabled on macOS). Both builds passed 5,539,194 mapper-254 assertions across
11 cases and 464 loading assertions. Shared mapper/multicart cases remain
465/51; shared loading/multicart cases remain 38/5. Mapper 252/253 regression
targets also passed. The full SDL3/NodalKit build, formatting, scoped clang-tidy,
whitespace and exact 162-ID registry/ledger comparison passed. Isolated
strict-consumer standards validation retained zero errors, 72 warnings and
three infos across 193 documents. Physical protection/reset/IRQ behavior and
commercial-title compatibility remain unverified.

## Mapper-255 classification: apparent duplicate of 225

[NESdev mapper 255 revision 20429](https://www.nesdev.org/w/index.php?oldid=20429)
describes the 110-in-1 address latch and calls the assignment an apparent
duplicate of mapper 225. The corresponding
[mapper 225 revision 18874](https://www.nesdev.org/w/index.php?oldid=18874)
identifies ET-4310 / K-1010 boards and likewise notes the duplicate assignment.
Following the project's existing duplicate-ID policy, 255 remains unregistered;
its catalog row now records the research result explicitly. Core, CLI and app
loading require `Unsupported mapper: 255`. No automatic header rewriting,
blanket alias or extra registered cartridge ID is introduced. The registry
still contains 162 cartridge IDs; this is classification and regression work,
not a new mapper implementation.

Existing `DiscreteMapper` 225 already implements the shared A14 outer bit for
both PRG and CHR. A6–A11 select the inner 16 KiB PRG bank, A12 selects mirrored
16 KiB versus aligned consecutive 32 KiB, A0–A5 select the inner 8 KiB CHR bank,
and A13 selects H/V CIRAM. The 255 report describes four 512 KiB PRG ROMs and
two 512 KiB CHR ROMs; tests now exercise the resulting 2 MiB / 1 MiB address
space. The existing 225 populated-RAM model retains four low-nibble locations
at $5800–$5803, mirrored through $5FFF, preserving undriven CPU data bits.

The [original board investigation](https://forums.nesdev.org/viewtopic.php?p=209732#p209732)
and [follow-up population discussion](https://forums.nesdev.org/viewtopic.php?start=15&t=12647)
report boards with and without the 74'670 RAM chip. The 255 article reports
latch reset pins tied high, hence unpredictable physical startup. These facts
do not establish a universal RAM-population distinction between mapper IDs.
The current 225 model includes nibble RAM and deterministically resets its
bank latch to zero/vertical; this is compatibility policy, not emulation of
unreset random latch contents. The RAM-absent population, physical startup
and commercial-title compatibility remain unverified. This step does not
claim that the populated 225 model faithfully represents every 110-in-1 PCB.

Four new `[mapper225]` core cases check all 32,768 bank-select addresses with
two data bytes, both PRG windows, shared outer CHR pins, mirroring and ROM
write protection. They also check every low cartridge write address, nibble
RAM boundaries/mirrors and all 256 data values. Four original CPU fixtures
cover legacy/native headers and H/V header flags. They relocate to console
RAM to switch both PRG windows, use PPUDATA and CIRAM, check nibble RAM and
verify soft/cold reset plus deterministic machine replay. The full-chip
fixtures contain only original generated bank data and firmware.

A new shared loading case rejects 255 in raw/ZIP/7z and prepared-load paths,
leaves a running 225 machine unchanged and verifies immutable source files.
`cli-mapper255` checks eight canonical 225 boot profiles and sixteen explicit
255 rejections across both header formats, H/V, ROM sizes and battery flags.
No production mapper logic, old state layout, machine version, allocation
ceiling, test timeout or PPU timing changes are needed. Mapper 256 is the next
catalog entry to investigate.

Validation on 2026-11-15 passed all 112 regular Meson targets with GPU checks
required. The two changed suites, `mappers` and `rom-source`, also passed under
ASan+UBSan with leak detection disabled on macOS. Both builds passed 789,430
new core assertions in four cases; shared totals are 5,285,691 assertions /
469 mapper cases and 20,632 assertions / 39 loading cases. The full build,
formatting, scoped clang-tidy, whitespace and exact 162-ID registry/ledger
comparison passed. Isolated strict-consumer standards validation retained
zero errors, 72 warnings and three infos across 193 documents.

## Mapper-256 first step: native PPU register routing

Mapper 256 remains **unregistered**. The registry still contains 162 cartridge
IDs; this step adds a prerequisite for OneBus support, not a working OneBus
mapper or VTxx console. Unsupported images continue to fail explicitly.

[NESdev mapper 256 revision 23934](https://www.nesdev.org/w/index.php?oldid=23934)
requires both native and MMC3-compatible banking, mirroring and IRQ registers.
The [native register reference r23930](https://www.nesdev.org/w/index.php?oldid=23930)
places graphics controls and CHR selectors at CPU $2010–$201F. Previously,
MemoryBus always reduced CPU $2000–$3FFF accesses to the low three bits before
calling the PPU. For example, $2012 reads became PPUSTATUS reads and $2016
writes became PPUADDR writes. A mapper could observe these accesses but could
not prevent their ordinary PPU side effects.

The new default-declining `Mapper::read_ppu_register` and
`write_ppu_register` hooks are forwarded by Cartridge and called by MemoryBus
before PPU-register mirroring. They receive the full CPU address. Reads also
receive the cartridge-masked CPU open-bus value; a device returns a complete
byte, including its chosen floating bits, or `nullopt` to decline. A returned
zero is still a consumed read. Writes return true when consumed. Read and
write claims are independent. A consumed access does not touch the ordinary
PPU I/O latch, status, OAM, address/scroll latch or VRAM. Declined accesses
retain the existing eight-register mirrors. The hook applies only to the CPU
PPU-register window and does not replace pattern/nametable accesses, pin
observations, CPU opcode fetches or the expansion-register decoder.

All existing mapper implementations decline by default. No guessed native
register mirrors or OneBus behavior is installed. CPU access observers keep
their original ordering; timed CPU accesses and OAM DMA use the same routing.
The new interface has no state or backing storage. Mapper payloads, machine
version 20, 4 MiB state ceiling, 16 MiB ROM-input cap and PPU dot timing remain
unchanged. This is an integration change, not a performance improvement claim.

Five `[native-ppu]` cases in `test-bus` use a synthetic device that temporarily
replaces the NROM factory during fixture construction, then restores builtin
factories before assertions. It is not registered as mapper 256 and adds no
fake permanent mapper ID. Tests inspect every CPU address in $2000–$3FFF
through timed and untimed paths, partial bus drivers and cartridge pull-downs,
zero-valued reads, independent read/write claims, exact-address selection,
ordinary NROM/no-cartridge fallback and PPU-state preservation during vblank.
Original CPU firmware verifies native register accesses; OAM DMA verifies all
sixteen selected native reads without touching the CPU instruction decoder.
The pre-change run failed four of these five cases, reproducing the aliasing
problem; the ordinary mirroring case already passed.

### Remaining OneBus integration

The next implementation layer was native/MMC3 PRG and CHR banking plus IRQs,
with a bounded atomic state contract; the component is described in the second
step below. The remaining console capabilities still prevent registration. Sources are
[PRG banking r12704](https://www.nesdev.org/w/index.php?oldid=12704),
[CHR banking r12696](https://www.nesdev.org/w/index.php?oldid=12696),
[MMC3 forwarding r23929](https://www.nesdev.org/w/index.php?oldid=23929) and
[VTxx overview r24587](https://www.nesdev.org/w/index.php?oldid=24587).
A OneBus image shares PRG/CHR storage; converted WXN images may use separate
spaces. Preserve that distinction without copying a shared ROM into another
CHR buffer. The native PRG middle/outer masks, selectable third bank and
COMR6 swapping require their own address calculation (the detailed register
table resolves the overview's ambiguous pair-swap wording; see below). Native $4105 and
MMC3 register selection interact; forwarding can be disabled. Do not assume
that an unchanged Mmc3 instance implements these rules.

IRQ integration must distinguish A12 and HSYNC clock sources. Further work
includes native CIRAM-as-CHR mode, graphics address extension, enhanced
palette/4bpp modes, extended sound/DMA and console capability metadata.
Submappers 0–5 describe register permutations; 11–15 add opcode-only scrambling
and control ports. Those require a distinct instruction-fetch seam rather
than transforming operand/data reads. VT369's extra CPU and embedded 4 KiB
misc ROM are separate machine support. Larger OneBus populations also require
an explicit allocation-cap decision: the documented address space reaches
32 MiB while the current input cap is 16 MiB. These items are not implemented
or inferred from this register-routing prerequisite. Keep 256 unregistered
until the supported hardware profile and its limitations are explicit and
validated; do not advance the registry count or claim game compatibility from
these routing tests.

Validation on 2026-11-15 passed all 112 regular Meson targets with GPU checks
required and nine focused ASan+UBSan targets (`bus`, `ppu`, `mapper-timing`,
`save-state`, `mappers`, `rom-source`, `fds`, `cpu`, `cpu-accuracy`) with four
workers and leak detection disabled on macOS. Both builds passed 141,934
assertions across 34 bus cases, including the five new routing cases. The
full SDL3/NodalKit build, formatting, scoped clang-tidy, whitespace and exact
162-ID registry/ledger comparison passed. Isolated strict-consumer standards
validation retained zero errors, 72 warnings and three infos across 193
documents. No OneBus title or enhanced-console fidelity is claimed.

## Mapper-256 second step: normal OneBus banking and IRQ component

`OneBus` now implements the normal, unencrypted register set as an isolated
core component. **Mapper 256 remains unregistered** and the registry count
remains 162. This step does not enable loading OneBus games or claim a VT02,
VT03 or VT369 machine. Native PPU controls for enhanced video, sound, GPIO,
opcode scrambling and console capability metadata still need integration.

The original implementation follows the pinned
[PRG r12704](https://www.nesdev.org/w/index.php?oldid=12704),
[CHR r12696](https://www.nesdev.org/w/index.php?oldid=12696),
[forwarding r23929](https://www.nesdev.org/w/index.php?oldid=23929) and
[detailed register r23930](https://www.nesdev.org/w/index.php?oldid=23930)
specifications. Native $4100–$410B select inner/middle/outer PRG banks, a
selectable third window, mirroring and IRQ controls. $2012–$2017 select the
paired/inverted CHR windows, $2018 supplies intermediate bits and $201A
supplies the middle mask/substitution. All eight PRG masks and CHR mask codes
are tested. The native and MMC3 register selectors share state, all normal
MMC3 aliases forward, and $410B.3 disables every forwarded port without
blocking native writes. $A001 has no native WRAM port and is ignored. No
external WRAM, battery or expansion audio is supplied by this component.

The PRG overview describes COMR6 as inverting CPU A14, but the detailed
r23930 decoding table leaves PQ1 and the fixed $FF window in place. The
component follows that table: only PQ0 and PQ2/$FE exchange the $8000/$C000
windows. This agrees with the
[NRS hardware reference at b685625](https://github.com/gmestanley/NintendulatorNRS-DBG/blob/b685625310ec026afa844340d94503f9767b72df/src-mappers/src/Hardware/h_OneBus.cpp).
The earlier first-step work list's pair-swap wording must not be used as an
implementation rule. Reference emulator code is not incorporated.

Other reference differences remain explicit policies. A forwarded $8000
write does not forward bit 5; this component preserves native CIRAM selection
where NRS clears it. Native $4105.5 routes pattern accesses to the console's
existing 2 KiB CIRAM, mirrored across the pattern window, without another RAM
buffer. CHR mask codes 3/7 are called invalid by r12696; a zero-mask fallback
follows the published NRS compatibility behavior. Exact reserved-code wiring,
CIRAM selection interactions and reset circuitry remain unverified.

A12 mode clocks on rising edges qualified by at least three low M2 cycles;
held-high pins and short pulses do not count. $410B.7 selects HSYNC instead.
A new default-noop `on_ppu_hblank` callback, forwarded by Cartridge, is emitted
at PPU dot 256 on every scanline, including blanked/vblank/pre-render lines.
The component treats it as one timer pulse regardless of renderer enable.
NRS gates IRQ visibility on rendering and restricts its scanline callback;
this component instead follows the native timer interpretation as explicit
policy. Physical phase, rendering gates and edge polarity still need hardware
checks. The existing MMC3 dot-260 callback remains separate and does not clock
OneBus, preventing duplicate counts. Existing mapper clocks, PPU scroll
updates and ADR-001's omitted prefetch increments are unchanged.

IRQ latch writes, next-clock reload, enable, disable/acknowledge, pending state,
repeating zero latches and partial A12 qualification are serialized. Reset and
cold power restore documented compatibility defaults: PRG 0/1/$FE/$FF, CHR
0–7, vertical CIRAM and cleared IRQ/pin state. The fixed 30-byte payload contains
a version tag, PRG/CHR sizes, bank/control registers, counter flags and filter
phase. It is staged on the stack and validated before mutation. Truncation,
reserved flags and mismatched geometries reject atomically without a ROM copy.
CIRAM remains in PPU state; immutable ROMs and derived offsets are not saved.
Existing mapper payloads, machine version 20 and the 4 MiB state ceiling stay
unchanged.

An absent CHR image addresses the existing PRG vector directly. A split image
uses its own immutable CHR vector. There are no extra decoded CHR planes or
ROM-sized scratch arrays. The component's bounded test envelope is power-of-two
32 KiB–8 MiB PRG and absent or 8 KiB–4 MiB CHR, no declared RAM/NVRAM or battery,
H/V header flags and NES 2.0 mapper 256 submapper zero. It is an address-width
compatibility envelope, not a census of physical populations or a loader
capability declaration. Other submappers reject; console profiles and the full
32 MiB population remain future work. The global 16 MiB ROM-input cap is not
raised. Native register reads use CPU open bus as policy, and unsupported
extended graphics controls are consumed but not emulated by this test-only
component. It must not be registered as a full console implementation.

`one-bus` has ten cases covering exact CPU/PPU apertures, every MMC3 CPU write
address, forwarding disable, native selector interaction, all bank data bytes,
mask/middle/outer combinations, patterned PRG cells, shared/split ROM, CHR
protection, all IRQ latches, pin qualification, reset, replay and malformed
state rollback. Original CPU firmware runs four shared/split and H/V-header
fixtures, checking actual PPUDATA, CIRAM aliases, both IRQ delivery paths and
machine-state replay/rollback, including checkpoints while each IRQ source is
active. Fixtures temporarily replace the existing
mapper-4 factory only as a transport and restore builtin factories before
assertions; they add no fake registry ID. Separate PPU tests exercise HSYNC
with rendering on/off in NTSC/PAL/Dendy timing, including blanked and pre-render
lines. PAL tests validate the generic timing seam, not a claimed PAL VTxx
console. No commercial ROM, physical board or enhanced-console game is used.

Next integrate explicit console capabilities and supported register/graphics
profiles before registering 256. Submapper permutations and opcode-only
variants require separate validation; the current component deliberately
supports only normal submapper zero.

Validation on 2026-11-15 passed all 113 regular Meson targets with GPU checks
required and ten focused ASan+UBSan targets (`one-bus`, `bus`, `ppu`,
`mapper-timing`, `save-state`, `mappers`, `rom-source`, `fds`, `cpu`,
`cpu-accuracy`) with four workers and leak detection disabled on macOS.
Both builds passed 1,950,857 OneBus assertions across ten cases. Existing
bus/PPU totals remained 141,934/212 assertions in 34/21 cases; shared mapper
and loading totals remained 5,285,691/20,632 assertions in 469/39 cases.
The full SDL3/NodalKit build, formatting, scoped clang-tidy, whitespace and
exact 162-ID registry/ledger comparison passed. Isolated strict-consumer
standards validation retained zero errors, 72 warnings and three infos across
193 documents. These are component/model results, not hardware or commercial
title certification.

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
| 006 | FFE Magic Card | Registered: FfeMagicCard | [r18921](https://www.nesdev.org/w/index.php?oldid=18921) |
| 007 | AxROM | Existing registration: Axrom | [r24152](https://www.nesdev.org/w/index.php?oldid=24152) |
| 008 | FFE Magic Card alias | Registered: FfeMagicCard | [r18921](https://www.nesdev.org/w/index.php?oldid=18921) |
| 009 | MMC2 / PxROM | Existing registration: Mmc2 | [r22483](https://www.nesdev.org/w/index.php?oldid=22483) |
| 010 | MMC4 / FxROM | New implementation: Mmc4 | [r6620](https://www.nesdev.org/w/index.php?oldid=6620) |
| 011 | Color Dreams | Existing registration: ColorDreams | [r24575](https://www.nesdev.org/w/index.php?oldid=24575) |
| 012 | Gouder SL-5020B / FFE 4M | Registered: Mapper012 | [r18954](https://www.nesdev.org/w/index.php?oldid=18954) |
| 013 | CPROM | New implementation: DiscreteMapper | [r1200](https://www.nesdev.org/w/index.php?oldid=1200) |
| 014 | Board-specific specification | Registered: Sl1632 | [r19503](https://www.nesdev.org/w/index.php?oldid=19503) |
| 015 | Board-specific specification | Registered: Mapper015 | [r19956](https://www.nesdev.org/w/index.php?oldid=19956) |
| 016 | Bandai FCG / LZ93D50 | New implementation: BandaiFcg | [r3876](https://www.nesdev.org/w/index.php?oldid=3876) |
| 017 | FFE Super Magic Card | Registered: FfeSuperMagicCard | [r18933](https://www.nesdev.org/w/index.php?oldid=18933) |
| 018 | Jaleco SS88006 | New implementation: JalecoSs88006 (external speech unsupported) | [r3918](https://www.nesdev.org/w/index.php?oldid=3918) |
| 019 | Namco 129/163 | Existing registration: Namco163 | [r24377](https://www.nesdev.org/w/index.php?oldid=24377) |
| 020 | Famicom Disk System | Separate FDS subsystem | [r23632](https://www.nesdev.org/w/index.php?oldid=23632) |
| 021 | VRC4 | New implementation: KonamiVrc | [r24169](https://www.nesdev.org/w/index.php?oldid=24169) |
| 022 | VRC2a | New implementation: KonamiVrc | [r24169](https://www.nesdev.org/w/index.php?oldid=24169) |
| 023 | VRC2b / VRC4 | New implementation: KonamiVrc | [r24169](https://www.nesdev.org/w/index.php?oldid=24169) |
| 024 | VRC6a | Existing registration: Vrc6 | [r23687](https://www.nesdev.org/w/index.php?oldid=23687) |
| 025 | VRC2c / VRC4 | New implementation: KonamiVrc | [r24169](https://www.nesdev.org/w/index.php?oldid=24169) |
| 026 | VRC6b | Existing registration: Vrc6 | [r23687](https://www.nesdev.org/w/index.php?oldid=23687) |
| 027 | Unlicensed VRC4 variant | Registered: Mapper027 | [r23209](https://www.nesdev.org/w/index.php?oldid=23209) |
| 028 | Action 53 | New implementation: DiscreteMapper | [r21316](https://www.nesdev.org/w/index.php?oldid=21316) |
| 029 | RET-CUFROM | Registered: RetCufrom | [r4093](https://www.nesdev.org/w/index.php?oldid=4093) |
| 030 | UNROM 512 | New implementation: FlashBoards | [r23183](https://www.nesdev.org/w/index.php?oldid=23183) |
| 031 | NSF-subset cartridge | New implementation: DiscreteMapper | [r19421](https://www.nesdev.org/w/index.php?oldid=19421) |
| 032 | Irem G-101 | New implementation: Irem | [r23869](https://www.nesdev.org/w/index.php?oldid=23869) |
| 033 | Taito TC0190 / TC0350 subset | New implementation: TaitoTc | [r24359](https://www.nesdev.org/w/index.php?oldid=24359) |
| 034 | BNROM / NINA-001 | New implementation: DiscreteMapper | [r24073](https://www.nesdev.org/w/index.php?oldid=24073) |
| 035 | J.Y. Company ASIC | New implementation: JyCompany | [r24184](https://www.nesdev.org/w/index.php?oldid=24184) |
| 036 | TXC/JV001 protection board | New implementation: Txc | [r4196](https://www.nesdev.org/w/index.php?oldid=4196) |
| 037 | Board-specific specification | Registered: Mmc3Multicart37 | [r4204](https://www.nesdev.org/w/index.php?oldid=4204) |
| 038 | Board-specific specification | New implementation: DiscreteMapper | [r23289](https://www.nesdev.org/w/index.php?oldid=23289) |
| 039 | Board-specific specification | Not registered | [r4217](https://www.nesdev.org/w/index.php?oldid=4217) |
| 040 | Board-specific specification | Registered: Ntdec2722 | [r19687](https://www.nesdev.org/w/index.php?oldid=19687) |
| 041 | Board-specific specification | Registered: Caltron041 | [r24572](https://www.nesdev.org/w/index.php?oldid=24572) |
| 042 | Board-specific specification | Registered: Mapper042 | [r4239](https://www.nesdev.org/w/index.php?oldid=4239) |
| 043 | Board-specific specification | Registered: Mapper043 | [r4258](https://www.nesdev.org/w/index.php?oldid=4258) |
| 044 | Board-specific specification | Registered: Mapper044 | [r4263](https://www.nesdev.org/w/index.php?oldid=4263) |
| 045 | Board-specific specification | Registered: Mapper045 | [r22818](https://www.nesdev.org/w/index.php?oldid=22818) |
| 046 | Board-specific specification | New implementation: DiscreteMapper | [r4278](https://www.nesdev.org/w/index.php?oldid=4278) |
| 047 | Board-specific specification | Registered: Mmc3Multicart47 | [r4284](https://www.nesdev.org/w/index.php?oldid=4284) |
| 048 | Taito TC0690 | New implementation: TaitoTc | [r22585](https://www.nesdev.org/w/index.php?oldid=22585) |
| 049 | Board-specific specification | Registered: Mapper049 | [r4300](https://www.nesdev.org/w/index.php?oldid=4300) |
| 050 | Board-specific specification | Registered: N32Mapper050 | [r4310](https://www.nesdev.org/w/index.php?oldid=4310) |
| 051 | Board-specific specification | Not registered | [r4311](https://www.nesdev.org/w/index.php?oldid=4311) |
| 052 | Board-specific specification | Registered: Realtec8213 | [r21408](https://www.nesdev.org/w/index.php?oldid=21408) |
| 053 | Board-specific specification | Not registered | [r4320](https://www.nesdev.org/w/index.php?oldid=4320) |
| 054 | Board-specific specification | Not registered | [r4322](https://www.nesdev.org/w/index.php?oldid=4322) |
| 055 | Board-specific specification | Registered: Mario1Malee2 | [r4324](https://www.nesdev.org/w/index.php?oldid=4324) |
| 056 | Board-specific specification | Registered: Mapper056Ks202 | [r4330](https://www.nesdev.org/w/index.php?oldid=4330) |
| 057 | Board-specific specification | Registered: Mapper057 | [r4333](https://www.nesdev.org/w/index.php?oldid=4333) |
| 058 | Board-specific specification | New implementation: DiscreteMapper | [r4340](https://www.nesdev.org/w/index.php?oldid=4340) |
| 059 | Board-specific specification | Registered: BmcT3h53 | [r22300](https://www.nesdev.org/w/index.php?oldid=22300) |
| 060 | Board-specific specification | Registered: Multicart060 | [r22301](https://www.nesdev.org/w/index.php?oldid=22301) |
| 061 | Board-specific specification | New implementation: DiscreteMapper | [r22920](https://www.nesdev.org/w/index.php?oldid=22920) |
| 062 | Board-specific specification | New implementation: DiscreteMapper | [r4357](https://www.nesdev.org/w/index.php?oldid=4357) |
| 063 | Board-specific specification | Registered: Multicart063 | [r19134](https://www.nesdev.org/w/index.php?oldid=19134) |
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
| 083 | Cony / Yoko | Registered: ConyYoko | [r24580](https://www.nesdev.org/w/index.php?oldid=24580) |
| 084 | Board-specific specification | Not registered | [r24088](https://www.nesdev.org/w/index.php?oldid=24088) |
| 085 | VRC7 | Existing registration: Vrc7 | [r22102](https://www.nesdev.org/w/index.php?oldid=22102) |
| 086 | Jaleco JF-13 | Registered: JalecoJf13 | [r24513](https://www.nesdev.org/w/index.php?oldid=24513) |
| 087 | Jaleco / Konami J87 | New implementation: DiscreteMapper | [r23104](https://www.nesdev.org/w/index.php?oldid=23104) |
| 088 | Namco 118 variant | New implementation: Namco108 | [r23903](https://www.nesdev.org/w/index.php?oldid=23903) |
| 089 | Sunsoft 2 | New implementation: DiscreteMapper | [r24544](https://www.nesdev.org/w/index.php?oldid=24544) |
| 090 | J.Y. Company ASIC | New implementation: JyCompany | [r24184](https://www.nesdev.org/w/index.php?oldid=24184) |
| 091 | Board-specific specification | Registered: Mapper091 | [r22474](https://www.nesdev.org/w/index.php?oldid=22474) |
| 092 | Jaleco JF-19 | New implementation: JalecoDiscrete (external speech unsupported) | [r4687](https://www.nesdev.org/w/index.php?oldid=4687) |
| 093 | Sunsoft 2 | New implementation: DiscreteMapper | [r24542](https://www.nesdev.org/w/index.php?oldid=24542) |
| 094 | UxROM bit-layout variant | New implementation: DiscreteMapper | [r24548](https://www.nesdev.org/w/index.php?oldid=24548) |
| 095 | Namco 118 nametable variant | New implementation: Namco108 | [r23892](https://www.nesdev.org/w/index.php?oldid=23892) |
| 096 | Oeka Kids | Registered: OekaKids | [r24545](https://www.nesdev.org/w/index.php?oldid=24545) |
| 097 | Irem TAM-S1 | New implementation: DiscreteMapper | [r22478](https://www.nesdev.org/w/index.php?oldid=22478) |
| 098 | Reserved | Not registered | [r22148](https://www.nesdev.org/w/index.php?oldid=22148) |
| 099 | Vs. System board | Not registered | [r23485](https://www.nesdev.org/w/index.php?oldid=23485) |
| 100 | Emulator tool use | Not registered | [r23979](https://www.nesdev.org/w/index.php?oldid=23979) |
| 101 | JF-10 legacy dump ordering | New implementation: JalecoDiscrete | [r21360](https://www.nesdev.org/w/index.php?oldid=21360) |
| 102 | Board-specific specification | Not registered | [r4801](https://www.nesdev.org/w/index.php?oldid=4801) |
| 103 | Board-specific specification | Registered: Mapper103 | [r4802](https://www.nesdev.org/w/index.php?oldid=4802) |
| 104 | Codemasters Golden Five | Registered: Mapper104GoldenFive | [r9567](https://www.nesdev.org/w/index.php?oldid=9567) |
| 105 | NES-EVENT / MMC1 | Registered: NesEvent | [r23388](https://www.nesdev.org/w/index.php?oldid=23388) |
| 106 | Board-specific specification | Registered: Mapper106 | [r4815](https://www.nesdev.org/w/index.php?oldid=4815) |
| 107 | Board-specific specification | New implementation: DiscreteMapper | [r24577](https://www.nesdev.org/w/index.php?oldid=24577) |
| 108 | FDS-to-cartridge boards | Registered: FdsConversion108 | [r21237](https://www.nesdev.org/w/index.php?oldid=21237) |
| 109 | Board-specific specification | Not registered | [r4825](https://www.nesdev.org/w/index.php?oldid=4825) |
| 110 | Board-specific specification | Not registered | [r4826](https://www.nesdev.org/w/index.php?oldid=4826) |
| 111 | GTROM / earlier MMC1 variant | New implementation: FlashBoards / Mmc1 | [r24005](https://www.nesdev.org/w/index.php?oldid=24005) |
| 112 | Board-specific specification | Registered: Mapper112 | [r23867](https://www.nesdev.org/w/index.php?oldid=23867) |
| 113 | Board-specific specification | New implementation: DiscreteMapper | [r20481](https://www.nesdev.org/w/index.php?oldid=20481) |
| 114 | SuperGame/Hosenkan scrambled MMC3, submappers 0/1 | New implementation: Mmc3 | [r4858](https://www.nesdev.org/w/index.php?oldid=4858) |
| 115 | Kasheng SFC-02B/-03/-004 | New implementation: Mmc3 | [r21614](https://www.nesdev.org/w/index.php?oldid=21614) |
| 116 | Board-specific specification | Registered: SomariP116 | [r22958](https://www.nesdev.org/w/index.php?oldid=22958) |
| 117 | Board-specific specification | Registered: Mapper117 | [r4888](https://www.nesdev.org/w/index.php?oldid=4888) |
| 118 | TxSROM / MMC3 | New implementation: Mmc3 variant | [r24058](https://www.nesdev.org/w/index.php?oldid=24058) |
| 119 | TQROM / MMC3 | New implementation: Mmc3 variant | [r24251](https://www.nesdev.org/w/index.php?oldid=24251) |
| 120 | Board-specific specification | Registered: Lh15 | [r20571](https://www.nesdev.org/w/index.php?oldid=20571) |
| 121 | Board-specific specification | Registered: Mapper121 | [r23905](https://www.nesdev.org/w/index.php?oldid=23905) |
| 122 | Board-specific specification | Registered: Jy043 | [r22366](https://www.nesdev.org/w/index.php?oldid=22366) |
| 123 | Kasheng H2288 / UNL-H2288 | New implementation: Mmc3 | [r4940](https://www.nesdev.org/w/index.php?oldid=4940) |
| 124 | Board-specific specification | Registered: SuperGameMegaTypeIII | [r4941](https://www.nesdev.org/w/index.php?oldid=4941) |
| 125 | Board-specific specification | Registered: Lh32 | [r4945](https://www.nesdev.org/w/index.php?oldid=4945) |
| 126 | Board-specific specification | Registered: Tec9719 | [r22468](https://www.nesdev.org/w/index.php?oldid=22468) |
| 127 | Board-specific specification | Not registered | [r4951](https://www.nesdev.org/w/index.php?oldid=4951) |
| 128 | Board-specific specification | Registered: Mapper128 | [r21955](https://www.nesdev.org/w/index.php?oldid=21955) |
| 129 | Board-specific specification | Not registered | [r24633](https://www.nesdev.org/w/index.php?oldid=24633) |
| 130 | Board-specific specification | Not registered | [r24051](https://www.nesdev.org/w/index.php?oldid=24051) |
| 131 | Board-specific specification | Not registered | [r24052](https://www.nesdev.org/w/index.php?oldid=24052) |
| 132 | TXC/JV001 protection board | New implementation: Txc | [r4967](https://www.nesdev.org/w/index.php?oldid=4967) |
| 133 | Sachen board | New implementation: Sachen | [r4977](https://www.nesdev.org/w/index.php?oldid=4977) |
| 134 | Board-specific specification | Registered: T4a54a | [r20085](https://www.nesdev.org/w/index.php?oldid=20085) |
| 135 | Sachen 8259 | New implementation: Sachen | [r23709](https://www.nesdev.org/w/index.php?oldid=23709) |
| 136 | TXC/JV001 protection board | New implementation: Txc | [r21611](https://www.nesdev.org/w/index.php?oldid=21611) |
| 137 | Sachen 8259 | New implementation: Sachen | [r20417](https://www.nesdev.org/w/index.php?oldid=20417) |
| 138 | Sachen 8259 | New implementation: Sachen | [r23709](https://www.nesdev.org/w/index.php?oldid=23709) |
| 139 | Sachen 8259 | New implementation: Sachen | [r23709](https://www.nesdev.org/w/index.php?oldid=23709) |
| 140 | Jaleco JF-11/14 | New implementation: DiscreteMapper | [r22403](https://www.nesdev.org/w/index.php?oldid=22403) |
| 141 | Sachen 8259 | New implementation: Sachen | [r23709](https://www.nesdev.org/w/index.php?oldid=23709) |
| 142 | Board-specific specification | Registered: KaiserKs202 | [r5032](https://www.nesdev.org/w/index.php?oldid=5032) |
| 143 | Sachen board | New implementation: Sachen | [r5034](https://www.nesdev.org/w/index.php?oldid=5034) |
| 144 | Board-specific specification | Registered: Mapper144 | [r19702](https://www.nesdev.org/w/index.php?oldid=19702) |
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
| 155 | MMC1A | Registered: Mmc1a | [r24550](https://www.nesdev.org/w/index.php?oldid=24550) |
| 156 | Board-specific specification | Registered: DaouMapper156 | [r22838](https://www.nesdev.org/w/index.php?oldid=22838) |
| 157 | Bandai Datach | New implementation: Datach | [r21310](https://www.nesdev.org/w/index.php?oldid=21310) |
| 158 | RAMBO-1 nametable variant | New implementation: Rambo1 | [r21299](https://www.nesdev.org/w/index.php?oldid=21299) |
| 159 | Bandai X24C01 EEPROM | New implementation: BandaiFcg | [r5171](https://www.nesdev.org/w/index.php?oldid=5171) |
| 160 | Obsolete mapper-90 alias | Registered: JyCompany | [r5173](https://www.nesdev.org/w/index.php?oldid=5173) |
| 161 | Board-specific specification | Not registered | [r5175](https://www.nesdev.org/w/index.php?oldid=5175) |
| 162 | Board-specific specification | Registered: Mapper162Fs304 | [r19013](https://www.nesdev.org/w/index.php?oldid=19013) |
| 163 | Board-specific specification | Registered: NanjingFc001 | [r24182](https://www.nesdev.org/w/index.php?oldid=24182) |
| 164 | Board-specific specification | Registered: Fs304 | [r22865](https://www.nesdev.org/w/index.php?oldid=22865) |
| 165 | Board-specific specification | Registered: Mapper165 | [r5217](https://www.nesdev.org/w/index.php?oldid=5217) |
| 166 | Board-specific specification | Registered: Mapper166 | [r22345](https://www.nesdev.org/w/index.php?oldid=22345) |
| 167 | Board-specific specification | Registered: Mapper166 | [r22345](https://www.nesdev.org/w/index.php?oldid=22345) |
| 168 | Racermate | Registered: Racermate168 | [r22805](https://www.nesdev.org/w/index.php?oldid=22805) |
| 169 | Board-specific specification | Not registered | [r5247](https://www.nesdev.org/w/index.php?oldid=5247) |
| 170 | Board-specific specification | Not registered | [r21130](https://www.nesdev.org/w/index.php?oldid=21130) |
| 171 | Board-specific specification | Not registered | [r22367](https://www.nesdev.org/w/index.php?oldid=22367) |
| 172 | TXC/JV001 protection board | New implementation: Txc | [r5257](https://www.nesdev.org/w/index.php?oldid=5257) |
| 173 | TXC/JV001 protection board | New implementation: Txc | [r5274](https://www.nesdev.org/w/index.php?oldid=5274) |
| 174 | Board-specific specification | Registered: Ntdec174 | [r5296](https://www.nesdev.org/w/index.php?oldid=5296) |
| 175 | Board-specific specification | Not registered | [r5297](https://www.nesdev.org/w/index.php?oldid=5297) |
| 176 | FK23C | Registered: Fk23c | [r24008](https://www.nesdev.org/w/index.php?oldid=24008) |
| 177 | Board-specific specification | Registered: Henggedianzi177 | [r24245](https://www.nesdev.org/w/index.php?oldid=24245) |
| 178 | Waixing / Nanjing family | Not registered | [r22866](https://www.nesdev.org/w/index.php?oldid=22866) |
| 179 | Board-specific specification | Not registered | [r5374](https://www.nesdev.org/w/index.php?oldid=5374) |
| 180 | UxROM fixed-low-window variant | New implementation: DiscreteMapper | [r19264](https://www.nesdev.org/w/index.php?oldid=19264) |
| 181 | Board-specific specification | Not registered | [r24634](https://www.nesdev.org/w/index.php?oldid=24634) |
| 182 | Historical duplicate of 114 | Duplicate; not registered | [r4858](https://www.nesdev.org/w/index.php?oldid=4858) |
| 183 | Board-specific specification | Registered: Mapper183 | [r24192](https://www.nesdev.org/w/index.php?oldid=24192) |
| 184 | Sunsoft CHR banking | New implementation: DiscreteMapper | [r23872](https://www.nesdev.org/w/index.php?oldid=23872) |
| 185 | CNROM CHR-disable protection | Registered: Cnrom185 | [r24346](https://www.nesdev.org/w/index.php?oldid=24346) |
| 186 | Fukutake Study Box convention | Not registered | [r19026](https://www.nesdev.org/w/index.php?oldid=19026) |
| 187 | Board-specific specification | Registered: Mapper187 | [r23904](https://www.nesdev.org/w/index.php?oldid=23904) |
| 188 | Bandai Karaoke Studio | Registered: KaraokeStudio188 | [r22386](https://www.nesdev.org/w/index.php?oldid=22386) |
| 189 | Board-specific specification | Registered: Txc189 | [r22901](https://www.nesdev.org/w/index.php?oldid=22901) |
| 190 | Board-specific specification | Registered: Mapper190 | [r22728](https://www.nesdev.org/w/index.php?oldid=22728) |
| 191 | Board-specific specification | Registered: Mapper191 | [r23949](https://www.nesdev.org/w/index.php?oldid=23949) |
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
| 224 | Jncota KT-008 / mapper-268 chipset | Registered: Kt008 | [r5752](https://www.nesdev.org/w/index.php?oldid=5752) |
| 225 | ET-4310 / K-1010 address-latched multicarts | New implementation: DiscreteMapper | [r18874](https://www.nesdev.org/w/index.php?oldid=18874) |
| 226 | Board-specific specification | New implementation: DiscreteMapper | [r22261](https://www.nesdev.org/w/index.php?oldid=22261) |
| 227 | Address-latched multicart / RPG boards | New implementation: Multicart227 | [r23188](https://www.nesdev.org/w/index.php?oldid=23188) |
| 228 | Action 52 / Active Enterprises | New implementation: Action52 | [r5784](https://www.nesdev.org/w/index.php?oldid=5784) |
| 229 | Board-specific specification | New implementation: DiscreteMapper | [r5785](https://www.nesdev.org/w/index.php?oldid=5785) |
| 230 | Reset-driven 22-in-1 / Contra + multicart | New implementation: Multicart230 | [r5788](https://www.nesdev.org/w/index.php?oldid=5788) |
| 231 | Board-specific specification | New implementation: DiscreteMapper | [r5791](https://www.nesdev.org/w/index.php?oldid=5791) |
| 232 | Codemasters Quattro | New implementation: DiscreteMapper | [r5797](https://www.nesdev.org/w/index.php?oldid=5797) |
| 233 | 42 Games / conflicting legacy models | New implementation: Multicart233 (profiled compatibility models) | [r5799](https://www.nesdev.org/w/index.php?oldid=5799) |
| 234 | Maxi 15 / AVE and Australian HES wiring | New implementation: Maxi15 | [r24540](https://www.nesdev.org/w/index.php?oldid=24540) |
| 235 | Golden Game / eight packed ROM socket populations | New implementation: GoldenGame | [r22514](https://www.nesdev.org/w/index.php?oldid=22514) |
| 236 | Realtec 8031/8155, 8099 and CHR-RAM 8106 | New implementation: RealtecMulticart | [r18875](https://www.nesdev.org/w/index.php?oldid=18875) |
| 237 | Teletubbies 420-in-1 / forced-A1 type wiring | New implementation: Teletubbies | [r5823](https://www.nesdev.org/w/index.php?oldid=5823) |
| 238 | Contra Fighter / UNL-603-5052 protection compatibility model | New implementation: Mmc3 | [r5824](https://www.nesdev.org/w/index.php?oldid=5824) |
| 240 | Board-specific specification | New implementation: DiscreteMapper | [r21944](https://www.nesdev.org/w/index.php?oldid=21944) |
| 241 | BxROM with 8 KiB WRAM and optional LPC speech profile | New implementation: BxromWram | [r23728](https://www.nesdev.org/w/index.php?oldid=23728) |
| 242 | 43272 multicart / battery Waixing RPG / ET-113 replacement menu | New implementation: Multicart242 | [r19709](https://www.nesdev.org/w/index.php?oldid=19709) |
| 243 | Sachen board | New implementation: Sachen | [r5861](https://www.nesdev.org/w/index.php?oldid=5861) |
| 244 | C&E Decathlon / scrambled bank latches | New implementation: Decathlon | [r5862](https://www.nesdev.org/w/index.php?oldid=5862) |
| 245 | Waixing F003 / CHR output drives PRG A19 | New implementation: Mmc3 | [r5871](https://www.nesdev.org/w/index.php?oldid=5871) |
| 246 | G0151-1 / Feng Shen Bang | New implementation: G0151 | [r22048](https://www.nesdev.org/w/index.php?oldid=22048) |
| 248 | Obsolete duplicate of 115; private-use reassignment proposed | Obsolete; not registered | [r23288](https://www.nesdev.org/w/index.php?oldid=23288) |
| 249 | Waixing T9552, legacy file order | New implementation: Mmc3 | [r20254](https://www.nesdev.org/w/index.php?oldid=20254) |
| 250 | Nitra L401 / Queen Bee V | New implementation: Mmc3 | [r21044](https://www.nesdev.org/w/index.php?oldid=21044) |
| 251 | Obsolete Nestopia assignment duplicating mapper 45 | Obsolete; not registered | [r5898](https://www.nesdev.org/w/index.php?oldid=5898) |
| 252 | Waixing VRC4e / Sangokushi mixed CHR | New implementation: KonamiVrc | [r5906](https://www.nesdev.org/w/index.php?oldid=5906) |
| 253 | Waixing VRC4 CHR4/5 / F009S relocation profile | New implementation: KonamiVrc | [r5910](https://www.nesdev.org/w/index.php?oldid=5910) |
| 254 | Pikachu Y2K WRAM XOR protection | New implementation: Mmc3 | [r24273](https://www.nesdev.org/w/index.php?oldid=24273) |
| 255 | 110-in-1 / apparent duplicate of 225 | Apparent duplicate; not registered | [r20429](https://www.nesdev.org/w/index.php?oldid=20429) |
| 256 | OneBus / VTxx native console registers | Not registered: normal banking/IRQ component implemented | [r23934](https://www.nesdev.org/w/index.php?oldid=23934) |
| 257 | Dongda / Hongda educational boards | Registered: Mapper257 | [r23118](https://www.nesdev.org/w/index.php?oldid=23118) |
| 258 | Board-specific specification | Not registered | [r7627](https://www.nesdev.org/w/index.php?oldid=7627) |
| 259 | Board-specific specification | Not registered | [r7628](https://www.nesdev.org/w/index.php?oldid=7628) |
| 260 | Board-specific specification | Registered: HpMulticart260 | [r22170](https://www.nesdev.org/w/index.php?oldid=22170) |
| 261 | Board-specific specification | Registered: Ntdec2746 | [r18984](https://www.nesdev.org/w/index.php?oldid=18984) |
| 262 | Sachen Street Heroes / UNL-SHERO | New implementation: Mmc3 | [r7640](https://www.nesdev.org/w/index.php?oldid=7640) |
| 263 | Rex Soft UNL-KOF97 | New implementation: Mmc3 | [r7642](https://www.nesdev.org/w/index.php?oldid=7642) |
| 264 | Board-specific specification | Registered: ConyYoko | [r24580](https://www.nesdev.org/w/index.php?oldid=24580) |
| 265 | Board-specific specification | Registered: Mapper128 | [r21955](https://www.nesdev.org/w/index.php?oldid=21955) |
| 266 | Board-specific specification | Registered: CityFighter266 | [r7660](https://www.nesdev.org/w/index.php?oldid=7660) |
| 267 | Board-specific specification | Registered: Mapper267 | [r7663](https://www.nesdev.org/w/index.php?oldid=7663) |
| 268 | Coolboy / Mindkids | Registered: Coolboy | [r24626](https://www.nesdev.org/w/index.php?oldid=24626) |
| 269 | Board-specific specification | Registered: Mapper269 | [r24351](https://www.nesdev.org/w/index.php?oldid=24351) |
| 270 | OneBus outer banking | Not registered | [r23373](https://www.nesdev.org/w/index.php?oldid=23373) |
| 271 | Board-specific specification | Registered: Bmc22026 | [r24533](https://www.nesdev.org/w/index.php?oldid=24533) |
| 272 | Board-specific specification | Registered: Mapper272 | [r7701](https://www.nesdev.org/w/index.php?oldid=7701) |
| 273 | Board-specific specification | Registered: Mapper273 | [r7703](https://www.nesdev.org/w/index.php?oldid=7703) |
| 274 | Board-specific specification | Registered: CartridgeStory274 | [r7708](https://www.nesdev.org/w/index.php?oldid=7708) |
| 275 | Board-specific specification | Not registered | [r7709](https://www.nesdev.org/w/index.php?oldid=7709) |
| 276 | Board-specific specification | Not registered | [r7710](https://www.nesdev.org/w/index.php?oldid=7710) |
| 277 | Board-specific specification | Not registered | [r24389](https://www.nesdev.org/w/index.php?oldid=24389) |
| 278 | Board-specific specification | Not registered | [r7712](https://www.nesdev.org/w/index.php?oldid=7712) |
| 279 | Board-specific specification | Not registered | [r23180](https://www.nesdev.org/w/index.php?oldid=23180) |
| 280 | Board-specific specification | Not registered | [r7714](https://www.nesdev.org/w/index.php?oldid=7714) |
| 281 | J.Y. Company multicart | New implementation: JyCompany | [r18938](https://www.nesdev.org/w/index.php?oldid=18938) |
| 282 | J.Y. Company multicart | New implementation: JyCompany | [r23351](https://www.nesdev.org/w/index.php?oldid=23351) |
| 283 | Board-specific specification | Registered: BlockFamily283 | [r7724](https://www.nesdev.org/w/index.php?oldid=7724) |
| 284 | Drip | Registered: Drip | [r7727](https://www.nesdev.org/w/index.php?oldid=7727) |
| 285 | Board-specific specification | Registered: Mapper285 | [r22915](https://www.nesdev.org/w/index.php?oldid=22915) |
| 286 | Board-specific specification | Registered: BenshiengBs5 | [r24644](https://www.nesdev.org/w/index.php?oldid=24644) |
| 287 | Board-specific specification | Registered: Mapper287 | [r7732](https://www.nesdev.org/w/index.php?oldid=7732) |
| 288 | Board-specific specification | Registered: Mapper288 | [r24622](https://www.nesdev.org/w/index.php?oldid=24622) |
| 289 | Board-specific specification | Registered: Mapper289 | [r20161](https://www.nesdev.org/w/index.php?oldid=20161) |
| 290 | Board-specific specification | Registered: Mapper290 | [r7743](https://www.nesdev.org/w/index.php?oldid=7743) |
| 291 | Board-specific specification | Registered: Mapper291 | [r7746](https://www.nesdev.org/w/index.php?oldid=7746) |
| 292 | Board-specific specification | Registered: Mapper292 | [r22336](https://www.nesdev.org/w/index.php?oldid=22336) |
| 293 | Board-specific specification | Registered: Mapper293 | [r7751](https://www.nesdev.org/w/index.php?oldid=7751) |
| 294 | Board-specific specification | Registered: Mapper294 | [r19317](https://www.nesdev.org/w/index.php?oldid=19317) |
| 295 | J.Y. Company multicart | New implementation: JyCompany | [r23352](https://www.nesdev.org/w/index.php?oldid=23352) |
| 296 | VT3x | Not registered | [r23820](https://www.nesdev.org/w/index.php?oldid=23820) |
| 297 | Board-specific specification | Not registered | [r7767](https://www.nesdev.org/w/index.php?oldid=7767) |
| 298 | Board-specific specification | Registered: UnlTf1201 | [r7772](https://www.nesdev.org/w/index.php?oldid=7772) |
| 299 | Board-specific specification | Registered: Mapper299 | [r22947](https://www.nesdev.org/w/index.php?oldid=22947) |
| 300 | Board-specific specification | Registered: Mapper300 | [r7775](https://www.nesdev.org/w/index.php?oldid=7775) |
| 301 | Board-specific specification | Not registered | [r19709](https://www.nesdev.org/w/index.php?oldid=19709) |
| 302 | Board-specific specification | Registered: KaiserKs7057 | [r22779](https://www.nesdev.org/w/index.php?oldid=22779) |
| 303 | Board-specific specification | Not registered | [r7779](https://www.nesdev.org/w/index.php?oldid=7779) |
| 304 | Board-specific specification | Registered: Mapper304 | [r7785](https://www.nesdev.org/w/index.php?oldid=7785) |
| 305 | Board-specific specification | Registered: Mapper305 | [r7786](https://www.nesdev.org/w/index.php?oldid=7786) |
| 306 | Board-specific specification | Registered: Mapper306 | [r7788](https://www.nesdev.org/w/index.php?oldid=7788) |
| 307 | Board-specific specification | Registered: Mapper307 | [r7791](https://www.nesdev.org/w/index.php?oldid=7791) |
| 308 | Board-specific specification | Registered: Mapper308 | [r7793](https://www.nesdev.org/w/index.php?oldid=7793) |
| 309 | Board-specific specification | Registered: Mapper309 | [r7795](https://www.nesdev.org/w/index.php?oldid=7795) |
| 310 | Board-specific specification | Registered: Mapper310 | [r19316](https://www.nesdev.org/w/index.php?oldid=19316) |
| 311 | Board-specific specification | Not registered | [r7798](https://www.nesdev.org/w/index.php?oldid=7798) |
| 312 | Board-specific specification | Registered: Mapper312 | [r7801](https://www.nesdev.org/w/index.php?oldid=7801) |
| 313 | Board-specific specification | Registered: Mapper313 | [r7804](https://www.nesdev.org/w/index.php?oldid=7804) |
| 314 | Board-specific specification | Registered: Mapper314 | [r23247](https://www.nesdev.org/w/index.php?oldid=23247) |
| 315 | Board-specific specification | Registered: Mapper315 | [r7811](https://www.nesdev.org/w/index.php?oldid=7811) |
| 316 | Board-specific specification | Not registered | [r23179](https://www.nesdev.org/w/index.php?oldid=23179) |
| 317 | Board-specific specification | Not registered | [r23178](https://www.nesdev.org/w/index.php?oldid=23178) |
| 318 | Board-specific specification | Not registered | [r23177](https://www.nesdev.org/w/index.php?oldid=23177) |
| 319 | Board-specific specification | Registered: Mapper319 | [r22946](https://www.nesdev.org/w/index.php?oldid=22946) |
| 320 | Board-specific specification | Registered: Mapper320 | [r20234](https://www.nesdev.org/w/index.php?oldid=20234) |
| 321 | Board-specific specification | Registered: Mapper321 | [r19761](https://www.nesdev.org/w/index.php?oldid=19761) |
| 322 | Board-specific specification | Registered: Mapper322 | [r7827](https://www.nesdev.org/w/index.php?oldid=7827) |
| 323 | Board-specific specification | Registered: FaridSlrom323 | [r7829](https://www.nesdev.org/w/index.php?oldid=7829) |
| 324 | Board-specific specification | Registered: Mapper324 | [r24547](https://www.nesdev.org/w/index.php?oldid=24547) |
| 325 | Board-specific specification | Registered: Mapper325 | [r19372](https://www.nesdev.org/w/index.php?oldid=19372) |
| 326 | Board-specific specification | Registered: Mapper326 | [r7837](https://www.nesdev.org/w/index.php?oldid=7837) |
| 327 | Board-specific specification | Registered: Mapper327 | [r7840](https://www.nesdev.org/w/index.php?oldid=7840) |
| 328 | Board-specific specification | Registered: Mapper328 | [r7844](https://www.nesdev.org/w/index.php?oldid=7844) |
| 329 | Board-specific specification | Not registered | [r24220](https://www.nesdev.org/w/index.php?oldid=24220) |
| 330 | Board-specific specification | Registered: Mapper330 | [r20367](https://www.nesdev.org/w/index.php?oldid=20367) |
| 331 | Board-specific specification | Registered: Mapper331 | [r7849](https://www.nesdev.org/w/index.php?oldid=7849) |
| 332 | Board-specific specification | Registered: Mapper332 | [r7853](https://www.nesdev.org/w/index.php?oldid=7853) |
| 333 | Board-specific specification | Registered: Mapper333 | [r7855](https://www.nesdev.org/w/index.php?oldid=7855) |
| 334 | Board-specific specification | Registered: Mapper334 | [r7857](https://www.nesdev.org/w/index.php?oldid=7857) |
| 335 | Board-specific specification | Registered: Mapper335 | [r24495](https://www.nesdev.org/w/index.php?oldid=24495) |
| 336 | Board-specific specification | Registered: Mapper336 | [r24537](https://www.nesdev.org/w/index.php?oldid=24537) |
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
