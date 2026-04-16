#include "core/mappers/mmc3.hpp"

namespace mapperbus::core {

Mmc3::Mmc3(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), use_chr_ram_(chr_rom_.empty()),
      mirror_mode_(header.mirror_mode),
      irq_zero_latch_repeats_(!(header.is_nes2 && header.submapper == 4)) {
    num_prg_banks_8k_ = static_cast<uint8_t>(prg_rom_.size() / 0x2000);
    num_chr_banks_1k_ = use_chr_ram_ ? 8 : static_cast<uint16_t>(chr_rom_.size() / 0x0400);
    reset();
}

void Mmc3::reset() {
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
    update_prg_banks();
    update_chr_banks();
}

void Mmc3::update_prg_banks() {
    uint8_t r6 = bank_registers_[6] % num_prg_banks_8k_;
    uint8_t r7 = bank_registers_[7] % num_prg_banks_8k_;
    uint8_t second_last = (num_prg_banks_8k_ - 2) % num_prg_banks_8k_;
    uint8_t last = (num_prg_banks_8k_ - 1) % num_prg_banks_8k_;

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
    if (addr >= 0x6000 && addr < 0x8000) {
        if (!prg_ram_enabled_) {
            return 0;
        }
        return prg_ram_[addr - 0x6000];
    }
    if (addr < 0x8000)
        return 0;

    uint8_t bank_index = (addr - 0x8000) / 0x2000;
    uint32_t offset = prg_offsets_[bank_index] + ((addr - 0x8000) % 0x2000);
    return prg_rom_[offset % prg_rom_.size()];
}

void Mmc3::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000 && addr < 0x8000) {
        if (prg_ram_enabled_ && !prg_ram_write_protected_) {
            prg_ram_[addr - 0x6000] = value;
        }
        return;
    }
    if (addr < 0x8000)
        return;

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
            bank_registers_[bank_select_] = value;
            if (bank_select_ < 6) {
                update_chr_banks();
            } else {
                update_prg_banks();
            }
        }
    } else if (addr < 0xC000) {
        if (even) {
            // $A000: Mirroring
            mirror_mode_ = (value & 0x01) ? MirrorMode::Horizontal : MirrorMode::Vertical;
        } else {
            prg_ram_enabled_ = (value & 0x80) != 0;
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
    return addr >= 0x6000 && addr < 0x8000 && prg_ram_enabled_;
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
    if (use_chr_ram_) {
        return chr_ram_[addr % 0x2000];
    }
    uint8_t bank_index = (addr / 0x0400) & 0x07;
    uint32_t offset = chr_offsets_[bank_index] + (addr % 0x0400);
    return chr_rom_[offset % chr_rom_.size()];
}

void Mmc3::write_chr(Address addr, Byte value) {
    if (use_chr_ram_) {
        chr_ram_[addr % 0x2000] = value;
    }
}

MirrorMode Mmc3::mirror_mode() const {
    return mirror_mode_;
}

} // namespace mapperbus::core
