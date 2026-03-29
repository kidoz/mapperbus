#include "core/mappers/namco163.hpp"

namespace mapperbus::core {

Namco163::Namco163(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), use_chr_ram_(chr_rom_.empty()),
      mirror_mode_(header.mirror_mode) {
    num_prg_8k_ = static_cast<uint8_t>(prg_rom_.size() / 0x2000);
    reset();
}

void Namco163::reset() {
    prg_banks_.fill(0);
    prg_banks_[3] = num_prg_8k_ - 1;
    chr_banks_.fill(0);
    irq_counter_ = 0;
    irq_enabled_ = false;
    irq_pending_ = false;
    sound_ram_.fill(0);
    sound_addr_ = 0;
    auto_increment_ = false;
    num_channels_ = 1;
    audio_timer_ = 0;
    current_channel_ = 0;
    channels_ = {};
}

Byte Namco163::read_prg(Address addr) {
    if (addr >= 0x6000 && addr < 0x8000)
        return prg_ram_[addr - 0x6000];
    if (addr < 0x8000)
        return 0;
    uint8_t bank_idx = (addr - 0x8000) / 0x2000;
    uint8_t bank = prg_banks_[bank_idx] % num_prg_8k_;
    uint32_t offset = static_cast<uint32_t>(bank) * 0x2000 + ((addr - 0x8000) % 0x2000);
    return prg_rom_[offset % prg_rom_.size()];
}

void Namco163::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000 && addr < 0x8000) {
        prg_ram_[addr - 0x6000] = value;
        return;
    }
    if (addr < 0x8000)
        return;

    uint16_t reg = addr & 0xF800;
    switch (reg) {
    case 0x8000:
    case 0x8800:
    case 0x9000:
    case 0x9800:
    case 0xA000:
    case 0xA800:
    case 0xB000:
    case 0xB800:
        chr_banks_[(addr - 0x8000) / 0x0800] = value;
        break;
    case 0xE000:
        prg_banks_[0] = value & 0x3F;
        break;
    case 0xE800:
        prg_banks_[1] = value & 0x3F;
        break;
    case 0xF000:
        prg_banks_[2] = value & 0x3F;
        break;
    case 0xF800:
        // Sound address + data port
        sound_addr_ = value & 0x7F;
        auto_increment_ = (value & 0x80) != 0;
        break;
    default:
        break;
    }
}

Byte Namco163::read_chr(Address addr) {
    if (use_chr_ram_)
        return chr_ram_[addr % 0x2000];
    uint8_t bank_idx = (addr / 0x0400) & 0x07;
    uint32_t offset = static_cast<uint32_t>(chr_banks_[bank_idx]) * 0x0400 + (addr % 0x0400);
    return chr_rom_[offset % chr_rom_.size()];
}

void Namco163::write_chr(Address addr, Byte value) {
    if (use_chr_ram_)
        chr_ram_[addr % 0x2000] = value;
}

MirrorMode Namco163::mirror_mode() const {
    return mirror_mode_;
}

Byte Namco163::read_expansion(Address addr) {
    if (addr == 0x4800) {
        Byte val = sound_ram_[sound_addr_];
        if (auto_increment_)
            sound_addr_ = (sound_addr_ + 1) & 0x7F;
        return val;
    }
    if (addr == 0x5000)
        return static_cast<Byte>(irq_counter_ & 0xFF);
    if (addr == 0x5800)
        return static_cast<Byte>((irq_counter_ >> 8) | (irq_enabled_ ? 0x80 : 0));
    return 0;
}

void Namco163::write_expansion(Address addr, Byte value) {
    if (addr == 0x4800) {
        sound_ram_[sound_addr_] = value;

        // Update channel state from sound RAM writes
        // Channels are stored at $40-$7F, 8 bytes each
        if (sound_addr_ >= 0x40) {
            uint8_t ch = (sound_addr_ - 0x40) / 8;
            uint8_t reg = (sound_addr_ - 0x40) % 8;
            if (ch < 8) {
                auto& c = channels_[ch];
                switch (reg) {
                case 0:
                    c.frequency = (c.frequency & 0x3FF00) | value;
                    break;
                case 2:
                    c.frequency = (c.frequency & 0x300FF) | (static_cast<uint32_t>(value) << 8);
                    break;
                case 4:
                    c.frequency =
                        (c.frequency & 0x0FFFF) | (static_cast<uint32_t>(value & 0x03) << 16);
                    c.wave_length = static_cast<uint8_t>(256 - (value & 0xFC));
                    break;
                case 6:
                    c.wave_addr = value;
                    break;
                case 7:
                    c.volume = value & 0x0F;
                    if (ch == 7)
                        num_channels_ = ((value >> 4) & 0x07) + 1;
                    break;
                default:
                    break;
                }
            }
        }

        if (auto_increment_)
            sound_addr_ = (sound_addr_ + 1) & 0x7F;
        return;
    }
    if (addr == 0x5000) {
        irq_counter_ = (irq_counter_ & 0xFF00) | value;
        irq_pending_ = false;
    }
    if (addr == 0x5800) {
        irq_counter_ = (irq_counter_ & 0x00FF) | (static_cast<uint16_t>(value & 0x7F) << 8);
        irq_enabled_ = (value & 0x80) != 0;
        irq_pending_ = false;
    }
}

void Namco163::clock_audio() {
    // IRQ
    if (irq_enabled_ && irq_counter_ < 0x7FFF) {
        ++irq_counter_;
        if (irq_counter_ >= 0x7FFF)
            irq_pending_ = true;
    }

    // N163 channels clock at CPU/15
    ++audio_timer_;
    if (audio_timer_ < 15)
        return;
    audio_timer_ = 0;

    // Clock one channel per tick (round-robin)
    uint8_t ch_start = 8 - num_channels_;
    uint8_t ch = ch_start + current_channel_;
    if (ch < 8) {
        auto& c = channels_[ch];
        if (c.wave_length > 0 && c.frequency > 0) {
            c.phase += c.frequency;
            c.phase %= (static_cast<uint32_t>(c.wave_length) << 16);
            uint8_t wave_pos = static_cast<uint8_t>((c.phase >> 16) + c.wave_addr);
            // 4-bit samples packed in sound RAM (2 per byte)
            Byte ram_byte = sound_ram_[(wave_pos / 2) & 0x7F];
            c.output = (wave_pos & 1) ? (ram_byte >> 4) : (ram_byte & 0x0F);
        }
    }

    current_channel_ = (current_channel_ + 1) % num_channels_;
}

float Namco163::audio_output() const {
    float output = 0.0f;
    uint8_t ch_start = 8 - num_channels_;
    for (uint8_t i = ch_start; i < 8; ++i) {
        output += static_cast<float>(channels_[i].output * channels_[i].volume);
    }
    // Normalize: max per channel = 15*15=225, max 8 channels
    return output * 0.0004f;
}

} // namespace mapperbus::core
