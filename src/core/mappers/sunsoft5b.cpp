#include "core/mappers/sunsoft5b.hpp"

namespace mapperbus::core {

Sunsoft5b::Sunsoft5b(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), use_chr_ram_(chr_rom_.empty()),
      mirror_mode_(header.mirror_mode) {
    num_prg_8k_ = static_cast<uint8_t>(prg_rom_.size() / 0x2000);
    reset();
}

void Sunsoft5b::reset() {
    command_ = 0;
    chr_banks_.fill(0);
    prg_banks_.fill(0);
    prg_banks_[3] = num_prg_8k_ - 1;
    prg_ram_enabled_ = false;
    irq_counter_ = 0;
    irq_enabled_ = false;
    irq_counter_enabled_ = false;
    irq_pending_ = false;
    channels_ = {};
    ay_register_ = 0;
    audio_divider_ = 0;
}

Byte Sunsoft5b::read_prg(Address addr) {
    if (addr >= 0x6000 && addr < 0x8000) {
        if (prg_ram_enabled_)
            return prg_ram_[addr - 0x6000];
        return 0;
    }
    if (addr < 0x8000)
        return 0;

    uint8_t bank_idx = (addr - 0x8000) / 0x2000;
    uint8_t bank = prg_banks_[bank_idx] % num_prg_8k_;
    uint32_t offset = static_cast<uint32_t>(bank) * 0x2000 + ((addr - 0x8000) % 0x2000);
    return prg_rom_[offset % prg_rom_.size()];
}

void Sunsoft5b::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000 && addr < 0x8000) {
        if (prg_ram_enabled_)
            prg_ram_[addr - 0x6000] = value;
        return;
    }

    if (addr >= 0x8000 && addr < 0xA000) {
        command_ = value & 0x0F;
    } else if (addr >= 0xA000 && addr < 0xC000) {
        switch (command_) {
        case 0x00:
        case 0x01:
        case 0x02:
        case 0x03:
        case 0x04:
        case 0x05:
        case 0x06:
        case 0x07:
            chr_banks_[command_] = value;
            break;
        case 0x08:
            prg_ram_enabled_ = (value & 0xC0) == 0xC0;
            prg_banks_[0] = value & 0x3F;
            break;
        case 0x09:
            prg_banks_[1] = value & 0x3F;
            break;
        case 0x0A:
            prg_banks_[2] = value & 0x3F;
            break;
        case 0x0B:
            prg_banks_[3] = value & 0x3F;
            break;
        case 0x0C: {
            uint8_t m = value & 0x03;
            if (m == 0)
                mirror_mode_ = MirrorMode::Vertical;
            else if (m == 1)
                mirror_mode_ = MirrorMode::Horizontal;
            else if (m == 2)
                mirror_mode_ = MirrorMode::SingleLower;
            else
                mirror_mode_ = MirrorMode::SingleUpper;
            break;
        }
        case 0x0D:
            irq_enabled_ = (value & 0x01) != 0;
            irq_counter_enabled_ = (value & 0x80) != 0;
            irq_pending_ = false;
            break;
        case 0x0E:
            irq_counter_ = (irq_counter_ & 0xFF00) | value;
            break;
        case 0x0F:
            irq_counter_ = (irq_counter_ & 0x00FF) | (static_cast<uint16_t>(value) << 8);
            break;
        }
    } else if (addr >= 0xC000 && addr < 0xE000) {
        // AY register select
        ay_register_ = value & 0x0F;
    } else if (addr >= 0xE000) {
        // AY register write
        switch (ay_register_) {
        case 0x00:
            channels_[0].period = (channels_[0].period & 0x0F00) | value;
            break;
        case 0x01:
            channels_[0].period = (channels_[0].period & 0x00FF) | ((value & 0x0F) << 8);
            break;
        case 0x02:
            channels_[1].period = (channels_[1].period & 0x0F00) | value;
            break;
        case 0x03:
            channels_[1].period = (channels_[1].period & 0x00FF) | ((value & 0x0F) << 8);
            break;
        case 0x04:
            channels_[2].period = (channels_[2].period & 0x0F00) | value;
            break;
        case 0x05:
            channels_[2].period = (channels_[2].period & 0x00FF) | ((value & 0x0F) << 8);
            break;
        case 0x07:
            channels_[0].tone_enabled = (value & 0x01) == 0;
            channels_[1].tone_enabled = (value & 0x02) == 0;
            channels_[2].tone_enabled = (value & 0x04) == 0;
            break;
        case 0x08:
            channels_[0].volume = value & 0x0F;
            break;
        case 0x09:
            channels_[1].volume = value & 0x0F;
            break;
        case 0x0A:
            channels_[2].volume = value & 0x0F;
            break;
        default:
            break;
        }
    }
}

Byte Sunsoft5b::read_chr(Address addr) {
    if (use_chr_ram_)
        return chr_ram_[addr % 0x2000];
    uint8_t bank_idx = (addr / 0x0400) & 0x07;
    uint32_t offset = static_cast<uint32_t>(chr_banks_[bank_idx]) * 0x0400 + (addr % 0x0400);
    return chr_rom_[offset % chr_rom_.size()];
}

void Sunsoft5b::write_chr(Address addr, Byte value) {
    if (use_chr_ram_)
        chr_ram_[addr % 0x2000] = value;
}

MirrorMode Sunsoft5b::mirror_mode() const {
    return mirror_mode_;
}

void Sunsoft5b::clock_audio() {
    // IRQ counter
    if (irq_counter_enabled_) {
        if (irq_counter_ == 0) {
            if (irq_enabled_)
                irq_pending_ = true;
        } else {
            --irq_counter_;
        }
    }

    // AY channels run at CPU/16
    ++audio_divider_;
    if (audio_divider_ < 16)
        return;
    audio_divider_ = 0;

    for (auto& ch : channels_) {
        if (ch.period == 0)
            continue;
        if (ch.timer == 0) {
            ch.timer = ch.period;
            ch.output_state = !ch.output_state;
        } else {
            --ch.timer;
        }
    }
}

float Sunsoft5b::audio_output() const {
    float output = 0.0f;
    for (const auto& ch : channels_) {
        if (!ch.tone_enabled)
            continue;
        if (ch.output_state) {
            output += static_cast<float>(ch.volume);
        }
    }
    return output * 0.005f; // Normalize to ~APU level
}

} // namespace mapperbus::core
