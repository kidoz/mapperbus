#include "core/mappers/vrc6.hpp"

namespace mapperbus::core {

// --- VRC6 Pulse ---

void Vrc6Pulse::clock() {
    if (timer == 0) {
        timer = timer_period;
        step = (step + 1) & 0x0F;
    } else {
        --timer;
    }
}

uint8_t Vrc6Pulse::output() const {
    if (!enabled)
        return 0;
    if (mode)
        return volume; // Constant output
    // Duty: output volume when step <= duty, else 0
    return (step <= duty) ? volume : 0;
}

// --- VRC6 Sawtooth ---

void Vrc6Sawtooth::clock() {
    if (timer == 0) {
        timer = timer_period;
        ++step;
        if (step >= 14) {
            step = 0;
            accumulator = 0;
        } else if ((step & 1) == 0) {
            accumulator += accumulator_rate;
        }
    } else {
        --timer;
    }
}

uint8_t Vrc6Sawtooth::output() const {
    if (!enabled)
        return 0;
    return accumulator >> 3; // Top 5 bits
}

// --- VRC6 Mapper ---

Vrc6::Vrc6(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), use_chr_ram_(chr_rom_.empty()),
      swap_address_lines_(header.mapper_number == 26) {
    num_prg_banks_8k_ = static_cast<uint8_t>(prg_rom_.size() / 0x2000);
    chr_banks_.fill(0);
    reset();
}

void Vrc6::reset() {
    prg_bank_16k_ = 0;
    prg_bank_8k_ = 0;
    chr_banks_.fill(0);
    mirror_mode_ = MirrorMode::Vertical;
    irq_latch_ = 0;
    irq_counter_ = 0;
    irq_enabled_ = false;
    irq_enabled_after_ack_ = false;
    irq_pending_ = false;
    irq_cycle_mode_ = false;
    irq_prescaler_ = 0;
    pulse1_ = {};
    pulse2_ = {};
    sawtooth_ = {};
    update_banks();
}

void Vrc6::update_banks() {
    // Banks are computed on access
}

Byte Vrc6::read_prg(Address addr) {
    if (addr >= 0x6000 && addr < 0x8000) {
        return prg_ram_[addr - 0x6000];
    }
    if (addr < 0x8000)
        return 0;

    if (addr < 0xC000) {
        // $8000-$BFFF: 16 KB switchable bank
        uint32_t offset = static_cast<uint32_t>(prg_bank_16k_) * 0x4000 + (addr - 0x8000);
        return prg_rom_[offset % prg_rom_.size()];
    }
    if (addr < 0xE000) {
        // $C000-$DFFF: 8 KB switchable bank
        uint32_t offset = static_cast<uint32_t>(prg_bank_8k_) * 0x2000 + (addr - 0xC000);
        return prg_rom_[offset % prg_rom_.size()];
    }
    // $E000-$FFFF: Fixed last 8 KB bank
    uint32_t offset = static_cast<uint32_t>(num_prg_banks_8k_ - 1) * 0x2000 + (addr - 0xE000);
    return prg_rom_[offset % prg_rom_.size()];
}

void Vrc6::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000 && addr < 0x8000) {
        prg_ram_[addr - 0x6000] = value;
        return;
    }
    if (addr < 0x8000)
        return;

    // Mapper 26 swaps A0 and A1
    Address reg_addr = addr;
    if (swap_address_lines_) {
        uint16_t a0 = (addr >> 0) & 1;
        uint16_t a1 = (addr >> 1) & 1;
        reg_addr = static_cast<Address>((addr & ~0x03) | (a0 << 1) | (a1 << 0));
    }

    uint16_t reg = reg_addr & 0xF003;

    switch (reg) {
    // PRG banking
    case 0x8000:
    case 0x8001:
    case 0x8002:
    case 0x8003:
        prg_bank_16k_ = value & 0x0F;
        break;
    case 0xC000:
    case 0xC001:
    case 0xC002:
    case 0xC003:
        prg_bank_8k_ = value & 0x1F;
        break;

    // Audio - Pulse 1
    case 0x9000:
        pulse1_.mode = (value & 0x80) != 0;
        pulse1_.duty = (value >> 4) & 0x07;
        pulse1_.volume = value & 0x0F;
        break;
    case 0x9001:
        pulse1_.timer_period = (pulse1_.timer_period & 0x0F00) | value;
        break;
    case 0x9002:
        pulse1_.timer_period =
            (pulse1_.timer_period & 0x00FF) | (static_cast<uint16_t>(value & 0x0F) << 8);
        pulse1_.enabled = (value & 0x80) != 0;
        break;

    // Audio - Pulse 2
    case 0xA000:
        pulse2_.mode = (value & 0x80) != 0;
        pulse2_.duty = (value >> 4) & 0x07;
        pulse2_.volume = value & 0x0F;
        break;
    case 0xA001:
        pulse2_.timer_period = (pulse2_.timer_period & 0x0F00) | value;
        break;
    case 0xA002:
        pulse2_.timer_period =
            (pulse2_.timer_period & 0x00FF) | (static_cast<uint16_t>(value & 0x0F) << 8);
        pulse2_.enabled = (value & 0x80) != 0;
        break;

    // Audio - Sawtooth
    case 0xB000:
        sawtooth_.accumulator_rate = value & 0x3F;
        break;
    case 0xB001:
        sawtooth_.timer_period = (sawtooth_.timer_period & 0x0F00) | value;
        break;
    case 0xB002:
        sawtooth_.timer_period =
            (sawtooth_.timer_period & 0x00FF) | (static_cast<uint16_t>(value & 0x0F) << 8);
        sawtooth_.enabled = (value & 0x80) != 0;
        break;

    // Mirroring
    case 0xB003: {
        uint8_t mirror = (value >> 2) & 0x03;
        switch (mirror) {
        case 0:
            mirror_mode_ = MirrorMode::Vertical;
            break;
        case 1:
            mirror_mode_ = MirrorMode::Horizontal;
            break;
        case 2:
            mirror_mode_ = MirrorMode::SingleLower;
            break;
        case 3:
            mirror_mode_ = MirrorMode::SingleUpper;
            break;
        }
        break;
    }

    // CHR banking ($D000-$E003)
    case 0xD000:
        chr_banks_[0] = value;
        break;
    case 0xD001:
        chr_banks_[1] = value;
        break;
    case 0xD002:
        chr_banks_[2] = value;
        break;
    case 0xD003:
        chr_banks_[3] = value;
        break;
    case 0xE000:
        chr_banks_[4] = value;
        break;
    case 0xE001:
        chr_banks_[5] = value;
        break;
    case 0xE002:
        chr_banks_[6] = value;
        break;
    case 0xE003:
        chr_banks_[7] = value;
        break;

    // IRQ
    case 0xF000:
        irq_latch_ = value;
        break;
    case 0xF001:
        irq_enabled_after_ack_ = (value & 0x01) != 0;
        irq_enabled_ = (value & 0x02) != 0;
        irq_cycle_mode_ = (value & 0x04) != 0;
        if (irq_enabled_) {
            irq_counter_ = irq_latch_;
            irq_prescaler_ = 341;
        }
        irq_pending_ = false;
        break;
    case 0xF002:
        irq_enabled_ = irq_enabled_after_ack_;
        irq_pending_ = false;
        break;

    default:
        break;
    }
}

