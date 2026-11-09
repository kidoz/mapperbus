#include "core/mappers/serial_eeprom.hpp"

#include <algorithm>

#include "core/state/state.hpp"

namespace mapperbus::core {
namespace {
// Deterministic approximation of the original Xicor parts' typical 5 ms
// programming interval, within their 10 ms maximum at supported NES regions.
constexpr Word kWriteCycles = 9000U;
} // namespace

// Xicor X24C01 (9900-2002.1) and X24C02 (3838-1.2) data sheets.
SerialEeprom::SerialEeprom(EepromKind kind) : kind_(kind) {
    data_.fill(0xFFU);
}

unsigned SerialEeprom::size() const {
    return kind_ == EepromKind::None ? 0U : (kind_ == EepromKind::X24C01 ? 128U : 256U);
}

bool SerialEeprom::sda() const {
    return host_sda_ != 0U && pull_low_ == 0U;
}

void SerialEeprom::receive_byte() {
    ack_ = 1U;
    next_ = State::Receive;
    if (stage_ == Stage::Header) {
        const bool reading = (shift_ & 1U) != 0U;
        if (kind_ == EepromKind::X24C01) {
            address_ = shift_ >> 1U;
            stage_ = Stage::Data;
        } else if ((shift_ & 0xFEU) != 0xA0U) {
            ack_ = 0U;
            next_ = State::Ignore;
            return;
        } else {
            stage_ = Stage::Address;
        }
        if (reading)
            next_ = State::Send;
    } else if (stage_ == Stage::Address) {
        address_ = shift_;
        stage_ = Stage::Data;
    } else {
        if (dirty_ == 0U)
            page_base_ = address_ & 0xFCU;
        const auto index = address_ & 3U;
        page_[index] = shift_;
        dirty_ = static_cast<Byte>(dirty_ | (1U << index));
        address_ = static_cast<Byte>((address_ & 0xFCU) | ((address_ + 1U) & 3U));
    }
}

void SerialEeprom::drive_read_bit() {
    pull_low_ = (data_[address_] & (0x80U >> bits_)) == 0U ? 1U : 0U;
}

void SerialEeprom::set_lines(bool scl, bool released_sda) {
    if (kind_ == EepromKind::None)
        return;
    const bool previous_sda = sda();
    const bool next_sda = released_sda && pull_low_ == 0U;
    const bool rising = scl_ == 0U && scl;
    const bool falling = scl_ != 0U && !scl;
    if (scl_ != 0U && scl && previous_sda != next_sda) {
        pull_low_ = 0U;
        bits_ = 0U;
        shift_ = 0U;
        if (!next_sda) {
            state_ = busy_ != 0U ? State::Ignore : State::Receive;
            stage_ = Stage::Header;
            if (busy_ == 0U)
                dirty_ = 0U; // A repeated START abandons an uncommitted page.
        } else {
            if (dirty_ != 0U && busy_ == 0U)
                busy_ = kWriteCycles;
            state_ = State::Idle;
        }
    } else if (rising) {
        if (state_ == State::Receive) {
            shift_ =
                static_cast<Byte>((static_cast<unsigned>(shift_) << 1U) | (released_sda ? 1U : 0U));
            if (++bits_ == 8U) {
                receive_byte();
                state_ = State::Ack;
                ack_phase_ = 0U;
            }
        } else if (state_ == State::Send) {
            if (++bits_ == 8U) {
                address_ = static_cast<Byte>((address_ + 1U) % size());
                state_ = State::HostAck;
            }
        } else if (state_ == State::HostAck) {
            state_ = released_sda ? State::Ignore : State::Send;
            bits_ = 0U;
        }
    } else if (falling) {
        if (state_ == State::Ack) {
            if (ack_phase_ == 0U) {
                pull_low_ = ack_;
                ack_phase_ = 1U;
            } else {
                pull_low_ = 0U;
                state_ = next_;
                bits_ = 0U;
                shift_ = 0U;
                if (state_ == State::Send)
                    drive_read_bit();
            }
        } else if (state_ == State::Send) {
            drive_read_bit();
        } else if (state_ == State::HostAck) {
            pull_low_ = 0U;
        }
    }
    scl_ = scl ? 1U : 0U;
    host_sda_ = released_sda ? 1U : 0U;
}

void SerialEeprom::clock_cpu() {
    if (busy_ == 0U)
        return;
    --busy_;
    if (busy_ == 0U) {
        for (unsigned index = 0U; index < page_.size(); ++index) {
            if ((dirty_ & (1U << index)) != 0U)
                data_[page_base_ + index] = page_[index];
        }
        dirty_ = 0U;
    }
}

std::span<const Byte> SerialEeprom::nvram() const {
    return std::span<const Byte>(data_).first(size());
}

void SerialEeprom::restore_nvram(std::span<const Byte> data) {
    std::copy_n(
        data.begin(), std::min(data.size(), static_cast<std::size_t>(size())), data_.begin());
}

void SerialEeprom::save_state(StateWriter& w) const {
    w.write_array(data_);
    w.write_array(page_);
    w.write(address_);
    w.write(page_base_);
    w.write(dirty_);
    w.write(state_);
    w.write(next_);
    w.write(stage_);
    w.write(shift_);
    w.write(bits_);
    w.write(ack_);
    w.write(ack_phase_);
    w.write(scl_);
    w.write(host_sda_);
    w.write(pull_low_);
    w.write(busy_);
}

void SerialEeprom::load_state(StateReader& r) {
    r.read_array(data_);
    r.read_array(page_);
    r.read(address_);
    r.read(page_base_);
    r.read(dirty_);
    r.read(state_);
    r.read(next_);
    r.read(stage_);
    r.read(shift_);
    r.read(bits_);
    r.read(ack_);
    r.read(ack_phase_);
    r.read(scl_);
    r.read(host_sda_);
    r.read(pull_low_);
    r.read(busy_);
    const unsigned limit = size() == 0U ? 256U : size();
    if (!r.ok() || address_ >= limit || page_base_ >= limit || (page_base_ & 3U) != 0U ||
        dirty_ > 15U || state_ > State::Ignore || next_ > State::Ignore || stage_ > Stage::Data ||
        bits_ > 8U || ack_ > 1U || ack_phase_ > 1U || scl_ > 1U || host_sda_ > 1U ||
        pull_low_ > 1U || busy_ > kWriteCycles || (state_ == State::Send && bits_ >= 8U) ||
        (busy_ != 0U && dirty_ == 0U)) {
        state_ = State::Idle;
        pull_low_ = 0U;
        bits_ = 0U;
        busy_ = 0U;
        dirty_ = 0U;
        r.fail();
    }
}

} // namespace mapperbus::core
