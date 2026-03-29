#include "core/fds/fds.hpp"

#include <algorithm>

namespace mapperbus::core {

void Fds::reset() {
    loaded_ = false;
    wavetable_.fill(0);
    wave_write_enabled_ = false;
    wave_frequency_ = 0;
    wave_accumulator_ = 0;
    wave_position_ = 0;
    volume_envelope_ = 32;
    volume_enabled_ = true;
    master_volume_ = 0;
    sound_enabled_ = false;
    envelope_disabled_ = false;
    mod_table_.fill(0);
    mod_frequency_ = 0;
    mod_accumulator_ = 0;
    mod_position_ = 0;
    mod_counter_ = 0;
    mod_enabled_ = false;
    mod_depth_ = 0;
}

void Fds::step([[maybe_unused]] uint32_t cpu_cycles) {
    // Stub: FDS disk I/O timing not implemented
}

Byte Fds::read(Address addr) {
    addr &= 0x00FF;

    // $4040-$407F: Wavetable RAM read
    if (addr >= 0x40 && addr <= 0x7F) {
        return wavetable_[addr - 0x40] | 0x40; // Bit 6 always set on read
    }

    // $4090: Volume envelope output
    if (addr == 0x90) {
        return volume_envelope_ | 0x40;
    }

    // $4092: Modulation envelope output
    if (addr == 0x92) {
        return mod_depth_ | 0x40;
    }

    return 0;
}

void Fds::write(Address addr, Byte value) {
    addr &= 0x00FF;

    // $4040-$407F: Wavetable RAM write (only when wave_write_enabled_)
    if (addr >= 0x40 && addr <= 0x7F) {
        if (wave_write_enabled_) {
            wavetable_[addr - 0x40] = value & 0x3F;
        }
        return;
    }

    switch (addr) {
    case 0x80: // $4080: Volume envelope
        envelope_disabled_ = (value & 0x80) != 0;
        if (envelope_disabled_) {
            volume_envelope_ = value & 0x3F;
        }
        break;

    case 0x82: // $4082: Wave frequency low
        wave_frequency_ = (wave_frequency_ & 0x0F00) | value;
        break;

    case 0x83: // $4083: Wave frequency high + flags
        wave_frequency_ = (wave_frequency_ & 0x00FF) | (static_cast<uint16_t>(value & 0x0F) << 8);
        sound_enabled_ = (value & 0x80) == 0;
        if (!sound_enabled_) {
            wave_accumulator_ = 0;
        }
        break;

    case 0x84: // $4084: Modulation envelope
        if (value & 0x80) {
            mod_depth_ = value & 0x3F;
        }
        break;

    case 0x85: // $4085: Modulation counter
        mod_counter_ = static_cast<int8_t>(value & 0x7F);
        if (mod_counter_ >= 64)
            mod_counter_ -= 128;
        break;

    case 0x86: // $4086: Modulation frequency low
        mod_frequency_ = (mod_frequency_ & 0x0F00) | value;
        break;

    case 0x87: // $4087: Modulation frequency high
        mod_frequency_ = (mod_frequency_ & 0x00FF) | (static_cast<uint16_t>(value & 0x0F) << 8);
        mod_enabled_ = (value & 0x80) == 0;
        if (!mod_enabled_) {
            mod_accumulator_ = 0;
        }
        break;

    case 0x88: // $4088: Modulation table write
        if (!mod_enabled_) {
            // Write to both halves of the mod table
            mod_table_[mod_position_ & 0x1F] = static_cast<int8_t>(value & 0x07);
            mod_table_[(mod_position_ + 1) & 0x1F] = static_cast<int8_t>(value & 0x07);
            mod_position_ = (mod_position_ + 2) & 0x3F;
        }
        break;

    case 0x89: // $4089: Master volume + wave write enable
        master_volume_ = value & 0x03;
        wave_write_enabled_ = (value & 0x80) != 0;
        break;

    default:
        break;
    }
}

void Fds::clock_audio() {
    if (!sound_enabled_ || wave_frequency_ == 0)
        return;

    // Advance modulation
    if (mod_enabled_ && mod_frequency_ > 0) {
        mod_accumulator_ += mod_frequency_;
        if (mod_accumulator_ >= 0x10000) {
            mod_accumulator_ &= 0xFFFF;
            // Read mod table and adjust counter
            int8_t mod_value = mod_table_[mod_position_ >> 1];
            mod_position_ = (mod_position_ + 1) & 0x3F;
            if (mod_value == 4) {
                mod_counter_ = 0;
            } else {
                mod_counter_ += mod_value;
                mod_counter_ =
                    static_cast<int8_t>(std::clamp(static_cast<int>(mod_counter_), -64, 63));
            }
        }
    }

    // Apply modulation to frequency
    int freq = wave_frequency_;
    if (mod_depth_ > 0 && mod_enabled_) {
        int mod_offset = mod_counter_ * mod_depth_;
        freq += mod_offset / 64;
        if (freq < 0)
            freq = 0;
        if (freq > 0xFFF)
            freq = 0xFFF;
    }

    // Advance wave accumulator
    wave_accumulator_ += static_cast<uint32_t>(freq);
    if (wave_accumulator_ >= 0x10000) {
        wave_accumulator_ &= 0xFFFF;
        wave_position_ = (wave_position_ + 1) & 0x3F;
    }
}

float Fds::audio_output() const {
    if (!sound_enabled_ || wave_write_enabled_)
        return 0.0f;

    uint8_t sample = wavetable_[wave_position_] & 0x3F; // 6-bit sample (0-63)
    uint8_t vol = volume_envelope_ & 0x3F;

    // Master volume attenuation
    static constexpr float kVolumeScale[] = {1.0f, 2.0f / 3.0f, 1.0f / 3.0f, 1.0f / 4.0f};
    float volume_mul = kVolumeScale[master_volume_ & 0x03];

    // Output: sample * volume * master_volume, normalized to ~0.0 - 0.15
    float output = static_cast<float>(sample * vol) / (63.0f * 63.0f);
    return output * volume_mul * 0.15f;
}

} // namespace mapperbus::core
