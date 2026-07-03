#include "core/mappers/sunsoft5b.hpp"

#include <algorithm>

namespace mapperbus::core {

namespace {
// 5B DAC steps: 3 dB per 4-bit volume level, 1.5 dB per 5-bit envelope
// level. Normalized so full scale is 15.0, matching the previous linear
// full-volume contribution (the 0.005f output gain keeps its meaning).
// amplitude(l) = 15 * 2^((l-15)/2), with level 0 silent.
constexpr std::array<float, 16> kVolumeDac = {
    0.000000f,
    0.117188f,
    0.165728f,
    0.234375f,
    0.331456f,
    0.468750f,
    0.662913f,
    0.937500f,
    1.325825f,
    1.875000f,
    2.651650f,
    3.750000f,
    5.303301f,
    7.500000f,
    10.606602f,
    15.000000f,
};
// amplitude(l) = 15 * 2^((l-31)/4), with level 0 silent.
constexpr std::array<float, 32> kEnvelopeDac = {
    0.000000f, 0.082864f, 0.098543f, 0.117188f, 0.139360f, 0.165728f,  0.197085f,  0.234375f,
    0.278720f, 0.331456f, 0.394170f, 0.468750f, 0.557441f, 0.662913f,  0.788340f,  0.937500f,
    1.114882f, 1.325825f, 1.576681f, 1.875000f, 2.229763f, 2.651650f,  3.153362f,  3.750000f,
    4.459527f, 5.303301f, 6.306723f, 7.500000f, 8.919053f, 10.606602f, 12.613446f, 15.000000f,
};
} // namespace

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
    noise_period_ = 0;
    noise_timer_ = 0;
    noise_lfsr_ = 1;
    env_period_ = 0;
    env_timer_ = 0;
    env_shape_ = 0;
    env_step_ = 0;
    env_level_ = 0;
    env_attack_ = false;
    env_holding_ = false;
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
        case 0x06:
            noise_period_ = value & 0x1F;
            break;
        case 0x07:
            channels_[0].tone_enabled = (value & 0x01) == 0;
            channels_[1].tone_enabled = (value & 0x02) == 0;
            channels_[2].tone_enabled = (value & 0x04) == 0;
            channels_[0].noise_enabled = (value & 0x08) == 0;
            channels_[1].noise_enabled = (value & 0x10) == 0;
            channels_[2].noise_enabled = (value & 0x20) == 0;
            break;
        case 0x08:
            channels_[0].volume = value & 0x0F;
            channels_[0].use_envelope = (value & 0x10) != 0;
            break;
        case 0x09:
            channels_[1].volume = value & 0x0F;
            channels_[1].use_envelope = (value & 0x10) != 0;
            break;
        case 0x0A:
            channels_[2].volume = value & 0x0F;
            channels_[2].use_envelope = (value & 0x10) != 0;
            break;
        case 0x0B:
            env_period_ = (env_period_ & 0xFF00) | value;
            break;
        case 0x0C:
            env_period_ = (env_period_ & 0x00FF) | (static_cast<uint16_t>(value) << 8);
            break;
        case 0x0D:
            // Writing the shape restarts the envelope from phase 0.
            env_shape_ = value & 0x0F;
            env_step_ = 0;
            env_attack_ = (env_shape_ & 0x04) != 0;
            env_holding_ = false;
            env_level_ = env_attack_ ? 0 : 31;
            env_timer_ = env_period_;
            break;
        default:
            break;
        }
    }
}

bool Sunsoft5b::maps_prg(Address addr) const {
    return addr >= 0x8000 || (addr >= 0x6000 && addr < 0x8000 && prg_ram_enabled_);
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

    // AY tone/noise/envelope units run at CPU/16
    ++audio_divider_;
    if (audio_divider_ < 16)
        return;
    audio_divider_ = 0;

    for (auto& ch : channels_) {
        if (ch.timer == 0) {
            // TP=0 counts as 1: the output toggles after exactly
            // max(TP, 1) ticks (full period = 32*TP CPU cycles).
            ch.timer = std::max<uint16_t>(ch.period, 1) - 1;
            ch.output_state = !ch.output_state;
        } else {
            --ch.timer;
        }
    }

    // Noise: the 17-bit LFSR advances every 2*(NP+1) ticks, i.e. every
    // 32*(NP+1) CPU cycles (noise runs at half the tone rate).
    if (noise_timer_ == 0) {
        noise_timer_ = static_cast<uint16_t>(2 * (noise_period_ + 1) - 1);
        const uint32_t feedback = (noise_lfsr_ ^ (noise_lfsr_ >> 3)) & 1;
        noise_lfsr_ = (noise_lfsr_ >> 1) | (feedback << 16);
    } else {
        --noise_timer_;
    }

    // Envelope: one 32-step ramp position every (EP+1) ticks.
    if (env_timer_ == 0) {
        env_timer_ = env_period_;
        clock_envelope();
    } else {
        --env_timer_;
    }
}

void Sunsoft5b::clock_envelope() {
    if (env_holding_)
        return;
    if (env_step_ == 31) {
        // End of a 32-step ramp: apply the shape's continue/hold/alternate
        // bits ($0D shapes 0-7 collapse to one ramp followed by silence).
        if ((env_shape_ & 0x08) == 0) { // continue=0: drop to 0 and stay
            env_holding_ = true;
            env_level_ = 0;
            return;
        }
        if ((env_shape_ & 0x01) != 0) { // hold: latch the final level
            env_holding_ = true;
            const bool high = ((env_shape_ & 0x02) != 0) ? !env_attack_ : env_attack_;
            env_level_ = high ? 31 : 0;
            return;
        }
        if ((env_shape_ & 0x02) != 0) // alternate: reverse direction
            env_attack_ = !env_attack_;
        env_step_ = 0;
    } else {
        ++env_step_;
    }
    env_level_ = env_attack_ ? env_step_ : static_cast<uint8_t>(31 - env_step_);
}

float Sunsoft5b::audio_output() const {
    const bool noise_high = (noise_lfsr_ & 1) != 0;
    float output = 0.0f;
    for (const auto& ch : channels_) {
        // Tone and noise gate each other: the channel is high only when
        // every enabled source is high (a disabled source counts as high).
        const bool tone_gate = !ch.tone_enabled || ch.output_state;
        const bool noise_gate = !ch.noise_enabled || noise_high;
        if (!(tone_gate && noise_gate))
            continue;
        output += ch.use_envelope ? kEnvelopeDac[env_level_ & 0x1F] : kVolumeDac[ch.volume & 0x0F];
    }
    return output * 0.005f; // Normalize to ~APU level
}

} // namespace mapperbus::core
