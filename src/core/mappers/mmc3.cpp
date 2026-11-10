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
Mmc3::Mmc3(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), use_chr_ram_(chr_rom_.empty()),
      mapper_(header.mapper_number == 250U || header.mapper_number == 262U ||
                      header.mapper_number == 263U || header.mapper_number == 118U ||
                      header.mapper_number == 119U || header.mapper_number == 512U ||
                      header.mapper_number == 513U
                  ? header.mapper_number
                  : 4U),
      initial_mirror_(header.mirror_mode), mirror_mode_(header.mirror_mode),
      irq_zero_latch_repeats_(!(mapper_ == 4U && header.is_nes2 && header.submapper == 4)) {
    if (mapper_ == 250U) {
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
    if (mapper_ != 4U)
        mirror_mode_ = initial_mirror_;
    update_prg_banks();
    update_chr_banks();
}

void Mmc3::update_prg_banks() {
    const std::size_t inner_banks = mapper_ == 513U ? 64U : num_prg_banks_8k_;
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
}

void Mmc3::update_chr_banks() {
    if (use_chr_ram_)
        return;

    auto bank = [this](uint8_t reg) -> uint32_t {
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
    if (mapper_ == 250U && nitra_prg_mux_ && addr >= 0xC000U)
        offset = (offset & ~0x2000U) | (addr & 0x2000U);
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

    if (mapper_ == 250U) {
        value = static_cast<Byte>(addr & 0xFFU);
        addr = static_cast<Address>((addr & 0xE000U) | ((addr >> 10U) & 1U));
    } else if (mapper_ == 263U) {
        addr = static_cast<Address>((addr & 0xE000U) | ((addr >> 12U) & 1U));
        const auto data = static_cast<unsigned>(value);
        value = static_cast<Byte>((data & 0xD8U) | ((data & 0x20U) >> 4U) | ((data & 4U) << 3U) |
                                  ((data & 2U) >> 1U) | ((data & 1U) << 2U));
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
            bank_registers_[bank_select_] =
                mapper_ != 4U && bank_select_ >= 6U ? value & 0x3FU : value;
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
    if (addr >= 0x8000) {
        return true;
    }
    if (mapper_ == 262U && addr >= 0x6000U && maps_expansion(addr))
        return true;
    return addr >= 0x6000 && addr < 0x8000 && prg_ram_enabled_ && prg_ram_size_ != 0U;
}

Byte Mmc3::prg_read_mask(Address addr) const {
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
    if (use_chr_ram_) {
        return chr_ram_[addr % 0x2000];
    }
    uint8_t bank_index = (addr / 0x0400) & 0x07;
    uint32_t offset = chr_offsets_[bank_index] + (addr % 0x0400);
    // Modulo (not a power-of-two mask): see read_prg.
    return chr_rom_[offset % chr_rom_.size()];
}

void Mmc3::write_chr(Address addr, Byte value) {
    if (mapper_ == 262U) {
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
    return (mapper_ == 262U || mapper_ == 512U) && (addr & 0xC100U) == 0x4100U;
}

Byte Mmc3::expansion_read_mask(Address /*addr*/) const {
    if (mapper_ == 262U)
        return 0x80U;
    return mapper_ == 512U ? 0U : 0xFFU; // The board-mode latch is write-only.
}

Byte Mmc3::read_expansion(Address addr) {
    return mapper_ == 262U && maps_expansion(addr) ? static_cast<Byte>(title_jumper_ << 7U) : 0U;
}

void Mmc3::write_expansion(Address addr, Byte value) {
    if (maps_expansion(addr))
        board_mode_ = value & (mapper_ == 262U ? 0x4FU : 3U);
}

Result<void> Mmc3::configure_board_jumpers(Byte value) {
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
    if (!reader.ok() || bank_select_ > 7U || (mode & 0x3FU) != 0U || bank_registers_[6] > 63U ||
        bank_registers_[7] > 63U || !valid_mirror ||
        (mapper_ == 118U && mirror != static_cast<Byte>(initial_mirror_)) || irq > 7U ||
        ((irq & 4U) != 0U && (irq & 2U) == 0U) || (ram & 0x3FU) != 0U ||
        (mapper_ == 512U && (board_mode_ > 3U || (ram & 0x80U) == 0U)) ||
        (mapper_ == 513U && ppu_chr_slot_ > 7U) ||
        (mapper_ == 250U && nitra_profile != (nitra_prg_mux_ ? 1U : 0U)) ||
        (mapper_ == 262U && ((board_mode_ & 0xB0U) != 0U || title_jumper_ > 1U))) {
        if (mapper_ == 262U && title_jumper_ > 1U)
            title_jumper_ = 0U;
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
