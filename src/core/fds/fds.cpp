#include "core/fds/fds.hpp"

#include <algorithm>
#include <string_view>

#include "core/state/state.hpp"

namespace mapperbus::core {

namespace {
// The disk surface streams ~96.4 kbit/s; one byte therefore arrives roughly
// every 149 CPU cycles (1.789773 MHz / (96400 / 8)).
constexpr int32_t kBytePeriod = 149;
// Small spin-up delay before the first byte after the head starts.
constexpr int32_t kStartupDelay = 50000;
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
    writer.write(vol_env_speed_);
    writer.write(vol_env_increase_);
    writer.write(vol_env_timer_);
    writer.write(env_halt_);
    writer.write(master_env_speed_);
    writer.write(mod_env_disabled_);
    writer.write(mod_env_speed_);
    writer.write(mod_env_increase_);
    writer.write(mod_env_timer_);
    writer.write(horizontal_mirroring_);
    writer.write(crc_enabled_);
    writer.write(read_gap_);
    writer.write(crc_accumulator_);
    writer.write(byte_phase_);
    writer.write(insertion_delay_);
    writer.write_bytes(disk_stream_);
}

void Fds::load_state(StateReader& reader, bool extended) {
    std::vector<Byte> saved_stream;
    reader.read(loaded_);
    reader.read(num_sides_);
    reader.read(current_side_);
    reader.read(write_protected_);
    const auto previous_disk = disk_data_;
    reader.read_bytes(disk_data_, kMaxDiskImageBytes);
    dirty_ = dirty_ || disk_data_ != previous_disk;

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
    reader.read(vol_env_speed_);
    reader.read(vol_env_increase_);
    reader.read(vol_env_timer_);
    reader.read(env_halt_);
    reader.read(master_env_speed_);
    reader.read(mod_env_disabled_);
    reader.read(mod_env_speed_);
    reader.read(mod_env_increase_);
    reader.read(mod_env_timer_);
    if (extended) {
        reader.read(horizontal_mirroring_);
        reader.read(crc_enabled_);
        reader.read(read_gap_);
        reader.read(crc_accumulator_);
        reader.read(byte_phase_);
        reader.read(insertion_delay_);
        reader.read_bytes(saved_stream, kMaxStreamBytes);
        if (insertion_delay_ < 0 || insertion_delay_ > 1000000)
            reader.fail();
        if (byte_phase_ > 2)
            reader.fail();
    } else {
        horizontal_mirroring_ = false;
        crc_enabled_ = false;
        read_gap_ = true;
        crc_accumulator_ = 0;
        byte_phase_ = 0;
        insertion_delay_ = 0;
    }

    const bool valid_transfer_delay = transfer_delay_ >= 0;
    const bool valid_audio = wave_position_ < wavetable_.size() && wave_frequency_ <= 0x0FFF &&
                             volume_envelope_ <= 0x3F && master_volume_ <= 0x03 &&
                             vol_env_speed_ <= 0x3F && mod_position_ < mod_table_.size() * 2 &&
                             mod_frequency_ <= 0x0FFF && mod_counter_ >= -64 &&
                             mod_counter_ <= 63 && mod_depth_ <= 0x3F && mod_env_speed_ <= 0x3F;
    if (!valid_transfer_delay || !valid_audio) {
        transfer_delay_ = 0;
        transfer_active_ = false;
        reset_audio();
        reader.fail();
    }

    // Always clamp. A later field can fail the reader after current_side_ and
    // disk_position_ were already taken from the blob; skipping this would
    // leave step_transfer an out-of-range index. Inconsistent but fully read
    // geometry is repaired, matching the previous success-path contract.
    sanitize_disk_geometry();
    if (extended && current_side_ >= 0 && !saved_stream.empty()) {
        disk_stream_ = std::move(saved_stream);
        disk_position_ = std::min(disk_position_, static_cast<uint32_t>(disk_stream_.size()));
    }
}

void Fds::sanitize_disk_geometry() {
    if (!loaded_) {
        num_sides_ = 0;
        current_side_ = -1;
        transfer_active_ = false;
        disk_data_.clear();
        return;
    }
    const int sides_in_data = static_cast<int>(disk_data_.size() / kSideSize);
    num_sides_ = std::clamp(num_sides_, 0, sides_in_data);
    if (current_side_ < -1 || current_side_ >= num_sides_) {
        current_side_ = -1;
    }
    if (current_side_ < 0) {
        transfer_active_ = false;
        disk_stream_.clear();
    }
    if (current_side_ >= 0 && !rebuild_disk_stream()) {
        current_side_ = -1;
        transfer_active_ = false;
    }
    disk_position_ = std::min(disk_position_, static_cast<uint32_t>(disk_stream_.size()));
}

void Fds::reset_audio() {
    wavetable_.fill(0);
    wave_write_enabled_ = false;
    wave_frequency_ = 0;
    wave_accumulator_ = 0;
    wave_position_ = 0;
    volume_envelope_ = 0;
    volume_enabled_ = true;
    master_volume_ = 0;
    sound_enabled_ = false;
    envelope_disabled_ = true;
    vol_env_speed_ = 0;
    vol_env_increase_ = false;
    vol_env_timer_ = 0;
    env_halt_ = false;
    master_env_speed_ = 0xE8; // NESdev: $408A power-on default
    mod_table_.fill(0);
    mod_frequency_ = 0;
    mod_accumulator_ = 0;
    mod_position_ = 0;
    mod_counter_ = 0;
    mod_enabled_ = false;
    mod_depth_ = 0;
    mod_env_disabled_ = false;
    mod_env_speed_ = 0;
    mod_env_increase_ = false;
    mod_env_timer_ = 0;
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
    horizontal_mirroring_ = false;
    crc_enabled_ = false;
    read_gap_ = true;
    crc_accumulator_ = 0;
    byte_phase_ = 0;
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
    Fds candidate;
    if (!candidate.load_disk_image(image))
        return false;
    *this = std::move(candidate);
    return true;
}

bool Fds::load_disk_image(std::span<const Byte> image) {
    // 16-byte fwNES header plus the capped payload. Reject before any copy.
    if (image.size() > kMaxDiskImageBytes + 16) {
        return false;
    }

    std::span<const Byte> payload = image;
    // Strip the optional 16-byte fwNES "FDS\x1a" header.
    if (payload.size() >= 16 && payload[0] == 'F' && payload[1] == 'D' && payload[2] == 'S' &&
        payload[3] == 0x1A) {
        payload = payload.subspan(16);
    }
    if (payload.empty() || payload.size() > kMaxDiskImageBytes || payload.size() % kSideSize != 0) {
        return false;
    }

    // Whole sides are required; never manufacture missing file bytes.
    const int sides = static_cast<int>((payload.size() + kSideSize - 1) / kSideSize);
    disk_data_.assign(payload.begin(), payload.end());
    disk_data_.resize(static_cast<std::size_t>(sides) * kSideSize, 0);
    num_sides_ = sides;
    write_protected_ = true;
    loaded_ = true;
    dirty_ = false;
    for (int side = 0; side < num_sides_; ++side) {
        current_side_ = side;
        if (!rebuild_disk_stream()) {
            unload();
            return false;
        }
    }
    current_side_ = -1;
    insert_side(0);
    if (current_side_ < 0) {
        unload();
        return false;
    }
    return true;
}

bool Fds::apply_disk_changes(std::span<const Byte> image) {
    if (!loaded_ || image.size() != disk_data_.size())
        return false;
    Fds candidate;
    if (!candidate.load_disk(image))
        return false;
    disk_data_ = std::move(candidate.disk_data_);
    if (current_side_ >= 0)
        (void)rebuild_disk_stream();
    dirty_ = false;
    return true;
}

void Fds::unload() {
    loaded_ = false;
    disk_data_.clear();
    num_sides_ = 0;
    current_side_ = -1;
    disk_stream_.clear();
    dirty_ = false;
    reset();
}

void Fds::insert_side(int side) {
    synchronize_disk();
    if (side < 0 || side >= num_sides_) {
        eject();
        return;
    }
    insertion_delay_ = current_side_ >= 0 && current_side_ != side ? 1000000 : 0;
    current_side_ = side;
    if (!rebuild_disk_stream()) {
        current_side_ = -1;
        transfer_active_ = false;
        return;
    }
    disk_position_ = 0;
    transfer_delay_ = kStartupDelay;
    read_gap_ = true;
    crc_accumulator_ = 0;
    byte_phase_ = 0;
    transfer_active_ = motor_on_ && !transfer_reset_ && disk_io_enabled_;
    end_of_disk_ = false;
    byte_transfer_flag_ = false;
    update_irq();
}

void Fds::eject() {
    synchronize_disk();
    disk_stream_.clear();
    insertion_delay_ = 0;
    current_side_ = -1;
    transfer_active_ = false;
    end_of_disk_ = false;
}

void Fds::step(uint32_t cpu_cycles) {
    step_timer(cpu_cycles);
    if (insertion_delay_ > 0) {
        const auto elapsed = std::min(cpu_cycles, static_cast<uint32_t>(insertion_delay_));
        insertion_delay_ -= static_cast<int32_t>(elapsed);
        cpu_cycles -= elapsed;
    }
    if (insertion_delay_ == 0)
        step_transfer(cpu_cycles);
}

void Fds::step_timer(uint32_t cpu_cycles) {
    if (!irq_timer_enabled_ || !disk_io_enabled_) {
        return;
    }
    for (uint32_t i = 0; i < cpu_cycles; ++i) {
        if (irq_timer_ > 0) {
            --irq_timer_;
        }
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

uint16_t Fds::update_crc(uint16_t crc, Byte value) {
    auto bits = static_cast<uint32_t>(crc) ^ value;
    for (int bit = 0; bit < 8; ++bit) {
        bits = (bits >> 1U) ^ ((bits & 1U) != 0 ? 0x8408U : 0U);
    }
    return static_cast<uint16_t>(bits);
}

bool Fds::rebuild_disk_stream() {
    disk_stream_.clear();
    if (current_side_ < 0 || current_side_ >= num_sides_)
        return false;
    const auto side_offset = static_cast<std::size_t>(current_side_) * kSideSize;
    const auto side = std::span<const Byte>(disk_data_).subspan(side_offset, kSideSize);
    constexpr std::string_view kSignature = "*NINTENDO-HVC*";
    if (side[0] != 1 || !std::equal(kSignature.begin(), kSignature.end(), side.begin() + 1))
        return false;
    const auto append = [&](Byte value) { disk_stream_.push_back(value); };
    for (std::size_t gap = 0; gap < 3537; ++gap)
        append(0);
    std::size_t offset = 0;
    std::size_t file_size = 0;
    Byte expected = 1;
    int files_read = 0;
    while (offset < side.size()) {
        const Byte block = side[offset];
        if (block != expected) {
            // Padding and erased remainder of the disk are not file blocks.
            if (expected == 3 && block != 3)
                break;
            return false;
        }
        std::size_t size = 0;
        if (block == 1) {
            size = 56;
            expected = 2;
        } else if (block == 2) {
            size = 2;
            expected = 3;
        } else if (block == 3) {
            if (side.size() - offset < 16)
                return false;
            file_size = static_cast<std::size_t>(side[offset + 13]) |
                        (static_cast<std::size_t>(side[offset + 14]) << 8U);
            size = 16;
            expected = 4;
        } else if (block == 4) {
            size = file_size + 1;
            expected = 3;
            ++files_read;
        } else
            return false;
        if (size > side.size() - offset || disk_stream_.size() + size + 125 > kMaxStreamBytes)
            return false;
        append(0x80);
        uint16_t crc = update_crc(0, 0x80);
        for (std::size_t index = 0; index < size; ++index) {
            const Byte value = side[offset + index];
            append(value);
            crc = update_crc(crc, value);
        }
        append(static_cast<Byte>(crc));
        append(static_cast<Byte>(static_cast<uint32_t>(crc) >> 8U));
        for (int gap = 0; gap < 122; ++gap)
            append(0);
        offset += size;
    }
    if (expected == 4 || files_read < static_cast<int>(side[57]))
        return false;
    while (disk_stream_.size() < 84000)
        append(0);
    return true;
}

void Fds::synchronize_disk() {
    if (current_side_ < 0 || disk_stream_.empty())
        return;
    // Firmware can change gap lengths while rewriting a block. Decode the
    // physical sync markers instead of mapping writes to old byte positions.
    auto rewritten = disk_data_;
    const auto side_start = static_cast<std::size_t>(current_side_) * kSideSize;
    std::size_t output = 0;
    std::size_t position = 0;
    std::size_t file_size = 0;
    Byte expected = 1;
    while (position < disk_stream_.size()) {
        while (position < disk_stream_.size() && disk_stream_[position] == 0)
            ++position;
        if (position == disk_stream_.size())
            break;
        if (disk_stream_[position] != 0x80)
            return;
        ++position;
        if (position >= disk_stream_.size())
            return;
        const Byte block = disk_stream_[position];
        if (block != expected)
            return;
        std::size_t size = 0;
        if (block == 1) {
            size = 56;
            expected = 2;
        } else if (block == 2) {
            size = 2;
            expected = 3;
        } else if (block == 3) {
            if (disk_stream_.size() - position < 16)
                return;
            file_size = disk_stream_[position + 13] |
                        (static_cast<std::size_t>(disk_stream_[position + 14]) << 8U);
            size = 16;
            expected = 4;
        } else if (block == 4) {
            size = file_size + 1;
            expected = 3;
        }
        if (size == 0 || size > kSideSize - output || size + 2 > disk_stream_.size() - position)
            return;
        std::copy_n(disk_stream_.begin() + static_cast<std::ptrdiff_t>(position),
                    size,
                    rewritten.begin() + static_cast<std::ptrdiff_t>(side_start + output));
        output += size;
        position += size + 2;
    }
    if (expected != 3)
        return;
    if (rewritten != disk_data_) {
        disk_data_ = std::move(rewritten);
        dirty_ = true;
    }
}

void Fds::update_irq() {
    irq_line_ = timer_irq_flag_ || (irq_on_transfer_ && byte_transfer_flag_);
}

void Fds::transfer_byte() {
    if (disk_position_ >= disk_stream_.size()) {
        end_of_disk_ = true;
        // Keep transfer flags clocking so software can detect a truncated file.
        if (crc_enabled_) {
            read_data_ = 0;
            byte_transfer_flag_ = true;
        }
        update_irq();
        return;
    }
    const auto position = disk_position_++;
    Byte value = disk_stream_[position];
    if (read_mode_) {
        if (!crc_enabled_) {
            read_gap_ = true;
            crc_accumulator_ = 0;
            return;
        }
        if (read_gap_) {
            if (value == 0x80) {
                read_gap_ = false;
                crc_accumulator_ = update_crc(0, value);
                crc_error_ = false;
            }
            return;
        }
        crc_accumulator_ = update_crc(crc_accumulator_, value);
        // BIOS selects CRC transfer after consuming the first CRC byte.
        // Checking the residue here also works when writes move block boundaries.
        if (crc_control_)
            crc_error_ = crc_accumulator_ != 0;
        read_data_ = value;
    } else {
        if (!crc_enabled_) {
            crc_accumulator_ = 0;
            // With CRC disabled the writer records zero bits (the inter-block
            // gap). Erase previous sync marks before writing a new boundary.
            if (!write_protected_)
                disk_stream_[position] = 0;
            return;
        }
        if (crc_control_) {
            value = static_cast<Byte>(crc_accumulator_);
            crc_accumulator_ = static_cast<uint16_t>(static_cast<uint32_t>(crc_accumulator_) >> 8U);
        } else {
            value = write_data_;
            crc_accumulator_ = update_crc(crc_accumulator_, value);
        }
        if (!write_protected_) {
            disk_stream_[position] = value;
        }
    }
    byte_transfer_flag_ = true;
    update_irq();
}

void Fds::step_transfer(uint32_t cpu_cycles) {
    if (!transfer_active_ || current_side_ < 0 || !disk_io_enabled_)
        return;
    transfer_delay_ -= static_cast<int32_t>(cpu_cycles);
    while (transfer_delay_ <= 0) {
        transfer_byte();
        byte_phase_ = static_cast<Byte>((byte_phase_ + 1) % 3);
        transfer_delay_ += kBytePeriod + (byte_phase_ == 0 ? 1 : 0);
    }
}

Byte Fds::read_drive_register(Address reg) {
    switch (reg) {
    case 0x30: { // Disk status: clears the timer IRQ on read
        Byte status = 0;
        status |= timer_irq_flag_ ? 0x01 : 0x00;
        status |= byte_transfer_flag_ ? 0x80 : 0x00;
        status |= horizontal_mirroring_ ? 0x08 : 0x00;
        status |= crc_error_ ? 0x10 : 0x00;
        status |= end_of_disk_ ? 0x40 : 0x00;
        timer_irq_flag_ = false;
        irq_line_ = irq_on_transfer_ && byte_transfer_flag_;
        return status;
    }
    case 0x31: { // Read data: clears the byte-transfer flag
        const Byte value = read_data_;
        byte_transfer_flag_ = false;
        update_irq();
        return value;
    }
    case 0x32: { // Drive status
        const bool inserted = current_side_ >= 0 && insertion_delay_ == 0;
        const bool ready = inserted && motor_on_ && !transfer_reset_ && disk_io_enabled_;
        Byte status = 0;
        status |= inserted ? 0x00 : 0x01;                      // bit0: 1 = no disk
        status |= ready ? 0x00 : 0x02;                         // bit1: 1 = not ready
        status |= !inserted || write_protected_ ? 0x04 : 0x00; // bit2: not writable
        return status;
    }
    case 0x33: // External connector: bit7 = battery present/good
        return 0x80;
    default:
        return 0;
    }
}

void Fds::write_drive_register(Address reg, Byte value) {
    if (reg != 0x20 && reg != 0x21 && reg != 0x23 && !disk_io_enabled_)
        return;
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
        if (!sound_io_enabled_) {
            hold_audio_reset();
        }
        if (!disk_io_enabled_) {
            irq_timer_enabled_ = false;
            timer_irq_flag_ = false;
            byte_transfer_flag_ = false;
            transfer_active_ = false;
            motor_on_ = false;
            transfer_reset_ = true;
            read_mode_ = true;
            horizontal_mirroring_ = false;
            update_irq();
        }
        break;
    case 0x24: // Write data register
        write_data_ = value;
        byte_transfer_flag_ = false;
        irq_line_ = timer_irq_flag_;
        break;
    case 0x25: { // See NESdev FDS Control: scan, motor stop, read, mirror, CRC.
        if (!read_mode_ && ((value & 0x04) != 0 || (value & 0x40) == 0 || (value & 0x03) != 1))
            synchronize_disk();
        horizontal_mirroring_ = (value & 0x08) != 0;
        motor_on_ = (value & 0x02) == 0;
        transfer_reset_ = (value & 0x01) == 0;
        read_mode_ = (value & 0x04) != 0;
        crc_control_ = (value & 0x10) != 0;
        crc_enabled_ = (value & 0x40) != 0;
        irq_on_transfer_ = (value & 0x80) != 0;
        if (!crc_enabled_) {
            crc_accumulator_ = 0;
            read_gap_ = true;
            crc_error_ = false;
            byte_transfer_flag_ = false;
        }
        if (transfer_reset_ || !motor_on_) {
            transfer_active_ = false;
            disk_position_ = 0;
            transfer_delay_ = kStartupDelay;
            end_of_disk_ = false;
            read_gap_ = true;
        } else if (!transfer_active_ && current_side_ >= 0) {
            transfer_active_ = true;
        }
        update_irq();
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
        if (!sound_io_enabled_)
            break;
        envelope_disabled_ = (value & 0x80) != 0;
        vol_env_increase_ = (value & 0x40) != 0;
        vol_env_speed_ = value & 0x3F;
        vol_env_timer_ = 0; // Any write restarts the envelope's tick timer.
        if (envelope_disabled_) {
            volume_envelope_ = value & 0x3F;
        }
        break;

    case 0x82: // $4082: Wave frequency low
        wave_frequency_ = (wave_frequency_ & 0x0F00) | value;
        break;

    case 0x83: // $4083: Wave frequency high + flags
        wave_frequency_ = (wave_frequency_ & 0x00FF) | (static_cast<uint16_t>(value & 0x0F) << 8U);
        env_halt_ = (value & 0x40) != 0;
        sound_enabled_ = (value & 0x80) == 0;
        if (!sound_enabled_) {
            // Halting the wave unit resets its phase to sample 0.
            wave_accumulator_ = 0;
            wave_position_ = 0;
        }
        break;

    case 0x84: // $4084: Modulation envelope
        mod_env_disabled_ = (value & 0x80) != 0;
        mod_env_increase_ = (value & 0x40) != 0;
        mod_env_speed_ = value & 0x3F;
        mod_env_timer_ = 0; // Any write restarts the envelope's tick timer.
        if (mod_env_disabled_) {
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
        mod_frequency_ = (mod_frequency_ & 0x00FF) | (static_cast<uint16_t>(value & 0x0F) << 8U);
        mod_enabled_ = (value & 0x80) == 0;
        if (!mod_enabled_) {
            mod_accumulator_ = 0;
        }
        break;

    case 0x88: // $4088: Modulation table write
        if (!mod_enabled_) {
            // One 3-bit entry per write; the 6-bit playback position covers
            // each entry twice, so the position advances by 2.
            mod_table_[(mod_position_ >> 1) & 0x1F] = static_cast<int8_t>(value & 0x07);
            mod_position_ = (mod_position_ + 2) & 0x3F;
        }
        break;

    case 0x89: // $4089: Master volume + wave write enable
        master_volume_ = value & 0x03;
        wave_write_enabled_ = (value & 0x80) != 0;
        break;

    case 0x8A: // $408A: Master envelope speed (does not reset envelope timers)
        master_env_speed_ = value;
        break;

    default:
        break;
    }
}

void Fds::clock_envelopes() {
    // $4083 bit 6 freezes both envelope timers. Each envelope also stops
    // while its own register's bit 7 selects direct-gain mode. Envelopes are
    // not gated by the wave unit or the wave-write mode.
    if (env_halt_)
        return;

    const uint32_t master = static_cast<uint32_t>(master_env_speed_) + 1;

    if (!envelope_disabled_) {
        const uint32_t period = 8 * (static_cast<uint32_t>(vol_env_speed_) + 1) * master;
        if (++vol_env_timer_ >= period) {
            vol_env_timer_ = 0;
            if (vol_env_increase_) {
                if (volume_envelope_ < 32)
                    ++volume_envelope_;
            } else if (volume_envelope_ > 0) {
                --volume_envelope_;
            }
        }
    }

    if (!mod_env_disabled_) {
        const uint32_t period = 8 * (static_cast<uint32_t>(mod_env_speed_) + 1) * master;
        if (++mod_env_timer_ >= period) {
            mod_env_timer_ = 0;
            if (mod_env_increase_) {
                if (mod_depth_ < 32)
                    ++mod_depth_;
            } else if (mod_depth_ > 0) {
                --mod_depth_;
            }
        }
    }
}

void Fds::hold_audio_reset() {
    volume_envelope_ = 0;
    envelope_disabled_ = true;
    vol_env_timer_ = 0;
    wave_accumulator_ = 0;
    wave_position_ = 0;
    mod_counter_ = 0;
}

void Fds::clock_audio() {
    if (!sound_io_enabled_) {
        hold_audio_reset();
        return;
    }
    clock_envelopes();

    // The mod unit runs independently of the wave unit; only $4087 bit 7
    // halts it.
    if (mod_enabled_ && mod_frequency_ > 0) {
        mod_accumulator_ += mod_frequency_;
        if (mod_accumulator_ >= 0x10000) {
            mod_accumulator_ &= 0xFFFF;
            // 3-bit table entries decode to counter deltas; entry 4 resets.
            static constexpr int8_t kModDelta[8] = {0, 1, 2, 4, 0, -4, -2, -1};
            uint8_t entry = static_cast<uint8_t>(mod_table_[mod_position_ >> 1]) & 0x07;
            if (entry == 4) {
                mod_counter_ = 0;
            } else {
                // 7-bit two's-complement counter: wraps -64 <-> +63.
                int wrapped = mod_counter_ + kModDelta[entry];
                if (wrapped > 63)
                    wrapped -= 128;
                else if (wrapped < -64)
                    wrapped += 128;
                mod_counter_ = static_cast<int8_t>(wrapped);
            }
            mod_position_ = (mod_position_ + 1) & 0x3F;
        }
    }

    if (!sound_enabled_ || wave_write_enabled_ || wave_frequency_ == 0)
        return;

    // Hardware pitch modulation (NESdev FDS audio): the offset is relative
    // to the wave frequency, with exact truncation/rounding steps. The
    // counter biases pitch even while the mod unit is halted ($4085 detune).
    int temp = mod_counter_ * (mod_depth_ & 0x3F);
    int remainder = temp & 0xF;
    temp >>= 4;
    if (remainder > 0 && (temp & 0x80) == 0) {
        temp += (mod_counter_ < 0) ? -1 : 2;
    }
    while (temp >= 192)
        temp -= 256;
    while (temp < -64)
        temp += 256;
    temp = wave_frequency_ * temp;
    remainder = temp & 0x3F;
    temp >>= 6;
    if (remainder >= 32)
        ++temp;

    int freq = wave_frequency_ + temp;
    if (freq <= 0)
        return;

    // Advance wave accumulator
    wave_accumulator_ += static_cast<uint32_t>(freq);
    if (wave_accumulator_ >= 0x10000) {
        wave_accumulator_ &= 0xFFFF;
        wave_position_ = (wave_position_ + 1) & 0x3F;
    }
}

float Fds::audio_output() const {
    if (!sound_io_enabled_)
        return 0.0f;
    // The DAC holds the current sample while the wave unit is halted or the
    // table is open for writes; gating to 0 here would pop on every table
    // swap (same failure mode as the triangle DAC).
    uint8_t sample = wavetable_[wave_position_] & 0x3F; // 6-bit sample (0-63)
    // Gains 33-63 are clamped to 32 by the DAC.
    uint8_t vol = std::min<uint8_t>(volume_envelope_ & 0x3F, 32);

    // Master volume attenuation: 2 / (2 + m)
    static constexpr float kVolumeScale[] = {1.0f, 2.0f / 3.0f, 0.5f, 0.4f};
    float volume_mul = kVolumeScale[master_volume_ & 0x03];

    // Output: sample * volume * master_volume, normalized to ~0.0 - 0.15
    float output = (static_cast<float>(sample) / 63.0f) * (static_cast<float>(vol) / 32.0f);
    return output * volume_mul * 0.15f;
}

} // namespace mapperbus::core
