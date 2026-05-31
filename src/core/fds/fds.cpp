#include "core/fds/fds.hpp"

#include <algorithm>

#include "core/state/state.hpp"

namespace mapperbus::core {

namespace {
// The disk surface streams ~96.4 kbit/s; one byte therefore arrives roughly
// every 149 CPU cycles (1.789773 MHz / (96400 / 8)).
constexpr int32_t kBytePeriod = 149;
// Small spin-up delay before the first byte after the head starts.
constexpr int32_t kStartupDelay = 200;
} // namespace

void Fds::save_state(StateWriter& writer) const {
    // Disk image (FDS titles write progress back to the disk surface).
    writer.write(loaded_);
    writer.write(num_sides_);
    writer.write(current_side_);
    writer.write(write_protected_);
    writer.write_bytes(disk_data_);

    // Timer IRQ + master I/O
    writer.write(irq_reload_);
    writer.write(irq_timer_);
    writer.write(irq_timer_enabled_);
    writer.write(irq_timer_repeat_);
    writer.write(disk_io_enabled_);
    writer.write(sound_io_enabled_);

    // Drive control + transfer engine
    writer.write(motor_on_);
    writer.write(transfer_reset_);
    writer.write(read_mode_);
    writer.write(crc_control_);
    writer.write(irq_on_transfer_);
    writer.write(disk_position_);
    writer.write(transfer_delay_);
    writer.write(read_data_);
    writer.write(write_data_);
    writer.write(transfer_active_);

    // Status flags
    writer.write(timer_irq_flag_);
    writer.write(byte_transfer_flag_);
    writer.write(crc_error_);
    writer.write(end_of_disk_);
    writer.write(irq_line_);

    // Audio
    writer.write_array(wavetable_);
    writer.write(wave_write_enabled_);
    writer.write(wave_frequency_);
    writer.write(wave_accumulator_);
    writer.write(wave_position_);
    writer.write(volume_envelope_);
    writer.write(volume_enabled_);
    writer.write(master_volume_);
    writer.write(sound_enabled_);
    writer.write(envelope_disabled_);
    writer.write_array(mod_table_);
    writer.write(mod_frequency_);
    writer.write(mod_accumulator_);
    writer.write(mod_position_);
    writer.write(mod_counter_);
    writer.write(mod_enabled_);
    writer.write(mod_depth_);
}

void Fds::load_state(StateReader& reader) {
    reader.read(loaded_);
    reader.read(num_sides_);
    reader.read(current_side_);
    reader.read(write_protected_);
    reader.read_bytes(disk_data_);

    reader.read(irq_reload_);
    reader.read(irq_timer_);
    reader.read(irq_timer_enabled_);
    reader.read(irq_timer_repeat_);
    reader.read(disk_io_enabled_);
    reader.read(sound_io_enabled_);

    reader.read(motor_on_);
    reader.read(transfer_reset_);
    reader.read(read_mode_);
    reader.read(crc_control_);
    reader.read(irq_on_transfer_);
    reader.read(disk_position_);
    reader.read(transfer_delay_);
    reader.read(read_data_);
    reader.read(write_data_);
    reader.read(transfer_active_);

    reader.read(timer_irq_flag_);
    reader.read(byte_transfer_flag_);
    reader.read(crc_error_);
    reader.read(end_of_disk_);
    reader.read(irq_line_);

    reader.read_array(wavetable_);
    reader.read(wave_write_enabled_);
    reader.read(wave_frequency_);
    reader.read(wave_accumulator_);
    reader.read(wave_position_);
    reader.read(volume_envelope_);
    reader.read(volume_enabled_);
    reader.read(master_volume_);
    reader.read(sound_enabled_);
    reader.read(envelope_disabled_);
    reader.read_array(mod_table_);
    reader.read(mod_frequency_);
    reader.read(mod_accumulator_);
    reader.read(mod_position_);
    reader.read(mod_counter_);
    reader.read(mod_enabled_);
    reader.read(mod_depth_);
}

void Fds::reset_audio() {
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

void Fds::reset_drive() {
    // A soft reset returns the head to the start and clears drive control, but
    // does not eject the mounted disk.
    irq_reload_ = 0;
    irq_timer_ = 0;
    irq_timer_enabled_ = false;
    irq_timer_repeat_ = false;
    disk_io_enabled_ = false;
    sound_io_enabled_ = false;
    motor_on_ = false;
    transfer_reset_ = false;
    read_mode_ = true;
    crc_control_ = false;
    irq_on_transfer_ = false;
    disk_position_ = 0;
    transfer_delay_ = 0;
    read_data_ = 0;
    write_data_ = 0;
    transfer_active_ = false;
    timer_irq_flag_ = false;
    byte_transfer_flag_ = false;
    crc_error_ = false;
    end_of_disk_ = false;
    irq_line_ = false;
}

void Fds::reset() {
    reset_drive();
    reset_audio();
}

bool Fds::load_disk(std::span<const Byte> image) {
    std::span<const Byte> payload = image;
    // Strip the optional 16-byte fwNES "FDS\x1a" header.
    if (payload.size() >= 16 && payload[0] == 'F' && payload[1] == 'D' && payload[2] == 'S' &&
        payload[3] == 0x1A) {
        payload = payload.subspan(16);
    }
    if (payload.empty()) {
        return false;
    }

    // Pad up to a whole number of sides so slightly-truncated dumps still load.
    const int sides = static_cast<int>((payload.size() + kSideSize - 1) / kSideSize);
    disk_data_.assign(payload.begin(), payload.end());
    disk_data_.resize(static_cast<std::size_t>(sides) * kSideSize, 0);
    num_sides_ = sides;
    write_protected_ = true;
    loaded_ = true;
    insert_side(0);
    return true;
}

void Fds::insert_side(int side) {
    if (side < 0 || side >= num_sides_) {
        current_side_ = -1;
        transfer_active_ = false;
        return;
    }
    current_side_ = side;
    disk_position_ = 0;
    transfer_active_ = false;
    end_of_disk_ = false;
    byte_transfer_flag_ = false;
}

void Fds::eject() {
    current_side_ = -1;
    transfer_active_ = false;
    end_of_disk_ = false;
}

void Fds::step(uint32_t cpu_cycles) {
    step_timer(cpu_cycles);
    step_transfer(cpu_cycles);
}

void Fds::step_timer(uint32_t cpu_cycles) {
    if (!irq_timer_enabled_ || !disk_io_enabled_ || irq_reload_ == 0) {
        return;
    }
    for (uint32_t i = 0; i < cpu_cycles; ++i) {
        if (irq_timer_ == 0) {
            irq_timer_ = irq_reload_;
        }
        --irq_timer_;
        if (irq_timer_ == 0) {
            timer_irq_flag_ = true;
            irq_line_ = true;
            if (irq_timer_repeat_) {
                irq_timer_ = irq_reload_;
            } else {
                irq_timer_enabled_ = false;
                break;
            }
        }
    }
}

void Fds::step_transfer(uint32_t cpu_cycles) {
    if (!transfer_active_ || current_side_ < 0 || !read_mode_) {
        return;
    }
    transfer_delay_ -= static_cast<int32_t>(cpu_cycles);
    while (transfer_delay_ <= 0) {
        if (disk_position_ >= kSideSize) {
            end_of_disk_ = true;
            transfer_active_ = false;
            break;
        }
        const std::size_t offset =
            static_cast<std::size_t>(current_side_) * kSideSize + disk_position_;
        read_data_ = disk_data_[offset];
        byte_transfer_flag_ = true;
        if (irq_on_transfer_) {
            irq_line_ = true;
        }
        ++disk_position_;
        transfer_delay_ += kBytePeriod;
    }
}

Byte Fds::read_drive_register(Address reg) {
    switch (reg) {
    case 0x30: { // Disk status: clears the timer IRQ on read
        Byte status = 0;
        status |= timer_irq_flag_ ? 0x01 : 0x00;
        status |= byte_transfer_flag_ ? 0x02 : 0x00;
        status |= crc_error_ ? 0x10 : 0x00;
        status |= end_of_disk_ ? 0x40 : 0x00;
        timer_irq_flag_ = false;
        irq_line_ = irq_on_transfer_ && byte_transfer_flag_;
        return status;
    }
    case 0x31: { // Read data: clears the byte-transfer flag
        const Byte value = read_data_;
        byte_transfer_flag_ = false;
        irq_line_ = timer_irq_flag_;
        return value;
    }
    case 0x32: { // Drive status
        const bool inserted = current_side_ >= 0;
        const bool ready = inserted && motor_on_ && disk_io_enabled_;
        Byte status = 0;
        status |= inserted ? 0x00 : 0x01;         // bit0: 1 = no disk
        status |= ready ? 0x00 : 0x02;            // bit1: 1 = not ready
        status |= write_protected_ ? 0x04 : 0x00; // bit2: 1 = write protected
        return status;
    }
    case 0x33: // External connector: bit7 = battery present/good
        return 0x80;
    default:
        return 0;
    }
}

void Fds::write_drive_register(Address reg, Byte value) {
    switch (reg) {
    case 0x20: // IRQ reload low
        irq_reload_ = static_cast<uint16_t>((irq_reload_ & 0xFF00) | value);
        break;
    case 0x21: // IRQ reload high
        irq_reload_ = static_cast<uint16_t>((irq_reload_ & 0x00FF) | (value << 8));
        break;
    case 0x22: // IRQ control
        irq_timer_repeat_ = (value & 0x01) != 0;
        irq_timer_enabled_ = (value & 0x02) != 0 && disk_io_enabled_;
        if (irq_timer_enabled_) {
            irq_timer_ = irq_reload_;
        } else {
            timer_irq_flag_ = false;
            irq_line_ = irq_on_transfer_ && byte_transfer_flag_;
        }
        break;
    case 0x23: // Master I/O enable
        disk_io_enabled_ = (value & 0x01) != 0;
        sound_io_enabled_ = (value & 0x02) != 0;
        if (!disk_io_enabled_) {
            irq_timer_enabled_ = false;
            timer_irq_flag_ = false;
            irq_line_ = irq_on_transfer_ && byte_transfer_flag_;
        }
        break;
    case 0x24: // Write data register
        write_data_ = value;
        byte_transfer_flag_ = false;
        irq_line_ = timer_irq_flag_;
        break;
    case 0x25: { // Drive control
        motor_on_ = (value & 0x01) != 0;
        transfer_reset_ = (value & 0x02) != 0;
        read_mode_ = (value & 0x04) != 0;
        crc_control_ = (value & 0x10) != 0;
        const bool start = (value & 0x40) != 0;
        irq_on_transfer_ = (value & 0x80) != 0;

        if (transfer_reset_ || !motor_on_) {
            transfer_active_ = false;
            if (transfer_reset_) {
                disk_position_ = 0;
                end_of_disk_ = false;
            }
        } else if (start && !transfer_active_) {
            transfer_active_ = true;
            transfer_delay_ = kStartupDelay;
        }
        irq_line_ = timer_irq_flag_ || (irq_on_transfer_ && byte_transfer_flag_);
        break;
    }
    default: // $4026 external connector and any unmapped register: ignored
        break;
    }
}

Byte Fds::read(Address addr) {
    addr &= 0x00FF;

    // $4030-$4033: Disk drive registers
    if (addr >= 0x30 && addr <= 0x33) {
        return read_drive_register(addr);
    }

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

    // $4020-$4026: Disk drive registers
    if (addr >= 0x20 && addr <= 0x26) {
        write_drive_register(addr, value);
        return;
    }

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
