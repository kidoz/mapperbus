#include "core/mappers/mmc2.hpp"

namespace mapperbus::core {

Mmc2::Mmc2(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), use_chr_ram_(chr_rom_.empty()),
      mirror_mode_(header.mirror_mode) {
    num_prg_banks_8k_ = static_cast<uint8_t>(prg_rom_.size() / 0x2000);
    prg_rom_mask_ = prg_rom_.size() - 1;
    chr_rom_mask_ = chr_rom_.empty() ? 0 : chr_rom_.size() - 1;
    reset();
}

void Mmc2::reset() {
    prg_bank_ = 0;
    chr_banks_.fill(0);
    // Latches power up in the FD state (selected page 0).
    latch_.fill(0);
    update_chr_banks();
}

Byte Mmc2::read_prg(Address addr) {
    if (addr >= 0x6000 && addr < 0x8000) {
        return prg_ram_[addr - 0x6000];
    }
    if (addr < 0x8000) {
        return 0;
    }

    if (addr < 0xA000) {
        // $8000-$9FFF: switchable 8 KB bank.
        uint32_t offset =
            static_cast<uint32_t>(prg_bank_ % num_prg_banks_8k_) * 0x2000 + (addr - 0x8000);
        return prg_rom_[offset & prg_rom_mask_];
    }
    // $A000-$FFFF: fixed to the last three 8 KB banks.
    uint8_t fixed_bank = static_cast<uint8_t>((addr - 0xA000) / 0x2000) + num_prg_banks_8k_ - 3;
    uint32_t offset = static_cast<uint32_t>(fixed_bank) * 0x2000 + ((addr - 0xA000) % 0x2000);
    return prg_rom_[offset & prg_rom_mask_];
}

void Mmc2::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000 && addr < 0x8000) {
        prg_ram_[addr - 0x6000] = value;
        return;
    }
    if (addr < 0x8000) {
        return;
    }

    switch (addr & 0xF000) {
    case 0xA000:
        // PRG bank select ($A000-$AFFF, only low 4 bits are meaningful).
        prg_bank_ = value & 0x0F;
        break;
    case 0xB000:
        chr_banks_[0] = value & 0x1F; // Left  half FD page ($B000-$BFFF)
        update_chr_banks();
        break;
    case 0xC000:
        chr_banks_[1] = value & 0x1F; // Right half FD page ($C000-$CFFF)
        update_chr_banks();
        break;
    case 0xD000:
        chr_banks_[2] = value & 0x1F; // Left  half FE page ($D000-$DFFF)
        update_chr_banks();
        break;
    case 0xE000:
        chr_banks_[3] = value & 0x1F; // Right half FE page ($E000-$EFFF)
        update_chr_banks();
        break;
    default:
        break;
    }
}

bool Mmc2::maps_prg(Address addr) const {
    if (addr >= 0x8000) {
        return true;
    }
    // PRG-RAM is always mapped at $6000-$7FFF (battery-backed).
    return addr >= 0x6000 && addr < 0x8000;
}

Byte Mmc2::read_chr(Address addr) {
    if (use_chr_ram_) {
        return chr_ram_[addr % 0x2000];
    }

    // MMC2 CHR latch: reading the trigger pattern-table addresses toggles
    // which of the two registered 4 KB pages is active for each half.
    // $0FD8/$1FD8 select the FD page; $0FE8/$1FE8 select the FE page.
    // Collapsed to two masked comparisons (down from four) since this runs
    // on every CHR read (per-pixel path). bit 0x1000 → right half.
    if ((addr & 0x1FFF) == 0x0FD8 || (addr & 0x1FFF) == 0x1FD8) {
        latch_[(addr >> 12) & 1] = 0; // FD page
        update_chr_banks();
    } else if ((addr & 0x1FFF) == 0x0FE8 || (addr & 0x1FFF) == 0x1FE8) {
        latch_[(addr >> 12) & 1] = 1; // FE page
        update_chr_banks();
    }

    uint32_t half = (addr < 0x1000) ? 0 : 1;
    uint32_t offset = chr_offsets_[half] + (addr & 0x0FFF);
    return chr_rom_[offset & chr_rom_mask_];
}

void Mmc2::write_chr(Address addr, Byte value) {
    if (use_chr_ram_) {
        chr_ram_[addr % 0x2000] = value;
    }
}

void Mmc2::update_chr_banks() {
    // chr_banks_[0/1] are the FD pages (latch=0), chr_banks_[2/3] the FE pages.
    uint8_t left_page = chr_banks_[latch_[0] == 0 ? 0 : 2];
    uint8_t right_page = chr_banks_[latch_[1] == 0 ? 1 : 3];
    chr_offsets_[0] = static_cast<uint32_t>(left_page) * 0x1000;
    chr_offsets_[1] = static_cast<uint32_t>(right_page) * 0x1000;
}

MirrorMode Mmc2::mirror_mode() const {
    return mirror_mode_;
}

} // namespace mapperbus::core