Byte Vrc6::read_chr(Address addr) {
    if (use_chr_ram_) {
        return chr_ram_[addr % 0x2000];
    }
    uint8_t bank_index = (addr / 0x0400) & 0x07;
    uint32_t offset = static_cast<uint32_t>(chr_banks_[bank_index]) * 0x0400 + (addr % 0x0400);
    return chr_rom_[offset % chr_rom_.size()];
}

void Vrc6::write_chr(Address addr, Byte value) {
    if (use_chr_ram_) {
        chr_ram_[addr % 0x2000] = value;
    }
}

MirrorMode Vrc6::mirror_mode() const {
    return mirror_mode_;
}

void Vrc6::clock_audio() {
    pulse1_.clock();
    pulse2_.clock();
    sawtooth_.clock();

    // VRC6 IRQ (CPU cycle mode)
    if (irq_enabled_ && irq_cycle_mode_) {
        if (irq_counter_ == 0xFF) {
            irq_counter_ = irq_latch_;
            irq_pending_ = true;
        } else {
            ++irq_counter_;
        }
    }
    // Scanline mode IRQ is clocked by PPU via clock_irq_counter() — not handled here.
    // VRC6 scanline mode uses a prescaler similar to VRC4/VRC7.
    if (irq_enabled_ && !irq_cycle_mode_) {
        irq_prescaler_ -= 3;
        if (irq_prescaler_ <= 0) {
            irq_prescaler_ += 341;
            if (irq_counter_ == 0xFF) {
                irq_counter_ = irq_latch_;
                irq_pending_ = true;
            } else {
                ++irq_counter_;
            }
        }
    }
}

float Vrc6::audio_output() const {
    // VRC6 output range: pulses 0-15 each, sawtooth 0-31
    // Normalize to roughly match APU output level (~0.0 to ~0.15)
    float p1 = static_cast<float>(pulse1_.output());
    float p2 = static_cast<float>(pulse2_.output());
    float sw = static_cast<float>(sawtooth_.output());
    return (p1 + p2 + sw) * 0.006f;
}

void Vrc6::write_expansion([[maybe_unused]] Address addr, [[maybe_unused]] Byte value) {
    // VRC6 expansion audio registers are in PRG space ($9000-$B002), handled by write_prg
}

} // namespace mapperbus::core
