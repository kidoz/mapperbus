#include "core/mappers/vrc7.hpp"

namespace mapperbus::core {

Vrc7::Vrc7(const INesHeader& header, std::vector<Byte> prg_rom, std::vector<Byte> chr_rom)
    : prg_rom_(std::move(prg_rom)), chr_rom_(std::move(chr_rom)), use_chr_ram_(chr_rom_.empty()),
      mirror_mode_(header.mirror_mode) {
    num_prg_8k_ = static_cast<uint8_t>(prg_rom_.size() / 0x2000);
    reset();
}

void Vrc7::reset() {
    prg_banks_.fill(0);
    prg_banks_[2] = num_prg_8k_ - 1;
    chr_banks_.fill(0);
    irq_latch_ = 0;
    irq_counter_ = 0;
    irq_enabled_ = false;
    irq_enabled_after_ack_ = false;
    irq_pending_ = false;
    irq_prescaler_ = 0;
    audio_register_ = 0;
    fm_channels_ = {};
    audio_divider_ = 0;
}

Byte Vrc7::read_prg(Address addr) {
    if (addr >= 0x6000 && addr < 0x8000)
        return prg_ram_[addr - 0x6000];
    if (addr < 0x8000)
        return 0;

    if (addr < 0xA000) {
        uint32_t off = static_cast<uint32_t>(prg_banks_[0]) * 0x2000 + (addr - 0x8000);
        return prg_rom_[off % prg_rom_.size()];
    }
    if (addr < 0xC000) {
        uint32_t off = static_cast<uint32_t>(prg_banks_[1]) * 0x2000 + (addr - 0xA000);
        return prg_rom_[off % prg_rom_.size()];
    }
    if (addr < 0xE000) {
        uint32_t off = static_cast<uint32_t>(prg_banks_[2]) * 0x2000 + (addr - 0xC000);
        return prg_rom_[off % prg_rom_.size()];
    }
    uint32_t off = static_cast<uint32_t>(num_prg_8k_ - 1) * 0x2000 + (addr - 0xE000);
    return prg_rom_[off % prg_rom_.size()];
}

void Vrc7::write_prg(Address addr, Byte value) {
    if (addr >= 0x6000 && addr < 0x8000) {
        prg_ram_[addr - 0x6000] = value;
        return;
    }
    if (addr < 0x8000)
        return;

    switch (addr) {
    case 0x8000:
        prg_banks_[0] = value & 0x3F;
        break;
    case 0x8010:
        prg_banks_[1] = value & 0x3F;
        break;
    case 0x9000:
        prg_banks_[2] = value & 0x3F;
        break;
    case 0x9010:
        audio_register_ = value;
        break;
    case 0x9030:
        // FM register write
        if (audio_register_ < 6) {
            // Custom instrument (not implemented, use presets only)
        } else if (audio_register_ >= 0x10 && audio_register_ <= 0x15) {
            uint8_t ch = audio_register_ - 0x10;
            fm_channels_[ch].frequency = (fm_channels_[ch].frequency & 0x100) | value;
        } else if (audio_register_ >= 0x20 && audio_register_ <= 0x25) {
            uint8_t ch = audio_register_ - 0x20;
            fm_channels_[ch].frequency =
                (fm_channels_[ch].frequency & 0xFF) | ((value & 0x01) << 8);
            fm_channels_[ch].octave = (value >> 1) & 0x07;
            fm_channels_[ch].key_on = (value & 0x10) != 0;
            fm_channels_[ch].sustain = (value & 0x20) != 0;
        } else if (audio_register_ >= 0x30 && audio_register_ <= 0x35) {
            uint8_t ch = audio_register_ - 0x30;
            fm_channels_[ch].instrument = (value >> 4) & 0x0F;
            fm_channels_[ch].volume = value & 0x0F;
        }
        break;
    case 0xA000:
    case 0xA010:
    case 0xB000:
    case 0xB010:
    case 0xC000:
    case 0xC010:
    case 0xD000:
    case 0xD010:
        chr_banks_[(addr - 0xA000) / 0x0010] = value;
        break;
    case 0xE000: {
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
    case 0xE010:
        irq_latch_ = value;
        break;
    case 0xF000:
        irq_enabled_after_ack_ = (value & 0x01) != 0;
        irq_enabled_ = (value & 0x02) != 0;
        if (irq_enabled_) {
            irq_counter_ = irq_latch_;
            irq_prescaler_ = 341;
        }
        irq_pending_ = false;
        break;
    case 0xF010:
        irq_enabled_ = irq_enabled_after_ack_;
        irq_pending_ = false;
        break;
    default:
        break;
    }
}

void Vrc7::clock_irq_counter() {
    if (!irq_enabled_)
        return;

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

Byte Vrc7::read_chr(Address addr) {
    if (use_chr_ram_)
        return chr_ram_[addr % 0x2000];
    uint8_t bank_idx = (addr / 0x0400) & 0x07;
    uint32_t offset = static_cast<uint32_t>(chr_banks_[bank_idx]) * 0x0400 + (addr % 0x0400);
    return chr_rom_[offset % chr_rom_.size()];
}

void Vrc7::write_chr(Address addr, Byte value) {
    if (use_chr_ram_)
        chr_ram_[addr % 0x2000] = value;
}

MirrorMode Vrc7::mirror_mode() const {
    return mirror_mode_;
}

void Vrc7::clock_audio() {
    // FM synthesis runs at CPU/36 (approximately 49716 Hz)
    ++audio_divider_;
    if (audio_divider_ < 36)
        return;
    audio_divider_ = 0;

    for (auto& ch : fm_channels_) {
        if (!ch.key_on)
            continue;
        // Simplified FM: frequency based on F-number and octave
        uint32_t freq = static_cast<uint32_t>(ch.frequency) << ch.octave;
        ch.phase += freq;
    }
}

float Vrc7::audio_output() const {
    float output = 0.0f;

    for (const auto& ch : fm_channels_) {
        if (!ch.key_on || ch.volume == 0)
            continue;

        // Simplified FM output: sine wave approximation
        // Phase is accumulated; output is a simple sine-based waveform
        float phase_f = static_cast<float>(ch.phase & 0x1FFFF) / 131072.0f;
        float sine = std::sin(phase_f * 6.2831853f); // 2*pi

        // Volume attenuation (0 = loudest, 15 = silent)
        float vol = static_cast<float>(15 - ch.volume) / 15.0f;
        output += sine * vol;
    }

    return output * 0.02f; // Normalize to ~APU level
}

} // namespace mapperbus::core
