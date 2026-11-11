#include "core/mappers/mmc3.hpp"

#include "core/state/state.hpp"

namespace mapperbus::core {

// Variant wiring: NESdev mapper 118 r24058, mapper 119 r24251,
// and TxROM board memory population r24047.
// Mapper 512: NESdev r8073 and NRS's NES 2.0 VRAM accounting discussion
// (forums.nesdev.org/viewtopic.php?t=16881). The published register masks
// remain tentative; use the documented $E001 / $C100 decoders.
// Mapper 513: NESdev r8077. CHR output pins select outer PRG banks; the
// fixed PRG windows remain in outer bank zero. The three-chip dump's bank-3
// alias follows the documented compatibility convention, not analog contention.
// Mapper 262: NESdev r7640. The four high CHR bits are selected by the
// physical 2 KiB PPU region, independently of MMC3 CHR inversion. Its $C100
// expansion decoder and normal $E001 MMC3 decoder are tentative in the source.
// Mapper 263 / UNL-KOF97: NESdev r7642, CPU A12 replaces the MMC3 A0
// register select, with permuted data pins on mapper-register writes only.
// Mapper 250: NESdev r21044 and Krzysiobal's PCB schematics 354/533.
// CPU A10 selects the register, A7..A0 supply data. Queen Bee V's 74153
// selects CPU A13 instead of MMC3 A13 at $C000-$FFFF during M2 high.
// Mapper 115: NESdev r21614. The low control ports bypass WRAM gating;
// PRG A18 is selected in both MMC3 and NROM modes. Retain Sharp-style
// zero-latch IRQ repeats, unlike mapper 114's different counter profile.
// Mapper 114: NESdev r4858. Two address/index permutations; the low $E001
// decoder is tentative. Use the existing MMC3A-type non-repeating IRQ profile
// described in MMC3 r24445, including its one software-forced zero reload IRQ.
// Mapper 123 / H2288: NESdev r4940. Ordinary MMC3 addresses, mapper-114
// type-0 index scrambling, and a write-only $F800-decoded PRG override.
// No clone-specific IRQ revision is documented; retain the default profile.
// T9552: NESdev r20254 and measured dump permutations in forum thread 24314.
// Apply address wiring to MMC3 outputs, including fixed banks, before ROM wrapping.
// Waixing 74/192: NESdev r24258/r23950 and mixed-CHR summary r23945.
// Decode banks 8 onward before ROM wrapping; only those pages are writable.
// Mapper 194: r23951/r23945. RAM starts at zero and mirrors when CHR ROM is absent.
// Mapper 195 / FS303: NESdev r23952 and measured hardware in forum thread 19041.
// Only PPU writes to banks >= $80 set the GAL mode; RAM uses raw CHR A10-A12.
// MRCM 196: NESdev r22320, with r22901's generalized mapper-189 nibble folding.
// Profile zero is a compatibility heuristic, distinct from fixed A1/A2 wiring.
// Mapper 197: NESdev r5527. Rewire PPU addresses before MMC3 inversion and
// shift MMC3 CHR outputs up one pin. Submapper 3 gates its PRG-A17 mux via A001.
// Mapper 198: NESdev r23191's TNROM640 compatibility model. Bank bit 6 selects
// the 128 KiB chip; its unused upper address pins mirror banks $40-$4F.
// Actual PCB/RAM circuitry remains unresolved (NESdev forum p=297200).
Mmc3::Mmc3(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), use_chr_ram_(chr_rom_.empty()),
      mapper_(header.mapper_number == 74U || header.mapper_number == 192U ||
                      header.mapper_number == 194U || header.mapper_number == 195U ||
                      header.mapper_number == 196U || header.mapper_number == 197U ||
                      header.mapper_number == 198U || header.mapper_number == 114U ||
                      header.mapper_number == 115U || header.mapper_number == 123U ||
                      header.mapper_number == 249U || header.mapper_number == 250U ||
                      header.mapper_number == 262U || header.mapper_number == 263U ||
                      header.mapper_number == 118U || header.mapper_number == 119U ||
                      header.mapper_number == 512U || header.mapper_number == 513U
                  ? header.mapper_number
                  : 4U),
      scramble_profile_(mapper_ == 114U && header.is_nes2 ? header.submapper : 0U),
      t9552_(mapper_ == 249U || (mapper_ == 4U && header.is_nes2 && header.submapper == 5U)),
      mrcm_profile_(mapper_ == 196U && header.is_nes2 ? header.submapper : 0U),
      mapper197_profile_(mapper_ == 197U && header.is_nes2 ? header.submapper : 0U),
      initial_mirror_(header.mirror_mode), mirror_mode_(header.mirror_mode),
      irq_zero_latch_repeats_(
          !(mapper_ == 114U || (mapper_ == 4U && header.is_nes2 && header.submapper == 4))) {
    if (mapper_ == 74U || mapper_ == 192U || mapper_ == 194U)
        mixed_chr_ram_size_ = header.is_nes2    ? header.chr_ram_size
                              : mapper_ == 192U ? 0x1000U
                                                : 0x800U;
    if (mapper_ == 198U) {
        mapper198_aux_ram_.resize(0x1000U);
        prg_ram_size_ = 0x2000U;
        nvram_size_ = 0x2000U;
    } else if (mapper_ == 195U) {
        fs303_aux_present_ = header.is_nes2 && header.prg_ram_size == 0x1000U;
        if (fs303_aux_present_)
            fs303_aux_ram_.resize(0x1000U);
        prg_ram_size_ =
            header.is_nes2 ? header.prg_nvram_size + (fs303_aux_present_ ? 0U : header.prg_ram_size)
            : header.has_battery ? 0x2000U
                                 : 0U;
        nvram_size_ = header.is_nes2 ? header.prg_nvram_size : prg_ram_size_;
    } else if (t9552_ || mixed_chr_ram_size_ != 0U) {
        prg_ram_size_ = header.is_nes2       ? header.prg_ram_size + header.prg_nvram_size
                        : header.has_battery ? 0x2000U
                                             : 0U;
        nvram_size_ = header.is_nes2 ? header.prg_nvram_size : prg_ram_size_;
    } else if (mapper_ == 114U || mapper_ == 115U || mapper_ == 123U || mapper_ == 196U ||
               mapper_ == 197U || mapper_ == 250U) {
        prg_ram_size_ = 0U;
        nvram_size_ = 0U;
    } else if (mapper_ == 262U) {
        prg_ram_size_ = 0U;
        nvram_size_ = 0U;
        initial_mirror_ = MirrorMode::FourScreen;
        mirror_mode_ = initial_mirror_;
        nametable_ram_.resize(0x1000U);
    } else if (mapper_ == 513U) {
        prg_ram_size_ = 0U;
        nvram_size_ = 0U;
        chr_nvram_.resize(0x8000U);
    } else if (mapper_ == 512U) {
        prg_ram_size_ = 0x2000U;
        nvram_size_ = 0x2000U;
        if (initial_mirror_ == MirrorMode::FourScreen) {
            initial_mirror_ =
                (header.nametable_bits & 1U) != 0U ? MirrorMode::Vertical : MirrorMode::Horizontal;
            mirror_mode_ = initial_mirror_;
        }
    } else if (mapper_ != 4U) {
        prg_ram_size_ = mapper_ == 119U      ? 0U
                        : header.is_nes2     ? header.prg_ram_size + header.prg_nvram_size
                        : header.has_battery ? 0x2000U
                                             : 0U;
        nvram_size_ = header.is_nes2 ? header.prg_nvram_size : prg_ram_size_;
    }
    num_prg_banks_8k_ = prg_rom_.size() / 0x2000U;
    num_chr_banks_1k_ = use_chr_ram_ ? 8 : static_cast<uint16_t>(chr_rom_.size() / 0x0400);
    reset();
}

void Mmc3::reset() {
    mapper197_prg_ = 0U;
    mrcm_prg_ = mapper_ == 196U && mrcm_profile_ == 3U ? 0x80U : 0U;
    fs303_chr_mode_ = 0x80U;
    t9552_mode_ = 0U;
    kasheng_prg_ = 0U;
    kasheng_chr_ = 0U;
    ppu_chr_slot_ = 0U;
    board_mode_ = 0U;
    bank_select_ = 0;
    prg_mode_ = false;
    chr_inversion_ = false;
    bank_registers_.fill(0);
    irq_counter_ = 0;
    irq_reload_ = 0;
    irq_reload_flag_ = false;
    irq_enabled_ = false;
    irq_pending_ = false;
    prg_ram_enabled_ = true;
    prg_ram_write_protected_ = false;
    if (mapper_ != 4U || t9552_)
        mirror_mode_ = initial_mirror_;
    update_prg_banks();
    update_chr_banks();
}

void Mmc3::update_prg_banks() {
    const std::size_t inner_banks =
        mapper_ == 198U                                                    ? 128U
        : mapper_ == 513U || t9552_                                        ? 64U
        : mapper_ == 115U || (mapper_ == 197U && mapper197_profile_ == 3U) ? 32U
                                                                           : num_prg_banks_8k_;
    const std::size_t r6 = bank_registers_[6] % inner_banks;
    const std::size_t r7 = bank_registers_[7] % inner_banks;
    const std::size_t second_last = inner_banks - 2U;
    const std::size_t last = inner_banks - 1U;

    if (!prg_mode_) {
        // Mode 0: $8000=R6, $A000=R7, $C000=(-2), $E000=(-1)
        prg_offsets_[0] = static_cast<uint32_t>(r6) * 0x2000;
        prg_offsets_[1] = static_cast<uint32_t>(r7) * 0x2000;
        prg_offsets_[2] = static_cast<uint32_t>(second_last) * 0x2000;
        prg_offsets_[3] = static_cast<uint32_t>(last) * 0x2000;
    } else {
        // Mode 1: $8000=(-2), $A000=R7, $C000=R6, $E000=(-1)
        prg_offsets_[0] = static_cast<uint32_t>(second_last) * 0x2000;
        prg_offsets_[1] = static_cast<uint32_t>(r7) * 0x2000;
        prg_offsets_[2] = static_cast<uint32_t>(r6) * 0x2000;
        prg_offsets_[3] = static_cast<uint32_t>(last) * 0x2000;
    }
    if (mapper_ == 198U)
        for (auto& offset : prg_offsets_)
            offset = (offset & 0x80000U) != 0U ? 0x80000U | (offset & 0x1FFFFU) : offset & 0x7FFFFU;
    if (t9552_)
        for (auto& offset : prg_offsets_)
            offset = t9552_address(offset, false) % prg_rom_.size();
}

uint32_t Mmc3::t9552_address(uint32_t addr, bool chr) const {
    // Each column in the published table lists the MMC3 signal at each
    // physical ROM pin. Column 0 is mapper 249's file order, column 2 is 4:5.
    constexpr std::array<std::array<unsigned, 4>, 4> kPrgPins = {
        {{16U, 17U, 15U, 14U}, {17U, 16U, 14U, 15U}, {14U, 15U, 16U, 17U}, {15U, 14U, 17U, 16U}}};
    constexpr std::array<std::array<unsigned, 6>, 8> kChrPins = {{{15U, 12U, 16U, 17U, 14U, 13U},
                                                                  {14U, 15U, 13U, 12U, 17U, 16U},
                                                                  {12U, 13U, 14U, 15U, 16U, 17U},
                                                                  {16U, 14U, 12U, 13U, 17U, 15U},
                                                                  {15U, 13U, 17U, 16U, 12U, 14U},
                                                                  {14U, 12U, 15U, 16U, 17U, 13U},
                                                                  {13U, 16U, 14U, 15U, 12U, 17U},
                                                                  {12U, 15U, 16U, 17U, 13U, 14U}}};
    const unsigned order = mapper_ == 249U ? 0U : 2U;
    uint32_t result = addr & ~(chr ? 0x3F000U : 0x3C000U);
    if (chr) {
        for (unsigned pin = 0U; pin < 6U; ++pin)
            result |= ((addr >> kChrPins[t9552_mode_][pin]) & 1U) << kChrPins[order][pin];
    } else {
        for (unsigned pin = 0U; pin < 4U; ++pin)
            result |= ((addr >> kPrgPins[t9552_mode_ & 3U][pin]) & 1U) << kPrgPins[order][pin];
    }
    return result;
}

void Mmc3::update_chr_banks() {
    if (use_chr_ram_)
        return;

    auto bank = [this](uint8_t reg) -> uint32_t {
        if (t9552_)
            return t9552_address(static_cast<uint32_t>(reg) * 0x400U, true) % chr_rom_.size();
        return static_cast<uint32_t>(reg % num_chr_banks_1k_) * 0x0400;
    };

    if (!chr_inversion_) {
        // R0: 2 KB at $0000, R1: 2 KB at $0800
        // R2-R5: 1 KB at $1000-$1FFF
        chr_offsets_[0] = bank(bank_registers_[0] & 0xFE);
        chr_offsets_[1] = bank(bank_registers_[0] | 0x01);
        chr_offsets_[2] = bank(bank_registers_[1] & 0xFE);
        chr_offsets_[3] = bank(bank_registers_[1] | 0x01);
        chr_offsets_[4] = bank(bank_registers_[2]);
        chr_offsets_[5] = bank(bank_registers_[3]);
        chr_offsets_[6] = bank(bank_registers_[4]);
        chr_offsets_[7] = bank(bank_registers_[5]);
    } else {
        // Inverted: R2-R5 at $0000-$0FFF, R0-R1 at $1000-$1FFF
        chr_offsets_[0] = bank(bank_registers_[2]);
        chr_offsets_[1] = bank(bank_registers_[3]);
        chr_offsets_[2] = bank(bank_registers_[4]);
        chr_offsets_[3] = bank(bank_registers_[5]);
        chr_offsets_[4] = bank(bank_registers_[0] & 0xFE);
        chr_offsets_[5] = bank(bank_registers_[0] | 0x01);
        chr_offsets_[6] = bank(bank_registers_[1] & 0xFE);
        chr_offsets_[7] = bank(bank_registers_[1] | 0x01);
    }
}

Byte Mmc3::read_prg(Address addr) {
    if (mapper_ == 115U && (addr & 0xE003U) == 0x6002U)
        return kasheng_solder_pad_;
    if (mapper_ == 262U && addr >= 0x6000U && maps_expansion(addr))
        return read_expansion(addr);
    if (addr >= 0x6000 && addr < 0x8000) {
        if (!prg_ram_enabled_ || prg_ram_size_ == 0U) {
            return 0;
        }
        return prg_ram_[(addr - 0x6000) % prg_ram_size_];
    }
    if (addr < 0x8000)
        return 0;

    uint8_t bank_index = (addr - 0x8000) / 0x2000;
    uint32_t offset = prg_offsets_[bank_index] + ((addr - 0x8000) % 0x2000);
    if (mapper_ == 114U || mapper_ == 115U || mapper_ == 123U) {
        const unsigned outer =
            mapper_ == 115U ? (static_cast<unsigned>(kasheng_prg_) & 0x40U) << 12U : 0U;
        if ((kasheng_prg_ & 0x80U) != 0U) {
            const bool nrom256 = (kasheng_prg_ & 0x20U) != 0U;
            offset = outer |
                     ((static_cast<unsigned>(kasheng_prg_) & (nrom256 ? 14U : 15U)) << 14U) |
                     (addr & (nrom256 ? 0x7FFFU : 0x3FFFU));
        } else if (mapper_ == 115U) {
            offset = (offset & 0x3FFFFU) | outer;
        }
    }
    if (mapper_ == 250U && nitra_prg_mux_ && addr >= 0xC000U)
        offset = (offset & ~0x2000U) | (addr & 0x2000U);
    if (mapper_ == 196U && (mrcm_prg_ & 0x80U) != 0U)
        offset = (static_cast<unsigned>(mrcm_prg_ & 0x0FU) << 15U) | (addr & 0x7FFFU);
    if (mapper_ == 197U && mapper197_profile_ == 3U && (mapper197_prg_ & 8U) != 0U)
        offset = (offset & ~0x20000U) | (static_cast<unsigned>(mapper197_prg_ & 1U) << 17U);
    if (mapper_ == 513U && bank_index != 3U && bank_index != (prg_mode_ ? 0U : 2U)) {
        unsigned outer =
            static_cast<unsigned>(chr_bank(static_cast<Address>(ppu_chr_slot_ * 0x400U))) >> 6U;
        if (prg_rom_.size() == 0x180000U && outer == 3U)
            outer = 2U;
        offset += outer * 0x80000U;
    }
    // Modulo (not a power-of-two mask): bank offsets are modulo-bounded at
    // write time, but a malformed save-state can inject raw offsets, and
    // non-power-of-two ROM sizes must wrap correctly.
    return prg_rom_.empty() ? 0 : prg_rom_[offset % prg_rom_.size()];
}

void Mmc3::write_prg(Address addr, Byte value) {
    if (mapper_ == 197U && mapper197_profile_ == 3U && (addr & 0xE000U) == 0x6000U) {
        if (prg_ram_enabled_ && !prg_ram_write_protected_)
            mapper197_prg_ = value & 9U;
        return;
    }
    if (mapper_ == 196U && (addr & 0xE000U) == 0x6000U &&
        (mrcm_profile_ == 0U || mrcm_profile_ == 3U)) {
        // The documented compatibility decoder is independent of A001 gates.
        mrcm_prg_ = static_cast<Byte>(0x80U | ((value | (value >> 4U)) & 0x0FU));
        return;
    }
    if ((mapper_ == 114U || mapper_ == 115U) && (addr & 0xE000U) == 0x6000U) {
        const unsigned reg = addr & (mapper_ == 114U ? 1U : 3U);
        if (reg == 0U)
            kasheng_prg_ = value & (mapper_ == 114U ? 0xAFU : 0xEFU);
        else if (reg == 1U)
            kasheng_chr_ = value & 1U;
    }
    // The tentative decoder also reaches $6100 mode aliases. Mapper 512's
    // WRAM uses an independent decoder, so those writes can update both.
    if ((mapper_ == 262U || mapper_ == 512U) && addr < 0x8000U && maps_expansion(addr))
        write_expansion(addr, value);
    if (addr >= 0x6000 && addr < 0x8000) {
        if (prg_ram_enabled_ && !prg_ram_write_protected_ && prg_ram_size_ != 0U) {
            prg_ram_[(addr - 0x6000) % prg_ram_size_] = value;
        }
        return;
    }
    if (addr < 0x8000)
        return;

    if (mapper_ == 196U) {
        const bool data = mrcm_profile_ == 0U   ? (addr & 1U) == 0U && (addr & 0x0EU) != 0U
                          : mrcm_profile_ == 2U ? (addr & 4U) != 0U
                                                : (addr & 2U) != 0U;
        addr = static_cast<Address>((addr & 0xE000U) | (data ? 1U : 0U));
    } else if (mapper_ == 114U) {
        constexpr std::array<std::array<Address, 8>, 2> kPorts = {
            {{0xA001U, 0xA000U, 0x8000U, 0xC000U, 0x8001U, 0xC001U, 0xE000U, 0xE001U},
             {0xA001U, 0x8001U, 0x8000U, 0xC001U, 0xA000U, 0xC000U, 0xE000U, 0xE001U}}};
        const unsigned slot = ((static_cast<unsigned>(addr) >> 12U) & 6U) | (addr & 1U);
        addr = kPorts[scramble_profile_][slot];
    } else if (mapper_ == 250U) {
        value = static_cast<Byte>(addr & 0xFFU);
        addr = static_cast<Address>((addr & 0xE000U) | ((addr >> 10U) & 1U));
    } else if (mapper_ == 263U) {
        addr = static_cast<Address>((addr & 0xE000U) | ((addr >> 12U) & 1U));
        const auto data = static_cast<unsigned>(value);
        value = static_cast<Byte>((data & 0xD8U) | ((data & 0x20U) >> 4U) | ((data & 4U) << 3U) |
                                  ((data & 2U) >> 1U) | ((data & 1U) << 2U));
    }

    if ((mapper_ == 114U || mapper_ == 123U) && (addr & 0xE001U) == 0x8000U) {
        constexpr std::array<std::array<Byte, 8>, 2> kIndices = {
            {{0U, 3U, 1U, 5U, 6U, 7U, 2U, 4U}, {0U, 2U, 5U, 3U, 6U, 1U, 7U, 4U}}};
        value = static_cast<Byte>((value & 0xC0U) | kIndices[scramble_profile_][value & 7U]);
    }

    bool even = (addr & 0x01) == 0;

    if (addr < 0xA000) {
        if (even) {
            // $8000: Bank select
            bank_select_ = value & 0x07;
            prg_mode_ = (value & 0x40) != 0;
            chr_inversion_ = (value & 0x80) != 0;
            update_prg_banks();
            update_chr_banks();
        } else {
            // $8001: Bank data
            const auto prg_mask = mapper_ == 198U ? 0x7FU : 0x3FU;
            bank_registers_[bank_select_] =
                (mapper_ != 4U || t9552_) && bank_select_ >= 6U ? value & prg_mask : value;
            if (bank_select_ < 6) {
                update_chr_banks();
            } else {
                update_prg_banks();
            }
        }
    } else if (addr < 0xC000) {
        if (even) {
            // $A000: Mirroring
            if (mapper_ != 118U && mapper_ != 262U)
                mirror_mode_ = (value & 0x01) ? MirrorMode::Horizontal : MirrorMode::Vertical;
        } else {
            prg_ram_enabled_ = mapper_ == 512U || (value & 0x80U) != 0U;
            prg_ram_write_protected_ = (value & 0x40) != 0;
        }
    } else if (addr < 0xE000) {
        if (even) {
            // $C000: IRQ latch
            irq_reload_ = value;
        } else {
            // $C001: IRQ reload
            irq_reload_flag_ = true;
            irq_counter_ = 0;
        }
    } else {
        if (even) {
            // $E000: IRQ disable + acknowledge
            irq_enabled_ = false;
            irq_pending_ = false;
        } else {
            // $E001: IRQ enable
            irq_enabled_ = true;
        }
    }
}

bool Mmc3::maps_prg(Address addr) const {
    if (mapper_ == 115U && (addr & 0xE003U) == 0x6002U)
        return true;
    if (addr >= 0x8000) {
        return true;
    }
    if (mapper_ == 262U && addr >= 0x6000U && maps_expansion(addr))
        return true;
    return addr >= 0x6000 && addr < 0x8000 && prg_ram_enabled_ && prg_ram_size_ != 0U;
}

Byte Mmc3::prg_read_mask(Address addr) const {
    if (mapper_ == 115U && (addr & 0xE003U) == 0x6002U)
        return 7U;
    return mapper_ == 262U && addr >= 0x6000U && maps_expansion(addr) ? 0x80U : 0xFFU;
}

void Mmc3::clock_irq_counter() {
    bool was_forced = irq_reload_flag_;
    uint8_t old_counter = irq_counter_;

    if (irq_counter_ == 0 || irq_reload_flag_) {
        irq_counter_ = irq_reload_;
        irq_reload_flag_ = false;
    } else {
        --irq_counter_;
    }

    if (irq_counter_ == 0 && irq_enabled_ &&
        (irq_zero_latch_repeats_ || old_counter > 0 || was_forced)) {
        irq_pending_ = true;
    }
}

Byte Mmc3::read_chr(Address addr) {
    if (mapper_ == 198U)
        return chr_ram_[(static_cast<unsigned>(chr_bank(addr)) & 7U) * 0x400U + (addr & 0x3FFU)];
    if (mapper_ == 197U) {
        const auto pa11 = mapper197_profile_ == 1U   ? 0x800U
                          : mapper197_profile_ == 2U ? addr & 0x800U
                                                     : 0U;
        const auto pins = static_cast<Address>((addr & 0x1000U) | ((addr & 0x800U) >> 1U) | pa11);
        return chr_rom_[(static_cast<unsigned>(chr_bank(pins)) * 0x800U + (addr & 0x7FFU)) %
                        chr_rom_.size()];
    }
    if (mapper_ == 114U || mapper_ == 115U)
        return chr_rom_[(static_cast<unsigned>(kasheng_chr_) * 0x40000U +
                         static_cast<unsigned>(chr_bank(addr)) * 0x400U + (addr & 0x3FFU)) %
                        chr_rom_.size()];
    if (mapper_ == 262U) {
        const auto bank = static_cast<unsigned>(chr_bank(addr));
        if ((board_mode_ & 0x40U) != 0U)
            return chr_ram_[(bank & 7U) * 0x400U + (addr & 0x3FFU)];
        constexpr std::array<unsigned, 4> kOuterBits = {3U, 2U, 0U, 1U};
        const unsigned outer = (board_mode_ >> kOuterBits[(addr >> 11U) & 3U]) & 1U;
        return chr_rom_[((bank | (outer << 8U)) * 0x400U + (addr & 0x3FFU)) % chr_rom_.size()];
    }
    if (mapper_ == 513U)
        return chr_nvram_[(static_cast<unsigned>(chr_bank(addr)) & 31U) * 0x400U + (addr & 0x3FFU)];
    if (mapper_ == 512U && (board_mode_ & 2U) != 0U)
        return chr_ram_[(static_cast<unsigned>(chr_bank(addr)) * 0x400U + (addr & 0x3FFU)) &
                        0xFFFU];
    if (mapper_ == 119U) {
        const auto bank = static_cast<unsigned>(chr_bank(addr));
        if ((bank & 0x40U) != 0U)
            return chr_ram_[(bank & 7U) * 0x400U + (addr & 0x3FFU)];
        // A16 selects the chip. For the documented 128 KiB extension, A17
        // supplies ROM A16; original 16/32/64 KiB chips ignore this bit.
        const auto rom_bank = (bank & 0x3FU) | ((bank & 0x80U) >> 1U);
        return chr_rom_[(rom_bank * 0x400U + (addr & 0x3FFU)) % chr_rom_.size()];
    }
    if (mixed_chr_ram_size_ != 0U)
        if (const auto offset = mixed_chr_ram_offset(addr))
            return chr_ram_[*offset];
    if (mapper_ == 195U)
        if (const auto offset = fs303_chr_ram_offset(addr))
            return chr_ram_[*offset];
    if (use_chr_ram_) {
        return chr_ram_[addr % 0x2000];
    }
    uint8_t bank_index = (addr / 0x0400) & 0x07;
    uint32_t offset = chr_offsets_[bank_index] + (addr % 0x0400);
    // Modulo (not a power-of-two mask): see read_prg.
    return chr_rom_[offset % chr_rom_.size()];
}

void Mmc3::write_chr(Address addr, Byte value) {
    if (mapper_ == 198U) {
        chr_ram_[(static_cast<unsigned>(chr_bank(addr)) & 7U) * 0x400U + (addr & 0x3FFU)] = value;
    } else if (mapper_ == 262U) {
        if ((board_mode_ & 0x40U) != 0U)
            chr_ram_[(static_cast<unsigned>(chr_bank(addr)) & 7U) * 0x400U + (addr & 0x3FFU)] =
                value;
    } else if (mapper_ == 513U) {
        chr_nvram_[(static_cast<unsigned>(chr_bank(addr)) & 31U) * 0x400U + (addr & 0x3FFU)] =
            value;
    } else if (mapper_ == 512U) {
        if ((board_mode_ & 2U) != 0U)
            chr_ram_[(static_cast<unsigned>(chr_bank(addr)) * 0x400U + (addr & 0x3FFU)) & 0xFFFU] =
                value;
    } else if (mapper_ == 119U) {
        const auto bank = static_cast<unsigned>(chr_bank(addr));
        if ((bank & 0x40U) != 0U)
            chr_ram_[(bank & 7U) * 0x400U + (addr & 0x3FFU)] = value;
    } else if (mapper_ == 195U) {
        const auto bank = chr_bank(addr);
        if ((bank & 0x80U) != 0U)
            fs303_chr_mode_ = bank & 0xDAU;
        else if (const auto offset = fs303_chr_ram_offset(addr))
            chr_ram_[*offset] = value;
    } else if (mixed_chr_ram_size_ != 0U) {
        if (const auto offset = mixed_chr_ram_offset(addr))
            chr_ram_[*offset] = value;
    } else if (use_chr_ram_) {
        chr_ram_[addr % 0x2000] = value;
    }
}

MirrorMode Mmc3::mirror_mode() const {
    if (mapper_ == 512U && board_mode_ == 1U)
        return MirrorMode::FourScreen;
    if (mapper_ == 118U) {
        const auto a = chr_bank(0U) & 0x80U;
        const auto b = chr_bank(0x400U) & 0x80U;
        const auto c = chr_bank(0x800U) & 0x80U;
        const auto d = chr_bank(0xC00U) & 0x80U;
        if (a == b && b == c && c == d)
            return a != 0U ? MirrorMode::SingleUpper : MirrorMode::SingleLower;
        if (a == b && c == d)
            return MirrorMode::Horizontal;
        // Arbitrary or reversed arrangements are represented exactly by
        // nametable_address(); MirrorMode alone cannot describe all 16.
        return MirrorMode::Vertical;
    }
    return mirror_mode_;
}

std::optional<std::size_t> Mmc3::mixed_chr_ram_offset(Address addr) const {
    const unsigned bank = chr_bank(addr);
    if (mapper_ == 194U && use_chr_ram_)
        return (bank * 0x400U + (addr & 0x3FFU)) % mixed_chr_ram_size_;
    const unsigned first = mapper_ == 194U ? 0U : 8U;
    if (bank < first || bank - first >= mixed_chr_ram_size_ / 0x400U)
        return std::nullopt;
    return (bank - first) * 0x400U + (addr & 0x3FFU);
}

uint16_t Mmc3::mixed_chr_state_tag() const {
    // Keep the historical 74/192 size marker. New 194 payloads distinguish
    // bank-zero wiring (bit 15) and RAM-only mirroring (bit 14).
    const unsigned flags = mapper_ == 194U ? 0x8000U | (use_chr_ram_ ? 0x4000U : 0U) : 0U;
    return static_cast<uint16_t>(mixed_chr_ram_size_ | flags);
}

std::optional<std::size_t> Mmc3::fs303_chr_ram_offset(Address addr) const {
    if ((fs303_chr_mode_ & 0x10U) != 0U)
        return std::nullopt;
    constexpr std::array<unsigned, 8> kFirst = {
        0x28U, 0U, 0x4CU, 0x64U, 0x46U, 0x7CU, 0x0AU, 0x100U};
    const unsigned index = ((fs303_chr_mode_ & 0x40U) >> 4U) | ((fs303_chr_mode_ & 8U) >> 2U) |
                           ((fs303_chr_mode_ & 2U) >> 1U);
    const unsigned bank = chr_bank(addr);
    const unsigned count = (fs303_chr_mode_ & 0x40U) != 0U ? 2U : 4U;
    if (bank < kFirst[index] || bank - kFirst[index] >= count)
        return std::nullopt;
    // The 32 KiB chip has only eight connected KiB pages; windows share pages.
    return (bank & 7U) * 0x400U + (addr & 0x3FFU);
}

uint16_t Mmc3::fs303_state_tag() const {
    return static_cast<uint16_t>(0xC300U | (fs303_aux_present_ ? 1U : 0U) |
                                 (prg_ram_size_ != 0U ? 2U : 0U) | (nvram_size_ != 0U ? 4U : 0U));
}

Byte Mmc3::chr_bank(Address addr) const {
    const auto slot = ((static_cast<unsigned>(addr) >> 10U) & 7U) ^ (chr_inversion_ ? 4U : 0U);
    if (slot < 4U)
        return static_cast<Byte>((bank_registers_[slot / 2U] & 0xFEU) | (slot & 1U));
    return bank_registers_[slot - 2U];
}

std::optional<Address> Mmc3::nametable_address(Address addr) const {
    if (mapper_ != 118U)
        return std::nullopt;
    // CHR decoding ignores PPU A13; A17 reaches CIRAM before ROM wrapping.
    return static_cast<Address>(((chr_bank(addr & 0xFFFU) & 0x80U) << 3U) | (addr & 0x3FFU));
}

bool Mmc3::maps_expansion(Address addr) const {
    if (mapper_ == 198U)
        return (addr & 0xF000U) == 0x5000U;
    if (mapper_ == 195U)
        return fs303_aux_present_ && (addr & 0xF000U) == 0x5000U;
    if (t9552_)
        return (addr & 0xF000U) == 0x5000U;
    if (mapper_ == 123U)
        return (addr & 0xF800U) == 0x5800U;
    return (mapper_ == 262U || mapper_ == 512U) && (addr & 0xC100U) == 0x4100U;
}

Byte Mmc3::expansion_read_mask(Address /*addr*/) const {
    if (mapper_ == 262U)
        return 0x80U;
    return mapper_ == 512U || mapper_ == 123U || t9552_ ? 0U : 0xFFU; // Write-only latches.
}

Byte Mmc3::read_expansion(Address addr) {
    if (mapper_ == 198U && maps_expansion(addr))
        return mapper198_aux_ram_[addr & 0xFFFU];
    if (mapper_ == 195U && maps_expansion(addr))
        return fs303_aux_ram_[addr & 0xFFFU];
    return mapper_ == 262U && maps_expansion(addr) ? static_cast<Byte>(title_jumper_ << 7U) : 0U;
}

void Mmc3::write_expansion(Address addr, Byte value) {
    if (!maps_expansion(addr))
        return;
    if (mapper_ == 198U) {
        mapper198_aux_ram_[addr & 0xFFFU] = value;
    } else if (mapper_ == 195U) {
        fs303_aux_ram_[addr & 0xFFFU] = value;
    } else if (t9552_) {
        t9552_mode_ = value & 7U;
        update_prg_banks();
        update_chr_banks();
    } else if (mapper_ == 123U) {
        // Normalize .M31 .2S0 to the shared M.S.3210 PRG latch.
        const unsigned data = value;
        kasheng_prg_ = static_cast<Byte>(((data & 0x40U) << 1U) | ((data & 2U) << 4U) |
                                         ((data & 0x20U) >> 2U) | (data & 4U) |
                                         ((data & 0x10U) >> 3U) | (data & 1U));
    } else
        board_mode_ = value & (mapper_ == 262U ? 0x4FU : 3U);
}

Result<void> Mmc3::configure_board_jumpers(Byte value) {
    if (mapper_ == 115U) {
        if (value > 7U)
            return std::unexpected("Mapper 115 solder pads accept values 0 through 7");
        kasheng_solder_pad_ = value;
        return {};
    }
    if (mapper_ != 262U || value > 1U)
        return std::unexpected("Only mapper 262 exposes a 0/1 title jumper");
    title_jumper_ = value;
    return {};
}

Result<void> Mmc3::configure_board_profile(std::string_view profile) {
    if (mapper_ != 250U || (profile != "nitra-l401" && profile != "nitra-queen-bee-v"))
        return std::unexpected(
            "Mapper 250 supports nitra-l401 and nitra-queen-bee-v board profiles");
    nitra_prg_mux_ = profile == "nitra-queen-bee-v";
    return {};
}

bool Mmc3::observes_ppu_address() const {
    return mapper_ == 513U;
}

void Mmc3::observe_ppu_address(Address addr) {
    // Keep actual address pins separate from cached renderer CHR lookups.
    // Track the slot rather than the resulting bank: CHR-register/inversion
    // writes then change the outer PRG pins immediately without a stale latch.
    if (mapper_ == 513U)
        ppu_chr_slot_ = static_cast<Byte>((static_cast<unsigned>(addr) >> 10U) & 7U);
}

std::optional<Byte> Mmc3::read_nametable(Address addr) const {
    if (mapper_ == 262U)
        return nametable_ram_[addr & 0xFFFU];
    if (mapper_ != 512U || board_mode_ != 1U)
        return std::nullopt;
    return chr_ram_[0x1000U + (addr & 0xFFFU)];
}

bool Mmc3::write_nametable(Address addr, Byte value) {
    if (mapper_ == 262U) {
        nametable_ram_[addr & 0xFFFU] = value;
        return true;
    }
    if (mapper_ != 512U || board_mode_ != 1U)
        return false;
    chr_ram_[0x1000U + (addr & 0xFFFU)] = value;
    return true;
}

// New board payloads serialize registers, never derived offsets or immutable
// IRQ profiles. Keep mapper 4's historical payload in mapper_state.cpp intact.
void Mmc3::save_variant_state(StateWriter& writer) const {
    if (mapper_ == 513U) {
        writer.write_bytes(chr_nvram_);
    } else {
        writer.write_array(prg_ram_);
        writer.write_array(chr_ram_);
    }
    writer.write(bank_select_);
    writer.write<Byte>((prg_mode_ ? 0x40U : 0U) | (chr_inversion_ ? 0x80U : 0U));
    writer.write_array(bank_registers_);
    writer.write<Byte>(static_cast<Byte>(mirror_mode_));
    writer.write(irq_counter_);
    writer.write(irq_reload_);
    writer.write<Byte>((irq_reload_flag_ ? 1U : 0U) | (irq_enabled_ ? 2U : 0U) |
                       (irq_pending_ ? 4U : 0U));
    writer.write<Byte>((prg_ram_enabled_ ? 0x80U : 0U) | (prg_ram_write_protected_ ? 0x40U : 0U));
    if (mapper_ == 114U) {
        writer.write(kasheng_prg_);
        writer.write(kasheng_chr_);
        writer.write(scramble_profile_);
    } else if (mapper_ == 115U) {
        writer.write(kasheng_prg_);
        writer.write(kasheng_chr_);
        writer.write(kasheng_solder_pad_);
    } else if (mapper_ == 123U) {
        writer.write(kasheng_prg_);
    }
    if (mixed_chr_ram_size_ != 0U)
        writer.write(mixed_chr_state_tag());
    if (mapper_ == 195U) {
        writer.write(fs303_state_tag());
        writer.write(fs303_chr_mode_);
        writer.write_bytes(fs303_aux_ram_);
    }
    if (mapper_ == 196U) {
        writer.write<uint16_t>(static_cast<uint16_t>(0xC400U | mrcm_profile_));
        writer.write(mrcm_prg_);
    }
    if (mapper_ == 197U) {
        writer.write<uint16_t>(static_cast<uint16_t>(0xC500U | mapper197_profile_));
        writer.write(mapper197_prg_);
    }
    if (mapper_ == 198U) {
        writer.write<uint16_t>(0xC600U);
        writer.write_bytes(mapper198_aux_ram_);
    }
    if (t9552_) {
        writer.write(t9552_mode_);
        writer.write<Byte>(mapper_ == 249U ? 0U : 2U);
    }
    if (mapper_ == 250U)
        writer.write<Byte>(nitra_prg_mux_ ? 1U : 0U);
    if (mapper_ == 512U)
        writer.write(board_mode_);
    if (mapper_ == 513U)
        writer.write(ppu_chr_slot_);
    if (mapper_ == 262U) {
        writer.write_bytes(nametable_ram_);
        writer.write(board_mode_);
        writer.write(title_jumper_);
    }
}

void Mmc3::load_variant_state(StateReader& reader) {
    if (mapper_ == 513U) {
        reader.read_bytes(chr_nvram_, 0x8000U);
        if (chr_nvram_.size() != 0x8000U) {
            chr_nvram_.resize(0x8000U);
            reader.fail();
        }
    } else {
        reader.read_array(prg_ram_);
        reader.read_array(chr_ram_);
    }
    reader.read(bank_select_);
    const auto mode = reader.read<Byte>();
    reader.read_array(bank_registers_);
    const auto mirror = reader.read<Byte>();
    reader.read(irq_counter_);
    reader.read(irq_reload_);
    const auto irq = reader.read<Byte>();
    const auto ram = reader.read<Byte>();
    if (mapper_ == 114U) {
        reader.read(kasheng_prg_);
        reader.read(kasheng_chr_);
    } else if (mapper_ == 115U) {
        reader.read(kasheng_prg_);
        reader.read(kasheng_chr_);
        reader.read(kasheng_solder_pad_);
    } else if (mapper_ == 123U) {
        reader.read(kasheng_prg_);
    }
    const auto mixed_chr_tag = mixed_chr_ram_size_ != 0U ? reader.read<uint16_t>() : 0U;
    const auto fs303_tag = mapper_ == 195U ? reader.read<uint16_t>() : 0U;
    if (mapper_ == 195U) {
        reader.read(fs303_chr_mode_);
        const auto size = fs303_aux_present_ ? 0x1000U : 0U;
        reader.read_bytes(fs303_aux_ram_, size);
        if (fs303_aux_ram_.size() != size) {
            fs303_aux_ram_.resize(size);
            reader.fail();
        }
    }
    const auto mrcm_tag = mapper_ == 196U ? reader.read<uint16_t>() : 0U;
    if (mapper_ == 196U)
        reader.read(mrcm_prg_);
    const auto mapper197_tag = mapper_ == 197U ? reader.read<uint16_t>() : 0U;
    if (mapper_ == 197U)
        reader.read(mapper197_prg_);
    const auto mapper198_tag = mapper_ == 198U ? reader.read<uint16_t>() : 0U;
    if (mapper_ == 198U) {
        reader.read_bytes(mapper198_aux_ram_, 0x1000U);
        if (mapper198_aux_ram_.size() != 0x1000U) {
            mapper198_aux_ram_.resize(0x1000U);
            reader.fail();
        }
    }
    if (t9552_)
        reader.read(t9552_mode_);
    const auto t9552_order = t9552_ ? reader.read<Byte>() : Byte{0U};
    const auto scramble = mapper_ == 114U ? reader.read<Byte>() : Byte{0U};
    const auto nitra_profile = mapper_ == 250U ? reader.read<Byte>() : Byte{0U};
    if (mapper_ == 512U)
        reader.read(board_mode_);
    if (mapper_ == 513U)
        reader.read(ppu_chr_slot_);
    if (mapper_ == 262U) {
        reader.read_bytes(nametable_ram_, 0x1000U);
        if (nametable_ram_.size() != 0x1000U) {
            nametable_ram_.resize(0x1000U);
            reader.fail();
        }
        reader.read(board_mode_);
        reader.read(title_jumper_);
    }
    const bool valid_mirror = mapper_ == 262U ? mirror == static_cast<Byte>(MirrorMode::FourScreen)
                                              : mirror <= static_cast<Byte>(MirrorMode::Vertical);
    const auto prg_mask = mapper_ == 198U ? 0x7FU : 0x3FU;
    if (!reader.ok() || mixed_chr_tag != mixed_chr_state_tag() || bank_select_ > 7U ||
        (mapper_ == 198U && mapper198_tag != 0xC600U) ||
        (mapper_ == 195U &&
         (fs303_tag != fs303_state_tag() || (fs303_chr_mode_ & 0xA5U) != 0x80U)) ||
        (mapper_ == 197U &&
         (mapper197_tag != (0xC500U | mapper197_profile_) || (mapper197_prg_ & 0xF6U) != 0U ||
          (mapper197_profile_ != 3U && mapper197_prg_ != 0U))) ||
        (mapper_ == 196U &&
         (mrcm_tag != (0xC400U | mrcm_profile_) || (mrcm_prg_ & 0x70U) != 0U ||
          (mrcm_profile_ == 0U && mrcm_prg_ != 0U && (mrcm_prg_ & 0x80U) == 0U) ||
          ((mrcm_profile_ == 1U || mrcm_profile_ == 2U) && mrcm_prg_ != 0U) ||
          (mrcm_profile_ == 3U && (mrcm_prg_ & 0x80U) == 0U))) ||
        (mode & 0x3FU) != 0U || bank_registers_[6] > prg_mask || bank_registers_[7] > prg_mask ||
        !valid_mirror || (mapper_ == 118U && mirror != static_cast<Byte>(initial_mirror_)) ||
        irq > 7U || ((irq & 4U) != 0U && (irq & 2U) == 0U) || (ram & 0x3FU) != 0U ||
        (mapper_ == 512U && (board_mode_ > 3U || (ram & 0x80U) == 0U)) ||
        (mapper_ == 513U && ppu_chr_slot_ > 7U) ||
        (mapper_ == 114U &&
         ((kasheng_prg_ & 0x50U) != 0U || kasheng_chr_ > 1U || scramble != scramble_profile_)) ||
        (mapper_ == 115U &&
         ((kasheng_prg_ & 0x10U) != 0U || kasheng_chr_ > 1U || kasheng_solder_pad_ > 7U)) ||
        (mapper_ == 123U && (kasheng_prg_ & 0x50U) != 0U) ||
        (t9552_ && (t9552_mode_ > 7U || t9552_order != (mapper_ == 249U ? 0U : 2U))) ||
        (mapper_ == 250U && nitra_profile != (nitra_prg_mux_ ? 1U : 0U)) ||
        (mapper_ == 262U && ((board_mode_ & 0xB0U) != 0U || title_jumper_ > 1U))) {
        if (mapper_ == 262U && title_jumper_ > 1U)
            title_jumper_ = 0U;
        if (mapper_ == 115U && kasheng_solder_pad_ > 7U)
            kasheng_solder_pad_ = 0U;
        reset();
        reader.fail();
        return;
    }
    prg_mode_ = (mode & 0x40U) != 0U;
    chr_inversion_ = (mode & 0x80U) != 0U;
    mirror_mode_ = static_cast<MirrorMode>(mirror);
    irq_reload_flag_ = (irq & 1U) != 0U;
    irq_enabled_ = (irq & 2U) != 0U;
    irq_pending_ = (irq & 4U) != 0U;
    prg_ram_enabled_ = (ram & 0x80U) != 0U;
    prg_ram_write_protected_ = (ram & 0x40U) != 0U;
    update_prg_banks();
    update_chr_banks();
}

} // namespace mapperbus::core
