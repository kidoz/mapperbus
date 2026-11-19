#include "core/mappers/mmc1.hpp"

namespace mapperbus::core {

Mmc1::Mmc1(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : ch501_sram_(header.mapper_number == 543U ? header.prg_nvram_size : 0U),
      ch501_chr_(header.mapper_number == 543U ? header.chr_ram_size : 0U),
      jy820845c_(header.mapper_number == 550U), board03101_(header.mapper_number == 546U),
      direct_(header.mapper_number == 111U),
      has_prg_ram_(!direct_ || !header.is_nes2 ||
                   header.prg_ram_size + header.prg_nvram_size != 0U),
      prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), use_chr_ram_(chr_rom_.empty()) {
    num_prg_banks_ = static_cast<uint8_t>(prg_rom_.size() / 0x4000);
    reset();
}

Result<void> Mmc1::validate_03101(const INesHeader& h, std::size_t prg, std::size_t chr) {
    if (!h.is_nes2 || h.mapper_number != 546 || h.submapper || h.has_trainer || prg != 524288U ||
        chr || h.chr_ram_size != 8192U || h.chr_nvram_size ||
        (h.has_battery ? (h.prg_nvram_size != 8192U || h.prg_ram_size)
                       : (h.prg_nvram_size || h.prg_ram_size != 8192U)) ||
        (h.mirror_mode != MirrorMode::Vertical && h.mirror_mode != MirrorMode::Horizontal))
        return std::unexpected(
            "03-101 requires 512 KiB PRG ROM and 8 KiB each of WRAM and pattern RAM");
    return {};
}

Result<void> Mmc1::validate_jy820845c(const INesHeader& h, std::size_t prg, std::size_t chr) {
    if (!h.is_nes2 || h.mapper_number != 550 || h.submapper || prg != 512 * 1024 ||
        chr != 128 * 1024 || h.has_trainer || h.has_battery || h.prg_ram_size != 8192 ||
        h.prg_nvram_size || h.chr_ram_size || h.chr_nvram_size ||
        (h.mirror_mode != MirrorMode::Vertical && h.mirror_mode != MirrorMode::Horizontal))
        return std::unexpected("JY820845C requires 512/128 KiB PRG/CHR ROM and 8 KiB WRAM");
    return {};
}

void Mmc1::reset() {
    outer_ = mhrom_ = 0;

    outer_bank_ = outer_shift_ = outer_count_ = 0;
    shift_register_ = 0x10;
    write_count_ = 0;
    control_ = 0x0C; // PRG mode 3 (fix last bank at $C000)
    chr_bank0_ = 0;
    chr_bank1_ = 0;
    prg_bank_ = 0;
    update_banks();
}

Byte Mmc1::read_prg(Address addr) {
    if (addr >= 0x6000 && addr < 0x8000) {
        // PRG RAM (if enabled: bit 4 of prg_bank_ clear)
        if (maps_prg(addr)) {
            if (!ch501_sram_.empty())
                return ch501_sram_[ch501_ram_offset(addr)];
            return prg_ram_[addr - 0x6000];
        }
        return 0;
    }
    if (addr < 0x8000)
        return 0;
    if (addr < 0xC000) {
        uint32_t offset = prg_offset0_ + (addr - 0x8000);
        return prg_rom_[offset % prg_rom_.size()];
    }
    uint32_t offset = prg_offset1_ + (addr - 0xC000);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mmc1::write_prg_cpu(Address addr, Byte value, bool consecutive) {
    // MMC1 filters consecutive serial-data writes, but its reset bit always
    // works and PRG RAM is not part of the serial port.
    if (board03101_ && (addr & 0xEF00U) == 0x8000U && consecutive && (value & 0x80U) == 0U) {
        outer_ = static_cast<Byte>(addr);
        update_banks();
        return;
    }
    if (jy820845c_ && addr >= 0x8000 && consecutive && (value & 0x80U) == 0U) {
        mhrom_ = value & 0x13U;
        update_banks();
        return;
    }
    if (direct_ || addr < 0x8000 || !consecutive || (value & 0x80U) != 0)
        write_prg(addr, value);
}

void Mmc1::write_prg(Address addr, Byte value) {
    if (board03101_ && (addr & 0xEF00U) == 0x8000U) {
        outer_ = static_cast<Byte>(addr);
        update_banks();
    }
    if (jy820845c_) {
        if (addr >= 0x7000 && addr < 0x8000 && (outer_ & 8U) == 0U)
            outer_ = addr & 15U;
        if (addr >= 0x8000)
            mhrom_ = value & 0x13U;
        update_banks();
    }
    if (addr >= 0x6000 && addr < 0x8000) {
        if (maps_prg(addr)) {
            if (!ch501_sram_.empty())
                ch501_sram_[ch501_ram_offset(addr)] = value;
            else
                prg_ram_[addr - 0x6000] = value;
        }
        return;
    }
    if (addr < 0x8000)
        return;

    if (direct_) {
        write_register(addr, value);
        return;
    }
    // Bit 7 set = reset shift register
    if (value & 0x80) {
        shift_register_ = 0x10;
        write_count_ = 0;
        control_ |= 0x0C; // Reset to PRG mode 3
        update_banks();
        return;
    }

    // Serial write: shift in bit 0, LSB first
    shift_register_ >>= 1;
    shift_register_ |= (value & 0x01) << 4;
    ++write_count_;

    if (write_count_ == 5) {
        write_register(addr, shift_register_);
        shift_register_ = 0x10;
        write_count_ = 0;
    }
}

std::span<Byte> Mmc1::trainer_ram() {
    if (!has_prg_ram_ || !ch501_sram_.empty())
        return {};
    return std::span{prg_ram_}.subspan(0x1000U);
}

bool Mmc1::maps_prg(Address addr) const {
    if (addr >= 0x8000) {
        return true;
    }
    return (!board03101_ || (outer_ & 16U)) && has_prg_ram_ && addr >= 0x6000 && addr < 0x8000 &&
           (prg_bank_ & 0x10U) == 0;
}

void Mmc1::write_register(Address addr, Byte value) {
    if (addr < 0xA000) {
        // Control ($8000-$9FFF)
        control_ = value & 0x1F;
    } else if (addr < 0xC000) {
        // CHR bank 0 ($A000-$BFFF)
        chr_bank0_ = value & (direct_ ? 0x3FU : 0x1FU);
    } else if (addr < 0xE000) {
        // CHR bank 1 ($C000-$DFFF)
        chr_bank1_ = value & (direct_ ? 0x3FU : 0x1FU);
    } else {
        // PRG bank ($E000-$FFFF)
        prg_bank_ = value & 0x1F;
    }
    update_banks();
}

void Mmc1::update_banks() {
    // Mirroring (control bits 0-1)
    switch (control_ & 0x03) {
    case 0:
        mirror_mode_ = MirrorMode::SingleLower;
        break;
    case 1:
        mirror_mode_ = MirrorMode::SingleUpper;
        break;
    case 2:
        mirror_mode_ = MirrorMode::Vertical;
        break;
    case 3:
        mirror_mode_ = MirrorMode::Horizontal;
        break;
    }

    // PRG banking mode (control bits 2-3)
    uint8_t prg_mode = (control_ >> 2) & 0x03;
    uint8_t prg_bank = prg_bank_ & 0x0F;

    switch (prg_mode) {
    case 0:
    case 1:
        // Mode 0/1: switch 32 KB at $8000 (ignore low bit of bank)
        prg_offset0_ = static_cast<uint32_t>((prg_bank & 0x0E) % num_prg_banks_) * 0x4000;
        prg_offset1_ = prg_offset0_ + 0x4000;
        break;
    case 2:
        // Mode 2: fix first bank at $8000, switch $C000
        prg_offset0_ = 0;
        prg_offset1_ = static_cast<uint32_t>(prg_bank % num_prg_banks_) * 0x4000;
        break;
    case 3:
        // Mode 3: switch $8000, fix last bank at $C000
        prg_offset0_ = static_cast<uint32_t>(prg_bank % num_prg_banks_) * 0x4000;
        prg_offset1_ = static_cast<uint32_t>(num_prg_banks_ - 1) * 0x4000;
        break;
    }

    if (!ch501_sram_.empty()) {
        const auto base = static_cast<uint32_t>(outer_bank_ & 7U) << 18U;
        prg_offset0_ = base | (prg_offset0_ & 0x3FFFFU);
        prg_offset1_ = base | (prg_offset1_ & 0x3FFFFU);
    }

    // CHR banking mode (control bit 4)
    if (control_ & 0x10) {
        // 4 KB mode: two independent banks
        chr_offset0_ = static_cast<uint32_t>(chr_bank0_) * 0x1000;
        chr_offset1_ = static_cast<uint32_t>(chr_bank1_) * 0x1000;
    } else {
        // 8 KB mode: chr_bank0 selects (ignore low bit)
        chr_offset0_ = static_cast<uint32_t>(chr_bank0_ & (direct_ ? 0x3EU : 0x1EU)) * 0x1000;
        chr_offset1_ = chr_offset0_ + 0x1000;
    }
    if (board03101_) {
        if (outer_ & 16U) {
            prg_offset0_ = 0x40000U | (prg_offset0_ & 0x3FFFFU);
            prg_offset1_ = 0x40000U | (prg_offset1_ & 0x3FFFFU);
        } else {
            const unsigned low = outer_ & 15U;
            prg_offset0_ = ((outer_ & 32U) ? (low & 14U) : low) * 16384U;
            prg_offset1_ = prg_offset0_ + ((outer_ & 32U) ? 16384U : 0U);
        }
    }
    if (jy820845c_) {
        const auto block = static_cast<unsigned>(outer_ & 6U);
        if (block == 6U) {
            prg_offset0_ = (block << 16U) | (prg_offset0_ & 0x1FFFFU);
            prg_offset1_ = (block << 16U) | (prg_offset1_ & 0x1FFFFU);
            chr_offset0_ = (block << 14U) | (chr_offset0_ & 0x7FFFU);
            chr_offset1_ = (block << 14U) | (chr_offset1_ & 0x7FFFU);
        } else {
            prg_offset0_ = ((outer_ & 7U) << 16U) | ((mhrom_ & 0x10U) << 11U);
            prg_offset1_ = prg_offset0_ + 0x4000U;
            chr_offset0_ = (block << 14U) | ((mhrom_ & 3U) << 13U);
            chr_offset1_ = chr_offset0_ + 0x1000U;
        }
    }
}

Byte Mmc1::read_chr(Address addr) {
    if (!ch501_chr_.empty())
        return ch501_chr_[ch501_chr_offset(addr)];
    if (use_chr_ram_) {
        return chr_ram_[addr % 0x2000];
    }
    uint32_t offset;
    if (addr < 0x1000) {
        offset = chr_offset0_ + addr;
    } else {
        offset = chr_offset1_ + (addr - 0x1000);
    }
    return chr_rom_[offset % chr_rom_.size()];
}

void Mmc1::write_chr(Address addr, Byte value) {
    if (board03101_ && (outer_ & 0x80U))
        return;
    if (!ch501_chr_.empty()) {
        ch501_chr_[ch501_chr_offset(addr)] = value;
        return;
    }
    if (use_chr_ram_) {
        chr_ram_[addr % 0x2000] = value;
    }
}

Result<void> Mmc1::validate_ch501(const INesHeader& h, std::size_t prg, std::size_t chr) {
    if (!h.is_nes2 || h.submapper != 0 || h.has_trainer || !h.has_battery ||
        (h.mirror_mode != MirrorMode::Horizontal && h.mirror_mode != MirrorMode::Vertical) ||
        prg != 2U * 1024U * 1024U || chr || h.prg_ram_size || h.prg_nvram_size != 65536U ||
        (h.chr_ram_size != 8192U && h.chr_ram_size != 32768U) || h.chr_nvram_size)
        return std::unexpected("Unsupported CH-501 memory population");
    return {};
}

void Mmc1::write_expansion(Address addr, Byte value) {
    if (ch501_sram_.empty() || (addr & 0xF000U) != 0x5000U)
        return;
    outer_shift_ |= ((value >> 3U) & 1U) << outer_count_;
    if (++outer_count_ == 4U) {
        outer_bank_ = outer_shift_;
        outer_shift_ = outer_count_ = 0;
        update_banks();
    }
}

std::size_t Mmc1::ch501_ram_offset(Address addr) const {
    const unsigned bank = (outer_bank_ & 2U)
                              ? 4U + ((outer_bank_ & 4U) >> 1U) + (outer_bank_ & 1U)
                              : ((outer_bank_ & 1U) << 1U) + ((chr_bank0_ >> 3U) & 1U);
    return bank * 8192U + (addr & 0x1FFFU);
}

std::size_t Mmc1::ch501_chr_offset(Address addr) const {
    const auto offset = (addr & 0x1000U) ? chr_offset1_ : chr_offset0_;
    return (offset + (addr & 0xFFFU)) % ch501_chr_.size();
}

MirrorMode Mmc1::mirror_mode() const {
    return mirror_mode_;
}

} // namespace mapperbus::core
