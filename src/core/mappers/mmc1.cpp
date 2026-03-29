#include "core/mappers/mmc1.hpp"

namespace mapperbus::core {

Mmc1::Mmc1([[maybe_unused]] const INesHeader& header,
           std::vector<Byte> prg_rom,
           std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), use_chr_ram_(chr_rom_.empty()) {
    num_prg_banks_ = static_cast<uint8_t>(prg_rom_.size() / 0x4000);
    reset();
}

void Mmc1::reset() {
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
        if ((prg_bank_ & 0x10) == 0) {
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

void Mmc1::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000 && addr < 0x8000) {
        if ((prg_bank_ & 0x10) == 0) {
            prg_ram_[addr - 0x6000] = value;
        }
        return;
    }
    if (addr < 0x8000)
        return;

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

void Mmc1::write_register(Address addr, Byte value) {
    if (addr < 0xA000) {
        // Control ($8000-$9FFF)
        control_ = value & 0x1F;
    } else if (addr < 0xC000) {
        // CHR bank 0 ($A000-$BFFF)
        chr_bank0_ = value & 0x1F;
    } else if (addr < 0xE000) {
        // CHR bank 1 ($C000-$DFFF)
        chr_bank1_ = value & 0x1F;
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

    // CHR banking mode (control bit 4)
    if (control_ & 0x10) {
        // 4 KB mode: two independent banks
        chr_offset0_ = static_cast<uint32_t>(chr_bank0_) * 0x1000;
        chr_offset1_ = static_cast<uint32_t>(chr_bank1_) * 0x1000;
    } else {
        // 8 KB mode: chr_bank0 selects (ignore low bit)
        chr_offset0_ = static_cast<uint32_t>(chr_bank0_ & 0x1E) * 0x1000;
        chr_offset1_ = chr_offset0_ + 0x1000;
    }
}

Byte Mmc1::read_chr(Address addr) {
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
    if (use_chr_ram_) {
        chr_ram_[addr % 0x2000] = value;
    }
}

MirrorMode Mmc1::mirror_mode() const {
    return mirror_mode_;
}

} // namespace mapperbus::core
